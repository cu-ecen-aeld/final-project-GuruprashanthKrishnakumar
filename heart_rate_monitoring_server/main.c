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
#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include "../hm11_lkm/hm11_ioctl.h"
#include "queue.h"

#define HEART_RATE_MAC              ("0C8CDC32BDEC")
#define HEART_RATE_CHARACTERISTIC   ("0026")
#define MAX_CLIENTS                 (10)

struct client_thread_t
{
    pthread_t thread_id;
    int socket_client;
    int socket_server;
    struct sockaddr_storage client_addr;
    pthread_mutex_t new_value_available_mutex;
    char new_value_available;
    char finished;
    sem_t new_value;

    //Linked list node instance
    SLIST_ENTRY(client_thread_t) node;
};

static char terminated = 0;
static char heart_rate;
//Linked list of threads
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER; 
SLIST_HEAD(head_s, client_thread_t) head;
//Timer that attempts client thread joins
static timer_t timer;
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

static void empty_function()
{
    return;
}

static int setup_signal(int signo)
{
    struct sigaction action;
    if(signo == SIGINT || signo == SIGTERM)
    {
        action.sa_handler = signalhandler;
    }
    else if(signo == SIGALRM)
    {
        action.sa_handler = empty_function;
    }
    action.sa_flags = 0;
    sigset_t empty;
    if(sigemptyset(&empty) == -1)
    {
        printf("Could not set up empty signal set: %s.", strerror(errno));
        return -1; 
    }
    action.sa_mask = empty;
    if(sigaction(signo, &action, NULL) == -1)
    {
        printf("Could not set up handle for signal: %s.", strerror(errno));
        return -1;         
    }
    return 0;
}

static void clean_threads()
{
    //Perform cleaning of the current list on every new connection
    printf("Cleaning threads...\n");
    struct client_thread_t *element = NULL;
    struct client_thread_t *tmp = NULL;

    SLIST_FOREACH_SAFE(element, &head, node, tmp)
    {
        if(element->finished)
        {
            SLIST_REMOVE(&head, element, client_thread_t, node);
            //Join the thread
            int ret = pthread_join(element->thread_id, NULL);
            if(ret != 0)
            {
                printf("Could not join thread: %s", strerror(ret));
            }
            //Free the memory used by the structure
            pthread_mutex_destroy(&element->new_value_available_mutex);
            free(element);
        }
    }
}

/**
* print_accepted_conn
* @brief Prints the IP address used by the client socket
*
* @param  sockaddr_storage contains client information
* @return void
*/
void print_accepted_conn(struct sockaddr_storage client_addr)
{
    //Get information from the client
    //Credits: https://stackoverflow.com/questions/1276294/getting-ipv4-address-from-a-sockaddr-structure
    if(client_addr.ss_family == AF_INET)
    {
        char addr[INET6_ADDRSTRLEN];
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_addr;
        inet_ntop(AF_INET, &(addr_in->sin_addr), addr, INET_ADDRSTRLEN);
        printf("Accepted connection from %s\n", addr);
    }
    else if(client_addr.ss_family == AF_INET6)
    {
        char addr[INET6_ADDRSTRLEN];
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&client_addr;
        inet_ntop(AF_INET6, &(addr_in6->sin6_addr), addr, INET6_ADDRSTRLEN);
        printf("Accepted connection from %s\n", addr);
    }
}

/**
* handle_client
* @brief Handles a client of the socket server.
*
* @param  void* pointer to relevant client information
* @return void
*/
static void *handle_client(void *client_info)
{
    struct client_thread_t *client_info_parsed = (struct client_thread_t *) client_info;
    print_accepted_conn(client_info_parsed->client_addr);

    char unused; //Server does not receive data from client

    do
    {   
        printf("Checking if the client has terminated connection...\n");
        //Check if the client has finished connection
        if(!recv(client_info_parsed->socket_client, &unused, sizeof(char), MSG_DONTWAIT))
            goto terminate_client;
        printf("Connection is still active; waiting for an available value...\n");

        //Wait for a new value to come from the HM11 driver
        if(sem_wait(&client_info_parsed->new_value))
        {
            printf("Error while waiting for the semaphore, iterating again.\n");
            continue;
        }
        //Clear flag
        client_info_parsed->new_value_available = 0;

        //Get the value
        char heart_rate_value = heart_rate;

        //Send it to the client
        int sent_bytes = 0;
        printf("Sending HR: %d to...\n", heart_rate);
        while(sent_bytes != 1)
        {
            sent_bytes = send(client_info_parsed->socket_client, &heart_rate_value, sizeof(char), 0);
            if(sent_bytes == -1)
                goto terminate_client;
        }
        
    } while(!terminated);

terminate_client:
    client_info_parsed->finished = 1;
    return NULL;
}

/**
* main_server_thread
* @brief Handled new connections and terminations to the socket server
*
* @param  void* pointer to the socket value
* @return void
*/
static void *main_server_thread(void *socket)
{
    if(!socket)
    {
        printf("Server thread could not be properly created since the socket pointer is NULL.\n");
        return NULL;
    }
    int sck = *(int *)socket;

    if(setup_signal(SIGALRM) < 0)
    {
        printf("Could not set up SIGARLM.\n");
        return NULL;
    }

    //Start a loop of receiving contents  
    while(!terminated)
    {
        struct sockaddr_storage client_addr;
        socklen_t addr_size = sizeof client_addr;

        //Accept a connection
        int connection_fd = accept(sck, (struct sockaddr *) &client_addr, &addr_size);
        if(connection_fd < 0)
        {
            //Implementation defined: wait for next request, not terminate server
            if(connection_fd != EINTR)
                printf("An error occurred accepting a new connection to the socket: %s", strerror(errno));
            clean_threads();
        }
        else
        {
            //Add new service information to a new element of the thread linked list
            struct client_thread_t *new = malloc(sizeof(struct client_thread_t));
            new->socket_client = connection_fd;
            new->finished = 0;
            if(sem_init(&new->new_value, 0, 0))
            {
                printf("Error initializing the client semaphore.\n");
                continue;
            }
            new->socket_server = sck;
            memcpy((void *) &new->client_addr, (const void *) &client_addr, sizeof(struct sockaddr_storage));
            
            //Create the thread that will serve the client
            int ret = pthread_create(&new->thread_id, NULL, handle_client, (void *) new);
            if(ret!= 0)
            {            
                printf("Could not create new thread: %s", strerror(ret));

                //Implementation defined: wait for next request, not terminate server
                continue; 
            }

            //Add the thread information to the linked list
            printf("Inserting the element to the list.\n");
            ret = pthread_mutex_lock(&list_mutex);
            if(ret != 0)
            {
                printf("Could not lock mutex to access list.\n");
                break;  
            }
            SLIST_INSERT_HEAD(&head, new, node);
            ret = pthread_mutex_unlock(&list_mutex);
            if(ret != 0)
            {
                printf("Could not unlock mutex to access list.\n");
                break;  
            }
        }
    }

    //Make sure all threads finish and are joined
    while(!SLIST_EMPTY(&head))
    {
        clean_threads();
    }

    return NULL;
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
        goto close_hm11;
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
        goto close_hm11;
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
        goto close_hm11;
    }
    cmd_str.str[0] = '1';
    ret = ioctl(hm11_dev, HM11_ROLE, &cmd_str);
    if(ret)
    {
        printf("An error occurred setting the device as Controller: %s\n", strerror(ret));
        free(cmd_str.str);
        goto close_hm11;
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
        goto close_hm11;
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
        goto close_hm11;
    }
    strncpy(cmd_str.str, HEART_RATE_MAC, MAC_SIZE_STR);
    cmd_str.str_len = MAC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CONN_MAC, &cmd_str);
    if(ret)
    {
        printf("Could not connect to the device, aborting: %s\n", strerror(ret));
        free(cmd_str.str);
        goto close_hm11;
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
        goto close_hm11;
    }
    strncpy(cmd_str.str, HEART_RATE_CHARACTERISTIC, CHARACTERISTIC_SIZE_STR);
    cmd_str.str_len = CHARACTERISTIC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY, &cmd_str);
    if(ret)
    {
        printf("Could not request characteristic notify: %s\n", strerror(ret));
        free(cmd_str.str);
        goto close_hm11;
    }
    else
    {
        printf("Characteristic successfully requested for notification.\n");
    }
    free(cmd_str.str);

    //Set a signal handler to gracefully terminate the server
    if(setup_signal(SIGINT) < 0)
    {
        printf("Could not set up SIGARLM.\n");
        goto close_hm11;
    }

    //Configure the socket server that clients will connect to get heart rate measurements
    struct addrinfo hints;
    //Needs to be freed after using
    struct addrinfo *res;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(NULL, "9000", &hints, &res) != 0)
    {
        printf("An error occurred setting up the socket.\n");
        goto close_hm11;
    }

    //Create the socket file descriptor
    int sck = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(sck == -1)
    {
        printf("An error occurred setting up the socket: %s\n", strerror(errno));
        freeaddrinfo(res);
        goto close_hm11;
    }
    //Bind the socket to the addr+port specified in "getaddrinfo"
    if(bind(sck, res->ai_addr, res->ai_addrlen) == -1)
    {
        printf("An error occurred binding the socket: %s\n", strerror(errno));
        freeaddrinfo(res);
        goto close_socket_hm11;     
    }

    //Free the addr linked list now that we have already used it
    freeaddrinfo(res);

    //Start listening for a max of MAX_CLIENTS connections
    if(listen(sck, MAX_CLIENTS) == -1)
    {
        printf("An error occurred listening the socket: %s\n", strerror(errno));
        goto close_socket_hm11;       
    }
    printf("The server is listening to port 9000\n");

    //Initialize Liked List of client threads
    SLIST_INIT(&head);

    //Create a timer routine that joins threads as they finish
    struct itimerspec interval_time;
	struct itimerspec last_interval_time;

	//Set up to signal SIGALRM if timer expires
	ret = timer_create(CLOCK_MONOTONIC, NULL, &timer);
	if(ret)
	{
		printf("Failed on creating time.\n");
		goto close_socket_hm11;    
	}

	//Arm the interval timer
	interval_time.it_interval.tv_sec = 5;
	interval_time.it_interval.tv_nsec = 0;
	interval_time.it_value.tv_sec = 5;
	interval_time.it_value.tv_nsec = 0;

	ret = timer_settime(timer, 0, &interval_time, &last_interval_time);
	if(ret)
	{
		printf("Scheduler setup failed on setting time value.");
		goto close_socket_hm11;
	}

    //Create a thread for the socket created, to wait for connections without interrumping the interaction with the driver
    pthread_t server_thread_id;
    ret = pthread_create(&server_thread_id, NULL, main_server_thread, (void *) &sck);
    if(ret)
    {            
        printf("Could not create server thread: %s\n", strerror(ret));
        goto close_socket_hm11;  
        return 1;  
    }

    //At this point, notification values will be received by the drivers; read most recent every 2s
    while(!terminated)
    {
        sleep(2);
        printf("Getting heart rate values...\n");
        ret = ioctl(hm11_dev, HM11_READ_NOTIFIED, &heart_rate);
        if(ret)
        {
            printf("Could not read notified heart rate value: %d, %s\n", ret, strerror(ret));
        }
        else
        {
            //Update the flag on every existing thread
            struct client_thread_t *element = NULL;
            struct client_thread_t *tmp = NULL;
            ret = pthread_mutex_lock(&list_mutex);
            if(ret != 0)
            {
                printf("Could not lock mutex to access list.\n");
                goto close_all;  
            }
            if(!SLIST_EMPTY(&head))
            {
                SLIST_FOREACH_SAFE(element, &head, node, tmp)
                {
                    if(sem_post(&element->new_value))
                    {
                        printf("Error while posting for the semaphore, iterating again.\n");
                        continue;
                    }
                    printf("Semaphore to the socket has been set.\n");
                }
            }
            else
            {
                printf("The current heart rate is: %d\n", heart_rate);
            }
            ret = pthread_mutex_unlock(&list_mutex);
            if(ret != 0)
            {
                printf("Could not unlock mutex to access list.\n");
                goto close_all;  
            }
        }
        
    }

    //Unsubscribe to the notified value
    cmd_str.str = malloc(sizeof(char)*CHARACTERISTIC_SIZE_STR);
    if(!cmd_str.str)
    {
        printf("Mallocing of a command string has not been possible, aborting.\n");
        goto close_all;
    }
    strncpy(cmd_str.str, HEART_RATE_CHARACTERISTIC, CHARACTERISTIC_SIZE_STR);
    cmd_str.str_len = CHARACTERISTIC_SIZE_STR;
    ret = ioctl(hm11_dev, HM11_CHARACTERISTIC_NOTIFY_OFF, &cmd_str);
    if(ret)
    {
        printf("Could not request characteristic unnotify: %s\n", strerror(ret));
        free(cmd_str.str);
        goto close_all;
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
        goto close_all;  
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


close_all:
    terminated = 1;
    //Wait for server thread to finalize
    printf("Joining threads...\n");
    ret = pthread_join(server_thread_id, NULL);
    if(ret)
    {
        printf("Could not join server thread: %s", strerror(ret));
        return 1;  
    }
    if(timer_delete(timer))
    {
        printf("Could not delete timer.\n");
    }

close_socket_hm11:
    if(close(sck) == -1)
    {
        //Else, error occurred, print it to syslog and finish program
        printf("Could not close socket: %s\n", strerror(errno));
    }

close_hm11:
    if(close(hm11_dev))
    {
        printf("Could not close HM11 File Descriptor: %s.\n", strerror(errno));
    }
    return 0;
}