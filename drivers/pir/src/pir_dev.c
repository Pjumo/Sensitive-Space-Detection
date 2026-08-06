// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/ktime.h>

#include "presence_uapi.h"

#define PIR_DEV_NAME       "pir_dev"
#define PIR_DEV_MAJOR      230
#define PIR_FIFO_DEPTH     32U

/*
 * 현재 테스트 환경:
 * gpiochip base 512 + BCM GPIO17 = 529
 *
 * 모듈 삽입 시 변경 가능:
 * sudo insmod pir_dev.ko pir_gpio=529
 */
static int pir_gpio = 529;

module_param(pir_gpio, int, 0444);
MODULE_PARM_DESC(pir_gpio, "HC-SR501 global GPIO number");


struct pir_device_data {
        int irq;

        spinlock_t lock;
        wait_queue_head_t read_wait;
        atomic_t reader_open;

        struct presence_event fifo[PIR_FIFO_DEPTH];

        unsigned int fifo_head;
        unsigned int fifo_tail;
        unsigned int fifo_count;

        bool dropped_pending;

        __u32 current_raw;
        __u32 sequence;
        __u64 last_timestamp_ns;

        __u64 total_events;
        __u64 delivered_events;
        __u64 dropped_events;
        __u64 stats_last_timestamp_ns;
};


static struct pir_device_data pir_dev;


/* FIFO에 이벤트가 있는지 확인 */
static bool pirEventAvailable(struct pir_device_data *dev)
{
        return READ_ONCE(dev->fifo_count) > 0;
}


/* 다음 이벤트에 앞선 이벤트 누락 표시 */
static void pirMarkDroppedLocked(struct pir_device_data *dev)
{
        dev->dropped_events++;

        if(dev->fifo_count > 0)
        {
                dev->fifo[dev->fifo_tail].flags |=
                        PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
        }
        else
        {
                dev->dropped_pending = true;
        }
}


/* GPIO 인터럽트 핸들러 */
static irqreturn_t pirIntHandler(int irq, void *dev_id)
{
        struct pir_device_data *dev;
        struct presence_event event = { 0 };
        unsigned long flags;
        __u32 raw_value;
        __u64 timestamp_ns;

        (void)irq;

        dev = dev_id;

        raw_value = gpio_get_value(pir_gpio) ? 1U : 0U;
        timestamp_ns = ktime_get_ns();

        event.api_version = PRESENCE_API_VERSION;
        event.sensor_type = PRESENCE_SENSOR_PIR;

        if(raw_value == 1U)
                event.event_type = PRESENCE_EVENT_ASSERTED;
        else
                event.event_type = PRESENCE_EVENT_DEASSERTED;

        event.timestamp_ns = timestamp_ns;
        event.raw_value = raw_value;
        event.flags = 0;

        spin_lock_irqsave(&dev->lock, flags);

        dev->sequence++;
        event.sequence = dev->sequence;

        dev->current_raw = raw_value;
        dev->last_timestamp_ns = timestamp_ns;

        dev->total_events++;
        dev->stats_last_timestamp_ns = timestamp_ns;

        /*
         * 이전 read() 실패 등으로 이벤트가 누락됐다면
         * 다음 이벤트에 플래그를 설정합니다.
         */
        if(dev->dropped_pending)
        {
                event.flags |=
                        PRESENCE_EVENT_FLAG_DROPPED_BEFORE;

                dev->dropped_pending = false;
        }

        /*
         * FIFO가 가득 찼으면 가장 오래된 이벤트를 버립니다.
         */
        if(dev->fifo_count == PIR_FIFO_DEPTH)
        {
                dev->fifo_tail =
                        (dev->fifo_tail + 1U) %
                        PIR_FIFO_DEPTH;

                dev->fifo_count--;
                dev->dropped_events++;

                event.flags |=
                        PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
        }

        dev->fifo[dev->fifo_head] = event;

        dev->fifo_head =
                (dev->fifo_head + 1U) %
                PIR_FIFO_DEPTH;

        dev->fifo_count++;

        spin_unlock_irqrestore(&dev->lock, flags);

        /* poll() 또는 read()에서 대기 중인 앱 깨우기 */
        wake_up_interruptible(&dev->read_wait);

        return IRQ_HANDLED;
}


/* /dev/pir_dev open() */
static int pir_open(struct inode *inode, struct file *filp)
{
        /*
         * PRESENCE_CAP_SINGLE_READER
         * 한 번에 하나의 앱만 장치를 열 수 있습니다.
         */
        if(atomic_cmpxchg(&pir_dev.reader_open, 0, 1) != 0)
                return -EBUSY;

        filp->private_data = &pir_dev;

        pr_info(
                "pir_dev: open major=%d minor=%d\n",
                MAJOR(inode->i_rdev),
                MINOR(inode->i_rdev)
        );

        return 0;
}


/* /dev/pir_dev read() */
static ssize_t pir_read(struct file *filp,
                        char __user *buf,
                        size_t count,
                        loff_t *f_pos)
{
        struct pir_device_data *dev;
        struct presence_event event;
        unsigned long flags;
        int ret;

        (void)f_pos;

        dev = filp->private_data;

        if(count < sizeof(event))
                return -EINVAL;

        while(1)
        {
                spin_lock_irqsave(&dev->lock, flags);

                if(dev->fifo_count > 0)
                {
                        event = dev->fifo[dev->fifo_tail];

                        dev->fifo_tail =
                                (dev->fifo_tail + 1U) %
                                PIR_FIFO_DEPTH;

                        dev->fifo_count--;

                        spin_unlock_irqrestore(
                                &dev->lock,
                                flags
                        );

                        break;
                }

                spin_unlock_irqrestore(&dev->lock, flags);

                /*
                 * O_NONBLOCK으로 열었다면
                 * 이벤트가 없을 때 즉시 반환합니다.
                 */
                if(filp->f_flags & O_NONBLOCK)
                        return -EAGAIN;

                ret = wait_event_interruptible(
                        dev->read_wait,
                        pirEventAvailable(dev)
                );

                if(ret != 0)
                        return ret;
        }

        if(copy_to_user(buf, &event, sizeof(event)) != 0)
        {
                spin_lock_irqsave(&dev->lock, flags);

                pirMarkDroppedLocked(dev);

                spin_unlock_irqrestore(&dev->lock, flags);

                return -EFAULT;
        }

        spin_lock_irqsave(&dev->lock, flags);

        dev->delivered_events++;

        spin_unlock_irqrestore(&dev->lock, flags);

        return sizeof(event);
}


/* poll(), select(), epoll() 지원 */
static __poll_t pir_poll(struct file *filp,
                         struct poll_table_struct *wait)
{
        struct pir_device_data *dev;
        __poll_t mask = 0;

        dev = filp->private_data;

        poll_wait(filp, &dev->read_wait, wait);

        if(pirEventAvailable(dev))
                mask |= EPOLLIN | EPOLLRDNORM;

        return mask;
}


/* ioctl 처리 */
static long pir_ioctl(struct file *filp,
                      unsigned int cmd,
                      unsigned long arg)
{
        struct pir_device_data *dev;
        struct presence_caps caps = { 0 };
        struct presence_state state = { 0 };
        struct presence_stats stats = { 0 };

        void __user *argp;
        unsigned long flags;
        __u32 version;

        dev = filp->private_data;
        argp = (void __user *)arg;

        if(_IOC_TYPE(cmd) != PRESENCE_IOC_MAGIC)
                return -ENOTTY;

        switch(cmd)
        {
        case PRESENCE_IOC_GET_API_VERSION:

                version = PRESENCE_API_VERSION;

                if(copy_to_user(
                        argp,
                        &version,
                        sizeof(version)) != 0)
                {
                        return -EFAULT;
                }

                return 0;


        case PRESENCE_IOC_GET_CAPS:

                caps.api_version = PRESENCE_API_VERSION;
                caps.sensor_type = PRESENCE_SENSOR_PIR;

                caps.capability_flags =
                        PRESENCE_CAP_READ |
                        PRESENCE_CAP_POLL |
                        PRESENCE_CAP_CURRENT_STATE |
                        PRESENCE_CAP_STATS |
                        PRESENCE_CAP_RISING_EDGE |
                        PRESENCE_CAP_SINGLE_READER;

                caps.event_size =
                        (__u32)sizeof(struct presence_event);

                caps.fifo_depth = PIR_FIFO_DEPTH;

                if(copy_to_user(
                        argp,
                        &caps,
                        sizeof(caps)) != 0)
                {
                        return -EFAULT;
                }

                return 0;


        case PRESENCE_IOC_GET_STATE:

                spin_lock_irqsave(&dev->lock, flags);

                state.api_version =
                        PRESENCE_API_VERSION;

                state.sensor_type =
                        PRESENCE_SENSOR_PIR;

                state.raw_value =
                        dev->current_raw;

                state.sequence =
                        dev->sequence;

                state.last_timestamp_ns =
                        dev->last_timestamp_ns;

                spin_unlock_irqrestore(
                        &dev->lock,
                        flags
                );

                if(copy_to_user(
                        argp,
                        &state,
                        sizeof(state)) != 0)
                {
                        return -EFAULT;
                }

                return 0;


        case PRESENCE_IOC_GET_STATS:

                spin_lock_irqsave(&dev->lock, flags);

                stats.total_events =
                        dev->total_events;

                stats.delivered_events =
                        dev->delivered_events;

                stats.dropped_events =
                        dev->dropped_events;

                stats.last_timestamp_ns =
                        dev->stats_last_timestamp_ns;

                stats.api_version =
                        PRESENCE_API_VERSION;

                spin_unlock_irqrestore(
                        &dev->lock,
                        flags
                );

                if(copy_to_user(
                        argp,
                        &stats,
                        sizeof(stats)) != 0)
                {
                        return -EFAULT;
                }

                return 0;


        case PRESENCE_IOC_CLEAR_STATS:

                spin_lock_irqsave(&dev->lock, flags);

                dev->total_events = 0;
                dev->delivered_events = 0;
                dev->dropped_events = 0;
                dev->stats_last_timestamp_ns = 0;

                spin_unlock_irqrestore(
                        &dev->lock,
                        flags
                );

                return 0;


        default:
                return -ENOTTY;
        }
}


/* /dev/pir_dev close() */
static int pir_release(struct inode *inode,
                       struct file *filp)
{
        (void)inode;
        (void)filp;

        atomic_set(&pir_dev.reader_open, 0);

        pr_info("pir_dev: release\n");

        return 0;
}


static const struct file_operations pir_fops = {
        .owner          = THIS_MODULE,
        .open           = pir_open,
        .read           = pir_read,
        .poll           = pir_poll,
        .unlocked_ioctl = pir_ioctl,
        .release        = pir_release,
};


/* 모듈 삽입 */
static int __init pir_module_init(void)
{
        int ret;

        pr_info("pir_dev: module init\n");

        spin_lock_init(&pir_dev.lock);
        init_waitqueue_head(&pir_dev.read_wait);
        atomic_set(&pir_dev.reader_open, 0);

        pir_dev.fifo_head = 0;
        pir_dev.fifo_tail = 0;
        pir_dev.fifo_count = 0;
        pir_dev.dropped_pending = false;

        ret = gpio_request(pir_gpio, PIR_DEV_NAME);
        if(ret < 0)
        {
                pr_err(
                        "pir_dev: gpio_request(%d) failed: %d\n",
                        pir_gpio,
                        ret
                );

                return ret;
        }

        ret = gpio_direction_input(pir_gpio);
        if(ret < 0)
        {
                pr_err(
                        "pir_dev: gpio_direction_input failed: %d\n",
                        ret
                );

                goto error_gpio;
        }

        pir_dev.current_raw =
                gpio_get_value(pir_gpio) ? 1U : 0U;

        pir_dev.irq = gpio_to_irq(pir_gpio);
        if(pir_dev.irq < 0)
        {
                ret = pir_dev.irq;

                pr_err(
                        "pir_dev: gpio_to_irq failed: %d\n",
                        ret
                );

                goto error_gpio;
        }

        ret = request_irq(
                pir_dev.irq,
                pirIntHandler,
                IRQF_TRIGGER_RISING |
                IRQF_TRIGGER_FALLING,
                PIR_DEV_NAME,
                &pir_dev
        );

        if(ret < 0)
        {
                pr_err(
                        "pir_dev: request_irq failed: %d\n",
                        ret
                );

                goto error_gpio;
        }

        ret = register_chrdev(
                PIR_DEV_MAJOR,
                PIR_DEV_NAME,
                &pir_fops
        );

        if(ret < 0)
        {
                pr_err(
                        "pir_dev: register_chrdev failed: %d\n",
                        ret
                );

                goto error_irq;
        }

        pr_info(
                "pir_dev: ready major=%d gpio=%d irq=%d\n",
                PIR_DEV_MAJOR,
                pir_gpio,
                pir_dev.irq
        );

        return 0;


error_irq:
        free_irq(pir_dev.irq, &pir_dev);

error_gpio:
        gpio_free(pir_gpio);

        return ret;
}


/* 모듈 제거 */
static void __exit pir_module_exit(void)
{
        unregister_chrdev(
                PIR_DEV_MAJOR,
                PIR_DEV_NAME
        );

        free_irq(
                pir_dev.irq,
                &pir_dev
        );

        gpio_free(pir_gpio);

        pr_info("pir_dev: module exit\n");
}


module_init(pir_module_init);
module_exit(pir_module_exit);

MODULE_AUTHOR("ygy");
MODULE_DESCRIPTION("HC-SR501 presence event driver");
MODULE_LICENSE("GPL");
