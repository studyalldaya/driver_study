
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
#include <linux/delay.h>
// 此驱动有问题，不用
/* 主设备号 																*/
static int major;

static struct class *dht11_class;
static struct gpio_desc *dht11_wait_cond_pin;
static int irq;

static int dht11_wait_cond = 0;
static u64 dht11_edge_time[100];
static int dht11_edge_irq_cnt = 0;

// static u64 dht11_wait_cond_ns = 0;
static DECLARE_WAIT_QUEUE_HEAD(dht11_waitqueue);

static void dht11_reset(void)
{
    gpiod_direction_output(dht11_wait_cond_pin, 1);
}

static void dht11_start(void)
{
    gpiod_direction_output(dht11_wait_cond_pin, 1);
    udelay(2);
    gpiod_set_value(dht11_wait_cond_pin, 0);
    mdelay(18);
    gpiod_set_value(dht11_wait_cond_pin, 1);
    udelay(40);
    gpiod_direction_input(dht11_wait_cond_pin);
}

static int wait_for_low(int timeout_us)
{
    while (gpiod_get_value(dht11_wait_cond_pin) && timeout_us)
    {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us)
    {
        return 1;
    }

    return 0;
}

static int wait_for_high(int timeout_us)
{
    while (!gpiod_get_value(dht11_wait_cond_pin) && timeout_us)
    {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us)
    {
        return 1;
    }

    return 0;
}

static int dht11_wait_for_ready(void)
{
    /* 等待低电平 */
    if (wait_for_low(200))
        return 1;

    /* 等待高电平 */
    if (wait_for_high(200))
        return 1;

    /* 高电平来了 */
    /* 等待低电平 */
    if (wait_for_low(200))
        return 1;

    return 0;
}

static int dht11_data_parse(char *data)
{
    int i, j, m = 0;
    int offset = 0;

    for (offset = 0; offset <= 2; offset++)
    {
        m = offset;
        for (i = 0; i < 5; i++)
        {
            data[i] = 0;
            for (j = 0; j < 8; j++)
            {
                data[i] = data[i] << 1;
                if (dht11_edge_time[m + 1] - dht11_edge_time[m] >= 40000)
                {
                    data[i] = data[i] | 1;
                }

                m = m + 2;
            }
        }

        // 根据校验码验证数据
        if (data[4] == (data[0] + data[1] + data[2] + data[3]))
            return 0;
    }

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 1;
}

static irqreturn_t dht11_isr(int irq, void *dev_id)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    dht11_edge_time[dht11_edge_irq_cnt] = ktime_get_ns();
    dht11_edge_irq_cnt++;
    if (dht11_edge_irq_cnt >= 82)
    { /* 40*2 +1+1 最多82个中断(加上回应信号的两个) */
        dht11_wait_cond = 1;
        wake_up(&dht11_waitqueue);
    }

    return IRQ_HANDLED;
}

/* 实现对应的open/read/write等函数，填入file_operations结构体 					*/
static ssize_t dht11_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    unsigned char data[5];
    int irq_ret;
    int timeout;

    if (size != 4)
        return -EINVAL;

    // 1.启动dht11
    dht11_start();

    // 请求中断
    irq_ret = request_irq(irq, dht11_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "dht11", NULL);
    if (irq_ret)
    {
        printk(KERN_ERR "request_irq error!\n");
        return -EAGAIN;
    }

#if 0

    //2.等待dht11就绪,等待回应
    if (dht11_wait_for_ready()) {
        return - EAGAIN;
    }

#endif

    // 每次请求之前清零
    dht11_edge_irq_cnt = 0;

    timeout = wait_event_timeout(dht11_waitqueue, dht11_wait_cond, HZ); /*超时有可能是81个或者80个，所
        以return错误*/
#if 0
    if (!wait_event_timeout(dht11_waitqueue, dht11_wait_cond, HZ)) {
        free_irq(irq, NULL);
        dht11_reset();
        return - ETIMEDOUT;
    }

#endif

    free_irq(irq, NULL);
    dht11_reset();
    if (dht11_data_parse(data))
    {
        return -EAGAIN;
    }

    dht11_wait_cond = 0;

    // 5.返回值 copy to user

    /* data[0] data[1] : 湿度*/
    /* data[2] data[3] : 温度*/
    if (copy_to_user(buf, data, 4))
        return -EFAULT;

    return 4;
}

/* 定义自己的file_operations结构体												*/
struct file_operations dht11_fops = {
    .owner = THIS_MODULE,
    .read = dht11_drv_read,
};

static int dht11_probe(struct platform_device *pdev)
{
    // int     irq_ret;
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1.获取硬件信息 ，可以记录下来     */
    dht11_wait_cond_pin = devm_gpiod_get(&pdev->dev, "dht11", GPIOD_ASIS);
    if (IS_ERR(dht11_wait_cond_pin))
    {
        dev_err(&pdev->dev, "failed to get dht11_wait_cond_pin\n");
        return PTR_ERR(dht11_wait_cond_pin);
    }

    irq = gpiod_to_irq(dht11_wait_cond_pin);

    // 2.device_create
    major = register_chrdev(0, "100ask_dht11", &dht11_fops);
    dht11_class = class_create(THIS_MODULE, "100ask_dht11_class");
    if (IS_ERR(dht11_class))
    {
        unregister_chrdev(major, "100ask_dht11");
        return PTR_ERR(dht11_class);
    }

    device_create(dht11_class, NULL, MKDEV(major, 0), NULL, "100ask_dht11"); // 对于多个节点，每次调用probe都创建设备。
    return 0;
}

static int dht11_remove(struct platform_device *pdev)
{
    device_destroy(dht11_class, MKDEV(major, 0));
    class_destroy(dht11_class);
    unregister_chrdev(major, "100ask_dht11");
    return 0;
}

static const struct of_device_id ask100_dht11[] =
    {
        {.compatible = "100ask,dht11"},
        {},

        // 必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

    struct platform_driver dht11_driver = {
        .probe = dht11_probe,
        .remove = dht11_remove,
        .driver = {
            .name = "100ask_dht11",
            .of_match_table = ask100_dht11,
        },
};

/* 2. 在入口函数注册platform_driver */
static int __init dht11_init(void)
{
    int err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err = platform_driver_register(&dht11_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出��
    �函数
 *		 卸载platform_driver
 */
static void __exit dht11_exit(void)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    platform_driver_unregister(&dht11_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(dht11_init);
module_exit(dht11_exit);
MODULE_LICENSE("GPL");
