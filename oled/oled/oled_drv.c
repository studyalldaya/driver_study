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
#include <linux/spi/spi.h>

#define OLED_SET_XY         99
#define OLED_SET_XY_WRITE_DATA  100 //IO()
#define OLED_SET_XY_WRITE_DATAS 101 //IO()
#define OLED_SET_DATAS    102//低8位表示ioctl 高位表示数据长度

#define OLED_CMD 	0
#define OLED_DATA 	1

static int major;

static struct class * oled_class;
static struct spi_device* spi_dev;
static struct gpio_desc* oled_dc_pin;

static unsigned char param_buf[3];
static unsigned char data_buf[1024];



static void oled_write_cmd_data(unsigned char uc_data,unsigned char uc_cmd)
{
	
	if(uc_cmd==0)
	{
		//*GPIO4_DR_s &= ~(1<<20);//拉低，表示写入指令
		//gpiod_set_raw_value(oled_dc_pin,0);//设备树上是GPIO_ACTIVE_LOW
		gpiod_set_value(oled_dc_pin,1);
	}
	else
	{
		//*GPIO4_DR_s |= (1<<20);//拉高，表示写入数据		
		//gpiod_set_raw_value(oled_dc_pin,1);//设备树上是GPIO_ACTIVE_LOW
		gpiod_set_value(oled_dc_pin,0);
	}
	//spi_writeread(ESCPI1_BASE,uc_data);//写入
	spi_write(spi_dev,&uc_data,1);
}
static void oled_write_datas(unsigned char* buf,int len)
{
	
	//gpiod_set_raw_value(oled_dc_pin,1);//设备树上是GPIO_ACTIVE_LOW         拉高，表示写入数据
	gpiod_set_value(oled_dc_pin,0);
	spi_write(spi_dev,buf,len);
}

static int oled_hardware_init(void)
{
					  					  				 	   		  	  	 	  
	oled_write_cmd_data(0xae,OLED_CMD);//关闭显示

	oled_write_cmd_data(0x00,OLED_CMD);//设置 lower column address
	oled_write_cmd_data(0x10,OLED_CMD);//设置 higher column address

	oled_write_cmd_data(0x40,OLED_CMD);//设置 display start line

	oled_write_cmd_data(0xB0,OLED_CMD);//设置page address

	oled_write_cmd_data(0x81,OLED_CMD);// contract control
	oled_write_cmd_data(0x66,OLED_CMD);//128

	oled_write_cmd_data(0xa1,OLED_CMD);//设置 segment remap

	oled_write_cmd_data(0xa6,OLED_CMD);//normal /reverse

	oled_write_cmd_data(0xa8,OLED_CMD);//multiple ratio
	oled_write_cmd_data(0x3f,OLED_CMD);//duty = 1/64

	oled_write_cmd_data(0xc8,OLED_CMD);//com scan direction

	oled_write_cmd_data(0xd3,OLED_CMD);//set displat offset
	oled_write_cmd_data(0x00,OLED_CMD);//

	oled_write_cmd_data(0xd5,OLED_CMD);//set osc division
	oled_write_cmd_data(0x80,OLED_CMD);//

	oled_write_cmd_data(0xd9,OLED_CMD);//ser pre-charge period
	oled_write_cmd_data(0x1f,OLED_CMD);//

	oled_write_cmd_data(0xda,OLED_CMD);//set com pins
	oled_write_cmd_data(0x12,OLED_CMD);//

	oled_write_cmd_data(0xdb,OLED_CMD);//set vcomh
	oled_write_cmd_data(0x30,OLED_CMD);//

	oled_write_cmd_data(0x8d,OLED_CMD);//set charge pump disable 
	oled_write_cmd_data(0x14,OLED_CMD);//

	oled_write_cmd_data(0xaf,OLED_CMD);//set dispkay on

	return 0;
}		  			

static void oled_disp_set_pos(int x, int y)
{ 	oled_write_cmd_data(0xb0+y,OLED_CMD);
	oled_write_cmd_data((x&0x0f),OLED_CMD); 
	oled_write_cmd_data(((x&0xf0)>>4)|0x10,OLED_CMD);
}   	

static void oled_disp_clear(void)  
{
    unsigned char x, y;
    for (y = 0; y < 8; y++)
    {
        oled_disp_set_pos(0, y);
        for (x = 0; x < 128; x++)
            //oled_write_cmd_data(0, OLED_DATA); /* 清零 */
			oled_write_cmd_data(0, OLED_DATA);//
    }
}

static ssize_t oled_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
    return size;
}
//cmd = OLED_SET_XY
//cmd = OLED_SET_XY_WRITE_DATA     buf[0]=x,buf[1]=y,buf[2]=data
//cmd = OLED_SET_XY_WRITE_DATAS    buf[0]=x,buf[1]=y,buf[2]=len   buf[3...] = datas
static long oled_drv_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
{
	const void __user *from = (const void __user *)arg;
	
	int len;
	switch(cmd & 0xff)
	{
		case OLED_SET_XY:
		{
			copy_from_user(param_buf,from,2);
			oled_disp_set_pos(param_buf[0],param_buf[1]);
			break;
		}
		case OLED_SET_XY_WRITE_DATA:
		{
			copy_from_user(param_buf,from,3);
			oled_disp_set_pos(param_buf[0],param_buf[1]);
			oled_write_cmd_data(param_buf[2],OLED_DATA);
			
			break;
		}
		case OLED_SET_XY_WRITE_DATAS:
		{
			copy_from_user(param_buf,from,3);
			len = param_buf[2];
			copy_from_user(data_buf,from+3,len);
			oled_disp_set_pos(param_buf[0],param_buf[1]);
			oled_write_datas(data_buf,len);
			
			break;
		}
		case OLED_SET_DATAS:
		{
			len = (cmd >> 8) ;
			copy_from_user(data_buf,from,len);
			oled_write_datas(data_buf,len);
			break;
		}
	
	}
	return 0;
	

	//spi_sync_transfer(spi_dev->dev,spi_trans,3);
	return 0;

}
 
struct file_operations oled_fops = {
    .owner = THIS_MODULE, 
    .read = oled_drv_read, 
    .unlocked_ioctl = oled_drv_ioctl, 
};


static int oled_drv_probe(struct spi_device *spi)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	spi_dev = spi;
	
	major		= register_chrdev(0, "100ask_oled", &oled_fops);
	oled_class = class_create(THIS_MODULE, "100ask_oled_class");
	if (IS_ERR(oled_class)) {
		unregister_chrdev(major, "100ask_oled");
		return PTR_ERR(oled_class);
	}
	
	device_create(oled_class, NULL, MKDEV(major, 0), NULL, "100ask_oled"); //对于多个节点，每次调用probe都创建设备。
	
	//oled init
	oled_dc_pin = devm_gpiod_get(&spi->dev,"dc",GPIOD_OUT_HIGH);
	oled_hardware_init();
	oled_disp_clear();
	
	return 0;
}
static int oled_drv_remove(struct spi_device *spi)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	device_destroy(oled_class,MKDEV(major, 0));
	class_destroy(oled_class);
	unregister_chrdev(major,"100ask_oled");

	
	return 0;

}

static const struct of_device_id oled_of_match[] = {
	{.compatible = "100ask,oled"},
	{}
};

static const struct spi_device_id oled_ids[] = {
	{.name = "100ask,oled"},
	{}
};


static struct spi_driver oled_drv = {
	.driver = {
		.name = "oled",
		.of_match_table = oled_of_match,
	},
	.probe = oled_drv_probe,
	.remove = oled_drv_remove,
	.id_table = oled_ids,
	
};
module_spi_driver(oled_drv);

MODULE_LICENSE("GPL");






