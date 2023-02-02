
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

#define DRIVER_NAME 			"100ask_dht11"


struct dht11_dev {
	dev_t			devid;
	struct cdev cdev;
	int 			major;
	struct class * class ;
	struct platform_device * pdev;
	struct gpio_desc * dht11_data_pin;
};

//static DECLARE_WAIT_QUEUE_HEAD(dht11_waitqueue);

static void dht11_reset(struct dht11_dev* dev)
{
    gpiod_direction_output(dev->dht11_data_pin, 1);

}

static void dht11_start(struct dht11_dev* dev)
{
    gpiod_direction_output(dev->dht11_data_pin, 1);
	udelay(2);
    gpiod_set_value(dev->dht11_data_pin, 0);
    mdelay(18);
    gpiod_set_value(dev->dht11_data_pin, 1);
    udelay(40);
    gpiod_direction_input(dev->dht11_data_pin);
}

static int wait_for_low(struct dht11_dev* dev,int timeout_us)
{
    while (gpiod_get_value(dev->dht11_data_pin) && timeout_us) {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us) {
        return -1;
    }

    return 0;
}

static int wait_for_high(struct dht11_dev* dev,int timeout_us)
{
    while (!gpiod_get_value(dev->dht11_data_pin) && timeout_us) {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us) {
        return -1;
    }

    return 0;
}

static int dht11_wait_for_ready(struct dht11_dev* dev)
{
    /* 等待低电平 */
    if (wait_for_low(dev,200))
        return -1;

    /* 等待高电平 */
    if (wait_for_high(dev,200))
        return -1;

    /* 高电平来了 */
    /* 等待低电平 */
    if (wait_for_low(dev,200))
        return -1;

    return 0;
}

static int dht11_read_byte(struct dht11_dev* dev,unsigned char * buf)
{
	int i;
    unsigned char data = 0;

    for(i = 0; i < 8; i++) {
        /* 等待高电平 */
        if (wait_for_high(dev,200))
            return -1;
		
        udelay(40);
        if (gpiod_get_value(dev->dht11_data_pin)) {
            //get bit 1
            data        = (data << 1) | 1;
			//！！！等待高电平结束！ 等待低电平
			if(wait_for_low(dev,400))
				return -1;
			
        }
        else {
            //get bit 0
            data        = (data << 1) | 0;
        }
    }
	*buf = data;
    return 0;
}


static int dht11_drv_open(struct inode * inode, struct file * file)
{
	//在fops中找到设备
	struct dht11_dev * my_dht11;
	my_dht11		= container_of(inode->i_cdev, struct dht11_dev, cdev);
	file->private_data = my_dht11;
	return 0;
}
static int dht11_drv_release (struct inode *inode, struct file *file)
{
	if(file->private_data != NULL)
		file->private_data = NULL;

	return 0;
}

static ssize_t dht11_drv_read (struct file *file, char __user *buf, size_t size, loff_t *offset)
{	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    unsigned long flags;
    unsigned char data[5] = {0};
	int i;
	
	struct dht11_dev * my_dht11;
	my_dht11 = (struct dht11_dev*)file->private_data;

	if(size != 4)
		return -EINVAL;

    local_irq_save(flags);                          //关闭中断

    //1.发送高脉冲启动dht11
    dht11_start(my_dht11);

    //2.等待dht11就绪
    if (dht11_wait_for_ready(my_dht11)) {
        local_irq_restore(flags);
        return - EAGAIN;
    }

    //3.读5字节数据
    
    for (i = 0; i < 5; i++) {
        if (dht11_read_byte(my_dht11,&data[i])) {
            local_irq_restore(flags);
            return - EAGAIN;
        }
    }
	dht11_reset(my_dht11);
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


static


struct file_operations dht11_fops = {
	.owner = THIS_MODULE, 
	.open = dht11_drv_open, 
	.release = dht11_drv_release,
	.read = dht11_drv_read, 
};


static int dht11_probe(struct platform_device * pdev)
{
	int 			i;
	struct dht11_dev * my_dht11;

	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	my_dht11		= devm_kzalloc(&pdev->dev, sizeof(*my_dht11), GFP_KERNEL);

	if (!my_dht11)
		return - ENOMEM;

	my_dht11->dht11_data_pin = devm_gpiod_get(&pdev->dev, "dht11", GPIOD_ASIS);
    if (IS_ERR(my_dht11->dht11_data_pin)) {
        dev_err(&pdev->dev, "failed to get dht11_data_pin\n");
        return PTR_ERR(my_dht11->dht11_data_pin);
    }
	
	my_dht11->pdev	= pdev;
	platform_set_drvdata(pdev, my_dht11);

	if (my_dht11->major) {
		my_dht11->devid = MKDEV(my_dht11->major, 0);
		register_chrdev_region(my_dht11->devid, 1, DRIVER_NAME);
	}
	else {
		alloc_chrdev_region(&my_dht11->devid, 0, 1, DRIVER_NAME);
		my_dht11->major = MAJOR(my_dht11->devid);
	}

	cdev_init(&my_dht11->cdev, &dht11_fops);
	cdev_add(&my_dht11->cdev, my_dht11->devid, 1);


	my_dht11->class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(my_dht11->class)) {
		cdev_del(&my_dht11->cdev);
		unregister_chrdev_region(my_dht11->devid, 1);
		return PTR_ERR(my_dht11->class);
	}

	device_create(my_dht11->class, NULL, my_dht11->devid, NULL, DRIVER_NAME); 

	return 0;
}


static int dht11_remove(struct platform_device * pdev)
{

	struct dht11_dev * my_dht11 = platform_get_drvdata(pdev);
	
	platform_set_drvdata(pdev, NULL);
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	device_destroy(my_dht11->class, my_dht11->devid);
	class_destroy(my_dht11->class);
	cdev_del(&my_dht11->cdev);
	unregister_chrdev_region(my_dht11->devid, 1);

	return 0;
}


static const struct of_device_id ask100_dht11[] =
{
	{
		.compatible 	= "100ask,dht11"
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
		.name			= DRIVER_NAME, 
		.of_match_table = ask100_dht11, 
	},


};


/* 2. 在入口函数注册platform_driver */
static int __init dht11_init(void)
{
	int 			err;

	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	err 			= platform_driver_register(&dht11_driver);
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
MODULE_AUTHOR("Long");



