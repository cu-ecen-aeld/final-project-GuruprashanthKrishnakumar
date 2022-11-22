/*
*  hw_serial.c - A "Hello World" serial UART driver for the Beaglebone Black
*
*  Copyright (C) 2020, Alex Rhodes <https://www.alexrhodes.io>
* 
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or (at
*  your option) any later version.
*
*  This program is distributed in the hope that it will be useful, but
*  WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  General Public License for more details.
*
*  <https://www.gnu.org/licenses/gpl-3.0.html>
* 
*/

#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/serial_reg.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/irqreturn.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

#define BUFF_SIZE 512

//Circular buffer struct
static struct circ_buff
{
    char buff[BUFF_SIZE];
    int read_pos;
    int write_pos;
    int length;
};

//Serial device struct
static struct hw_serial_dev
{
    void __iomem *regs;
    struct miscdevice mDev;
    int irq;
    struct circ_buff buf;
    wait_queue_head_t waitQ;
    unsigned long irqFlags;
    spinlock_t lock;

};

//Driver probe routine
static int hw_probe(struct platform_device *pdev);

//Driver remove routine
static int hw_remove(struct platform_device *pdev);

//FOPS open
static int hw_open(struct inode *inode, struct file *file);

//FOPS close
static int hw_close(struct inode *inodep, struct file *filp);

//FOPS read
static ssize_t hw_read(struct file *file, char __user *buf, size_t size, loff_t *ppos);

//FOPS write
static ssize_t hw_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos);

//Routine to read from serial device registers
static unsigned int reg_read(struct hw_serial_dev *dev, int offset);

//Routine to write to serial device registers
static void reg_write(struct hw_serial_dev *dev, int val, int offset);

//Routine to write a character to the seriald device
static void write_char(struct hw_serial_dev *dev, char test);

//Interrupt handler
static irqreturn_t irqHandler(int irq, void *devid);

//Utility method to write to circular buffer
static void write_circ_buff(char c, struct hw_serial_dev *dev);

//Utility method to read from circular buff
static char read_circ_buff(struct hw_serial_dev *dev);

//Device id struct
static struct of_device_id hw_match_table[] =
    {
        {
            .compatible = "serial",
        },
};

//File operations struct
static const struct file_operations hw_fops = {
    .owner = THIS_MODULE,
    .read = hw_read,
    .write = hw_write,
    .open = hw_open,
    .release = hw_close,
    .llseek = no_llseek,
};

//Platform driver structure
static struct platform_driver hw_plat_driver = {
    .driver = {
        .name = "serial",
        .owner = THIS_MODULE,
        .of_match_table = hw_match_table},
    .probe = hw_probe,
    .remove = hw_remove
};

/*********************************************************/
static int hw_open(struct inode *inode, struct file *file)
{
    return 0;
}
/*********************************************************/
static int hw_close(struct inode *inodep, struct file *filp)
{
    return 0;
}
/*********************************************************/
static ssize_t hw_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    struct miscdevice *mdev = (struct miscdevice *)file->private_data;
    struct hw_serial_dev *dev = container_of(mdev, struct hw_serial_dev, mDev);
    wait_event_interruptible(dev->waitQ, dev->buf.length > 0);

    char ret = read_circ_buff(dev);
    copy_to_user(buf, &ret, 1);
    return 1;
}
/*********************************************************/
static ssize_t hw_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
    struct miscdevice *mdev = (struct miscdevice *)file->private_data;
    struct hw_serial_dev *dev = container_of(mdev, struct hw_serial_dev, mDev);

    char kmem[len + 1];
    copy_from_user(kmem, buf, len);
    int i;
    for (i = 0; i < len; i++)
    {
        if (kmem[i] == '\n')
        {
            write_char(dev, '\n');
            write_char(dev, '\r');
        }
        else
        {
            write_char(dev, kmem[i]);
        }
    }   
    return len;
}

/*********************************************************/
static unsigned int reg_read(struct hw_serial_dev *dev, int offset)
{
    spin_lock_irqsave(&dev->lock, dev->irqFlags);
    unsigned int ret = ioread32(dev->regs + (4 * offset));
    spin_unlock_irqrestore(&dev->lock, dev->irqFlags);
    return ret;
}

/*********************************************************/
static void reg_write(struct hw_serial_dev *dev, int val, int offset)
{
    spin_lock_irqsave(&dev->lock, dev->irqFlags);
    iowrite32(val, dev->regs + (4 * offset));
    spin_unlock_irqrestore(&dev->lock, dev->irqFlags);
    return;
}

/*********************************************************/
static void write_char(struct hw_serial_dev *dev, char c)
{
    unsigned int lsr = reg_read(dev, UART_LSR);
    while (1)
    {
        if (lsr & UART_LSR_THRE)
        {
            break;
        }
        lsr = reg_read(dev, UART_LSR);
    }
    reg_write(dev, c, UART_TX);
}

/*********************************************************/
static irqreturn_t irqHandler(int irq, void *d)
{
    struct hw_serial_dev *dev = d;
    do 
    {
        char recv = reg_read(dev, UART_RX);
        write_circ_buff(recv, dev);
        wake_up(&dev->waitQ);
    }
    while (reg_read(dev, UART_LSR) & UART_LSR_DR);
    return IRQ_HANDLED;
}

/*********************************************************/
static void write_circ_buff(char c, struct hw_serial_dev *dev)
{
    spin_lock_irqsave(&dev->lock, dev->irqFlags);
    if(dev->buf.length < BUFF_SIZE)
    {
        dev->buf.buff[dev->buf.write_pos] = c;
        dev->buf.write_pos = ((dev->buf.write_pos + 1) % BUFF_SIZE);
        dev->buf.length++;
    }
    spin_unlock_irqrestore(&dev->lock, dev->irqFlags);
}

/*********************************************************/
static char read_circ_buff(struct hw_serial_dev *dev)
{
    spin_lock_irqsave(&dev->lock, dev->irqFlags);
    char c = dev->buf.buff[dev->buf.read_pos];
    dev->buf.buff[dev-> buf.read_pos] = '\0';
    if(dev->buf.length > 0)
    {
        dev->buf.buff[dev->buf.read_pos] = '\0';
        dev->buf.read_pos = ((dev->buf.read_pos + 1 ) % BUFF_SIZE);
        dev->buf.length--;
    }
    spin_unlock_irqrestore(&dev->lock, dev->irqFlags);
    return c;
}


/*********************************************************/
static int hw_probe(struct platform_device *pdev)
{
    struct resource *res;
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (!res)
    {
        pr_err("%s: platform_get_resource returned NULL\n", __func__);
        return -EINVAL;
    }

    struct hw_serial_dev *dev = devm_kzalloc(&pdev->dev, sizeof(struct hw_serial_dev), GFP_KERNEL);
    if (!dev)
    {
        pr_err("%s: devm_kzalloc returned NULL\n", __func__);
        return -ENOMEM;
    }
    dev->regs = devm_ioremap_resource(&pdev->dev, res);

    if (IS_ERR(dev->regs))
    {
        dev_err(&pdev->dev, "%s: Can not remap registers\n", __func__);
        return PTR_ERR(dev->regs);
    }
    
    //Configure interrupts
    dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		dev_err(&pdev->dev, "%s: unable to get IRQ\n", __func__);
		return dev->irq;
	}
	int ret = devm_request_irq(&pdev->dev, dev->irq, irqHandler, 0, "hw_serial", dev);
	if (ret < 0) 
    {
		dev_err(&pdev->dev, "%s: unable to request IRQ %d (%d)\n", __func__, dev->irq, ret);
		return ret;
	}
    dev->buf.read_pos = 0;
    dev->buf.write_pos = 0;
    dev->buf.buff[0] = '\0';
    dev->buf.length = 0;
    init_waitqueue_head(&dev->waitQ);

    //Enable power management runtime
    pm_runtime_enable(&pdev->dev);
    pm_runtime_get_sync(&pdev->dev);

    //Configure the UART device
    unsigned int baud_divisor;
    unsigned int uartclk;

    of_property_read_u32(pdev->dev.of_node, "clock-frequency", &uartclk);

    baud_divisor = uartclk / 16 / 115200;

    reg_write(dev, UART_OMAP_MDR1_DISABLE, UART_OMAP_MDR1);
    reg_write(dev, 0x00, UART_LCR);
    reg_write(dev, UART_LCR_DLAB, UART_LCR);
    reg_write(dev, baud_divisor & 0xff, UART_DLL);
    reg_write(dev, (baud_divisor >> 8) & 0xff, UART_DLM);
    reg_write(dev, UART_LCR_WLEN8, UART_LCR);
    reg_write(dev, UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT | UART_FCR_ENABLE_FIFO, UART_FCR);

    reg_write(dev, UART_OMAP_MDR1_16X_MODE, UART_OMAP_MDR1);

    //Initialize and register a misc device
    dev->mDev.minor = MISC_DYNAMIC_MINOR;
    dev->mDev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hw_serial-%x", res->start);
    dev->mDev.fops = &hw_fops;

    int error = misc_register(&dev->mDev);
    if (error)
    {
        pr_err("%s: misc register failed.", __func__);
        return error;
    }

    dev_set_drvdata(&pdev->dev, dev);

    //Enable RX interrupt
    reg_write(dev, UART_IER_RDI, UART_IER);

    return 0;
}

/*********************************************************/
static int hw_remove(struct platform_device *pdev)
{
    pm_runtime_disable(&pdev->dev);
    struct hw_serial_dev *dev = dev_get_drvdata(&pdev->dev);
    misc_deregister(&dev->mDev);
    return 0;
}

MODULE_AUTHOR("Alex Rhodes");
MODULE_LICENSE("GPL");

//Register the platform driver
module_platform_driver(hw_plat_driver);