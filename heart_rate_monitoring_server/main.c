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
#include <signal.h>
#include "../hm11_lkm/hm11_ioctl.h"

#define HEART_RATE_MAC              ("0C8CDC32BDEC")
#define HEART_RATE_CHARACTERISTIC   ("0026")

static char terminated = 0;

/**
* sighandler
* @brief Handles the SIGINT and SIGTERM signals.
*
* @param  int signal that triggered this function
* @return void
*/
static void signalhandler(int sig)
{
    if(sig == SIGINT)
    {
       printf("Signal received, gracefully terminating server.\n");
    
       terminated = 1;
    }
}

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
        printf("HM-11 module could not be open: %s.\n", strerror(errno));
        return 1;
    }

    printf("The HM11 module has been successfully opened.\n");

    //Perform sanity check
    printf("Performing sanity check...\n");
    ret = ioctl(hm11_dev, HM11_ECHO, &char_ret);
    if(ret)
    {
        printf("An error occured while issuing an ECHO to the HM11 module: %s\n", strerror(ret));
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
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
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
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
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    cmd_str.str[0] = '1';
    ret = ioctl(hm11_dev, HM11_ROLE, &cmd_str);
    if(ret)
    {
        printf("An error occurred setting the device as Controller: %s\n", strerror(ret));
        free(cmd_str.str);
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
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
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    else
    {
        printf("Device successfully set to passive mode.\n");
    }

    //Now the device is ready to be connected to the heart rate belt
    printf("Attempting connection with the heart rate belt\n");
    sleep(2);
    cmd_str.str = malloc(sizeof(char)*MAC_SIZE_STR);
    if(!cmd_str.str)
    {
        printf("Mallocing of a command string has not been possible, aborting.\n");
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    strncpy(cmd_str.str, HEART_RATE_MAC, MAC_SIZE_STR);
    cmd_str.str_len = MAC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CONN_MAC, &cmd_str);
    if(ret)
    {
        printf("Could not connect to the device, aborting: %s\n", strerror(ret));
        free(cmd_str.str);
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
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
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    strncpy(cmd_str.str, HEART_RATE_CHARACTERISTIC, CHARACTERISTIC_SIZE_STR);
    cmd_str.str_len = CHARACTERISTIC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY, &cmd_str);
    if(ret)
    {
        printf("Could not request characteristic notify: %s\n", strerror(ret));
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    else
    {
        printf("Characteristic successfully requested for notification.\n");
    }
    free(cmd_str.str);

    //Set a signal handler to gracefully terminate the server
    struct sigaction action;
    action.sa_handler = signalhandler;
    action.sa_flags = 0;
    sigset_t empty;
    if(sigemptyset(&empty) == -1)
    {
        printf("Could not set up empty signal set: %s.", strerror(errno));
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    action.sa_mask = empty;
    if(sigaction(SIGINT, &action, NULL) == -1)
    {
        printf("Could not set up handle for SIGINT: %s.", strerror(errno));
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }

    //At this point, notification values will be received by the drivers; read most recent every 2s
    char heart_rate;
    while(!terminated)
    {
        sleep(2);
        printf("Getting heart rate values...\n");
        ret = ioctl(hm11_dev, HM11_READ_NOTIFIED, &heart_rate);
        if(ret)
        {
            printf("Could not read notified heart rate value: %s\n", strerror(ret));
        }
        else
        {
            printf("The current heart rate is: %d\n", heart_rate);
        }
        
    }

    //Unsubscribe to the notified value
    cmd_str.str = malloc(sizeof(char)*CHARACTERISTIC_SIZE_STR);
    if(!cmd_str.str)
    {
        printf("Mallocing of a command string has not been possible, aborting.\n");
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    strncpy(cmd_str.str, HEART_RATE_CHARACTERISTIC, CHARACTERISTIC_SIZE_STR);
    cmd_str.str_len = CHARACTERISTIC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY_OFF, &cmd_str);
    if(ret)
    {
        printf("Could not request characteristic unnotify: %s\n", strerror(ret));
        free(cmd_str.str);
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    else
    {
        printf("Characteristic successfully unsubscribed for notification.\n");
    }
    free(cmd_str.str);

    //Disconnect device
    printf("Disconnecting from peer device...\n");
    ret = ioctl(hm11_dev, HM11_ECHO, &char_ret);
    if(ret)
    {
        printf("An error occured while issuing an ECHO to the HM11 module: %s\n", strerror(ret));
        if(close(hm11_dev))
        {
            printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
        }
        return 1;
    }
    switch(char_ret)
    {
    case 0:
        printf("Echo performed successfully, device was unexpectedly idle.\n");
        break;
    case 1:
        printf("Echo performed successfully, device has been disconnected from its peer.\n");
        break;
    case 2:
        printf("Echo performed successfully, device has been awaken from sleep.\n");
        break;
    }

    //Close file descriptor
    if(close(hm11_dev))
    {
        printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
    }
    return 0;
}