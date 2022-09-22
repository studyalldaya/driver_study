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
#include <linux/fb.h>
#include <linux/dma-mapping.h>

//使用framebuffer
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
static struct fb_info *oled_fb_info;
static unsigned int pseudo_palette[16];

static unsigned char param_buf[3];
static unsigned char data_buf[1024];
//？
static int mylcd_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;

	/* dprintk("setcol: regno=%d, rgb=%d,%d,%d\n",
		   regno, red, green, blue); */

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */

		if (regno < 16) {
			u32 *pal = info->pseudo_palette;

			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);

			pal[regno] = val;
		}
		break;

	default:
		return 1;	/* unknown type */
	}

	return 0;
}

static struct fb_ops myfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= mylcd_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};


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


 


static int oled_drv_probe(struct spi_device *spi)
{
	dma_addr_t phy_addr;
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	spi_dev = spi;

	//分配，设置，注册fb_info
	//size为除fb_info外额外分配的空间保存私有数据或硬件数据，使用fb_info+1就能访问该数据
	
	/* 1.1 分配fb_info */
	oled_fb_info = framebuffer_alloc(0,&spi->dev);//kmalloc保证物理地址连续,vmalloc保证虚拟地址连续
	
	/* 1.2 设置fb_info */
	/* a. var : LCD分辨率、颜色格式  可变屏幕信息*/
	oled_fb_info->var.xres_virtual = oled_fb_info->var.xres = 128;
	oled_fb_info->var.yres_virtual = oled_fb_info->var.yres = 64;
		
	oled_fb_info->var.bits_per_pixel = 1;  
	
		
	
	/* b. fix 固定屏幕信息*/
	strcpy(oled_fb_info->fix.id, "100ask_lcd");
	oled_fb_info->fix.smem_len = oled_fb_info->var.xres * oled_fb_info->var.yres * oled_fb_info->var.bits_per_pixel / 8;
	if (oled_fb_info->var.bits_per_pixel == 24)
		oled_fb_info->fix.smem_len = oled_fb_info->var.xres * oled_fb_info->var.yres * 4;
	
	
	/* fb的虚拟地址 */
	//dma_alloc_wc保证物理地址连续，可能有需要使用DMA传输
	oled_fb_info->screen_base = dma_alloc_wc(NULL, oled_fb_info->fix.smem_len, &phy_addr,GFP_KERNEL);
						 
	oled_fb_info->fix.smem_start = phy_addr;  /* fb的物理地址 */
		
	oled_fb_info->fix.type = FB_TYPE_PACKED_PIXELS;
	oled_fb_info->fix.visual = FB_VISUAL_MONO10;//黑白屏
	
	oled_fb_info->fix.line_length = oled_fb_info->var.xres * oled_fb_info->var.bits_per_pixel / 8;
	if (oled_fb_info->var.bits_per_pixel == 24)
		oled_fb_info->fix.line_length = oled_fb_info->var.xres * 4;
		
	
	/* c. fbops */
	oled_fb_info->fbops = &myfb_ops;//必须要有fbops 不然会出错
	oled_fb_info->pseudo_palette = pseudo_palette;
	
	register_framebuffer(oled_fb_info);
	
	//oled init
	oled_dc_pin = devm_gpiod_get(&spi->dev,"dc",GPIOD_OUT_HIGH);
	oled_hardware_init();
	oled_disp_clear();
	
	return 0;
}
static int oled_drv_remove(struct spi_device *spi)
{
	printk("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	unregister_framebuffer(oled_fb_info);
	framebuffer_release(oled_fb_info);
	
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






