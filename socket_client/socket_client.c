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


#define IP_ADDR                 ("10.0.0.103")
#define TCP_PORT                (9000)
#define LOG_FILE                ("/dev/uart_serial-481a8000")

bool signal_caught = false;
static void signal_handler();
static int write_to_log_file(char buf);
static int setup_signal(int signo);
static int write_fd(int fd, char *str, int size);

static void signal_handler()
{
    printf("A signal has been caught.\n");
    signal_caught = true;
}

static int write_fd(int fd, char *str, int size)
{
    int written_bytes;
    char *ptr_to_write = str;
    while(size != 0)
    {
        written_bytes = write(fd, ptr_to_write, size);
        if(written_bytes == -1)
        {
            //If the error is caused by an interruption of the system call try again
            if(errno == EINTR)
                continue;

            //Else, error occurred, print it to syslog and finish program
            printf("Could not write to the file: %s", strerror(errno));
            return -errno;
        }
        size -= written_bytes;
        ptr_to_write += written_bytes; 
    }

    return 0;
}

static int write_to_log_file(char buf)
{
    int log_file_desc, ret = 0;
    log_file_desc = open(LOG_FILE,O_RDWR);
    if(log_file_desc == -1)
    {
        printf("Open: %s",strerror(errno));
        return -1;
    }
    char uart_message[40];
    int size = snprintf(uart_message, 32, "Heart rate value received: %d\n", buf); 
    if(size < 0)
    {
        printf("Could not parse string to be sent to the UART.\n");
        ret = -1;
        goto out;
    }

    if(write_fd(log_file_desc, uart_message, size))
    {
        printf("An error occured while writing to UART.\n");
        ret = -1;
        goto out;
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
	char server_reply;
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
        ret = recv(socket_desc, &server_reply, 1, 0);
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
            break;
        }
        else
        {   
            printf("Writing value %d to the log file.\n", server_reply);
            if(write_to_log_file(server_reply)==-1)
            {
                goto out;
            }
        }
    }
	out: close(socket_desc);
         printf("Signal got caught and terminating.\n");
	     return 0;
}