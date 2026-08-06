// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>

#include "presence_uapi.h"

#define RADAR_DEV_NAME       "radar_presence"
#define RADAR_FIFO_DEPTH     64U   /* 반드시 2의 거듭제곱 (kfifo 요구사항) */

/*
 * === Mock 전용 파라미터 ===
 * period_ms      : 상태 체크 주기 (delayed_work 재스케줄 간격)
 * occupied_duration : "사람 있음" 상태 지속 시간(초)
 * empty_duration     : "사람 없음" 상태 지속 시간(초)
 *
 * 실제 IWR6843 연동 시에는 radarWorkFn() 내부를
 * SPI/UART 수신 + 알고리즘 결과 파싱으로 교체하면 됩니다.
 * kfifo/poll/read/ioctl 쪽은 그대로 재사용합니다.
 */
static int period_ms = 1000;
static int occupied_duration = 10;
static int empty_duration = 5;

module_param(period_ms, int, 0444);
module_param(occupied_duration, int, 0444);
module_param(empty_duration, int, 0444);
MODULE_PARM_DESC(period_ms, "State machine tick interval in ms");
MODULE_PARM_DESC(occupied_duration, "Occupied phase duration in seconds");
MODULE_PARM_DESC(empty_duration, "Empty phase duration in seconds");


struct radar_device_data {
        struct cdev cdev;
        struct delayed_work work;

        spinlock_t lock;
        wait_queue_head_t read_wait;
        atomic_t reader_open;

        DECLARE_KFIFO(fifo, struct presence_event, RADAR_FIFO_DEPTH);

        bool dropped_pending;

        bool  presence_state;     /* 현재 phase: true=occupied, false=empty */
        __u32 elapsed_ms;         /* 현재 phase 진입 후 경과 시간 */
        __u32 current_raw;
        __u32 sequence;
        __u64 last_timestamp_ns;

        __u64 total_events;
        __u64 delivered_events;
        __u64 dropped_events;
        __u64 stats_last_timestamp_ns;
};

static struct radar_device_data radar_dev;
static dev_t radar_devno;
static struct class *radar_class;


static bool radarEventAvailable(struct radar_device_data *dev)
{
        return !kfifo_is_empty(&dev->fifo);
}


static void radarMarkDroppedLocked(struct radar_device_data *dev)
{
        dev->dropped_events++;
        dev->dropped_pending = true;
}


static void radarPushEventLocked(struct radar_device_data *dev,
                                  struct presence_event *event)
{
        struct presence_event discard;

        if (kfifo_is_full(&dev->fifo)) {
                kfifo_out(&dev->fifo, &discard, 1);
                dev->dropped_events++;
                event->flags |= PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
        }

        kfifo_in(&dev->fifo, event, 1);
}


/*
 * delayed_work 콜백: period_ms(기본 1초)마다 실행되어
 * "empty_duration초 동안 없음 → occupied_duration초 동안 있음"을
 * 반복하는 결정론적 상태 머신을 진행시킵니다.
 */
static void radarWorkFn(struct work_struct *work)
{
        struct radar_device_data *dev =
                container_of(to_delayed_work(work),
                             struct radar_device_data, work);
        struct presence_event event = { 0 };
        unsigned long flags;
        __u64 timestamp_ns;
        __u32 phase_limit_ms;
        bool phase_changed = false;

        timestamp_ns = ktime_get_ns();

        spin_lock_irqsave(&dev->lock, flags);

        dev->elapsed_ms += (__u32)period_ms;

        phase_limit_ms = dev->presence_state
                        ? (__u32)occupied_duration * 1000U
                        : (__u32)empty_duration * 1000U;

        if (dev->elapsed_ms >= phase_limit_ms) {
                dev->presence_state = !dev->presence_state;
                dev->elapsed_ms = 0;
                phase_changed = true;
        }

        dev->current_raw = dev->presence_state ? 1U : 0U;
        dev->last_timestamp_ns = timestamp_ns;

        if (phase_changed) {
                dev->sequence++;

                event.api_version = PRESENCE_API_VERSION;
                event.sensor_type = PRESENCE_SENSOR_RADAR;
                event.event_type = dev->presence_state
                                  ? PRESENCE_EVENT_ASSERTED
                                  : PRESENCE_EVENT_DEASSERTED;
                event.sequence = dev->sequence;
                event.timestamp_ns = timestamp_ns;
                event.raw_value = dev->current_raw;
                event.flags = 0;

                dev->total_events++;
                dev->stats_last_timestamp_ns = timestamp_ns;

                if (dev->dropped_pending) {
                        event.flags |= PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
                        dev->dropped_pending = false;
                }

                radarPushEventLocked(dev, &event);
        }

        spin_unlock_irqrestore(&dev->lock, flags);

        if (phase_changed)
                wake_up_interruptible(&dev->read_wait);

        /* 다음 tick 예약 (인터럽트와 달리 워크큐도 직접 재스케줄 필요) */
        schedule_delayed_work(&dev->work, msecs_to_jiffies(period_ms));
}


static int radar_open(struct inode *inode, struct file *filp)
{
        (void)inode;

        if (atomic_cmpxchg(&radar_dev.reader_open, 0, 1) != 0)
                return -EBUSY;

        filp->private_data = &radar_dev;

        pr_info("radar_presence: open\n");

        return 0;
}


static ssize_t radar_read(struct file *filp, char __user *buf,
                           size_t count, loff_t *f_pos)
{
        struct radar_device_data *dev = filp->private_data;
        struct presence_event event;
        unsigned long flags;
        unsigned int copied;
        int ret;

        (void)f_pos;

        if (count < sizeof(event))
                return -EINVAL;

        while (1) {
                spin_lock_irqsave(&dev->lock, flags);

                if (!kfifo_is_empty(&dev->fifo)) {
                        ret = kfifo_out(&dev->fifo, &event, 1);
                        spin_unlock_irqrestore(&dev->lock, flags);

                        if (ret == 1)
                                break;

                        continue;
                }

                spin_unlock_irqrestore(&dev->lock, flags);

                if (filp->f_flags & O_NONBLOCK)
                        return -EAGAIN;

                ret = wait_event_interruptible(dev->read_wait,
                                                radarEventAvailable(dev));
                if (ret != 0)
                        return ret;
        }

        copied = sizeof(event);

        if (copy_to_user(buf, &event, copied) != 0) {
                spin_lock_irqsave(&dev->lock, flags);
                radarMarkDroppedLocked(dev);
                spin_unlock_irqrestore(&dev->lock, flags);
                return -EFAULT;
        }

        spin_lock_irqsave(&dev->lock, flags);
        dev->delivered_events++;
        spin_unlock_irqrestore(&dev->lock, flags);

        return copied;
}


static __poll_t radar_poll(struct file *filp, struct poll_table_struct *wait)
{
        struct radar_device_data *dev = filp->private_data;
        __poll_t mask = 0;

        poll_wait(filp, &dev->read_wait, wait);

        if (radarEventAvailable(dev))
                mask |= EPOLLIN | EPOLLRDNORM;

        return mask;
}


static long radar_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        struct radar_device_data *dev = filp->private_data;
        struct presence_caps caps = { 0 };
        struct presence_state state = { 0 };
        struct presence_stats stats = { 0 };
        void __user *argp = (void __user *)arg;
        unsigned long flags;
        __u32 version;

        if (_IOC_TYPE(cmd) != PRESENCE_IOC_MAGIC)
                return -ENOTTY;

        switch (cmd) {
        case PRESENCE_IOC_GET_API_VERSION:
                version = PRESENCE_API_VERSION;
                if (copy_to_user(argp, &version, sizeof(version)) != 0)
                        return -EFAULT;
                return 0;

        case PRESENCE_IOC_GET_CAPS:
                caps.api_version = PRESENCE_API_VERSION;
                caps.sensor_type = PRESENCE_SENSOR_RADAR;
                caps.capability_flags = PRESENCE_CAP_READ |
                                         PRESENCE_CAP_POLL |
                                         PRESENCE_CAP_CURRENT_STATE |
                                         PRESENCE_CAP_STATS |
                                         PRESENCE_CAP_SINGLE_READER;
                caps.event_size = (__u32)sizeof(struct presence_event);
                caps.fifo_depth = RADAR_FIFO_DEPTH;

                if (copy_to_user(argp, &caps, sizeof(caps)) != 0)
                        return -EFAULT;
                return 0;

        case PRESENCE_IOC_GET_STATE:
                spin_lock_irqsave(&dev->lock, flags);
                state.api_version = PRESENCE_API_VERSION;
                state.sensor_type = PRESENCE_SENSOR_RADAR;
                state.raw_value = dev->current_raw;
                state.sequence = dev->sequence;
                state.last_timestamp_ns = dev->last_timestamp_ns;
                spin_unlock_irqrestore(&dev->lock, flags);

                if (copy_to_user(argp, &state, sizeof(state)) != 0)
                        return -EFAULT;
                return 0;

        case PRESENCE_IOC_GET_STATS:
                spin_lock_irqsave(&dev->lock, flags);
                stats.total_events = dev->total_events;
                stats.delivered_events = dev->delivered_events;
                stats.dropped_events = dev->dropped_events;
                stats.last_timestamp_ns = dev->stats_last_timestamp_ns;
                stats.api_version = PRESENCE_API_VERSION;
                spin_unlock_irqrestore(&dev->lock, flags);

                if (copy_to_user(argp, &stats, sizeof(stats)) != 0)
                        return -EFAULT;
                return 0;

        case PRESENCE_IOC_CLEAR_STATS:
                spin_lock_irqsave(&dev->lock, flags);
                dev->total_events = 0;
                dev->delivered_events = 0;
                dev->dropped_events = 0;
                dev->stats_last_timestamp_ns = 0;
                spin_unlock_irqrestore(&dev->lock, flags);
                return 0;

        default:
                return -ENOTTY;
        }
}


static int radar_release(struct inode *inode, struct file *filp)
{
        (void)inode;
        (void)filp;

        atomic_set(&radar_dev.reader_open, 0);
        pr_info("radar_presence: release\n");

        return 0;
}


static const struct file_operations radar_fops = {
        .owner          = THIS_MODULE,
        .open           = radar_open,
        .read           = radar_read,
        .poll           = radar_poll,
        .unlocked_ioctl = radar_ioctl,
        .release        = radar_release,
};


static int __init radar_module_init(void)
{
        int ret;
        struct device *dev_ret;

        pr_info("radar_presence: mock init (period=%dms, occupied=%ds, empty=%ds)\n",
                 period_ms, occupied_duration, empty_duration);

        spin_lock_init(&radar_dev.lock);
        init_waitqueue_head(&radar_dev.read_wait);
        atomic_set(&radar_dev.reader_open, 0);
        INIT_KFIFO(radar_dev.fifo);

        radar_dev.dropped_pending = false;
        radar_dev.presence_state = false;   /* empty로 시작 */
        radar_dev.elapsed_ms = 0;
        radar_dev.current_raw = 0;

        /* 동적 major 할당 + udev로 /dev/radar_presence 자동 생성 */
        ret = alloc_chrdev_region(&radar_devno, 0, 1, RADAR_DEV_NAME);
        if (ret < 0) {
                pr_err("radar_presence: alloc_chrdev_region failed: %d\n", ret);
                return ret;
        }

        cdev_init(&radar_dev.cdev, &radar_fops);
        radar_dev.cdev.owner = THIS_MODULE;

        ret = cdev_add(&radar_dev.cdev, radar_devno, 1);
        if (ret < 0) {
                pr_err("radar_presence: cdev_add failed: %d\n", ret);
                goto err_chrdev;
        }

        radar_class = class_create(THIS_MODULE, "radar_presence_class");
        if (IS_ERR(radar_class)) {
                ret = PTR_ERR(radar_class);
                pr_err("radar_presence: class_create failed: %d\n", ret);
                goto err_cdev;
        }

        dev_ret = device_create(radar_class, NULL, radar_devno, NULL,
                                 RADAR_DEV_NAME);
        if (IS_ERR(dev_ret)) {
                ret = PTR_ERR(dev_ret);
                pr_err("radar_presence: device_create failed: %d\n", ret);
                goto err_class;
        }

        INIT_DELAYED_WORK(&radar_dev.work, radarWorkFn);
        schedule_delayed_work(&radar_dev.work, msecs_to_jiffies(period_ms));

        pr_info("radar_presence: ready at /dev/%s (mock mode)\n", RADAR_DEV_NAME);

        return 0;

err_class:
        class_destroy(radar_class);
err_cdev:
        cdev_del(&radar_dev.cdev);
err_chrdev:
        unregister_chrdev_region(radar_devno, 1);
        return ret;
}


static void __exit radar_module_exit(void)
{
        cancel_delayed_work_sync(&radar_dev.work);

        device_destroy(radar_class, radar_devno);
        class_destroy(radar_class);
        cdev_del(&radar_dev.cdev);
        unregister_chrdev_region(radar_devno, 1);

        pr_info("radar_presence: module exit\n");
}


module_init(radar_module_init);
module_exit(radar_module_exit);

MODULE_AUTHOR("sanghyeok");
MODULE_DESCRIPTION("Mock radar presence event driver (TI IWR6843 stand-in)");
MODULE_LICENSE("GPL");
