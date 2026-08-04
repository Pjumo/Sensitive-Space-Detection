#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>

static ssize_t pir_read(struct file *file,
                        char __user *buf,
                        size_t count,
                        loff_t *ppos)
{
    const char message[] = "PIR driver ready\n";

    return simple_read_from_buffer(buf,
                                   count,
                                   ppos,
                                   message,
                                   sizeof(message) - 1);
}

static const struct file_operations pir_fops = {
    .owner = THIS_MODULE,
    .read = pir_read,
    .llseek = no_llseek,
};

static struct miscdevice pir_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "pir",
    .fops = &pir_fops,
    .mode = 0444,
};

static int __init pir_init(void)
{
    int ret;

    ret = misc_register(&pir_device);
    if (ret) {
        pr_err("pir: device registration failed: %d\n", ret);
        return ret;
    }

    pr_info("pir: driver loaded\n");
    return 0;
}

static void __exit pir_exit(void)
{
    misc_deregister(&pir_device);
    pr_info("pir: driver unloaded\n");
}

module_init(pir_init);
module_exit(pir_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sensitive Space Detection Team");
MODULE_DESCRIPTION("PIR sensor character device driver");
MODULE_VERSION("0.1");
