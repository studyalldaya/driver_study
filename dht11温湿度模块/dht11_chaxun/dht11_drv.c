
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

static struct class * dht11_class;
static struct gpio_desc * dht11_data_pin;
//static int irq;

//static u64 dht11_data_ns = 0;
static DECLARE_WAIT_QUEUE_HEAD(dht11_waitqueue);

static void dht11_reset(void)
{
    gpiod_direction_output(dht11_data_pin, 1);

}

static void dht11_start(void)
{
    gpiod_direction_output(dht11_data_pin, 1);
	udelay(2);
    gpiod_set_value(dht11_data_pin, 0);
    mdelay(18);
    gpiod_set_value(dht11_data_pin, 1);
    udelay(40);
    gpiod_direction_input(dht11_data_pin);
}

static int wait_for_low(int timeout_us)
{
    while (gpiod_get_value(dht11_data_pin) && timeout_us) {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us) {
        return 1;
    }

    return 0;
}

static int wait_for_high(int timeout_us)
{
    while (!gpiod_get_value(dht11_data_pin) && timeout_us) {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us) {
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

static int dht11_read_byte(unsigned char * buf)
{
	int i;
    unsigned char data = 0;

    for(i = 0; i < 8; i++) {
        /* 等待高电平 */
        if (wait_for_high(200))
            return 1;
		
        udelay(40);
        if (gpiod_get_value(dht11_data_pin)) {
            //get bit 1
            data        = (data << 1) | 1;
			//！！！等待高电平结束！ 等待低电平
			if(wait_for_low(400))
				return 1;
			
        }
        else {
            //get bit 0
            data        = (data << 1) | 0;
        }
    }
	*buf = data;
    return 0;
}

/* 实现对应的open/read/write等函数，填入file_operations结构体 					*/
static ssize_t dht11_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    unsigned long flags;
    unsigned char data[5];
	int i;

	if(size != 4)
		return -EINVAL;

    local_irq_save(flags);                          //关闭中断

    //1.发送高脉冲启动dht11
    dht11_start();

    //2.等待dht11就绪
    if (dht11_wait_for_ready()) {
        local_irq_restore(flags);
        return - EAGAIN;
    }

    //3.读5字节数据
    
    for (i = 0; i < 5; i++) {
        if (dht11_read_byte(&data[i])) {
            local_irq_restore(flags);
            return - EAGAIN;
        }
    }
	dht11_reset();
    local_irq_restore(flags);                       //恢复中断

    //4.根据校验码验证数据
	if(data[4] != (data[0]+data[1]+data[2]+data[3]))
		return -1;
	
    //5.返回值 copy to user
    /* data[0] data[1] : 湿度*/
	/* data[2] data[3] : 温度*/
	if(copy_to_user(buf , data , 4))
		return -EFAULT;

    return 4;
}

/* 定义自己的file_operations结构体												*/
struct file_operations dht11_fops = {
    .owner = THIS_MODULE, 
    .read = dht11_drv_read, 
};
	
#if 0
static irqreturn_t dht11_isr(int irq, void * dev_id)
{
    int     val;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    val         = gpiod_get_value(dht11_echo);
    if (val) {
        dht11_data_ns = ktime_get_ns();             //上升沿        
    }
    else {
        dht11_data_ns = ktime_get_ns() -dht11_data_ns; //下降沿		

        /*2.唤醒app，在等待队列中唤醒*/
        wake_up(&dht11_waitqueue);
    }

    return IRQ_HANDLED;
}
#endif

static int dht11_probe(struct platform_device * pdev)
{
    //int     irq_ret;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1.获取硬件信息 ，可以记录下来     */
    dht11_data_pin = devm_gpiod_get(&pdev->dev, "dht11", GPIOD_ASIS);
    if (IS_ERR(dht11_data_pin)) {
        dev_err(&pdev->dev, "failed to get dht11_data_pin\n");
        return PTR_ERR(dht11_data_pin);
    }
/*
    irq         = gpiod_to_irq(dht11_echo);
    irq_ret     = request_irq(irq, dht11_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "dht11", NULL);
    if (irq_ret) {
        dev_err(&pdev->dev, "request_irq error!\n");
        return - EAGAIN;
    }
*/
    //2.device_create
    major       = register_chrdev(0, "100ask_dht11", &dht11_fops);
    dht11_class = class_create(THIS_MODULE, "100ask_dht11_class");
    if (IS_ERR(dht11_class)) {
        unregister_chrdev(major, "100ask_dht11");
        return PTR_ERR(dht11_class);
    }

    device_create(dht11_class, NULL, MKDEV(major, 0), NULL, "100ask_dht11"); //对于多个节点，每次调用probe都创建设备。
    return 0;
}

static int dht11_remove(struct platform_device * pdev)
{
    device_destroy(dht11_class, MKDEV(major, 0));
    class_destroy(dht11_class);
    unregister_chrdev(major, "100ask_dht11");
    //free_irq(irq, NULL);
    return 0;
}

static const struct of_device_id ask100_dht11[] =
{
    {
        .compatible = "100ask,dht11"
    },
    {
    },

    //必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

struct platform_driver dht11_driver = {
    .probe = dht11_probe, 
    .remove = dht11_remove, 
    .driver = {
        .name       = "100ask_dht11", 
        .of_match_table = ask100_dht11, 
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init dht11_init(void)
{
    int     err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err         = platform_driver_register(&dht11_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出�
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
