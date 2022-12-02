/*
*   file:      aesdsocket.c
*   brief:     Implements socket functionality. Listens to port 9000 and stores any packet received in the file /var/aesdsocketdata and sends it back over the same port.
*              Packets are assumed to be separated with the \n character.
*              Can optionally be executed as daemon with '-d' command line argument.
*   author:    Guruprashanth Krishnakumar, gukr5411@colorado.edu
*   date:      10/01/2022
*   refs:      https://beej.us/guide/bgnet/html/, lecture slides of ECEN 5713 - Advanced Embedded Software Dev.
*/
/*
*   HEADER FILES
*/
#include <sys/types.h>
#include <sys/socket.h>
#include "queue.h"
#include <pthread.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include "aesd_ioctl.h"
/*
*   MACROS
*/
#define BUF_SIZE_UNIT           (1024)
#define USE_AESD_CHAR_DEVICE

#define LOG_FILE                ("/dev/uart_serial-481a8000")
#define IOCTL_COMMAND           ("")

/*
*   GLOBALS
*/
typedef enum
{
    Accept_Connections,
    Join_threads,
}main_thread_states_t;
typedef enum
{
    Receive_From_Socket,
    Parse_data,
}worker_threads_states_t;
struct worker_thread_s
{
    pthread_t thread_id;
    bool thread_completed;
    worker_threads_states_t curr_state;
    int socket_file_descriptor;
    char ip_addr[INET6_ADDRSTRLEN];
    TAILQ_ENTRY(worker_thread_s) entries;
};
typedef struct worker_thread_s worker_thread_t;
typedef TAILQ_HEAD(head_s, worker_thread_s) head_t;
typedef struct
{
    bool signal_caught;
    bool free_address_info;
    bool free_socket_descriptor;
    struct addrinfo *host_addr_info;
    int socket_descriptor;
    int connection_count;
    pthread_mutex_t mutex;
}socket_state_t;
socket_state_t socket_state;


typedef struct
{
	const char *command;
} command_table_t;

static const command_table_t commands[] = {
    {"AESDCHAR_IOCSEEKTO:"}
};

/*
*   STATIC FUNCTION PROTOTYPES
*/
/*
*   Checks the state of booleans in the socket_state structure and performs neccesarry cleanups like 
*   closing fds, freeing memory etc.,
*
*   Args:
*       None
*   Params:
*       None
*/
static void perform_cleanup();
static void* aesd_char_thread(void* thread_param);

/*
*   Setup signal handler for the signal number passed 
*
*   Args:
*       signo - signal number for which handler needs to be set up for 
*   Params:
*       0 if successful, -1 if failed 
*/
static int setup_signal(int signo);

/*
*   Signal handler for SIGINT and SIGTERM. If any open connection is on-going on the socket,
*   it sets a flag that a signal was caught. Otherwise, performs neccessarry cleanup and exits.
*
*   Args:
*       None
*   Params:
*       None
*/
static void sighandler();

/*
*   Returns the IP address present in the socket address data structure passed.
*
*   Args:
*       sockaddr    -   socket address data structure
*   Params:
*       IPv4 or IPv6 address contained in the socket address data structure
*/
static void *get_in_addr(struct sockaddr *sa);
/*
*   Dumps the content passed to a file.
*
*   Args:
*       fd    -   handle to the file to write into
*       string  -   Data to write to file
*       write_len   -   Number of bytes contained in string.
*   Params:
*       Success or failure
*/
static int dump_content(int fd, char* string,int write_len);
/*
*   Reads from a file and echoes it back across the socket.
*
*   Args:
*       fd    -   handle to the file to read
*       read_len   -   Number of bytes contained in file.
*   Params:
*       Success or failure
*/
static int echo_file_socket(int fd, int socket_fd);
/*
*   Initializes the socket state structure to a known initial state.
*
*   Args:
*       None
*   Params:
*       None
*/
static void initialize_socket_state();

/*
*   FUNCTION DEFINITIONS
*/

static int setup_signal(int signo)
{
    struct sigaction action;
    if(signo == SIGINT || signo == SIGTERM)
    {
        action.sa_handler = sighandler;
        action.sa_flags = 0;
        sigset_t empty;
        if(sigemptyset(&empty) == -1)
        {
            syslog(LOG_ERR, "Could not set up empty signal set: %s.", strerror(errno));
            return -1; 
        }
        action.sa_mask = empty;
        if(sigaction(signo, &action, NULL) == -1)
        {
            syslog(LOG_ERR, "Could not set up handle for signal: %s.", strerror(errno));
            return -1;         
        }
        return 0;
    }
    return -1;
}

static void initialize_socket_state()
{
    socket_state.free_address_info = false;
    socket_state.free_socket_descriptor = false;
    socket_state.signal_caught = false;
    socket_state.host_addr_info = NULL;
    socket_state.connection_count = 0;
    pthread_mutex_init(&socket_state.mutex, NULL);
}
static void perform_cleanup()
{
    if(socket_state.host_addr_info && socket_state.free_address_info)
    {
        freeaddrinfo(socket_state.host_addr_info);
    }
    if(socket_state.free_address_info)
    {
        close(socket_state.socket_descriptor);
    }
    if(socket_state.disarm_alarm)
    {
        disarm_and_destroy_timer();
    }
    pthread_mutex_destroy(&socket_state.mutex);
}
static void shutdown_function()
{
    printf("\nCaught Signal. Exiting\n");
    perform_cleanup();
    printf("Deleting file\n");
    unlink("/var/tmp/aesdsocketdata");
    exit(1);
}
static void sighandler()
{
    socket_state.signal_caught = true;
}

static void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
static int dump_content(int fd, char* string,int write_len)
{
    ssize_t ret; 
    while(write_len!=0)
    {
        ret = write(fd,string,write_len);
        if(ret == 0)
        {
            break;
        } 
        if(ret == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            //printf("Write len %d\n",write_len);
            syslog(LOG_ERR,"Write: %s",strerror(errno));
            return -1;
        }
        write_len -= ret;
        string += ret;
    }
    return 0;
}
static int echo_file_socket(int fd, int socket_fd)
{
    ssize_t ret; 
    char write_str[BUF_SIZE_UNIT];
    while(1)
    {
        memset(write_str,0,sizeof(write_str));
        ret = read(fd,write_str,sizeof(write_str));
        if(ret == 0)
        {
            break;
        } 
        if(ret == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            //printf("Read Len %d\n",read_len);
            syslog(LOG_ERR,"Read: %s",strerror(errno));
            return -1;
        }
        int num_bytes_to_send = ret;
        int num_bytes_sent = 0;
        int str_index = 0;
        while(num_bytes_to_send>0)
        {
            num_bytes_sent = send(socket_fd,&write_str[str_index],num_bytes_to_send,0);
            if(num_bytes_sent == -1)
            {
                syslog(LOG_ERR,"Send: %s",strerror(errno));
                return -1;
            }
            num_bytes_to_send -= num_bytes_sent;
            str_index += num_bytes_sent;
        }
    }
    return 0;
}


static void* server_thread(void* thread_param)
{
    worker_thread_t *thread_params = (worker_thread_t *)thread_param;
    int file_descriptor,num_bytes_read = 0;
    char c;
    while(1)
    {
        num_bytes_read = 0;
        num_bytes_read = recv(thread_params->socket_file_descriptor,&c,1,0);
        if(num_bytes_read == -1)
        {
            syslog(LOG_ERR,"Recv: %s",strerror(errno));
            //buf has to be freed
            //socket_file_descriptor needs to be freed.
            //completion boolean has to be set
            goto free_mem;
        }
        else if(num_bytes_read>0)
        {
            if(pthread_mutex_lock(&socket_state.mutex))
            {
                syslog(LOG_ERR,"Mutex_Lock %s",strerror(errno));
                goto free_socket_fd;
            }
            file_descriptor = open(LOG_FILE,O_RDWR,666);
            if(file_descriptor == -1)
            {
                syslog(LOG_ERR,"Open: %s",strerror(errno));
                goto mutex_unlock;
            }
            ret = -EINTR;
            while(ret == -EINTR)
            {
                ret = write(file_descriptor,&c,1);
                if(ret == -1)
                {
                    if(errno != EINTR)
                    {
                        //printf("Write len %d\n",write_len);
                        syslog(LOG_ERR,"Write: %s",strerror(errno));
                        goto close_file_descriptor;
                    }
                }
            }
            if(close(file_descriptor))
            {
                syslog(LOG_ERR,"Close: %s",strerror(errno));
                goto close_file_descriptor;
            }
            if(pthread_mutex_unlock(&socket_state.mutex))
            {
                syslog(LOG_ERR,"Mutex_unlock %s",strerror(errno));
                goto mutex_unlock;
            }

        }
        else if(num_bytes_read == 0)
        {
            //buf has to be freed
            //socket_file_descriptor needs to be freed.
            //completion boolean has to be set
            goto free_socket_fd;
        }
    }
    close_file_descriptor: close(file_descriptor);
    mutex_unlock: pthread_mutex_unlock(&socket_state.mutex);
    free_socket_fd: close(thread_params->socket_file_descriptor);
                    thread_params->thread_completed = true;
                    syslog(LOG_DEBUG,"Closed connection from %s",thread_params->ip_addr);
                    return 0;
}

/*
*   Writing the timestamp back to file is done from the main thread if there are no open connections,
*   otherwise it is done from the threads itself. This is to avoid skipping timestamps if the 
*   traffic is busy. 
*/
int main(int argc,char **argv)
{
    initialize_socket_state();
    bool run_as_daemon = false;
    main_thread_states_t main_thread_state;
    openlog(NULL,0,LOG_USER);
    int opt;
    while((opt = getopt(argc, argv,"d")) != -1)
    {
        switch(opt)
        {
            case 'd':
                run_as_daemon = true;
                break;
        }
    }
    int status=0,yes=1;
    struct addrinfo hints;
    struct addrinfo *p = NULL;  // will point to the results
    char ip_addr_str[INET6_ADDRSTRLEN];
    memset(ip_addr_str,0,sizeof(s));
    struct sockaddr_storage client_addr;
    socklen_t addr_size = sizeof(client_addr);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
    //TODO: Call freeaddrinfo() once done with servinfo
    /*
    *   Get info regarding the peer at Port 9000
    */
    status = getaddrinfo(NULL, "9000", &hints, &socket_state.host_addr_info);
    if(status != 0)
    {
        syslog(LOG_ERR,"getaddrinfo: %s",strerror(errno));
        return -1;
    }
    socket_state.free_address_info = true;
    /*
    *   Try to bind to one of the socket descriptors returned by getaddrinfo
    */
    for(p = socket_state.host_addr_info; p != NULL; p = p->ai_next) 
    {
        socket_state.socket_descriptor = socket(p->ai_family, p->ai_socktype,p->ai_protocol);
        if(socket_state.socket_descriptor == -1)
        {
            syslog(LOG_ERR,"Socket: %s",strerror(errno));
            continue;
        }
        socket_state.free_socket_descriptor = true;
        status = setsockopt(socket_state.socket_descriptor,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        if(status == -1)
        {
            syslog(LOG_ERR,"Set Socket Options: %s",strerror(errno)); 
            perform_cleanup();
            return -1;
        }
        status = bind(socket_state.socket_descriptor,p->ai_addr, p->ai_addrlen);
        if(status == -1)
        {
            syslog(LOG_ERR,"Bind: %s",strerror(errno));
            close(socket_state.socket_descriptor);
            continue;
        }
        break;
    }
    if(p == NULL)
    {
        syslog(LOG_ERR,"server: failed to bind");
        fprintf(stderr, "server: failed to bind\n");
        perform_cleanup();
        return -1;
    }
    
    
    freeaddrinfo(socket_state.host_addr_info);
    socket_state.free_address_info = false;
    /*
    *   if opt command line was specified, then run this program as a daemon
    */
    if(run_as_daemon)
    {
        pid_t pid;
        /* create new process */
        pid = fork ();
        if (pid == -1)
        {
            syslog(LOG_ERR,"Fork: %s",strerror(errno));
            perform_cleanup();
            return -1;
        }
        else if (pid != 0)
        {
            perform_cleanup();
            exit (EXIT_SUCCESS);
        }
        else
        {
            if(setsid()==-1)
            {
                syslog(LOG_ERR,"SetSid: %s",strerror(errno));
                perform_cleanup();
                return -1;
            }
            if(chdir("/")==-1)
            {
                syslog(LOG_ERR,"Chdir: %s",strerror(errno));;
                perform_cleanup();
                return -1;
            }
            /* redirect fd's 0,1,2 to /dev/null */
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            open ("/dev/null", O_RDWR); /* stdin */
            dup (0); /* stdout */
            dup (0); /* stderror */
        }
    }
    int backlog = 10;
    printf("Listening for connections...\n");
    status = listen(socket_state.socket_descriptor,backlog);
    if(status == -1)
    {
        syslog(LOG_ERR,"Listen: %s",strerror(errno));
        perform_cleanup();
        return -1;
    }
    /*
    *   Setup signal handler
    */
    //TODO: Setup Signal Alarm 
    //Set up the signals handler
    
    if(setup_signal(SIGINT)== -1)
    {
        perform_cleanup();
        return -1;
    }
    if(setup_signal(SIGTERM)==-1)
    {
        perform_cleanup();
        return -1;        
    }

    
    head_t head;
    TAILQ_INIT(&head);
    //Use inside thread
    /*
    *   Simple statemachine implemented to handle socket reading loop.
    *
    *   Accept_Connections -  accept new connections. Open socket file descriptor. If same IP as before, open /var/aesdsocketdata in append mode, else in truncate mode.
    *   Receive_From_Socket - Read data from socket. If no data received, close connection. Else send it for parsing.
    *   Parse_Data  -   Parse the data received character by character to check for '\n', if newline found, dump data until that character and echo back across socket. 
    */
    if(socket_state.signal_caught)
    {
        main_thread_state = Join_threads;
    }
    else
    {
        main_thread_state = Accept_Connections;
    }
    
    int socket_fd;
    while(1)
    {
        switch(main_thread_state)
        {
            case Accept_Connections:
                
                socket_fd = accept(socket_state.socket_descriptor,(struct sockaddr*)&client_addr,&addr_size);
                if(socket_fd == -1)
                {   
                    if(errno == EINTR)
                    {
                        printf("EINTR\n");
                        goto next_state;
                    }
                    syslog(LOG_ERR,"Accept: %s",strerror(errno));
                    goto next_state;
                }                    
                inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), ip_addr_str, sizeof(ip_addr_str));
                //compare
                worker_thread_t *node = NULL;
                node = malloc(sizeof(worker_thread_t));
                if(!node)
                {
                    syslog(LOG_ERR,"Malloc failed");
                    goto next_state;
                }
                node->thread_completed = false;
                node->curr_state = Receive_From_Socket;
                node->socket_file_descriptor = socket_fd;
                strcpy(node->ip_addr,s);
                status = pthread_create(&node->thread_id,
                             (void*)0,
                             server_thread,
                             node);

                if(status !=0)
                {
                    syslog(LOG_ERR,"Listen: %s",strerror(errno));
                    free(node);
                    goto next_state;
                }   
                TAILQ_INSERT_TAIL(&head, node, entries);
                socket_state.connection_count++;
                node = NULL;
                goto next_state;
                next_state:
                    main_thread_state = Join_threads;
                    break;
            case Join_threads:
                if(socket_state.connection_count>0)
                {
                    worker_thread_t *var = NULL;
                    worker_thread_t *tvar = NULL;
                    TAILQ_FOREACH_SAFE(var,&head,entries,tvar)
                    {
                        if(var->thread_completed)
                        {
                            pthread_join(var->thread_id,NULL);
                            TAILQ_REMOVE(&head, var, entries);
                            free(var);
                            var = NULL;
                            socket_state.connection_count--;
                        }
                    }
                }
                if(socket_state.signal_caught)
                {
                    if(socket_state.connection_count==0)
                    {
                        shutdown_function();
                    }
                    else
                    {
                        break;
                    }
                }
                main_thread_state = Accept_Connections;
                break;
        }    
    }
}