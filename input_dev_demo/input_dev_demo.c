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

static struct input_dev* input;


static irqreturn_t input_dev_demo_irq(int irq, void *dev_id)
{
	//读取硬件

	//上报数据 input
	input_event(input,     );
	input_sync(input);

	return IRQ_HANDLED;
}

static int input_dev_demo_probe(struct platform_device * pdev)
{
	int ret;
	struct device* dev = &pdev->dev;
	//从设备树获取硬件信息

	//分配、设置、注册input_dev
	input = devm_input_allocate_device(dev);

	input->name = "input_dev_demo";
	input->phys = "input_dev_demo";
	input->dev.parent = dev ;
	/*
	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x0001;
	input->id.product = 0x5520;
	input->id.version = 0x0001;
	*/
	//设置 event type
	__set_bit(EV_KEY, input->evbit);//1	
	__set_bit(EV_ABS, input->evbit);//2
	
	//设置 event
	__set_bit(BTN_TOUCH, input->keybit);//1
	
	__set_bit(ABS_MT_SLOT, input->absbit);//2	
	__set_bit(ABS_MT_POSITION_X, input->absbit);//2
	__set_bit(ABS_MT_POSITION_Y, input->absbit);//2

	//注册
	ret = input_register_device(input);
	if (ret) {
		dev_err(dev, "unable to register input device\n");
		return ret;
	}
	

	//相关操作   ...irq...
	request_irq();
	
    return 0;
}

static int input_dev_demo_remove(struct platform_device * pdev)
{
	free_irq()
	input_unregister_device(input);
   
    return 0;
}

static const struct of_device_id ask100_input_dev[] =
{
    {
        .compatible = "100ask,input_dev_demo"
    },
    {
    },

    //必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

struct platform_driver input_dev_demo_driver = {
    .probe = input_dev_demo_probe, 
    .remove = input_dev_demo_remove, 
    .driver = {
        .name       = "input_dev_demo", 
        .of_match_table = ask100_input_dev, 
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init input_dev_demo_init(void)
{
    int     err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err         = platform_driver_register(&input_dev_demo_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出��
    �函数
 *		 卸载platform_driver
 */
static void __exit input_dev_demo_exit(void)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    platform_driver_unregister(&input_dev_demo_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(input_dev_demo_init);
module_exit(input_dev_demo_exit);
MODULE_LICENSE("GPL");






