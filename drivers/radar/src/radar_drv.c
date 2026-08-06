// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/random.h>

#include "presence_uapi.h"

#define RADAR_DEV_NAME       "radar_dev"
#define RADAR_DEV_MAJOR      231
#define RADAR_FIFO_DEPTH     64U   /* 설계 문서 기준(64), PIR은 32로 구현되어 있어 불일치 참고 */

/*
 * === Mock 전용 파라미터 ===
 * 실제 TI IWR6843을 붙일 때는 이 타이머 로직 전체를
 * SPI/UART 수신 인터럽트 또는 워커 스레드로 교체하면 됩니다.
 * presence_event를 만들어 FIFO에 넣는 이후 로직은 그대로 재사용 가능합니다.
 */
static int radar_period_ms = 300;      /* 샘플링 주기 */
static int radar_min_cm = 50;          /* 시뮬레이션 최소 거리(cm) */
static int radar_max_cm = 400;         /* 시뮬레이션 최대 거리(cm) */
static int radar_threshold_cm = 150;   /* 이 값보다 가까우면 "감지"로 판단 */

module_param(radar_period_ms, int, 0444);
module_param(radar_min_cm, int, 0444);
module_param(radar_max_cm, int, 0444);
module_param(radar_threshold_cm, int, 0444);
MODULE_PARM_DESC(radar_period_ms, "Mock sampling period in ms");
MODULE_PARM_DESC(radar_threshold_cm, "Distance threshold(cm) for presence");


struct radar_device_data {
        struct timer_list sample_timer;

        spinlock_t lock;
        wait_queue_head_t read_wait;
        atomic_t reader_open;

        struct presence_event fifo[RADAR_FIFO_DEPTH];
        unsigned int fifo_head;
        unsigned int fifo_tail;
        unsigned int fifo_count;

        bool dropped_pending;

        __u32 current_raw;      /* 마지막 시뮬레이션 거리값(cm) */
        bool  presence_state;   /* 현재 "감지됨" 여부 (threshold 기준) */
        __u32 sequence;
        __u64 last_timestamp_ns;

        __u64 total_events;
        __u64 delivered_events;
        __u64 dropped_events;
        __u64 stats_last_timestamp_ns;
};

static struct radar_device_data radar_dev;


static bool radarEventAvailable(struct radar_device_data *dev)
{
        return READ_ONCE(dev->fifo_count) > 0;
}


static void radarMarkDroppedLocked(struct radar_device_data *dev)
{
        dev->dropped_events++;

        if (dev->fifo_count > 0)
                dev->fifo[dev->fifo_tail].flags |= PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
        else
                dev->dropped_pending = true;
}


/*
 * 가짜 거리값 생성.
 * 완전 균일 난수 대신, 이전 값에서 조금씩 랜덤워크 시키는 방식으로
 * 사람이 다가오고 멀어지는 듯한 자연스러운 패턴을 흉내냅니다.
 */
static __u32 radarGenerateRawCm(__u32 prev_cm)
{
        int delta;
        int next;

        delta = (int)(get_random_u32() % 41) - 20; /* -20 ~ +20 cm 변화 */
        next = (int)prev_cm + delta;

        if (next < radar_min_cm)
                next = radar_min_cm;
        if (next > radar_max_cm)
                next = radar_max_cm;

        return (__u32)next;
}


/* 커널 타이머 콜백: 실제 하드웨어의 인터럽트 핸들러 자리를 대신함 */
static void radarSampleTimerFn(struct timer_list *t)
{
        struct radar_device_data *dev = from_timer(dev, t, sample_timer);
        struct presence_event event = { 0 };
        unsigned long flags;
        __u32 raw_value;
        __u64 timestamp_ns;
        bool new_presence;

        raw_value = radarGenerateRawCm(dev->current_raw ? dev->current_raw : (__u32)radar_max_cm);
        timestamp_ns = ktime_get_ns();
        new_presence = (raw_value < (__u32)radar_threshold_cm);

        spin_lock_irqsave(&dev->lock, flags);

        dev->current_raw = raw_value;
        dev->last_timestamp_ns = timestamp_ns;

        /* PIR과 동일하게 "상태가 바뀔 때만" 이벤트를 만듭니다 (edge-triggered) */
        if (new_presence != dev->presence_state) {
                dev->presence_state = new_presence;

                dev->sequence++;
                event.api_version = PRESENCE_API_VERSION;
                event.sensor_type = PRESENCE_SENSOR_RADAR;
                event.event_type = new_presence ? PRESENCE_EVENT_ASSERTED
                                                 : PRESENCE_EVENT_DEASSERTED;
                event.sequence = dev->sequence;
                event.timestamp_ns = timestamp_ns;
                event.raw_value = raw_value;
                event.flags = 0;

                dev->total_events++;
                dev->stats_last_timestamp_ns = timestamp_ns;

                if (dev->dropped_pending) {
                        event.flags |= PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
                        dev->dropped_pending = false;
                }

                if (dev->fifo_count == RADAR_FIFO_DEPTH) {
                        dev->fifo_tail = (dev->fifo_tail + 1U) % RADAR_FIFO_DEPTH;
                        dev->fifo_count--;
                        dev->dropped_events++;
                        event.flags |= PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
                }

                dev->fifo[dev->fifo_head] = event;
                dev->fifo_head = (dev->fifo_head + 1U) % RADAR_FIFO_DEPTH;
                dev->fifo_count++;

                spin_unlock_irqrestore(&dev->lock, flags);

                wake_up_interruptible(&dev->read_wait);
        } else {
                spin_unlock_irqrestore(&dev->lock, flags);
        }

        /* 다음 샘플링을 위해 타이머 재무장 (인터럽트는 재트리거가 자동이지만 타이머는 직접 재무장해야 함) */
        mod_timer(&dev->sample_timer,
                  jiffies + msecs_to_jiffies(radar_period_ms));
}


static int radar_open(struct inode *inode, struct file *filp)
{
        if (atomic_cmpxchg(&radar_dev.reader_open, 0, 1) != 0)
                return -EBUSY;

        filp->private_data = &radar_dev;

        pr_info("radar_dev: open major=%d minor=%d\n",
                MAJOR(inode->i_rdev), MINOR(inode->i_rdev));

        return 0;
}


static ssize_t radar_read(struct file *filp, char __user *buf,
                           size_t count, loff_t *f_pos)
{
        struct radar_device_data *dev = filp->private_data;
        struct presence_event event;
        unsigned long flags;
        int ret;

        (void)f_pos;

        if (count < sizeof(event))
                return -EINVAL;

        while (1) {
                spin_lock_irqsave(&dev->lock, flags);

                if (dev->fifo_count > 0) {
                        event = dev->fifo[dev->fifo_tail];
                        dev->fifo_tail = (dev->fifo_tail + 1U) % RADAR_FIFO_DEPTH;
                        dev->fifo_count--;

                        spin_unlock_irqrestore(&dev->lock, flags);
                        break;
                }

                spin_unlock_irqrestore(&dev->lock, flags);

                if (filp->f_flags & O_NONBLOCK)
                        return -EAGAIN;

                ret = wait_event_interruptible(dev->read_wait,
                                                radarEventAvailable(dev));
                if (ret != 0)
                        return ret;
        }

        if (copy_to_user(buf, &event, sizeof(event)) != 0) {
                spin_lock_irqsave(&dev->lock, flags);
                radarMarkDroppedLocked(dev);
                spin_unlock_irqrestore(&dev->lock, flags);
                return -EFAULT;
        }

        spin_lock_irqsave(&dev->lock, flags);
        dev->delivered_events++;
        spin_unlock_irqrestore(&dev->lock, flags);

        return sizeof(event);
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
        pr_info("radar_dev: release\n");

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

        pr_info("radar_dev: module init (mock, period=%dms, threshold=%dcm)\n",
                 radar_period_ms, radar_threshold_cm);

        spin_lock_init(&radar_dev.lock);
        init_waitqueue_head(&radar_dev.read_wait);
        atomic_set(&radar_dev.reader_open, 0);

        radar_dev.fifo_head = 0;
        radar_dev.fifo_tail = 0;
        radar_dev.fifo_count = 0;
        radar_dev.dropped_pending = false;
        radar_dev.current_raw = (__u32)radar_max_cm;
        radar_dev.presence_state = false;

        ret = register_chrdev(RADAR_DEV_MAJOR, RADAR_DEV_NAME, &radar_fops);
        if (ret < 0) {
                pr_err("radar_dev: register_chrdev failed: %d\n", ret);
                return ret;
        }

        timer_setup(&radar_dev.sample_timer, radarSampleTimerFn, 0);
        mod_timer(&radar_dev.sample_timer,
                  jiffies + msecs_to_jiffies(radar_period_ms));

        pr_info("radar_dev: ready major=%d (mock mode)\n", RADAR_DEV_MAJOR);

        return 0;
}


static void __exit radar_module_exit(void)
{
        del_timer_sync(&radar_dev.sample_timer);

        unregister_chrdev(RADAR_DEV_MAJOR, RADAR_DEV_NAME);

        pr_info("radar_dev: module exit\n");
}


module_init(radar_module_init);
module_exit(radar_module_exit);

MODULE_AUTHOR("sanghyeok");
MODULE_DESCRIPTION("Mock radar presence event driver (TI IWR6843 stand-in)");
MODULE_LICENSE("GPL");
