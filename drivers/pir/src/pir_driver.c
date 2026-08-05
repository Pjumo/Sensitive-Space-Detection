// SPDX-License-Identifier: GPL-2.0

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "presence_uapi.h"

#define PIR_DEVICE_NAME "pir_presence"
#define PIR_FIFO_DEPTH  64U

struct pir_presence_device {
    struct device *dev;
    struct gpio_desc *gpio;
    int irq;

    struct miscdevice miscdev;

    DECLARE_KFIFO_PTR(event_fifo, struct presence_event);

    spinlock_t lock;
    wait_queue_head_t read_queue;

    atomic_t opened;
    bool shutting_down;

    __u32 sequence;
    __u32 last_raw_value;
    __u64 last_event_timestamp_ns;

    __u64 total_events;
    __u64 delivered_events;
    __u64 dropped_events;
    __u64 stats_last_timestamp_ns;
};

static bool pir_event_available(struct pir_presence_device *pir)
{
    unsigned long flags;
    bool available;

    spin_lock_irqsave(&pir->lock, flags);
    available = !kfifo_is_empty(&pir->event_fifo);
    spin_unlock_irqrestore(&pir->lock, flags);

    return available;
}

static void pir_queue_event(struct pir_presence_device *pir,
                            struct presence_event *event)
{
    struct presence_event discarded;
    unsigned long flags;
    bool queued;

    spin_lock_irqsave(&pir->lock, flags);

    pir->sequence++;
    event->sequence = pir->sequence;

    pir->total_events++;
    pir->last_raw_value = event->raw_value;
    pir->last_event_timestamp_ns = event->timestamp_ns;
    pir->stats_last_timestamp_ns = event->timestamp_ns;

    if (kfifo_is_full(&pir->event_fifo)) {
        if (kfifo_get(&pir->event_fifo, &discarded)) {
            pir->dropped_events++;
            event->flags |=
                PRESENCE_EVENT_FLAG_DROPPED_BEFORE;
        }
    }

    queued = kfifo_put(&pir->event_fifo, *event);

    if (!queued)
        pir->dropped_events++;

    spin_unlock_irqrestore(&pir->lock, flags);

    if (queued)
        wake_up_interruptible(&pir->read_queue);
}

static irqreturn_t pir_irq_thread(int irq, void *data)
{
    struct pir_presence_device *pir = data;
    struct presence_event event = {0};
    int value;

    (void)irq;

    if (READ_ONCE(pir->shutting_down))
        return IRQ_HANDLED;

    value = gpiod_get_value_cansleep(pir->gpio);

    if (value < 0) {
        dev_err_ratelimited(
            pir->dev,
            "failed to read PIR GPIO: %d\n",
            value);

        return IRQ_HANDLED;
    }

    /*
     * This version reports only rising-edge events.
     * Ignore the event if the GPIO has already returned LOW.
     */
    if (!value)
        return IRQ_HANDLED;

    event.api_version = PRESENCE_API_VERSION;
    event.sensor_type = PRESENCE_SENSOR_PIR;
    event.event_type = PRESENCE_EVENT_ASSERTED;
    event.timestamp_ns = ktime_get_boottime_ns();
    event.raw_value = 1U;
    event.flags = 0U;

    pir_queue_event(pir, &event);

    return IRQ_HANDLED;
}

static int pir_open(struct inode *inode, struct file *file)
{
    struct miscdevice *miscdev = file->private_data;
    struct pir_presence_device *pir;
    int ret;

    pir = container_of(
        miscdev,
        struct pir_presence_device,
        miscdev);

    if (READ_ONCE(pir->shutting_down))
        return -ENODEV;

    if (atomic_cmpxchg(&pir->opened, 0, 1) != 0)
        return -EBUSY;

    ret = nonseekable_open(inode, file);

    if (ret) {
        atomic_set(&pir->opened, 0);
        return ret;
    }

    file->private_data = pir;

    return 0;
}

static int pir_release(struct inode *inode, struct file *file)
{
    struct pir_presence_device *pir = file->private_data;

    (void)inode;

    atomic_set(&pir->opened, 0);

    return 0;
}

static ssize_t pir_read(struct file *file,
                        char __user *buffer,
                        size_t count,
                        loff_t *offset)
{
    struct pir_presence_device *pir = file->private_data;
    struct presence_event event;
    unsigned long flags;
    int ret;

    (void)offset;

    if (count < sizeof(event))
        return -EINVAL;

    for (;;) {
        spin_lock_irqsave(&pir->lock, flags);

        if (kfifo_get(&pir->event_fifo, &event)) {
            spin_unlock_irqrestore(&pir->lock, flags);
            break;
        }

        if (pir->shutting_down) {
            spin_unlock_irqrestore(&pir->lock, flags);
            return -ENODEV;
        }

        spin_unlock_irqrestore(&pir->lock, flags);

        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(
            pir->read_queue,
            READ_ONCE(pir->shutting_down) ||
            pir_event_available(pir));

        if (ret)
            return ret;
    }

    if (copy_to_user(buffer, &event, sizeof(event))) {
        spin_lock_irqsave(&pir->lock, flags);
        pir->dropped_events++;
        spin_unlock_irqrestore(&pir->lock, flags);

        return -EFAULT;
    }

    spin_lock_irqsave(&pir->lock, flags);
    pir->delivered_events++;
    spin_unlock_irqrestore(&pir->lock, flags);

    return sizeof(event);
}

static __poll_t pir_poll(struct file *file,
                         struct poll_table_struct *wait)
{
    struct pir_presence_device *pir = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &pir->read_queue, wait);

    if (pir_event_available(pir))
        mask |= EPOLLIN | EPOLLRDNORM;

    if (READ_ONCE(pir->shutting_down))
        mask |= EPOLLERR | EPOLLHUP;

    return mask;
}

static long pir_ioctl(struct file *file,
                      unsigned int command,
                      unsigned long argument)
{
    struct pir_presence_device *pir = file->private_data;
    void __user *argument_pointer =
        (void __user *)argument;
    unsigned long flags;

    if (READ_ONCE(pir->shutting_down))
        return -ENODEV;

    switch (command) {
    case PRESENCE_IOC_GET_API_VERSION: {
        __u32 version = PRESENCE_API_VERSION;

        if (copy_to_user(
                argument_pointer,
                &version,
                sizeof(version)))
            return -EFAULT;

        return 0;
    }

    case PRESENCE_IOC_GET_CAPS: {
        struct presence_caps caps = {
            .api_version = PRESENCE_API_VERSION,
            .sensor_type = PRESENCE_SENSOR_PIR,

            .capability_flags =
                PRESENCE_CAP_READ |
                PRESENCE_CAP_POLL |
                PRESENCE_CAP_CURRENT_STATE |
                PRESENCE_CAP_STATS |
                PRESENCE_CAP_RISING_EDGE |
                PRESENCE_CAP_SINGLE_READER,

            .event_size =
                sizeof(struct presence_event),

            .fifo_depth = PIR_FIFO_DEPTH,
        };

        if (copy_to_user(
                argument_pointer,
                &caps,
                sizeof(caps)))
            return -EFAULT;

        return 0;
    }

    case PRESENCE_IOC_GET_STATE: {
        struct presence_state state = {0};
        int value;

        value = gpiod_get_value_cansleep(pir->gpio);

        if (value < 0)
            return value;

        state.api_version = PRESENCE_API_VERSION;
        state.sensor_type = PRESENCE_SENSOR_PIR;
        state.raw_value = value ? 1U : 0U;

        spin_lock_irqsave(&pir->lock, flags);

        state.sequence = pir->sequence;
        state.last_timestamp_ns =
            pir->last_event_timestamp_ns;

        spin_unlock_irqrestore(&pir->lock, flags);

        if (copy_to_user(
                argument_pointer,
                &state,
                sizeof(state)))
            return -EFAULT;

        return 0;
    }

    case PRESENCE_IOC_GET_STATS: {
        struct presence_stats stats = {0};

        stats.api_version = PRESENCE_API_VERSION;

        spin_lock_irqsave(&pir->lock, flags);

        stats.total_events =
            pir->total_events;

        stats.delivered_events =
            pir->delivered_events;

        stats.dropped_events =
            pir->dropped_events;

        stats.last_timestamp_ns =
            pir->stats_last_timestamp_ns;

        spin_unlock_irqrestore(&pir->lock, flags);

        if (copy_to_user(
                argument_pointer,
                &stats,
                sizeof(stats)))
            return -EFAULT;

        return 0;
    }

    case PRESENCE_IOC_CLEAR_STATS:
        spin_lock_irqsave(&pir->lock, flags);

        pir->total_events = 0;
        pir->delivered_events = 0;
        pir->dropped_events = 0;
        pir->stats_last_timestamp_ns = 0;

        spin_unlock_irqrestore(&pir->lock, flags);

        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations pir_file_operations = {
    .owner = THIS_MODULE,
    .open = pir_open,
    .release = pir_release,
    .read = pir_read,
    .poll = pir_poll,
    .unlocked_ioctl = pir_ioctl,
};

static int pir_probe(struct platform_device *platform_device)
{
    struct device *dev = &platform_device->dev;
    struct pir_presence_device *pir;
    int initial_value;
    int ret;

    pir = devm_kzalloc(
        dev,
        sizeof(*pir),
        GFP_KERNEL);

    if (!pir)
        return -ENOMEM;

    pir->dev = dev;

    spin_lock_init(&pir->lock);
    init_waitqueue_head(&pir->read_queue);
    atomic_set(&pir->opened, 0);

    ret = kfifo_alloc(
        &pir->event_fifo,
        PIR_FIFO_DEPTH,
        GFP_KERNEL);

    if (ret) {
        return dev_err_probe(
            dev,
            ret,
            "failed to allocate event FIFO\n");
    }

    pir->gpio = devm_gpiod_get(
        dev,
        "pir",
        GPIOD_IN);

    if (IS_ERR(pir->gpio)) {
        ret = PTR_ERR(pir->gpio);

        dev_err_probe(
            dev,
            ret,
            "failed to acquire pir-gpios\n");

        goto error_free_fifo;
    }

    initial_value =
        gpiod_get_value_cansleep(pir->gpio);

    if (initial_value < 0) {
        ret = initial_value;

        dev_err_probe(
            dev,
            ret,
            "failed to read initial GPIO state\n");

        goto error_free_fifo;
    }

    pir->last_raw_value =
        initial_value ? 1U : 0U;

    pir->irq = gpiod_to_irq(pir->gpio);

    if (pir->irq < 0) {
        ret = pir->irq;

        dev_err_probe(
            dev,
            ret,
            "failed to convert GPIO to IRQ\n");

        goto error_free_fifo;
    }

    platform_set_drvdata(
        platform_device,
        pir);

    ret = devm_request_threaded_irq(
        dev,
        pir->irq,
        NULL,
        pir_irq_thread,
        IRQF_TRIGGER_RISING | IRQF_ONESHOT,
        PIR_DEVICE_NAME,
        pir);

    if (ret) {
        dev_err_probe(
            dev,
            ret,
            "failed to request IRQ %d\n",
            pir->irq);

        goto error_free_fifo;
    }

    pir->miscdev.minor = MISC_DYNAMIC_MINOR;
    pir->miscdev.name = PIR_DEVICE_NAME;
    pir->miscdev.fops = &pir_file_operations;
    pir->miscdev.parent = dev;
    pir->miscdev.mode = 0444;

    ret = misc_register(&pir->miscdev);

    if (ret) {
        dev_err_probe(
            dev,
            ret,
            "failed to register misc device\n");

        goto error_free_fifo;
    }

    dev_info(
        dev,
        "registered /dev/%s, IRQ=%d, initial GPIO=%u\n",
        PIR_DEVICE_NAME,
        pir->irq,
        pir->last_raw_value);

    return 0;

error_free_fifo:
    kfifo_free(&pir->event_fifo);

    return ret;
}

static void pir_remove(
    struct platform_device *platform_device)
{
    struct pir_presence_device *pir =
        platform_get_drvdata(platform_device);

    WRITE_ONCE(pir->shutting_down, true);

    disable_irq(pir->irq);
    misc_deregister(&pir->miscdev);

    wake_up_interruptible(&pir->read_queue);

    kfifo_free(&pir->event_fifo);

    dev_info(
        &platform_device->dev,
        "PIR presence driver removed\n");
}

static const struct of_device_id pir_of_match[] = {
    {
        .compatible = "project,pir-presence",
    },
    {}
};

MODULE_DEVICE_TABLE(of, pir_of_match);

static struct platform_driver pir_platform_driver = {
    .probe = pir_probe,
    .remove = pir_remove,

    .driver = {
        .name = PIR_DEVICE_NAME,
        .of_match_table = pir_of_match,
    },
};

module_platform_driver(pir_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sensitive Space Detection Team");
MODULE_DESCRIPTION("PIR GPIO presence event driver");
MODULE_VERSION("1.0");

