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
#include "lib.h"

#define OK 0
#define DEFAULT_BUFFER_SIZE 10
#define PF_MAX_CHARS 100
#define LATE_SECS 10

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
        print_result = snprintf(op_str, sizeof(op_str), "2LATE");
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
    while (true)
    {
        pthread_mutex_lock(&balance_mut);
        bal = balance;
        pthread_mutex_unlock(&balance_mut);

        if (bal > 0)
        {
            msg = buffer[buffer_front_index];
            buffer_front_index++;
            if (buffer_front_index == buffer_size)
                buffer_front_index -= buffer_size;
            pthread_mutex_lock(&balance_mut);
            balance--;
            pthread_mutex_unlock(&balance_mut);

            char private_fifo_name[PF_MAX_CHARS];
            if (snprintf(private_fifo_name, sizeof(private_fifo_name), "/tmp/%d.%lu", msg.pid, msg.tid) < 0)
            {
                perror("snprintf failed");
                printf("CONSUMER_EXIT_1 -> errno: %d \n", errno);
                return NULL;
            }
            int pnp;
            while ((pnp = open(private_fifo_name, O_WRONLY | O_NONBLOCK)) < 0)
            {
                if (errno != EWOULDBLOCK && errno != ENXIO && errno != ENOENT)
                {
                    perror("cannot open private fifo");
                }

                if (errno == ENOENT)
                {
                    break;
                }
            }
            msg.pid = getpid();
            msg.tid = pthread_self();
            int write_no = 0;
            while (pnp != -1 && (write_no = write(pnp, &msg, sizeof(Message))) <= 0)
            {
                if (write_no == -1 && errno != EAGAIN)
                {
                    perror("cannot write to private fifo");
                }
            }
            close(pnp);

            if (write_no == 0 || write_no == -1)
            {
                operation_register(FAILD, msg);
                //return NULL;
            }
            else if (msg.tskres == -1)
            {
                operation_register(TLATE, msg);
            }
            else
            {
                operation_register(TSKDN, msg);
            }
        }
    }
    return NULL;
}

void *producer(void *arg)
{
    unsigned bal;
    unsigned write_index;
    Message message = *(Message *)arg;
    free(arg);
    if ((time(NULL) - start_time) <= nsecs)
    {
        message.tskres = task(message.tskload);
        Message tskres_msg = message;
        tskres_msg.pid = getpid();
        tskres_msg.tid = pthread_self();
        operation_register(TSKEX, tskres_msg);
    }
    else
    {
        message.tskres = -1;
    }

    while (true)
    {
        pthread_mutex_lock(&balance_mut);
        bal = balance;
        if (bal < buffer_size)
        {
            pthread_mutex_lock(&backindex_mut);
            write_index = buffer_back_index;
            if (buffer_back_index < buffer_size - 1)
                buffer_back_index++;
            else
                buffer_back_index = 0;
            pthread_mutex_unlock(&backindex_mut);

            buffer[write_index] = message;
            balance++;
            pthread_mutex_unlock(&balance_mut);
            break;
        }
        pthread_mutex_unlock(&balance_mut);
    }

    return NULL;
}

int main(int argc, char *argv[], char *envp[])
{
    start_time = time(NULL);
    char *fifo_name;
    if (argc != 4 && argc != 6)
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

        fifo_name = argv[5];
    }
    else
    {
        fifo_name = argv[3];
        buffer_size = DEFAULT_BUFFER_SIZE;
    }

    buffer = (Message *)malloc(buffer_size * sizeof(Message));

    if (mkfifo(fifo_name, 0666) != OK)
    {
        error(EXIT_FAILURE, errno, "cannot create public fifo");
    }

    while ((np = open(fifo_name, O_RDONLY | O_NONBLOCK)) < 0 && (time(NULL) - start_time) <= nsecs)
    {
        if (errno != ENOENT && errno != ENXIO && errno != EWOULDBLOCK)
        {
            error(EXIT_FAILURE, errno, "cannot open public fifo");
        }
    }

    pthread_t tid;
    int errn;
    Message *arg;

    while ((errn = pthread_create(&tid, NULL, consumer, NULL)) != OK) // create single consumer thread
    {
        error(0, errn, "cannot create a consumer thread");
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
        if ((read_no == 0 || read_no == -1) && (time(NULL) - start_time) > nsecs)
        {
            break;
        }
        // received message, create producer thread
        operation_register(RECVD, msg);
        *arg = msg;

        // create producer thread
        while ((errn = pthread_create(&tid, NULL, producer, arg) != OK))
        {
            error(0, errn, "cannot create a producer thread");
        }
    }

    unlink(fifo_name);

    do
    {
        arg = (Message *)malloc(sizeof(Message));
        read_no = read(np, &msg, sizeof(Message));

        if (read_no > 0)
        {
            operation_register(RECVD, msg);
            *arg = msg;
            // create producer thread
            while ((errn = pthread_create(&tid, NULL, producer, arg) != OK))
            {
                error(0, errn, "cannot create a producer thread");
            }
        }
    } while (((read_no != 0 && read_no != -1) || (read_no == -1 && errno == EAGAIN)));

    sleep(1);
    //Finish
    close(np);
    free(buffer);
    pthread_mutex_destroy(&balance_mut);
    pthread_mutex_destroy(&backindex_mut);
    return EXIT_SUCCESS;
}
