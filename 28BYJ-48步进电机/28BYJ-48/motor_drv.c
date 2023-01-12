
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

#define DRIVER_NAME 			"100ask_motor"


struct motor_dev {
	dev_t			devid;
	struct cdev cdev;
	int 			major;
	struct class * class ;
	struct platform_device * pdev;
	struct gpio_desc * gpio[4];
};

/*

//顺着正转,//一步 
static int		g_motor_ctl[8] =
{
	0x2, 0x3, 0x1, 0x9, 0x8, 0xc, 0x4, 0x6
};
*/


static int		g_motor_ctl[8] =
{
	0x04, 0x0c, 0x08, 0x09, 0x01, 0x03, 0x02, 0x06
};


static int		speed[10] =
{
	40, 30, 20, 10, 8, 6, 4, 3, 2, 1
};


static int		g_motor_index = 0;


static void set_motor_pins(struct motor_dev * motor, int index)
{
	int 			i;

	for (i = 0; i < ARRAY_SIZE(motor->gpio); i++) {
		gpiod_set_value(motor->gpio[i], g_motor_ctl[index] & (1 << i) ? 1: 0);
	}
}


static int motor_drv_open(struct inode * inode, struct file * file)
{
	//在fops中找到设备
	struct motor_dev * my_motor;
	my_motor		= container_of(inode->i_cdev, struct motor_dev, cdev);
	file->private_data = my_motor;
	return 0;
}
static int motor_drv_release (struct inode *inode, struct file *file)
{
	if(file->private_data != NULL)
		file->private_data = NULL;

	return 0;
}


//buf[0] = 步进的次数，buf[1] = 速度（1到10）  delay大 速度慢
static ssize_t motor_drv_write(struct file * file, const char __user *buf, size_t size, loff_t * offset)
{
	struct motor_dev * my_motor = file->private_data;
	int 			ker_buf[2];
	int 			err;
	int 			i;

	if (size != 8)
		return - EINVAL;


	err 			= copy_from_user(ker_buf, buf, size);

	if (ker_buf[1] > 10 || ker_buf[1] < 1)
		ker_buf[1] = 5;

	if (ker_buf[0] > 0) {
		/*正转*/
		for (i = 0; i < ker_buf[0]; i++) {
			set_motor_pins(my_motor, g_motor_index);
			mdelay(speed[ker_buf[1] -1]);
			g_motor_index++;

			if (g_motor_index == 8)
				g_motor_index = 0;

		}
	}
	else {
		/*反转*/
		ker_buf[0]		= 0 - ker_buf[0];

		if (ker_buf[1] > 10 || ker_buf[1] < 1)
			ker_buf[1] = 5;

		for (i = 0; i < ker_buf[0]; i++) {
			set_motor_pins(my_motor, g_motor_index);
			mdelay(speed[ker_buf[1] -1]);
			g_motor_index--;

			if (g_motor_index == -1)
				g_motor_index = 7;

		}
	}

	//旋转到位后motor关闭，引脚设为高阻态
	for (i = 0; i < ARRAY_SIZE(my_motor->gpio); i++) {
		gpiod_set_value(my_motor->gpio[i], 0);
	}


	return size;

}



static


struct file_operations motor_fops = {
	.owner = THIS_MODULE, 
	.open = motor_drv_open, 
	.release = motor_drv_release,
	.write = motor_drv_write, 
};


static int motor_probe(struct platform_device * pdev)
{
	int 			i;
	struct motor_dev * my_motor;

	
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	my_motor		= devm_kzalloc(&pdev->dev, sizeof(*my_motor), GFP_KERNEL);

	if (!my_motor)
		return - ENOMEM;

	my_motor->pdev	= pdev;
	platform_set_drvdata(pdev, my_motor);




	if (my_motor->major) {
		my_motor->devid = MKDEV(my_motor->major, 0);
		register_chrdev_region(my_motor->devid, 1, DRIVER_NAME);
	}
	else {
		alloc_chrdev_region(&my_motor->devid, 0, 1, DRIVER_NAME);
		my_motor->major = MAJOR(my_motor->devid);
	}

	cdev_init(&my_motor->cdev, &motor_fops);
	cdev_add(&my_motor->cdev, my_motor->devid, 1);

	my_motor->class = class_create(THIS_MODULE, DRIVER_NAME);

	if (IS_ERR(my_motor->class)) {
		cdev_del(&my_motor->cdev);
		unregister_chrdev_region(my_motor->devid, 1);
		return PTR_ERR(my_motor->class);
	}

	device_create(my_motor->class, NULL, my_motor->devid, NULL, DRIVER_NAME); //对于多个节点，每次调用probe都创建设备。

	
	for (i = 0; i < ARRAY_SIZE(my_motor->gpio); i++) {
		my_motor->gpio[i] = devm_gpiod_get_index(&pdev->dev, "motor", i, GPIOD_OUT_LOW);

		if (IS_ERR(my_motor->gpio[i])) {
			dev_err(&pdev->dev, "motor_gpio_err:devm_gpiod_get_index() failed when i=%d\n", i);

			device_destroy(my_motor->class, my_motor->devid);
			return PTR_ERR(my_motor->gpio[i]);
		}
	}



	return 0;
}


static int motor_remove(struct platform_device * pdev)
{

	struct motor_dev * my_motor = platform_get_drvdata(pdev);
	
	platform_set_drvdata(pdev, NULL);
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	
	device_destroy(my_motor->class, my_motor->devid);
	class_destroy(my_motor->class);
	cdev_del(&my_motor->cdev);
	unregister_chrdev_region(my_motor->devid, 1);

	return 0;
}


static const struct of_device_id ask100_motor[] =
{
	{
		.compatible 	= "100ask,motor"
	},
	{
	},

	//必须空一个，内核才能知道到了结尾！
};


/* 1. 定义platform_driver */
static


struct platform_driver motor_driver = {
	.probe = motor_probe, 
	.remove = motor_remove, 
	.driver = {
		.name			= DRIVER_NAME, 
		.of_match_table = ask100_motor, 
	},


};


/* 2. 在入口函数注册platform_driver */
static int __init motor_init(void)
{
	int 			err;

	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	err 			= platform_driver_register(&motor_driver);
	return err;
}


/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出��
	�函数
 *		 卸载platform_driver
 */
static void __exit motor_exit(void)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	platform_driver_unregister(&motor_driver);
}


/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(motor_init);
module_exit(motor_exit);
MODULE_LICENSE("GPL");

