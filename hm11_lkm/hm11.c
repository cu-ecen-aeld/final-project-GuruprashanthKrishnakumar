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
#include <linux/slab.h>
#include "hm11_ioctl.h"

#define HEART_RATE_ID   (0x16)

MODULE_AUTHOR("Jordi Cros Mompart");
MODULE_LICENSE("Dual BSD/GPL");

static int hm11_major =   0; // use dynamic major
static int hm11_minor =   0;
static struct cdev cdev;

static ssize_t hm11_transmit(char *buf, size_t len);
static ssize_t variable_wait_limited(char *buf, size_t len);
static ssize_t reallocate_memory_if(int condition,struct hm11_ioctl_str *buf,size_t packet_length);
static ssize_t parse_response_by_delimiter_char(size_t unit_length,struct hm11_ioctl_str *buf);

static ssize_t hm11_echo(void);
static void hm11_mac_read(char *str);
static void hm11_mac_write(char *str);
static long hm11_connect_last(void);
static long hm11_mac_connect(char *str);
static void hm11_discover(char *str);
static ssize_t hm11_services_probe(void);
static ssize_t hm11_characteristics_probe(void);
static long hm11_characteristic_notify(char *str);
static long hm11_characteristic_notify_off(char *str);
static ssize_t hm11_passive(void);
static void hm11_set_name(char *str);
static ssize_t hm11_reset(void);
static ssize_t hm11_set_role(char *str);
static void hm11_sleep(void);
static ssize_t hm11_read_notified(void);


//static struct hm11_ioctl_str devices = {NULL,0};
static size_t service_str_num_chars_to_copy = 0;
static struct hm11_ioctl_str services = {NULL,0};
static size_t characteristics_str_num_chars_to_copy = 0;
static struct hm11_ioctl_str characteristics = {NULL,0};


extern ssize_t uart_send(const char *buf, size_t size);
extern ssize_t uart_receive(char *buf, size_t size);
extern ssize_t uart_receive_timeout(char *buf, size_t size,int msecs);
extern void uart_flush_buffer(void);


int hm11_open(struct inode *inode, struct file *filp)
{
    //Handle open
    printk("hm11: Module open\n");
    try_module_get(THIS_MODULE);

    return 0;
}

int hm11_release(struct inode *inode, struct file *filp)
{
    //Handle close
    printk("hm11: Module released\n");
    uart_flush_buffer();
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
    ssize_t ret_val = 0;
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
        if(res < 0)
        {
            ret_val = res;
            //RETURN ERROR
        }
        else
        {
            if (copy_to_user((void __user *)arg, &res, sizeof(char)))
            {
                ret_val = -EFAULT;
            }
        }

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
        {
            ret_val = -EFAULT;
            goto free_mem_mac;
        }
        if (ioctl_str.str_len != MAC_SIZE_STR)
        {
            ret_val = -EOVERFLOW;
            goto free_mem_mac;
        }
        if (copy_from_user(str, (const void __user *)ioctl_str.str, MAC_SIZE_STR))
        {
            ret_val = -EFAULT;
            goto free_mem_mac;
        }

        ret_val = hm11_mac_connect(str);
        //TODO: Parse retval according to what is defined in hm11_ioctl.h

        //Free the used space
        free_mem_mac:
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
    case HM11_SERVICE_DISCOVER_PROBE:
        res = hm11_services_probe();
        if(res < 0)
        {
            ret_val = res;
            //RETURN ERROR
        }
        else
        {
            struct hm11_ioctl_str copy_struct = {NULL,res};
            if (copy_to_user((void __user *)arg, &copy_struct, sizeof(struct hm11_ioctl_str)))
            {
                ret_val = -EFAULT;
            }
        }
        break;
    case HM11_SERVICE_DISCOVER:
        printk("hm11: Performing service discovery on the connected device...\n");

        if(!service_str_num_chars_to_copy)
        {
            return -EINVAL;
        }
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
        {
            return -EFAULT;
        }
        if (ioctl_str.str_len != (service_str_num_chars_to_copy + 1))
        {
            return -EOVERFLOW;
        }
        if (copy_to_user((void __user *)ioctl_str.str, services.str, service_str_num_chars_to_copy))
        {
            return -EFAULT;
        }
        else
        {
            kfree(services.str);
            services.str_len = 0;
            service_str_num_chars_to_copy = 0;
        }

        break;
    case HM11_CHARACTERISTIC_DISCOVER_PROBE:
        res = hm11_characteristics_probe();
        if(res < 0)
        {
            ret_val = res;
            //RETURN ERROR
        }
        else
        {
            struct hm11_ioctl_str copy_struct = {NULL,res};
            if (copy_to_user((void __user *)arg, &copy_struct, sizeof(struct hm11_ioctl_str)))
            {
                ret_val = -EFAULT;
            }
        }
        break;
    case HM11_CHARACTERISTIC_DISCOVER:
        printk("hm11: Performing characteristic discovery on the connected device...\n");

        if(!characteristics_str_num_chars_to_copy)
        {
            return -EINVAL;
        }
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
        {
            return -EFAULT;
        }
        if (ioctl_str.str_len != (characteristics_str_num_chars_to_copy + 1))
        {
            return -EOVERFLOW;
        }
        if (copy_to_user((void __user *)ioctl_str.str, characteristics.str, characteristics_str_num_chars_to_copy))
        {
            return -EFAULT;
        }
        else
        {
            kfree(characteristics.str);
            characteristics_str_num_chars_to_copy = 0;
            characteristics.str_len = 0;
        }

        break;
    case HM11_CHARACTERISTIC_NOTIFY:
        printk("hm11: Subscribing to a characteristic notification...\n");
        str = kmalloc(sizeof(char)*CHARACTERISTIC_SIZE_STR, GFP_KERNEL);
        if(!str)
        {
            return -ENOMEM;
        }

        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
        {
            ret_val = -EFAULT;
            goto free_mem_notif_on;
        }
        if (ioctl_str.str_len != CHARACTERISTIC_SIZE_STR)
        {
            ret_val = -EOVERFLOW;
            goto free_mem_notif_on;
        }
        if (copy_from_user(str, (const void __user *)ioctl_str.str, CHARACTERISTIC_SIZE_STR))
        {
            ret_val = -EFAULT;
            goto free_mem_notif_on;
        }

        ret_val = hm11_characteristic_notify(str);
        //TODO: Parse retval according to what is defined in hm11_ioctl.h

        //Free the used space
        free_mem_notif_on:
            kfree(str);

        break;
    case HM11_CHARACTERISTIC_NOTIFY_OFF:
        printk("hm11: Stopping subscription to a characteristic notification...\n");
        str = kmalloc(sizeof(char)*CHARACTERISTIC_SIZE_STR, GFP_KERNEL);
        if(!str)
        {
            printk("Could not malloc outside notifyoff handler before the response\n");
            return -ENOMEM;
        }
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
        {
            ret_val = -EFAULT;
            goto free_mem_notif_off;
        }
        if (ioctl_str.str_len != CHARACTERISTIC_SIZE_STR)
        {
            ret_val = -EOVERFLOW;
            goto free_mem_notif_off;
        }
            
        if (copy_from_user(str, (const void __user *)ioctl_str.str, CHARACTERISTIC_SIZE_STR))
        {
            ret_val = -EFAULT;
            goto free_mem_notif_off;
        }

        ret_val = hm11_characteristic_notify_off(str);

        //Free the used space
        free_mem_notif_off:
            kfree(str);

        break;
    case HM11_PASSIVE:
        printk("hm11: Setting deice to passive mode...\n");
        ret_val = hm11_passive();

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

        printk("User-space string: %s", str);
        hm11_set_name(str);

        //Free the used space
        kfree(str);

        break;
    case HM11_DEFAULT:
        printk("hm11: Performing device reset to defaults...\n");
        ret_val = hm11_reset();

        break;
    case HM11_ROLE:
        printk("hm11: Modifying device role...\n");
        str = kmalloc(sizeof(char), GFP_KERNEL);
        if(!str)
            return -ENOMEM;
        
        if (copy_from_user(&ioctl_str, (const void __user *)arg, sizeof(struct hm11_ioctl_str)))
        {
            ret_val = -EFAULT;
            goto free_mem_role;
        }
        if (ioctl_str.str_len != sizeof(char))
        {
            ret_val = -EINVAL;
            goto free_mem_role;
        }
        if (copy_from_user(str, (const void __user *)ioctl_str.str, sizeof(char)))
        {
            ret_val = -EFAULT;
            goto free_mem_role;
        }
        if (str[0]!= '0' && str[0]!= '1')
        {
            ret_val = -EINVAL;
            goto free_mem_role;
        }
        
        ret_val = hm11_set_role(str);

        //Free the used space
        free_mem_role:
            kfree(str);

        break;
    case HM11_SLEEP:
        printk("hm11: Jumping to sleep mode...\n");
        hm11_sleep();

        break;
    case HM11_READ_NOTIFIED:
        printk("hm11: Reading most recent notified value...\n");
        res = hm11_read_notified();

        if(res < 0)
        {
            ret_val = res;
            //RETURN ERROR
        }
        else if(res == 0)
        {
            ret_val = -EAGAIN;
        }
        else
        {
            if (copy_to_user((void __user *)arg, &res, sizeof(char)))
            {
                ret_val = -EFAULT;
            }
        }

        break;
    default:
        printk("hm11: Invalid ioctl command.\n");
        ret_val = -ENOTTY;
        break;
    }

    return ret_val;
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

static ssize_t hm11_transmit(char *buf, size_t len)
{
    size_t num_bytes_sent = 0;
    while(num_bytes_sent < len)
    {
        int ret = uart_send(&buf[num_bytes_sent],(len - num_bytes_sent));
        if(ret < 0)
        {
            printk("HM11 Write: Error in transmission %d",ret);
            return ret;
        }
        num_bytes_sent += ret;
    }
    return num_bytes_sent;
}

static ssize_t fixed_wait(char *buf, size_t len)
{
    size_t num_bytes_received = 0;
    int ret;
    while(num_bytes_received < len)
    {
        ret = uart_receive(&buf[num_bytes_received],1);

        if(ret < 0)
        {
            if(ret == -EINTR)
            {
                continue;
            }
            else
            {
                printk("fixed_wait: Error in reception %d",ret);
                return ret;
            }
        }
        //byte received
        else
        {
            num_bytes_received += ret;
        }
    }
    return num_bytes_received;  
}

static ssize_t variable_wait_limited(char *buf, size_t len)
{
    size_t num_bytes_received = 0;
    int ret;
    while(num_bytes_received < len)
    {
        //receive one byte at a time with a gap of 1000 ms 
        ret = uart_receive_timeout(&buf[num_bytes_received],1,1000);
        //return value of 0 indicates, timeout occured and no bytes were read
        if(ret == 0)
        {
            goto out;
        }
        //error
        else if(ret < 0)
        {
            if(ret == -EINTR)
            {
                continue;
            }
            else
            {
                printk("variable_wait_limited: Error in reception %d",ret);
                return ret;
            }
        }
        //byte received
        else
        {
            num_bytes_received += ret;
        }
    }
    out:
        return num_bytes_received;
}


static ssize_t reallocate_memory_if(int condition,struct hm11_ioctl_str *buf,size_t packet_length)
{
    char *tmp;
    if(condition)
    {
        tmp = krealloc(buf->str,((buf->str_len+packet_length)*sizeof(char)),GFP_KERNEL);
        {
            if(!tmp)
            {
                return -ENOMEM;
            }
            buf->str = tmp;
            buf->str_len += packet_length;
        }
    }
    return condition;
}

/*
*   Function to read data delimited by a delimiting character of certain length (for e.g. 56 "*"s)
*   For e.g.,
*   The AT+FINDSERVICES? command finds services
*   56 bytes of '*'s at the start
*   Each service has 14 bytes of data-> 4 bytes of start handle:4 bytes of end handle:4 bytes of UUID
*   56 bytes of of '*'s at the end
*   The return string is formatted in such a way that the '*'s are not included in the string and each service is seperated by ','
*/

static ssize_t parse_response_by_delimiter_char(size_t unit_length,struct hm11_ioctl_str *buf)
{
    size_t num_bytes_read = 0;
    ssize_t ret = 0;
    char c;
    //accounting for the ',' at the end
    size_t packet_length = unit_length + 1;
    //ignore the first 56 bytes
    while(num_bytes_read < 56)
    {
        ret = fixed_wait(&c,1);
        if(ret<0)
        {
            return ret;
        }
        num_bytes_read +=1;
    }

    num_bytes_read = 0;
    while(true)
    {
        //malloc the first time through
        if(!buf->str_len)
        {
            buf->str = kmalloc(packet_length*sizeof(char),GFP_KERNEL);
            if(!buf->str)
            {
                return -ENOMEM;
            }
            buf->str_len +=packet_length;
        }
        //read the character from receive buffer
        ret = fixed_wait(&c,1);
        if(ret<0)
        {
            break;
        }
        //if * was received, that means the end string has begun, hence break out
        else if(c == '\r')
        {
            //read the \n as well
            ret = fixed_wait(&c,1);
            //read the new character
            ret = fixed_wait(&c,1);
            if(c == '*')
            {
                break;
            }
            else
            {
                ret = reallocate_memory_if(((buf->str_len - num_bytes_read)<2),buf,packet_length);
                if(ret < 0)
                {
                    break;
                }
                //the first service/character entry does not need a ','
                if(num_bytes_read)
                {
                    buf->str[num_bytes_read] = ',';
                    num_bytes_read += 1;
                }

                buf->str[num_bytes_read] = c;
                num_bytes_read += 1;
            }
        }
        else
        {
            ret = reallocate_memory_if(((buf->str_len - num_bytes_read)<1),buf,packet_length);
            if(ret < 0)
            {
                break;
            }
            buf->str[num_bytes_read] = c;
            num_bytes_read +=1;
        }
    }
    if(ret<0)
    {
        kfree(buf->str);
        buf->str_len = 0;
        return ret;
    }
    ret = num_bytes_read;
    //ignore the end terminating bytes which will just be '*'s
    while(num_bytes_read < (ret + 56))
    {
        fixed_wait(&c,1);
        num_bytes_read++;
    }
    //if memory was allocated but no characters were read
    if(!ret)
    {
        kfree(buf->str);
        //if no characters were read str_len of 0 will catch that there's no characters to be copied and return an error.
        buf->str_len = ret;
    }
    return ret;
}

static ssize_t hm11_echo()
{
    ssize_t ret = 0, bytes_read = 0;
    char *receive_buf;
    ret = hm11_transmit("AT",2);

    if(ret<0)
    {
        return ret;
    }

    receive_buf = kmalloc(7*sizeof(char),GFP_KERNEL);
    if(!receive_buf)
    {
        return -ENOMEM;
    }
    //unconditional wait for two bytes (since we expect a minimum of two bytes) and optional wait for more (upto 7)
    while(bytes_read <2)
    {
        ret = variable_wait_limited(&receive_buf[bytes_read],(7 - bytes_read));
        //return error
        if(ret < 0)
        {
            goto free_mem;
        }
        bytes_read += ret;
        //if minimum two bytes read
        if(bytes_read >= 2)
        {
            if(bytes_read == 9)
            {
                if(strncmp(receive_buf,"OK+WAKE",7)==0)
                {
                    ret = 2;
                    goto free_mem;
                }
                else if(strncmp(receive_buf,"OK+LOST",7)==0)
                {
                    ret = 1;
                    goto free_mem;
                }
                //HANDLE GARBAGE CASE: 7 bytes read but they didn't correspond to expected values
            }
            else if(bytes_read == 2)
            {
                if(strncmp(receive_buf,"OK",bytes_read)==0)
                {
                    ret = 0;
                    goto free_mem;
                }
                //HANDLE GARBAGE CASE: two bytes were read but it was not OK
            }
            //HANDLE GARBAGE CASE: Some number of bytes other than 7 and two bytes were read
        }
    }
    //uart_receive()
    free_mem:
        kfree(receive_buf);
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
    ssize_t ret = 0,bytes_read=0;

    char mac_cmd[20];
    char *receive_buf;
    snprintf(mac_cmd, sizeof(mac_cmd), "AT+CON%s", str);
    
    ret = hm11_transmit(mac_cmd,18);
    if(ret<0)
    {
        return ret;
    }
    receive_buf = kmalloc(8*sizeof(char),GFP_KERNEL);
    if(!receive_buf)
    {
        return -ENOMEM;
    }
    ret = fixed_wait(receive_buf,8);
    if(ret < 0)
    {
        goto free_mem;
    }
    while(bytes_read <9)
    {
        ret = variable_wait_limited(&receive_buf[bytes_read],(10 - bytes_read));
        //return error
        if(ret < 0)
        {
            goto free_mem;
        }
        bytes_read += ret;
        //if minimum seven bytes read
        if(bytes_read >= 9)
        {
            if(bytes_read == 10)
            {
                if(strncmp(receive_buf,"OK+CONNA",8)==0)
                {
                    ret = 0;
                    goto free_mem;
                }
                else if(strncmp(receive_buf,"OK+CONNE",8)==0)
                {
                    ret = -ENODEV;
                    goto free_mem;
                }
                else if(strncmp(receive_buf,"OK+CONNF",8)==0)
                {
                    ret = -ENODEV;
                    goto free_mem;
                }
                //HANDLE GARBAGE CASE: 8 bytes read but they didn't correspond to expected values
            }
            else if(bytes_read == 9)
            {
                if(strncmp(receive_buf,"OK+CONN",7)==0)
                {
                    ret = 0;
                    goto free_mem;
                }
                //HANDLE GARBAGE CASE: 7 bytes were read but it was not OK+CONN
            }
            //HANDLE GARBAGE CASE: Some number of bytes other than 7 and 8 bytes were read
        }
    }
    free_mem:
        kfree(receive_buf);
    return ret;
}

static void hm11_discover(char *str)
{
    /*write_uart("AT+DISC?");
    read_uart();*/
}

static ssize_t hm11_services_probe(void)
{
    ssize_t ret = 0;
    ret = hm11_transmit("AT+FINDSERVICES?",16);
    if(ret<0)
    {
        return ret;
    }
    //unit length:
    //P1: 4 Bytes, Services start handle.
    //P2: 4 Bytes, Services end handle
    //P3: Services UUID (upto 16 bytes)
    //P1:P2:P3
    //4+1+4+1+16 = 26 
    ret = parse_response_by_delimiter_char(26,&services);
    if(ret<0)
    {
        service_str_num_chars_to_copy = 0;
        return ret;
    }
    service_str_num_chars_to_copy = ret;
    //convention to require one more byte than actually needed.
    return (ret + 1);
}

static ssize_t hm11_characteristics_probe(void)
{
    ssize_t ret = 0;
    ret = hm11_transmit("AT+FINDALLCHARS?",16);
    if(ret<0)
    {
        return ret;
    }
    //unit length:
    //P1 - 4 Bytes, Characteristic handle.
    //P2 - 14 Bytes, “RD|WR|WN|NO|IN” 
    //P3 - Characteristic UUID (assumed to be 16 bytes since it can be max that)
    //P1:P2:P3
    //4+1+14+1+16 = 36 
    ret = parse_response_by_delimiter_char(36,&characteristics);
    if(ret<0)
    {
        characteristics_str_num_chars_to_copy = 0;
        return ret;
    }
    characteristics_str_num_chars_to_copy = ret;
    //convention to require one more byte than actually needed.
    return (ret + 1);
}

static long hm11_characteristic_notify(char *str)
{
    ssize_t ret = 0;
    char characteristic_notify_cmd[20];
    char *buf;
    snprintf(characteristic_notify_cmd, sizeof(characteristic_notify_cmd), "AT+NOTIFY_ON%s", str);
    ret = hm11_transmit(characteristic_notify_cmd,16);
    if(ret<0)
    {
        return ret;
    }
    buf = kmalloc(10*sizeof(char),GFP_KERNEL);
    if(!buf)
    {
        return -ENOMEM;
    }
    ret = fixed_wait(buf,12);
    if(ret>0)
    {
        if(strncmp(buf,"OK+SEND-OK",10)==0)
        {
            ret = 0;
        }
        else if(strncmp(buf,"OK+DATA-OK",10)==0)
        {
            ret = 0;
        }
        else if(strncmp(buf,"OK+SEND-ER",10)==0)
        {
            ret = -ENODEV;
            //RETURN ERROR
        }
        else if(strncmp(buf,"OK+DATA-ER",10)==0)
        {
            ret = -ENODEV;
        }
        //Handle error
    }
    kfree(buf);
    return ret;
}

static long hm11_characteristic_notify_off(char *str)
{
    long ret = 0;
    char characteristic_notify_off_cmd[20];
    char *buf;
    snprintf(characteristic_notify_off_cmd, sizeof(characteristic_notify_off_cmd), "AT+NOTIFYOFF%s", str);
    ret = hm11_transmit(characteristic_notify_off_cmd,16);
    printk("Transmitted notifyoff with return value %d\n", ret);
    
    if(ret<0)
    {
        return ret;
    }

    buf = kmalloc(10*sizeof(char),GFP_KERNEL);
    if(!buf)
    {
        printk("Could not malloc inside notifyoff handler after the response\n");
        return -ENOMEM;
    }

    ret = fixed_wait(buf,12);

    if(ret>0)
    {
        if(strncmp(buf,"OK+SEND-OK",10)==0)
        {
            ret = 0;
        }
        else if(strncmp(buf,"OK+DATA-OK",10)==0)
        {
            ret = 0;
        }
        else if(strncmp(buf,"OK+SEND-ER",10)==0)
        {
            ret = -ENODEV;
        }
        else if(strncmp(buf,"OK+DATA-ER",10)==0)
        {
            ret = -ENODEV;
        }
        //Handle error
    }
    kfree(buf);

    //Flush contents on the UART buffer
    uart_flush_buffer();

    return ret;
}

static ssize_t hm11_passive()
{
    ssize_t ret = 0;
    char *buf;
    ret = hm11_transmit("AT+IMME1",8);
    if(ret<0)
    {
        return ret;
    }
    buf = kmalloc(8*sizeof(char),GFP_KERNEL);
    if(!buf)
    {
        return -ENOMEM;
    }
    ret = fixed_wait(buf,8);
    if(ret>0)
    {
        if(strncmp(buf,"OK+Set:1",8)==0)
        {
            ret = 0;
        }
        else
        {
            //RETURN ERROR
        }
    }
    kfree(buf);
    return ret;
}

static void hm11_set_name(char *str)
{
    char name_cmd[20];
    snprintf(name_cmd, sizeof(name_cmd), "AT+NAME%s", str);
    /*write_uart("name_cmd");
    read_uart();*/
}

static ssize_t hm11_reset()
{
    ssize_t ret = 0;
    char *buf;
    ret = hm11_transmit("AT+RESET",8);
    if(ret<0)
    {
        return ret;
    }
    buf = kmalloc(8*sizeof(char),GFP_KERNEL);
    if(!buf)
    {
        return -ENOMEM;
    }
    ret = fixed_wait(buf,8);
    if(ret>0)
    {
        if(strncmp(buf,"OK+RESET",8)==0)
        {
            ret = 0;
        }
        else
        {
            //RETURN ERROR
        }
    }
    kfree(buf);
    return ret;
    /*write_uart("AT+RENEW");
      read_uart();
    */
}

static ssize_t hm11_set_role(char *str)
{
    ssize_t ret = 0;
    char role_cmd[9];
    char *buf;
    snprintf(role_cmd, sizeof(role_cmd), "AT+ROLE%s", str);
    ret = hm11_transmit(role_cmd,8);
    if(ret<0)
    {
        return ret;
    }
    buf = kmalloc(8*sizeof(char),GFP_KERNEL);
    if(!buf)
    {
        return -ENOMEM;
    }
    ret = fixed_wait(buf,8);
    if(ret>0)
    {
        char expected_str[9];
        snprintf(expected_str,sizeof(expected_str), "OK+Set:%s", str);
        if(str[0] == '1')
        {
            if(strncmp(buf, "OK+Set:1", 8) == 0)
            {
                ret = 0;
            }
            else
            {
                //RETURN ERROR
            }
        }
        else if(str[0] == '0')
        {
            if(strncmp(buf, "OK+Set:0", 8) == 0)
            {
                ret = 0;
            }
            else
            {
                //RETURN ERROR
            }
        }
        else
        {
            //RETURN ERROR
        }

    }
    kfree(buf);
    return ret;
    /*write_uart("role_cmd");
    read_uart();*/
}

static void hm11_sleep()
{
    /*write_uart("AT+SLEEP");
    read_uart();*/
}

static ssize_t hm11_read_notified(void)
{
    ssize_t bytes_received = 0;
    ssize_t index = 0;
    char found = 0;

    char buffer_contents[512];

    //Read all buffer contents
    bytes_received = variable_wait_limited(buffer_contents,512);

    //return error
    if(bytes_received < 0)
    {
        return bytes_received;
    }
    else if(bytes_received < 2)
    {
        return 0;
    }

    //Look for the starting byte of a notification message, starting from the end
    //so the last value is returned to user-space
    // -2 is used because the buffer might contain the identifier at its last position, and no data is found after that
    index = bytes_received - 2; 
    while(!found && index >= 0)
    {
        if(buffer_contents[index] == HEART_RATE_ID)
            found = 1;
        else
            index--;
    }

    if(index < 0)
        return 0;
    else
        //Return the value right after the identifier found
        return buffer_contents[index + 1];
}

module_init(hm11_init_module);
module_exit(hm11_cleanup_module);