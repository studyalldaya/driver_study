
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
static int major;

static struct class * DAC_class;
static struct spi_device * spi_dev;
static struct device_node *DAC_parent_node;
static int cs_gpio;
static struct gpio_desc* cs_gpiod;

static ssize_t DAC_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
    return size;
}

ssize_t DAC_drv_write(struct file * file, const char __user *buf, size_t size, loff_t * offset)
{
    unsigned short val;
	unsigned char ker_buf[2];

    if (size != 2)
        return - EINVAL;

    copy_from_user(&val, buf, 2);
    val         = val & 0xFFC;                      //高4位 低2位为零 中间10位为数据
	ker_buf[0] = val >> 8;
	ker_buf[1] = val ;
	gpiod_set_value(cs_gpiod,1);
    spi_write(spi_dev, ker_buf, 2);
	gpiod_set_value(cs_gpiod,0);
    return 2;
}

struct file_operations DAC_fops = {
    .owner = THIS_MODULE, 
    .read = DAC_drv_read, 
    .write = DAC_drv_write, 
};

static int DAC_drv_probe(struct spi_device * spi)
{
    printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
    spi_dev     = spi;
    major       = register_chrdev(0, "100ask_DAC", &DAC_fops);
    DAC_class   = class_create(THIS_MODULE, "100ask_DAC_class");
    if (IS_ERR(DAC_class)) {
        unregister_chrdev(major, "100ask_DAC");
        return PTR_ERR(DAC_class);
    }

    device_create(DAC_class, NULL, MKDEV(major, 0), NULL, "100ask_DAC"); //对于多个节点，每次调用probe都创建设备。


	DAC_parent_node = of_get_parent(spi_dev->dev.of_node);
	cs_gpio = of_get_named_gpio(DAC_parent_node,"cs-gpios",0);//发送需要cs引脚拉低提供开始，默认是一直拉低，发送完需要拉高
	cs_gpiod = gpio_to_desc(cs_gpio);
	gpiod_direction_output(cs_gpiod,0);//拉高
    return 0;
}

static int DAC_drv_remove(struct spi_device * spi)
{
    printk("%s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
    device_destroy(DAC_class, MKDEV(major, 0));
    class_destroy(DAC_class);
    unregister_chrdev(major, "100ask_DAC");
    return 0;
}

static const struct of_device_id DAC_of_match[] =
{
    {
        .compatible = "100ask,DAC"
    },
    {
    }
};

static const struct spi_device_id DAC_ids[] =
{
    {
        .name       = "100ask,DAC"
    },
    {
    }
};

static

struct spi_driver DAC_drv = {
    .driver = {
        .name       = "DAC", 
        .of_match_table = DAC_of_match, 
    },
    .probe = DAC_drv_probe, 
    .remove = DAC_drv_remove, 
    .id_table = DAC_ids, 
};


module_spi_driver(DAC_drv);
MODULE_LICENSE("GPL");
