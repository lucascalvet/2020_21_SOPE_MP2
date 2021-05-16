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
#define DEFAULT_BUFFER_SIZE 10
#define PF_MAX_CHARS 40

enum Operation
{
    RECVD,
    TSKEX,
    TSKDN,
    TLATE, //2LATE
    FAILD
};

static pthread_mutex_t balance_mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t backindex_mut = PTHREAD_MUTEX_INITIALIZER;
static int np;
static time_t start_time;
static time_t nsecs;
static int buffer_size;
static Message *buffer;
static unsigned buffer_front_index = 0;
static unsigned buffer_back_index = 0;
static unsigned balance = 0;

void print_usage()
{
    printf("Usage: s <-t nsecs> [-l bufsz] <fifoname>\n"
           "nsecs - the number of seconds the program shall run for (approximately)\n"
           "bufsz - size of the buffer (warehouse) that holds the results form the requests\n"
           "fifoname - the name of the public comunication channel (FIFO) used by the client to send requests to the server\n\n");
}

void operation_register(enum Operation op, Message msg)
{
    char op_str[6];
    int print_result = 0;
    switch (op)
    {
    case RECVD:
        print_result = snprintf(op_str, sizeof(op_str), "RECVD");
        break;
    case TSKEX:
        print_result = snprintf(op_str, sizeof(op_str), "TSKEX");
        break;
    case TSKDN:
        print_result = snprintf(op_str, sizeof(op_str), "TSKDN");
        break;
    case TLATE:
        print_result = snprintf(op_str, sizeof(op_str), "TLATE");
        break;
    case FAILD:
        print_result = snprintf(op_str, sizeof(op_str), "FAILD");
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

void *consumer(void *arg)
{
    unsigned bal;
    Message msg;
    Message msg_send;
    while ((time(NULL) - start_time) <= nsecs)
    {

        phtread_mutex_lock(&balance_mut);
        bal = balance;
        phtread_mutex_unlock(&balance_mut);
        if (balance > 0)
        {
            msg = buffer[buffer_front_index];
            buffer_front_index++;
            if (buffer_front_index == buffer_size)
                buffer_front_index -= buffer_size;

            char private_fifo_name[PF_MAX_CHARS];
            if (snprintf(private_fifo_name, sizeof(private_fifo_name), "/tmp/%d.%lu", msg.pid, msg.tid) < 0)
            {
                perror("snprintf failed");
                return NULL;
            }

            while ((pnp = open(private_fifo_name, O_WRONLY | O_NONBLOCK)) < 0 && (time(NULL) - start_time) <= nsecs)
            {
                if (errno != EWOULDBLOCK)
                {
                    //if (!server_closed)
                    perror("cannot open private fifo");
                    return NULL;
                }
            }
            msg.pid = getpid();
            msg.tid = pthread_self();
            int read_no;
            while ((read_no = write(pnp, &msg, sizeof(Message))) <= 0 && (time(NULL) - start_time) <= nsecs)
            {
                if (read_no == -1 && errno != EAGAIN)
                {
                    close(pnp);
                    //if (!server_closed)
                    perror("cannot read private fifo");
                    return NULL;
                }
            }
            phtread_mutex_lock(&balance_mut);
            balance--;
            phtread_mutex_unlock(&balance_mut);
        }
    }
}

void *producer(void *arg)
{
    unsigned bal;
    unsigned write_index;
    Message message = *(Message *)arg;

    message.tskres = task(message.tskload);
    while ((time(NULL) - start_time) <= nsecs)
    {
        phtread_mutex_lock(&balance_mut);
        bal = balance;
        phtread_mutex_unlock(&balance_mut);

        if (bal < buffer_size)
        {
            phtread_mutex_lock(&backindex_mut);
            write_index = buffer_back_index;
            if (buffer_back_index < buffer_size - 1)
                buffer_back_index++;
            else
                buffer_back_index = 0;
            phtread_mutex_unlock(&backindex_mut);

            buffer[write_index] = message;
            phtread_mutex_lock(&balance_mut);
            balance++;
            phtread_mutex_unlock(&balance_mut);
        }
    }
}

int main(int argc, char *argv[], char *envp[])
{
    start_time = time(NULL);
    if (argc != 4 && argv != 6)
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

    if (argc == 6)
    {
        if (strcmp(argv[3], "-l"))
        {
            print_usage();
            error(EXIT_FAILURE, EINVAL, "incorrect argument");
        }

        buffer_size = atoi(argv[4]);
        if (buffer_size <= 0)
        {
            print_usage();
            error(EXIT_FAILURE, EINVAL, "invalid time");
        }

        char *fifo_name = argv[5];
    }
    else
    {
        char *fifo_name = argv[3];
        buffer_size = DEFAULT_BUFFER_SIZE;
    }

    buffer = (Message *)malloc(buffer_size * sizeof(Message));

    if (mkfifo(fifo_name, 0666) != OK)
    {
        perror("cannot create public fifo");
        return NULL;
    }

    while ((np = open(fifo_name, O_RDONLY | O_NONBLOCK)) < 0 && (time(NULL) - start_time) <= nsecs)
    {
        if (errno != ENOENT && errno != ENXIO && errno != EWOULDBLOCK)
        {
            error(EXIT_FAILURE, errno, "cannot open public fifo");
        }
    }

    pthread_t tid;
    int current_rid = 0;
    int errn;
    Message *arg;

    if ((errn = pthread_create(&tid, NULL, consumer, NULL)) != OK) // create single consumer thread
    {
        error(EXIT_FAILURE, errn, "cannot create a consumer thread");
    }

    Message msg;
    int read_no;
    while ((time(NULL) - start_time) <= nsecs)
    {
        arg = (Message *)malloc(sizeof(Message));
        //Read
        while ((read_no = read(np, &msg, sizeof(Message))) <= 0 && (time(NULL) - start_time) <= nsecs) //block while not timed out
        {
            if (read_no == -1 && errno != EAGAIN)
            {
                close(np);
                perror("cannot read public fifo");
                //Close
            }
        }
        // received message, create producer thread
        *arg = msg;
        if ((errn = pthread_create(&tid, NULL, producer, arg)) != OK) // create producer thread
        {
            error(EXIT_FAILURE, errn, "cannot create a producer thread");
        }
    }

    //Finish
    pthread_mutex_destroy(&balance_mut);
    pthread_mutex_destroy(&backindex_mut);
    close(np);
}
