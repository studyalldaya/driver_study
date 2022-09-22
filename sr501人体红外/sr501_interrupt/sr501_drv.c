
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/current.h>
#include <linux/wait.h>

/* 主设备号 																*/
static int major;

static struct class * sr501_class;
static struct gpio_desc * sr501_gpio;
static int irq;

static int sr501_data = 0;
static DECLARE_WAIT_QUEUE_HEAD(sr501_waitqueue);

/* 实现对应的open/read/write等函数，填入file_operations结构体 					*/
static ssize_t sr501_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
    /*printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    int val;
    int len = (size < 4)? size : 4;
    val=gpiod_get_value(sr501_gpio);
    copy_to_user(buf,&val,len);
    return len;*/
    int     len = (size < 4) ? size: 4;

    //1.有数据就copy to user
    //2.无数据就休眠,放入等待队列
    if (wait_event_interruptible(sr501_waitqueue, sr501_data))
        return - EAGAIN;

    copy_to_user(buf, &sr501_data, len);

    //此时再次发生中断，下一步就是清零，需要处理 可以使用环形缓冲区
    sr501_data  = 0;
    return len;
}

static unsigned sr501_drv_poll(struct file * fp, poll_table * wait)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

/* 定义自己的file_operations结构体												*/
struct file_operations sr501_fops = {
    .owner = THIS_MODULE, 
    .read = sr501_drv_read, 
    .poll = sr501_drv_poll, 
};

static irqreturn_t sr501_isr(int irq, void * dev_id)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1. 记录数据*/
    //sr501_data=1;
    sr501_data  = gpiod_get_value(sr501_gpio);

    /*2.唤醒app，在等待队列中唤醒*/
    if (sr501_data > 0)
        wake_up_interruptible(&sr501_waitqueue);

    return IRQ_HANDLED;
}

static int sr501_probe(struct platform_device * pdev)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1.获取硬件信息 ，可以记录下来     */
    sr501_gpio  = devm_gpiod_get(&pdev->dev, NULL, 0);
    if (IS_ERR(sr501_gpio)) {
        dev_err(&pdev->dev, "failed to get sr501_gpio\n");
        return PTR_ERR(sr501_gpio);
    }

    //gpiod_direction_input(sr501_gpio);
    irq         = gpiod_to_irq(sr501_gpio);
    request_irq(irq, sr501_isr, IRQF_TRIGGER_RISING, "sr501", NULL);

    //2.device_create
    major       = register_chrdev(0, "100ask_sr501", &sr501_fops);
    sr501_class = class_create(THIS_MODULE, "100ask_sr501_class");
    if (IS_ERR(sr501_class)) {
        unregister_chrdev(major, "100ask_sr501");
        return PTR_ERR(sr501_class);
    }

    device_create(sr501_class, NULL, MKDEV(major, 0), NULL, "100ask_sr501"); //对于多个节点，每次调用probe都创建设备。
    return 0;
}

static int sr501_remove(struct platform_device * pdev)
{
    device_destroy(sr501_class, MKDEV(major, 0));
    class_destroy(sr501_class);
    unregister_chrdev(major, "100ask_sr501");
    free_irq(irq, NULL);
    return 0;
}

static const struct of_device_id ask100_sr501[] =
{
    {
        .compatible = "100ask,sr501"
    },
    {
    },

    //必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

struct platform_driver sr501_driver = {
    .probe = sr501_probe, 
    .remove = sr501_remove, 
    .driver = {
        .name       = "100ask_sr501", 
        .of_match_table = ask100_sr501, 
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init sr501_init(void)
{
    int     err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err         = platform_driver_register(&sr501_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出�
    �函数
 *		 卸载platform_driver
 */
static void __exit sr501_exit(void)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    platform_driver_unregister(&sr501_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(sr501_init);
module_exit(sr501_exit);
MODULE_LICENSE("GPL");
