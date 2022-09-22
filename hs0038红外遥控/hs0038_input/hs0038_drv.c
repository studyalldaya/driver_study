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
#include <linux/input.h>


//使用环形缓冲区存放val

/* 主设备号 																*/
static int major;
static struct input_dev * hs0038_input_dev;

struct device * g_dev;
static struct class * hs0038_class;
static struct gpio_desc * hs0038_data_pin;
static int irq;

static unsigned int hs0038_data = 0;

static u64 hs0038_edge_time[100];
static int hs0038_edge_irq_cnt = 0;

static DECLARE_WAIT_QUEUE_HEAD(hs0038_waitqueue);
static unsigned int hs0038_data_buf[8];
static int r, w;

static void put_data(unsigned int val)
{
    //没满
    if (((w + 1) & 7) != r) {
        hs0038_data_buf[w] = val;
        w           = (w + 1) & 7;
    }
}

static int get_data(unsigned int * val)
{
    if (r == w) {
        return - 1;
    }
    else {
        *val        = hs0038_data_buf[r];
        r           = (r + 1) & 7;
        return 0;
    }
}
static int has_data(void)
{
    if (r ==w) {
        return 0;
    }
	else
		return 1;
}


/*
** 0 : 成功
** -1 ： 没有接收完毕
** -2 :  解析错误
** 
*/
static int hs0038_data_parse(unsigned int * val)
{
    u64     temp;
    unsigned char data[4];
    int     i, j, m;

    //判断是否是重复码
    if (hs0038_edge_irq_cnt == 4) {
        temp        = hs0038_edge_time[1] -hs0038_edge_time[0];
        if (temp > 8000000 && temp < 10000000) {
            temp        = hs0038_edge_time[2] -hs0038_edge_time[1];
            if (temp < 3000000) {
                //获得了重复码
                *val        = hs0038_data;
                return 0;
            }
        }
    }

    //接收到了68次中断
    m           = 3;
    if (hs0038_edge_irq_cnt >= 68) {
        //接收到数据,解析数据
        for (i = 0; i < 4; i++) {
            data[i]     = 0;

            //bit0先接收
            for (j = 0; j < 8; j++) {
                //1
                if (hs0038_edge_time[m + 1] -hs0038_edge_time[m] > 1000000) {
                    data[i]     |= (1 << j);
                }

                m           += 2;
            }
        }

        //检验数据
        data[1]     = ~data[1];
        if ((data[0]) != (data[1]))
            return - 2;

        data[3]     = ~data[3];
        if ((data[2]) != (data[3]))
            return - 2;

        hs0038_data = (data[0] << 8) | (data[2]);
        *val        = hs0038_data;
        return 0;
    }
    else {
        //数据没有接收完
        return - 1;
    }

    return 0;
}

static irqreturn_t hs0038_isr(int irq, void * dev_id)
{
    //printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    unsigned int val;
    int     ret;

    hs0038_edge_time[hs0038_edge_irq_cnt] = ktime_get_boot_ns();
    hs0038_edge_irq_cnt++;

    //判断是否超时
    if (hs0038_edge_irq_cnt >= 2) {
        if (hs0038_edge_time[hs0038_edge_irq_cnt - 1] -hs0038_edge_time[hs0038_edge_irq_cnt - 2] > 30000000) {
            //超时	
            dev_err(g_dev, "hs0038 data timeout!\n");
            hs0038_edge_time[0] = hs0038_edge_time[hs0038_edge_irq_cnt - 1];
            hs0038_edge_irq_cnt = 1;
            return IRQ_HANDLED;
        }
    }

    ret         = hs0038_data_parse(&val);
    if (!ret) {
        //解析完成或者为重复码
        hs0038_edge_irq_cnt = 0;
        put_data(val);
		
		wake_up(&hs0038_waitqueue);
		//上报数据 input
	    input_event(hs0038_input_dev, EV_KEY, val,1);	
	    input_event(hs0038_input_dev, EV_KEY, val,0);
	    input_sync(hs0038_input_dev);
    }
    else if (ret == -2) {
        hs0038_edge_irq_cnt = 0;
        dev_err(g_dev, "hs0038 data parse err!\n");
    }

    return IRQ_HANDLED;
}

/* 实现对应的open/read/write等函数，填入file_operations结构体 					*/
static ssize_t hs0038_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
	unsigned int val;
	if(size != 4)
		return -EINVAL;
	
    if(wait_event_interruptible(hs0038_waitqueue,has_data()))
		return -EAGAIN;
	get_data(&val);
	if(copy_to_user(buf,&val,4))
		return -EFAULT;
	
    return 4;
}

/* 定义自己的file_operations结构体												*/
struct file_operations hs0038_fops = {
    .owner = THIS_MODULE, 
    .read = hs0038_drv_read, 
};

static int hs0038_probe(struct platform_device * pdev)
{
    int     ret;
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1.获取硬件信息 ，可以记录下来     */
    g_dev       = &pdev->dev;
    hs0038_data_pin = devm_gpiod_get(g_dev, "hs0038", 0);
    if (IS_ERR(hs0038_data_pin)) {
        dev_err(g_dev, "failed to get hs0038_data_pin\n");
        return PTR_ERR(hs0038_data_pin);
    }

    irq         = gpiod_to_irq(hs0038_data_pin);
    if (request_irq(irq, hs0038_isr, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "hs0038", NULL)) {
        dev_err(g_dev, "failed to request irq\n");
        return - EAGAIN;
    }

    // 2.device_create
    major       = register_chrdev(0, "100ask_hs0038", &hs0038_fops);
    hs0038_class = class_create(THIS_MODULE, "100ask_hs0038_class");
    if (IS_ERR(hs0038_class)) {
        unregister_chrdev(major, "100ask_hs0038");
        return PTR_ERR(hs0038_class);
    }

    device_create(hs0038_class, NULL, MKDEV(major, 0), NULL, "100ask_hs0038"); //对于多个节点，每次调用probe都创建设备。

	//分配、设置、注册input_dev
    hs0038_input_dev       = devm_input_allocate_device(&pdev->dev);
    hs0038_input_dev->name = "100ask_hs0038";
    hs0038_input_dev->phys = "100ask_hs0038";
    

    //设置 event type
    __set_bit(EV_KEY, hs0038_input_dev->evbit);
    __set_bit(EV_REP, hs0038_input_dev->evbit);

    //设置 event
    //__set_bit(BTN_TOUCH, input->keybit);
    memset(hs0038_input_dev->keybit, 0xff, sizeof(hs0038_input_dev->keybit)); //支持所有按键类事件

    //注册
    ret         = input_register_device(hs0038_input_dev);
    if (ret) {
        dev_err(g_dev, "unable to register input device\n");
        return ret;
    }
	
    return 0;
}

static int hs0038_remove(struct platform_device * pdev)
{
	input_unregister_device(hs0038_input_dev);
    device_destroy(hs0038_class, MKDEV(major, 0));
    class_destroy(hs0038_class);
    unregister_chrdev(major, "100ask_hs0038");
	free_irq(irq,NULL);
    return 0;
}

static const struct of_device_id ask100_hs0038[] =
{
    {
        .compatible = "100ask,hs0038"
    },
    {
    },

    //必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

struct platform_driver hs0038_driver = {
    .probe = hs0038_probe, 
    .remove = hs0038_remove, 
    .driver = {
        .name       = "100ask_hs0038", 
        .of_match_table = ask100_hs0038, 
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init hs0038_init(void)
{
    int     err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err         = platform_driver_register(&hs0038_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出��
    �函数
 *		 卸载platform_driver
 */
static void __exit hs0038_exit(void)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    platform_driver_unregister(&hs0038_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(hs0038_init);
module_exit(hs0038_exit);
MODULE_LICENSE("GPL");


