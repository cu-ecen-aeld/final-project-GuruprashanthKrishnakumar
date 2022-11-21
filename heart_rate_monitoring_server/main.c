/**
 * @file main.c
 * @brief Implements a socket server that connects to a BLE module.
 *
 * The BLE module (HM-11) connects to a heart rate sensor and sends
 * the different values to the connected clients
 *  
 * @author Jordi Cros Mompart
 * @date November 20 2022
 */

//Includes
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "../hm11_lkm/hm11_ioctl.h"

/**
* main
* @brief Follows the steps described in the file header.
* 
* @param int	number of command arguments
* @param char**	array of arguments
* @return 0
*/
int main(int c, char **argv)
{
    int hm11_dev = open("/dev/hm11", O_RDWR);
    if(hm11_dev < 0)
    {
        printf("HM-11 module could not be open.\n");
        return 1;
    }

    char res;
    struct hm11_ioctl_str ioctl_str;
    ioctl_str.str = malloc(2048);
    if(!ioctl_str.str)
    {
        printf("Could not allocate space to test ioctl commands.\n");
        return 1;
    }
    ioctl_str.str_len = 8;
    strcpy(ioctl_str.str, "Testing");

    //Test ioctl calls
    printf("Testing HM11_ECHO...\n");
    if(ioctl(hm11_dev, HM11_ECHO, &res))
    {
        printf("Echo did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_MAC_RD...\n");
    if(ioctl(hm11_dev, HM11_MAC_RD, &ioctl_str))
    {
        printf("MAC read did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_MAC_WR...\n");
    if(ioctl(hm11_dev, HM11_MAC_WR, &ioctl_str))
    {
        printf("MAC write did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_CONN_LAST_DEVICE...\n");
    if(ioctl(hm11_dev, HM11_CONN_LAST_DEVICE))
    {
        printf("Connect to last device did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_CONN_MAC...\n");
    if(ioctl(hm11_dev, HM11_CONN_MAC, &ioctl_str))
    {
        printf("Connect to a MAC address did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_DISCOVER...\n");
    if(ioctl(hm11_dev, HM11_DISCOVER, &ioctl_str))
    {
        printf("Discovery did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_SERVICE_DISCOVER...\n");
    if(ioctl(hm11_dev, HM11_SERVICE_DISCOVER, &ioctl_str))
    {
        printf("Service discovery did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_CHARACTERISTIC_DISCOVER...\n");
    if(ioctl(hm11_dev, HM11_CHARACTERISTIC_DISCOVER, &ioctl_str))
    {
        printf("Characteristic discovery did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_CHARACTERISTIC_NOTIFY...\n");
    if(ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY, &ioctl_str))
    {
        printf("Characteristic notify did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_CHARACTERISTIC_NOTIFY_OFF...\n");
    if(ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY_OFF, &ioctl_str))
    {
        printf("Unsubscription to characteristic did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_PASSIVE...\n");
    if(ioctl(hm11_dev, HM11_PASSIVE))
    {
        printf("Passive mode set did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_NAME...\n");
    if(ioctl(hm11_dev, HM11_NAME, &ioctl_str))
    {
        printf("Change device name did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_DEFAULT...\n");
    if(ioctl(hm11_dev, HM11_DEFAULT))
    {
        printf("Set default device configuration did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_ROLE...\n");
    if(ioctl(hm11_dev, HM11_ROLE, &ioctl_str))
    {
        printf("Set device role did not perform successfully: %s\n", strerror(errno));
    }
    printf("Testing HM11_SLEEP...\n");
    if(ioctl(hm11_dev, HM11_SLEEP, &res))
    {
        printf("Device sleep did not perform successfully: %s\n", strerror(errno));
    }

    if(close(hm11_dev) == -1)
    {
        //Else, error occurred, print it to syslog and finish program
        printf("Could not close HM-11 driver\n");
        //Even though error, try to close following files
    }

    return 0;
}