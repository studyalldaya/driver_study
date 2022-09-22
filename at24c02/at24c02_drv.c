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
#include <linux/i2c.h>

/* 内核已经有了at24的driver，需要配置内核，把该驱动程序取消编译进内核  */

#define IOC_AT24C02_READ 100
#define IOC_AT24C02_WRITE 101

static int major;

static struct class * at24c02_class;

static struct i2c_client *at24c02_client;

static ssize_t at24c02_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
   

    

    return size;
}

static long at24c02_drv_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	
	struct i2c_msg msgs[2];
	unsigned char addr;
	unsigned char data;
	unsigned int ker_buf[2];
	unsigned int* usr_buf = (unsigned int*)arg;
	unsigned char byte_buf[2];

	copy_from_user(ker_buf,usr_buf,8);
	addr = ker_buf[0];
	switch(cmd)
	{
		case IOC_AT24C02_READ:
		{
			msgs[0].addr= at24c02_client->addr;
			msgs[0].flags= 0;//写
			msgs[0].len = 1;
			msgs[0].buf = &addr;

			msgs[1].addr= at24c02_client->addr;
			msgs[1].flags= I2C_M_RD;//读
			msgs[1].len = 1;
			msgs[1].buf = &data;
			i2c_transfer(at24c02_client->adapter,msgs,2);
			ker_buf[1] =data;
			copy_to_user(usr_buf,ker_buf,8);
			
			break;
		}
		case IOC_AT24C02_WRITE:
		{
			byte_buf[0] = addr;
			byte_buf[1] = ker_buf[1];
			msgs[0].addr= at24c02_client->addr;
			msgs[0].flags= 0;//写
			msgs[0].len = 2;
			msgs[0].buf = byte_buf;

			i2c_transfer(at24c02_client->adapter,msgs,1);
			mdelay(20);
			break;
		}
	}
	
	return 0;
}


/* 定义自己的file_operations结构体												*/
struct file_operations at24c02_fops = {
    .owner = THIS_MODULE, 
    .read = at24c02_drv_read, 
    .unlocked_ioctl = at24c02_drv_ioctl, 
};

static int at24c02_drv_probe(struct i2c_client *client, const struct i2c_device_id *id){
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	at24c02_client =client;
	
	major       = register_chrdev(0, "100ask_at24c02", &at24c02_fops);
    at24c02_class = class_create(THIS_MODULE, "100ask_at24c02_class");
    if (IS_ERR(at24c02_class)) {
        unregister_chrdev(major, "100ask_at24c02");
        return PTR_ERR(at24c02_class);
    }

    device_create(at24c02_class, NULL, MKDEV(major, 0), NULL, "100ask_at24c02"); //对于多个节点，每次调用probe都创建设备。
    return 0;
}
static int at24c02_drv_remove(struct i2c_client *client){
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	device_destroy(at24c02_class,MKDEV(major, 0));
	class_destroy(at24c02_class);
	unregister_chrdev(major,"100ask_at24c02");

	return 0;

}



static const struct of_device_id at24c02_of_match[] = {
	{.compatible = "100ask,at24c02"},
	{}
};

static const struct i2c_device_id at24c02_ids[] ={
	{.name = "100ask_at24c02"},
	{}
};


static struct i2c_driver at24c02_drv = {
	.probe = at24c02_drv_probe,
	.remove = at24c02_drv_remove,
	//  i2c_driver必须要有id_table,不然probe不会调用！
	.id_table = at24c02_ids,
	.driver = {
		.name = "100ask_at24c02",
		.of_match_table = at24c02_of_match,
	}
};

static int at24c02_init(void){
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	return i2c_add_driver(&at24c02_drv);
	
}

static void at24c02_exit(void){
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);

	i2c_del_driver(&at24c02_drv);
	
}
module_init(at24c02_init);
module_exit(at24c02_exit);
MODULE_LICENSE("GPL");




