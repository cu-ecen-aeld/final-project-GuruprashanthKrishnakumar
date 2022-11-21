/**
 * @file main.c
 * @brief Functions and data related to the HM-11 char driver implementation
 *
 * @author Jordi Cros Mompart
 * @date November 20 2022
 */

#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <asm/uaccess.h>
#include "hm11_ioctl.h"

MODULE_AUTHOR("Jordi Cros Mompart");
MODULE_LICENSE("Dual BSD/GPL");

static int hm11_major =   0; // use dynamic major
static int hm11_minor =   0;
static struct cdev cdev;

static long hm11_echo(void);
static void hm11_mac_read(char *str);
static void hm11_mac_write(char *str);
static long hm11_connect_last(void);
static long hm11_mac_connect(char *str);
static void hm11_discover(char *str);
static void hm11_service_discover(char *str);
static void hm11_characteristic_discover(char *str);
static long hm11_characteristic_notify(char *str);
static long hm11_characteristic_notify_off(char *str);
static void hm11_passive(void);
static void hm11_set_name(char *str);
static void hm11_reset(void);
static void hm11_set_role(char *str);
static void hm11_sleep(void);

int hm11_open(struct inode *inode, struct file *filp)
{
    //Handle open
    printk("hm11: Module open\n");
    try_module_get(THIS_MODULE);

    return 0;
}

int hm11_release(struct inode *inode, struct file *filp)
{
    //Handle open
    printk("hm11: Module released\n");
    module_put(THIS_MODULE);

    return 0;
}

ssize_t hm11_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    printk("hm11: Read still to be developed\n");
    return 0;
}

ssize_t hm11_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    printk("hm11: Write still to be developed\n");
    return count;
}

long hm11_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;
    char res = 0;
    struct hm11_ioctl_str ioctl_str;
    char *str = NULL;

    if(_IOC_TYPE(cmd) != HM11_IOC_MAGIC) 
        return -EINVAL;

    if(_IOC_NR(cmd) > HM11_IOC_MAXNR) 
        return -EINVAL;

    switch(cmd)
    {
    case HM11_ECHO:
        printk("hm11: Performing echo...\n");
        res = hm11_echo();
        if (copy_to_user((void __user *)arg, &res, sizeof(char)))
            return -EFAULT;

        break;
    case HM11_MAC_RD:
        printk("hm11: Reading MAC address...\n");

        str = kmalloc(sizeof(char)*MAC_SIZE_STR, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len < MAC_SIZE_STR)
            return -EOVERFLOW;
        
        hm11_mac_read(str);

        if (copy_to_user((void __user *)ioctl_str.str, str, MAC_SIZE_STR))
            return -EFAULT;

        //Free the used space
        kfree(str);

        break;
    case HM11_MAC_WR:
        printk("hm11: Modifying MAC address...\n");
        str = kmalloc(sizeof(char)*MAC_SIZE_STR, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len != MAC_SIZE_STR)
            return -EOVERFLOW;
        if (copy_from_user(str, (const void __user *)ioctl_str.str, MAC_SIZE_STR))
            return -EFAULT;

        hm11_mac_write(str);

        //Free the used space
        kfree(str);

        break;
    case HM11_CONN_LAST_DEVICE:
        printk("hm11: Connecting to last successfully paired device...\n");
        res = hm11_connect_last();
        //TODO: Parse retval according to what is defined in hm11_ioctl.h

        break;
    case HM11_CONN_MAC:
        printk("hm11: Connecting to the provided MAC address...\n");
        str = kmalloc(sizeof(char)*MAC_SIZE_STR, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len != MAC_SIZE_STR)
            return -EOVERFLOW;
        if (copy_from_user(str, (const void __user *)ioctl_str.str, MAC_SIZE_STR))
            return -EFAULT;

        retval = hm11_mac_connect(str);
        //TODO: Parse retval according to what is defined in hm11_ioctl.h

        //Free the used space
        kfree(str);

        break;
    case HM11_DISCOVER:
        printk("hm11: Performing device discovery...\n");

        str = kmalloc(sizeof(char)*MAC_SIZE_STR*100, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len < MAC_SIZE_STR*100)
            return -EOVERFLOW;
        
        hm11_discover(str);

        if (copy_to_user((void __user *)ioctl_str.str, str, MAC_SIZE_STR))
            return -EFAULT;

        //Free the used space
        kfree(str);

        break;
    case HM11_SERVICE_DISCOVER:
        printk("hm11: Performing service discovery on the connected device...\n");

        //TODO: check if device is connected

        str = kmalloc(sizeof(char)*1024, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len < 1024)
            return -EOVERFLOW;
        
        hm11_service_discover(str);

        if (copy_to_user((void __user *)ioctl_str.str, str, MAC_SIZE_STR))
            return -EFAULT;

        //Free the used space
        kfree(str);

        break;
    case HM11_CHARACTERISTIC_DISCOVER:
        printk("hm11: Performing characteristic discovery on the connected device...\n");

        //TODO: check if device is connected

        str = kmalloc(sizeof(char)*1024, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len < 1024)
            return -EOVERFLOW;
        
        hm11_characteristic_discover(str);

        if (copy_to_user((void __user *)ioctl_str.str, str, MAC_SIZE_STR))
            return -EFAULT;

        //Free the used space
        kfree(str);

        break;
    case HM11_CHARACTERISTIC_NOTIFY:
        printk("hm11: Subscribing to a characteristic notification...\n");
        str = kmalloc(sizeof(char)*CHARACTERISTIC_SIZE_LEN, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len != CHARACTERISTIC_SIZE_LEN)
            return -EOVERFLOW;
        if (copy_from_user(str, (const void __user *)ioctl_str.str, CHARACTERISTIC_SIZE_LEN))
            return -EFAULT;

        retval = hm11_characteristic_notify(str);
        //TODO: Parse retval according to what is defined in hm11_ioctl.h

        //Free the used space
        kfree(str);

        break;
    case HM11_CHARACTERISTIC_NOTIFY_OFF:
        printk("hm11: Stopping subscription to a characteristic notification...\n");
        str = kmalloc(sizeof(char)*CHARACTERISTIC_SIZE_LEN, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len != CHARACTERISTIC_SIZE_LEN)
            return -EOVERFLOW;
        if (copy_from_user(str, (const void __user *)ioctl_str.str, CHARACTERISTIC_SIZE_LEN))
            return -EFAULT;

        retval = hm11_characteristic_notify_off(str);
        //TODO: Parse retval according to what is defined in hm11_ioctl.h

        //Free the used space
        kfree(str);

        break;
    case HM11_PASSIVE:
        printk("hm11: Setting deice to passive mode...\n");
        hm11_passive();

        break;
    case HM11_NAME:
        printk("hm11: Modifying device name...\n");
        str = kmalloc(sizeof(char)*MAX_NAME_LEN, GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len > MAX_NAME_LEN)
            return -EOVERFLOW;
        if (copy_from_user(str, (const void __user *)ioctl_str.str, MAX_NAME_LEN))
            return -EFAULT;

        hm11_set_name(str);

        //Free the used space
        kfree(str);

        break;
    case HM11_DEFAULT:
        printk("hm11: Performing device reset to defaults...\n");
        hm11_reset();

        break;
    case HM11_ROLE:
        printk("hm11: Modifying device role...\n");
        str = kmalloc(sizeof(char), GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
            return -EFAULT;
        if (ioctl_str.str_len != sizeof(char))
            return -EINVAL;
        if (copy_from_user(str, (const void __user *)ioctl_str.str, sizeof(char)))
            return -EFAULT;

        hm11_set_role(str);

        //Free the used space
        kfree(str);

        break;
    case HM11_SLEEP:
        printk("hm11: Jumping to sleep mode...\n");
        hm11_sleep();

        break;
    default:
        retval = -ENOTTY;
        break;
    }

    return retval;
}

struct file_operations hm11_fops = {
    .owner =    THIS_MODULE,
    .read =     hm11_read,
    .write =    hm11_write,
    .open =     hm11_open,
    .release =  hm11_release,
    .unlocked_ioctl = hm11_ioctl,
};

int hm11_init_module(void)
{
    int err, devno;
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, hm11_minor, 1, "hm11");
    hm11_major = MAJOR(dev);
    if (result < 0) 
	{
        printk(KERN_WARNING "Can't get major %d\n", hm11_major);
        return result;
    }

    devno = MKDEV(hm11_major, hm11_minor);
	cdev_init(&cdev, &hm11_fops);
    cdev.owner = THIS_MODULE;
    cdev.ops = &hm11_fops;
    err = cdev_add (&cdev, devno, 1);
    if (err) 
	{
        printk(KERN_ERR "Error %d adding HM-11 cdev\n", err);
		unregister_chrdev_region(dev, 1);
    }
	
    return err;
}

void hm11_cleanup_module(void)
{
    dev_t devno = MKDEV(hm11_major, hm11_minor);
    cdev_del(&cdev);
    unregister_chrdev_region(devno, 1);
}

static long hm11_echo()
{
    long ret = 0;

    /*write_uart("AT");
    read_uart();*/

    return ret;
}

static void hm11_mac_read(char *str)
{
    /*write_uart("AT+ADDR?");
    read_uart();
    //Process the response to only get the MAC*/
}

static void hm11_mac_write(char *str)
{
    char mac_cmd[20];
    snprintf(mac_cmd, sizeof(mac_cmd), "AT+ADDR%s", str);
    /*write_uart("mac_cmd");
    read_uart();*/
}

static long hm11_connect_last()
{
    long ret = 0;

    /*write_uart("AT+CONNL");
      read_uart();
    */
    return ret;
}

static long hm11_mac_connect(char *str)
{
    long ret = 0;

    char mac_cmd[20];
    snprintf(mac_cmd, sizeof(mac_cmd), "AT+CON%s", str);
    /*write_uart("mac_cmd");
    read_uart();*/

    return ret;
}

static void hm11_discover(char *str)
{
    /*write_uart("AT+DISC?");
    read_uart();*/
}

static void hm11_service_discover(char *str)
{
    /*write_uart("AT+FINDSERVICES?");
    read_uart();*/
}

static void hm11_characteristic_discover(char *str)
{
    /*write_uart("AT+FINDALLCHARS?");
    read_uart();*/
}

static long hm11_characteristic_notify(char *str)
{
    long ret = 0;
    char characteristic_notify_cmd[20];
    snprintf(characteristic_notify_cmd, sizeof(characteristic_notify_cmd), "AT+NOTIFY_ON%s", str);
    /*write_uart("characteristic_notify_cmd");
    read_uart();*/

    return ret;
}

static long hm11_characteristic_notify_off(char *str)
{
    long ret = 0;
    char characteristic_notify_off_cmd[20];
    snprintf(characteristic_notify_off_cmd, sizeof(characteristic_notify_off_cmd), "AT+NOTIFY_OFF%s", str);
    /*write_uart("characteristic_notify_off_cmd");
    read_uart();*/

    return ret;
}

static void hm11_passive()
{
    /*write_uart("AT+IMME1");
      read_uart();
    */
}

static void hm11_set_name(char *str)
{
    char name_cmd[20];
    snprintf(name_cmd, sizeof(name_cmd), "AT+NAME%s", str);
    /*write_uart("name_cmd");
    read_uart();*/
}

static void hm11_reset()
{
    /*write_uart("AT+RENEW");
      read_uart();
    */
}

static void hm11_set_role(char *str)
{
    char role_cmd[9];
    snprintf(role_cmd, sizeof(role_cmd), "AT+ROLE%s", str);
    /*write_uart("role_cmd");
    read_uart();*/
}

static void hm11_sleep()
{
    /*write_uart("AT+SLEEP");
    read_uart();*/
}

module_init(hm11_init_module);
module_exit(hm11_cleanup_module);
