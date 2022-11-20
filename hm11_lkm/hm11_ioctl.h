/**
* @file hm11_ioctl.h
* @brief Declares ioctl commands
*
* Used both for user-space and kernel-space
*
* @author Jordi Cros Mompart
* @date November 2022
*
* @version 1.0
*
*/

#ifndef HM11_IOCTL_H
#define HM11_IOCTL_H

#ifdef __KERNEL__
#include <asm-generic/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
#endif

#define MAC_SIZE                (12)
#define MAC_SIZE_STR            (MAC_SIZE + 1)
#define CHARACTERISTIC_SIZE     (4)
#define CHARACTERISTIC_SIZE_LEN (CHARACTERISTIC_SIZE + 1)
#define MAX_NAME_LEN            (12 + 1)

struct hm11_ioctl_str 
{
    //Contains the string contents. MUST be allocated before use
    char *str;
    //Size of the string including the null-terminator
    size_t str_len;
};


//Picked an arbitrary unused value from https://github.com/torvalds/linux/blob/master/Documentation/userspace-api/ioctl/ioctl-number.rst
#define HM11_IOC_MAGIC 0x18

//Echo command.
    //If char == 0, the device is awake and not paired
    //If char == 1, the device was paired and has been disconnected (this cmd does not force a disconnection)
    //If char == 2, the device was asleep and has been awaken
#define HM11_ECHO _IOR(HM11_IOC_MAGIC, 1, char)

//MAC Address request
#define HM11_MAC_RD _IOR(HM11_IOC_MAGIC, 2, struct hm11_ioctl_str)

//MAC Address change
#define HM11_MAC_WR _IOW(HM11_IOC_MAGIC, 3, struct hm11_ioctl_str)

//Connect last succeeded device
    //If return value == 0, connection has been successful
    //If return value == -ENODEV, connection has not been possible
#define HM11_CONN_LAST_DEVICE _IO(HM11_IOC_MAGIC, 4)

//Connect to MAC address
    //If return value == 0, connection is successful
    //If return value == -ENODEV, connection has not been possible
    //If return value == -EBUSY, the device has already an active connection
#define HM11_CONN_MAC _IOW(HM11_IOC_MAGIC, 5, struct hm11_ioctl_str)

//Discover devices
    //The string provided must have space for up to 100 MAC addresses
#define HM11_DISCOVER _IOR(HM11_IOC_MAGIC, 6, struct hm11_ioctl_str)

//Find services on connected device
    //TODO define max size expected. For now, 1024 characters
#define HM11_SERVICE_DISCOVER  _IOR(HM11_IOC_MAGIC, 7, struct hm11_ioctl_str)

//Find characteristics on connected device
    //TODO define max size expected. For now, 1024 characters
#define HM11_CHARACTERISTIC_DISCOVER  _IOR(HM11_IOC_MAGIC, 8, struct hm11_ioctl_str)

//Subscribe to a characteristic notification
    //If return value == 0, subscription is successful
    //If return value == -ENODEV, the characteristic cannot handle subscription or doesn't exist
#define HM11_CHARACTERISTIC_NOTIFY  _IOR(HM11_IOC_MAGIC, 9, struct hm11_ioctl_str)

//Subscribe to a characteristic notification
    //If return value == 0, subscription is successful
    //If return value == -ENODEV, the characteristic cannot handle subscription or doesn't exist
#define HM11_CHARACTERISTIC_NOTIFY_OFF  _IOR(HM11_IOC_MAGIC, 10, struct hm11_ioctl_str)

//Let device not perform any automatic work
#define HM11_PASSIVE    _IO(HM11_IOC_MAGIC, 11)

//Set device name
#define HM11_NAME   _IOW(HM11_IOC_MAGIC, 12, struct hm11_ioctl_str)

//Reset command
#define HM11_DEFAULT _IO(HM11_IOC_MAGIC, 13)

//Role command
    //"1" for Master
    //"0" for Peripheral
#define HM11_ROLE   _IOW(HM11_IOC_MAGIC, 14, struct hm11_ioctl_str)

//Sleep command
#define HM11_SLEEP  _IO(HM11_IOC_MAGIC, 15)


/**
 * The maximum number of commands supported, used for bounds checking
 */
#define HM11_IOC_MAXNR 1

#endif /* HM11_IOCTL_H */
