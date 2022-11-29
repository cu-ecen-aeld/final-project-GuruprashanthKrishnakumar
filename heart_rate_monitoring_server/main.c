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

#define HEART_RATE_MAC  ("0C8CDC32BDEC")

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
    int ret;
    struct hm11_ioctl_str cmd_str;
    char char_ret;
    int hm11_dev = open("/dev/hm11", O_RDWR);
    if(hm11_dev < 0)
    {
        printf("HM-11 module could not be open.\n");
        return 1;
    }

    printf("The HM11 module has been successfully opened.\n");

    //Perform sanity check
    printf("Performing sanity check...\n");
    ret = ioctl(hm11_dev, HM11_ECHO, &char_ret);
    if(ret)
    {
        printf("An error occured while issuing an ECHO to the HM11 module: %s\n", strerror(ret));
    }
    switch(char_ret)
    {
    case 0:
        printf("Echo performed successfully, device was idle.\n");
        break;
    case 1:
        printf("Echo performed successfully, device has been disconnected from its peer.\n");
        break;
    case 2:
        printf("Echo performed successfully, device has been awaken from sleep.\n");
        break;
    }

    //Reset device to get default configuration
    printf("Resetting device to get default configuration.\n");
    ret = ioctl(hm11_dev, HM11_DEFAULT);
    if(ret)
    {
        printf("Setting the device to default did not perform successfully: %s\n", strerror(ret));
        printf("Terminating program.\n");
        return 1;
    }
    else
    {
        printf("HM11 successfully set to default configuration.\n");
    }

    //Set device to Master
    printf("Setting device to Controller (Master)\n");
    cmd_str.str_len = 1;
    cmd_str.str = malloc(sizeof(char)*1);
    if(!cmd_str.str)
    {
        printf("Mallocing of a command string has not been possible, aborting.\n");
        return 1;
    }
    cmd_str.str[0] = '1';
    ret = ioctl(hm11_dev, HM11_ROLE, cmd_str);
    if(ret)
    {
        printf("An error occurred setting the device as Controller: %s\n", strerror(ret));
        return 1;
    }
    else
    {
        printf("Device successfully set as Controller.\n");
    }
    free(cmd_str.str);

    //Set device to passive mode (avoid automatic discovery performance, etc.)
    printf("Setting device to passive mode\n");
    ret = ioctl(hm11_dev, HM11_PASSIVE);
    if(ret)
    {
        printf("Could not set device to passive mode, aborting.\n");
        return 1;
    }
    else
    {
        printf("Device successfully set to passive mode.\n");
    }

    //Now the device is ready to be connected to the heart rate belt
    printf("Attempting connection with the heart rate belt\n");
    cmd_str.str = malloc(sizeof(char)*MAC_SIZE_STR);
    if(!cmd_str.str)
    {
        printf("Mallocing of a command string has not been possible, aborting.\n");
        return 1;
    }
    cmd_str.str_len = MAC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CONN_MAC, cmd_str);
    if(ret)
    {
        printf("Could not connect to the device, aborting: %s\n", strerror(ret));
        return 1;
    }
    else
    {
        printf("Connection to the heart rate has been successful.\n");
    }
    free(cmd_str.str);

    //Subscribe to heart rate characteristic
    printf("Subscribing to the heart rate value.\n");
    cmd_str.str = malloc(sizeof(char)*CHARACTERISTIC_SIZE_STR);
    if(!cmd_str.str)
    {
        printf("Mallocing of a command string has not been possible, aborting.\n");
        return 1;
    }
    cmd_str.str_len = CHARACTERISTIC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY, cmd_str);
    if(ret)
    {
        printf("Could not request characteristic notify: %s\n", strerror(ret));
        return 1;
    }
    else
    {
        printf("Characteristic successfully requested for notification.\n");
    }
    free(cmd_str.str);

    return 0;
}