
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

/* 主设备号 																*/
static int major;

static struct class * sr04_class;
static struct gpio_desc * sr04_trig;
static struct gpio_desc * sr04_echo;
static int irq;

static u64 sr04_data_ns = 0;
static DECLARE_WAIT_QUEUE_HEAD(sr04_waitqueue);

/* 实现对应的open/read/write等函数，填入file_operations结构体 					*/
static ssize_t sr04_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
	//int timeout;
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    //开启
    gpiod_set_value(sr04_trig, 1);
    udelay(15);
    gpiod_set_value(sr04_trig, 0);

    //等待数据        condition为真 函数是返回0
    if (wait_event_interruptible(sr04_waitqueue, sr04_data_ns))
        return - EAGAIN;
    
	if(copy_to_user(buf, &sr04_data_ns, sizeof(int)))
		return -EFAULT;
	sr04_data_ns = 0;
	return sizeof(int);
	
    /*
	timeout = wait_event_interruptible_timeout(sr04_waitqueue, sr04_data_ns,HZ);
	if(timeout){
	    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	    //if (copy_to_user(buf, &sr04_data_ns, sizeof(int)))
	        //return - EFAULT;
		copy_to_user(buf, &sr04_data_ns, sizeof(int));
	    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	    sr04_data_ns = 0;
		return sizeof(int);
	}
	else{
		return -EAGAIN;
	}*/
    
}

/* 定义自己的file_operations结构体												*/
struct file_operations sr04_fops = {
    .owner = THIS_MODULE, 
    .read = sr04_drv_read, 
};

static irqreturn_t sr04_isr(int irq, void * dev_id)
{
    int     val;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    val         = gpiod_get_value(sr04_echo);
    if (val) {
        sr04_data_ns = ktime_get_ns();              //上升沿        
    }
    else {
        sr04_data_ns = ktime_get_ns() -sr04_data_ns; //下降沿		
        /*2.唤醒app，在等待队列中唤醒*/       
        wake_up(&sr04_waitqueue);
    }

    return IRQ_HANDLED;
}

static int sr04_probe(struct platform_device * pdev)
{
	int irq_ret;
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1.获取硬件信息 ，可以记录下来     */
    sr04_trig   = devm_gpiod_get(&pdev->dev, "trig", GPIOD_OUT_LOW);
    if (IS_ERR(sr04_trig)) {
        dev_err(&pdev->dev, "failed to get sr04_trig\n");
        return PTR_ERR(sr04_trig);
    }

    sr04_echo   = devm_gpiod_get(&pdev->dev, "echo", GPIOD_IN);
    if (IS_ERR(sr04_echo)) {
        dev_err(&pdev->dev, "failed to get sr04_echo\n");
        return PTR_ERR(sr04_echo);
    }

    irq         = gpiod_to_irq(sr04_echo);
    irq_ret = request_irq(irq, sr04_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "sr04", NULL);
	if(irq_ret){
		dev_err(&pdev->dev , "request_irq error!\n");
		return -EAGAIN;
	}

    //2.device_create
    major       = register_chrdev(0, "100ask_sr04", &sr04_fops);
    sr04_class  = class_create(THIS_MODULE, "100ask_sr04_class");
    if (IS_ERR(sr04_class)) {
        unregister_chrdev(major, "100ask_sr04");
        return PTR_ERR(sr04_class);
    }

    device_create(sr04_class, NULL, MKDEV(major, 0), NULL, "100ask_sr04"); //对于多个节点，每次调用probe都创建设备。
    return 0;
}

static int sr04_remove(struct platform_device * pdev)
{
    device_destroy(sr04_class, MKDEV(major, 0));
    class_destroy(sr04_class);
    unregister_chrdev(major, "100ask_sr04");
    free_irq(irq, NULL);
    return 0;
}

static const struct of_device_id ask100_sr04[] =
{
    {
        .compatible = "100ask,sr04"
    },
    {
    },

    //必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

struct platform_driver sr04_driver = {
    .probe = sr04_probe, 
    .remove = sr04_remove, 
    .driver = {
        .name       = "100ask_sr04", 
        .of_match_table = ask100_sr04, 
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init sr04_init(void)
{
    int     err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err         = platform_driver_register(&sr04_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出�
    �函数
 *		 卸载platform_driver
 */
static void __exit sr04_exit(void)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    platform_driver_unregister(&sr04_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(sr04_init);
module_exit(sr04_exit);
MODULE_LICENSE("GPL");
