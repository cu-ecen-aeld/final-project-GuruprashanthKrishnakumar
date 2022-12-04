#include<stdio.h>
#include<string.h>	//strlen
#include<sys/socket.h>
#include<arpa/inet.h>	//inet_addr
#include<unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>


#define IP_ADDR                 ("192.168.1.3")
#define TCP_PORT                (9000)
#define LOG_FILE                ("/dev/uart_serial-481a8000")

bool signal_caught = false;
static void signal_handler();
static int write_to_log_file(char *buf, size_t len);
static int write_byte(int fd, char *buf);
static int setup_signal(int signo);

static void signal_handler()
{
    signal_caught = true;
}

static int write_byte(int fd, char *buf)
{
    int ret = 0;
    ret = write(fd,buf,1);
    if(ret < 0)
    {
        printf("Write: %s",strerror(errno));
        return -1;
    }
    return 0;
}

static int write_to_log_file(char *buf, size_t len)
{
    int log_file_desc,num_bytes_written = 0,ret = 0;
    log_file_desc = open(LOG_FILE,O_RDWR);
    if(log_file_desc == -1)
    {
        printf("Open: %s",strerror(errno));
        return -1;
    }
    while(num_bytes_written < len)
    {
        printf("Value: %d\n", buf[num_bytes_written]);
        if(write_byte(log_file_desc,&buf[num_bytes_written])==-1)
        {
            goto out;
        }
        if(write_byte(log_file_desc,"\n")==-1)
        {
            goto out;
        }
        num_bytes_written +=1;
    }    
    ret = 0;
    out: close(log_file_desc);
        return ret;
}

static int setup_signal(int signo)
{
    struct sigaction action;
    if(signo == SIGINT || signo == SIGTERM)
    {
        action.sa_handler = signal_handler;
    }
    action.sa_flags = 0;
    sigset_t empty;
    if(sigemptyset(&empty) == -1)
    {
        printf( "Could not set up empty signal set: %s.", strerror(errno));
        return -1; 
    }
    action.sa_mask = empty;
    if(sigaction(signo, &action, NULL) == -1)
    {
        printf( "Could not set up handle for signal: %s.", strerror(errno));
        return -1;         
    }
    return 0;
}
int main(void)
{
	int socket_desc,ret = 0;
	struct sockaddr_in server;
	char server_reply[512];
	//char *message;
	//Create socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
	}
		
	server.sin_addr.s_addr = inet_addr(IP_ADDR);
	server.sin_family = AF_INET;
	server.sin_port = htons( TCP_PORT );

	//Connect to remote server, do we need to handle any specific error?
	while (connect(socket_desc , (struct sockaddr *)&server , sizeof(server)) < 0);
	
	puts("Connected\n");
	
	// //Send some data
	// message = "Start";
	// if( send(socket_desc , message , strlen(message) , 0) < 0)
	// {
	// 	puts("Send failed");
	// 	return 1;
	// }
	// puts("Data Send\n");
	if(setup_signal(SIGINT)== -1)
    {
        printf("Error setting up SIGINT\n");
        return -1;
    }
    if(setup_signal(SIGTERM)==-1)
    {
        printf("Error setting up SIGTERM\n");
        return -1;
    }
	//Receive a reply from the server
    while(!signal_caught)
    {
        ret = recv(socket_desc, server_reply , 512 , 0);
        if(ret<0)
        {
            if(ret == -EINTR)
            {
                //caught signal. Send stop message to the socket server and exit.
                printf("Caught a signal and terminating program.\n");
                goto out;
            }
            else
            {
                //need to handle any other errors?
                printf("Unexpected.\n");
            }
        }
        else if(ret == 0)
        {
            //?? server closed the connection?
            printf("Server closed the connection.\n");
        }
        else
        {
            if(write_to_log_file(server_reply,ret)==-1)
            {
                goto out;
            }
        }
    }
	out: close(socket_desc);
	     return 0;
}