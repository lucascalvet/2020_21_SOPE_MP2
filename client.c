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
#include "common.h"
#include <math.h>

#define OK 0

enum Operation
{
    IWANT,
    RECVD,
    TSKEX,
    TSKDN,
    GOTRS,
    TLATE,
    CLOSD,
    GAVUP,
    FAILD
};

void print_usage()
{
    printf("\n"
           "Usage: c <-t nsecs> <fifoname>\n"
           "\n"
           "nsecs: nº (aproximado) de segundos que o programa deve funcionar\n" //ja se traduz
           "fifoname: - nome (absoluto ou relativo) do canal público de comunicação com nome (FIFO) por onde o Cliente envia pedidos ao Servidor\n");
    exit(EXIT_SUCCESS);
}

void operation_register(enum Operation op, Message msg)
{
    char op_str[6];
    switch (op)
    {
    case IWANT:
        strcpy(op_str, "IWANT");
        break;
    case RECVD:
        strcpy(op_str, "RECVD");
        break;
    case TSKEX:
        strcpy(op_str, "TSKEX");
        break;
    case TSKDN:
        strcpy(op_str, "TSKDN");
        break;
    case GOTRS:
        strcpy(op_str, "GOTRS");
        break;
    case TLATE:
        strcpy(op_str, "2LATE");
        break;
    case CLOSD:
        strcpy(op_str, "CLOSD");
        break;
    case GAVUP:
        strcpy(op_str, "GAVUP");
        break;
    case FAILD:
        strcpy(op_str, "FAILD");
        break;
    default:
        break;
    }

    printf("%ld ; %d ; %d ; %d ; %lu ; %d ; %s\n", time(NULL), msg.rid, msg.tskload, msg.pid, msg.tid, msg.tskres, op_str);
}

void *make_request(void *arg)
{
    int pnp;
    int np = ((int *)arg)[1];
    Message msg;
    msg.rid = ((int *)arg)[0]; // we need to pass current_rid
    msg.tid = pthread_self();
    msg.pid = getpid();
    msg.tskload = 1 + (rand() % 9);
    msg.tskres = -1;

    //char *private_fifo_name = (char *)malloc((7 + floor(log10((double) msg.pid)) + 1 + floor(log10((double) msg.tid)) + 1) * sizeof(char));
    char *private_fifo_name = (char *)malloc(40 * sizeof(char));
    sprintf(private_fifo_name, "/tmp/%d.%lu", msg.pid, msg.tid);
    mkfifo(private_fifo_name, 0666);

    operation_register(IWANT, msg);
    write(np, &msg, sizeof(Message)); //we need to pass np

    while ((pnp = open(private_fifo_name, O_RDONLY)) < 0)
        ;
    read(pnp, &msg, sizeof(Message));
    if (msg.tskres == -1)
    {
        operation_register(CLOSD, msg);
    }
    else
    {
        operation_register(GOTRS, msg);
    }
    close(pnp);
    unlink(private_fifo_name);
    free(private_fifo_name);
    return NULL;
}

int main(int argc, char *argv[], char *envp[])
{ //argc inclui nome do programa vai ter sempre de ser 4
    if (argc != 4)
    {
        error(EXIT_FAILURE, EINVAL, "incorrect number of arguments");
    }
    if (strcmp(argv[1], "-t"))
    {
        error(EXIT_FAILURE, EINVAL, "incorrect argument");
    }

    int nsecs = atoi(argv[2]);
    if (nsecs <= 0)
    {
        error(EXIT_FAILURE, EINVAL, "invalid time");
    }

    char *fifo_name = argv[3];
    int np;

    while ((np = open(fifo_name, O_WRONLY)) < 0)
        ;

    pthread_t tid;
    int current_rid = 0;
    int arg[2] = {current_rid++, np};

    int errn;
    if ((errn = pthread_create(&tid, NULL, make_request, (void *)arg)) != OK)
    {
        error(EXIT_FAILURE, errn, "cannot create a new thread");
    }

    sleep(5);
}
