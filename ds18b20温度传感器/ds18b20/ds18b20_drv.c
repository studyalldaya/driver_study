
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

static struct class * ds18b20_class;
static struct gpio_desc * ds18b20_data_pin;
static DECLARE_WAIT_QUEUE_HEAD(ds18b20_waitqueue);

//精准延时
static void ds18b20_delay_us(int us)
{
	int T1,T2;
	T1 = ktime_get_boot_ns();
	while(1){
		T2 = ktime_get_boot_ns();
		if( T2 - T1 >= us*1000)
			break;
	}
}

static int wait_for_low(int timeout_us)
{
    while (gpiod_get_value(ds18b20_data_pin) && timeout_us) {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us) {
        return -1;
    }

    return 0;
}

static int wait_for_high(int timeout_us)
{
    while (!gpiod_get_value(ds18b20_data_pin) && timeout_us) {
        timeout_us--;
        udelay(1);
    }

    if (!timeout_us) {
        return -1;
    }

    return 0;
}


static int wait_for_ACK(void)
{
	if(wait_for_low(500))
		return -1;
	if(wait_for_high(500))
		return -1;



	
    return 0;
}

static int ds18b20_reset(void)
{
    //拉低电平，开始
    gpiod_direction_output(ds18b20_data_pin, 0);
    ds18b20_delay_us(480);
    gpiod_direction_input(ds18b20_data_pin);

    //等待ACK
    if (wait_for_ACK()) {
        return -1;
    }

    return 0;
}

static void ds18b20_write_byte(unsigned char data)
{	//低位先发
	int i;
	
	for( i=0 ; i<8 ; i++){
		if(data & (1<<i)){
			//1
			gpiod_direction_output(ds18b20_data_pin,0);
			ds18b20_delay_us(2);
			gpiod_direction_input(ds18b20_data_pin);//上拉电阻拉高			
			ds18b20_delay_us(60);
		
		}
		else{
			//0
			gpiod_direction_output(ds18b20_data_pin,0);
			ds18b20_delay_us(60);
			
			gpiod_direction_input(ds18b20_data_pin);//上拉电阻拉高			
			ds18b20_delay_us(2);
		}
	}
	

}

static unsigned char ds18b20_read_byte(void)
{
	unsigned char data = 0;
	int i;
	for( i=0 ; i<8 ; i++){
		
		gpiod_direction_output(ds18b20_data_pin,0);
		ds18b20_delay_us(2);
		gpiod_direction_input(ds18b20_data_pin);
		ds18b20_delay_us(8);
		
		if(gpiod_get_value(ds18b20_data_pin)){
			data |=(1<<i);
		}
		//读取数据后等待到60us
		ds18b20_delay_us(60);
	}

	return data;
}

/* 实现对应的open/read/write等函数，填入file_operations结构体 					*/
static ssize_t ds18b20_drv_read(struct file * file, char __user *buf, size_t size, loff_t * offset)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    unsigned long flags;
    unsigned char tempL = 0, tempH = 0;
    unsigned int integer;
    unsigned char decimal1, decimal2, decimal;
	

    if (size != 5)
        return - EINVAL;

    local_irq_save(flags);                          //关闭中断

    //开始
    if (ds18b20_reset()) {
		gpiod_set_value(ds18b20_data_pin, 1);
        local_irq_restore(flags);
        return - ENODEV;
    }

    ds18b20_write_byte(0xCC);                       //忽略rom指令，直接发功能指令
    ds18b20_write_byte(0x44);                       //温度转换指令

	gpiod_direction_output(ds18b20_data_pin,1);//必需拉高

    //	ds18b20_delay_us(1000000); //转换时间1s，对于cpu1s很长，可以开中断处理其它程序
    local_irq_restore(flags);
	//必须先设置state
	set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(HZ);
	
    local_irq_save(flags);                          //关闭中断

    //开始
    if (ds18b20_reset()) {		
		gpiod_set_value(ds18b20_data_pin, 1);
        local_irq_restore(flags);
        return - ENODEV;
    }

    ds18b20_write_byte(0xcc);                       //忽略rom指令，直接发功能指令
    ds18b20_write_byte(0xbe);                       //读寄存器指令
    tempL       = ds18b20_read_byte();              //温度低8位
    tempH       = ds18b20_read_byte();              //高8位

    //最高位为1时温度是负
    if (tempH > 0x7f) {
        tempL       = ~tempL;                       //补码转换，取反加一
        tempH       = ~tempH + 1;
        integer     = tempL / 16 + tempH * 16;      //整数部分
        decimal1    = (tempL & 0x0f) * 10 / 16;     //小数第一位
        decimal2    = (tempL & 0x0f) * 100 / 16 % 10; //小数第二位
        decimal     = decimal1 * 10 + decimal2;     //小数两位
    }
    else {
        integer     = tempL / 16 + tempH * 16;      //整数部分
        decimal1    = (tempL & 0x0f) * 10 / 16;     //小数第一位
        decimal2    = (tempL & 0x0f) * 100 / 16 % 10; //小数第二位
        decimal     = decimal1 * 10 + decimal2;     //小数两位
    }

    local_irq_restore(flags);                       //恢复中断
	gpiod_set_value(ds18b20_data_pin, 1);

	
	
    if (copy_to_user(buf, &integer, 4)){
        return - EFAULT;
    }
	if (copy_to_user(buf+4, &decimal, 1)){
        return - EFAULT;
	}
	
    return 5;
}

/* 定义自己的file_operations结构体												*/
struct file_operations ds18b20_fops = {
    .owner = THIS_MODULE, 
    .read = ds18b20_drv_read, 
};

static int ds18b20_probe(struct platform_device * pdev)
{
    //int     irq_ret;
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    /*1.获取硬件信息 ，可以记录下来     */
    ds18b20_data_pin = devm_gpiod_get(&pdev->dev, "ds18b20", GPIOD_OUT_HIGH);
    if (IS_ERR(ds18b20_data_pin)) {
        dev_err(&pdev->dev, "failed to get ds18b20_data_pin\n");
        return PTR_ERR(ds18b20_data_pin);
    }

    //2.device_create
    major       = register_chrdev(0, "100ask_ds18b20", &ds18b20_fops);
    ds18b20_class = class_create(THIS_MODULE, "100ask_ds18b20_class");
    if (IS_ERR(ds18b20_class)) {
        unregister_chrdev(major, "100ask_ds18b20");
        return PTR_ERR(ds18b20_class);
    }

    device_create(ds18b20_class, NULL, MKDEV(major, 0), NULL, "100ask_ds18b20"); //对于多个节点，每次调用probe都创建设备。
    return 0;
}

static int ds18b20_remove(struct platform_device * pdev)
{
    device_destroy(ds18b20_class, MKDEV(major, 0));
    class_destroy(ds18b20_class);
    unregister_chrdev(major, "100ask_ds18b20");

    //free_irq(irq, NULL);
    return 0;
}

static const struct of_device_id ask100_ds18b20[] =
{
    {
        .compatible = "100ask,ds18b20"
    },
    {
    },

    //必须空一个，内核才能知道到了结尾！
};

/* 1. 定义platform_driver */
static

struct platform_driver ds18b20_driver = {
    .probe = ds18b20_probe, 
    .remove = ds18b20_remove, 
    .driver = {
        .name       = "100ask_ds18b20", 
        .of_match_table = ask100_ds18b20, 
    },
};

/* 2. 在入口函数注册platform_driver */
static int __init ds18b20_init(void)
{
    int     err;

    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    err         = platform_driver_register(&ds18b20_driver);
    return err;
}

/* 3. 有入口函数就应该有出口函数：卸载驱动程序时，就会去调用这个出�
    �函数
 *		 卸载platform_driver
 */
static void __exit ds18b20_exit(void)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    platform_driver_unregister(&ds18b20_driver);
}

/* 7. 其他完善：提供设备信息，自动创建设备节点										 */
module_init(ds18b20_init);
module_exit(ds18b20_exit);
MODULE_LICENSE("GPL");
