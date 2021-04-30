#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "./common.h"

#define OK 0
#define PF_MAX_CHARS 40

enum Operation
{
    IWANT,
    GOTRS,
    CLOSD,
    GAVUP
};

static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static int np;
static time_t start_time;
static time_t nsecs;
static sigset_t signal_mask;
static bool server_closed;

void print_usage()
{
    printf("Usage: c <-t nsecs> <fifoname>\n"
           "nsecs - the number of seconds the program shall run for (approximately)\n"
           "fifoname - the name of the public comunication channel (FIFO) used by the client to send requests to the server\n\n");
}

void operation_register(enum Operation op, Message msg)
{
    char op_str[6];
    int print_result = 0;
    switch (op)
    {
    case IWANT:
        print_result = snprintf(op_str, sizeof(op_str), "IWANT");
        break;
    case GOTRS:
        print_result = snprintf(op_str, sizeof(op_str), "GOTRS");
        break;
    case CLOSD:
        print_result = snprintf(op_str, sizeof(op_str), "CLOSD");
        break;
    case GAVUP:
        print_result = snprintf(op_str, sizeof(op_str), "GAVUP");
        break;
    default:
        break;
    }

    if (print_result < 0)
    {
        error(EXIT_FAILURE, ENOTTY, "snprintf failed");
    }

    if (printf("%ld ; %d ; %d ; %d ; %lu ; %d ; %s\n", time(NULL), msg.rid, msg.tskload, msg.pid, msg.tid, msg.tskres, op_str) < 0)
    {
        error(EXIT_FAILURE, ENOTTY, "printf failed");
    }
}

void *make_request(void *arg)
{
    Message msg;
    msg.rid = *(int *)arg;
    free((int *)arg);
    int pnp;
    pthread_t tid = pthread_self();
    pid_t pid = getpid();
    msg.tid = tid;
    msg.pid = pid;
    unsigned seed = tid;
    msg.tskload = 1 + (rand_r(&seed) % 9);
    msg.tskres = -1;

    pthread_mutex_lock(&mut);
    if (write(np, &msg, sizeof(Message)) < 0)
    {
        printf("Closing Server\n");
        server_closed = true;
        pthread_mutex_unlock(&mut);
        return NULL;
    }
    pthread_mutex_unlock(&mut);

    if (server_closed)
    {
        printf("Server Closed\n");
        return NULL;
    }

    operation_register(IWANT, msg);

    char private_fifo_name[PF_MAX_CHARS];
    if (snprintf(private_fifo_name, sizeof(private_fifo_name), "/tmp/%d.%lu", msg.pid, msg.tid) < 0)
    {
        perror("snprintf failed");
        return NULL;
    }
    if (mkfifo(private_fifo_name, 0666) != OK)
    {
        perror("cannot create private fifo");
        return NULL;
    }

    while ((pnp = open(private_fifo_name, O_RDONLY | O_NONBLOCK)) < 0 && (time(NULL) - start_time) <= nsecs)
    {
        if (errno != EWOULDBLOCK)
        {
            unlink(private_fifo_name);
            if (!server_closed)
                perror("cannot open private fifo");
            return NULL;
        }
    }

    int read_no;
    while ((read_no = read(pnp, &msg, sizeof(Message))) <= 0 && (time(NULL) - start_time) <= nsecs)
    {
        if (read_no == -1 && errno != EAGAIN)
        {
            close(pnp);
            unlink(private_fifo_name);
            if (!server_closed)
                perror("cannot read private fifo");
            return NULL;
        }
    }

    if ((read_no == 0 || read_no == -1) && (time(NULL) - start_time) > nsecs)
    {
        operation_register(GAVUP, msg);
        close(pnp);
        unlink(private_fifo_name);
        return NULL;
    }

    msg.pid = pid;
    msg.tid = tid;

    
    if (msg.tskres == -1)
    {
        server_closed = true;
        operation_register(CLOSD, msg);
    }
    else
    {
        operation_register(GOTRS, msg);
    }


    close(pnp);
    unlink(private_fifo_name);
    return NULL;
}

int main(int argc, char *argv[], char *envp[])
{
    start_time = time(NULL);
    if (argc != 4)
    {
        print_usage();
        error(EXIT_FAILURE, EINVAL, "incorrect number of arguments");
    }
    if (strcmp(argv[1], "-t"))
    {
        print_usage();
        error(EXIT_FAILURE, EINVAL, "incorrect argument");
    }

    nsecs = atoi(argv[2]);
    if (nsecs <= 0)
    {
        print_usage();
        error(EXIT_FAILURE, EINVAL, "invalid time");
    }

    char *fifo_name = argv[3];

    while ((np = open(fifo_name, O_WRONLY | O_NONBLOCK)) < 0 && (time(NULL) - start_time) <= nsecs)
    {
        if (errno != ENOENT && errno != ENXIO && errno != EWOULDBLOCK)
        {
            error(EXIT_FAILURE, errno, "cannot open public fifo");
        }
    }

    server_closed = false;

    pthread_t tid;
    int current_rid = 0;
    int errn;
    int *arg;
    unsigned seed = time(NULL);

    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &signal_mask, NULL) != OK)
    {
        error(EXIT_FAILURE, errno, "pthread_sigmask failed");
    }

    while ((time(NULL) - start_time) <= nsecs && !server_closed)
    {
        if(server_closed){
            printf("SC\n");
        }
        else{
            printf("SO\n");
        }
        arg = (int *)malloc(sizeof(int));
        if (arg == NULL)
        {
            error(EXIT_FAILURE, errno, "cannot allocate memory");
        }
        *arg = current_rid++;
        if ((errn = pthread_create(&tid, NULL, make_request, arg)) != OK)
        {
            error(EXIT_FAILURE, errn, "cannot create a new thread");
        }
        usleep(1000 * (rand_r(&seed) % 50));
    }
    sleep(1);
    pthread_mutex_destroy(&mut);
    close(np);
}
