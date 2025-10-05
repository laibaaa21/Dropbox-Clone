#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <pthread.h>
#include <stdbool.h>

typedef enum
{
    TASK_TEST, // placeholder for now
} task_type_t;

typedef struct Task
{
    task_type_t type;
    int client_fd;     // socket of the client who requested it
    char payload[256]; // small message or filename etc.
} Task;

typedef struct TaskQueue
{
    Task *tasks;
    int capacity;
    int head;
    int tail;
    int size;
    bool shutdown;
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} TaskQueue;

int task_queue_init(TaskQueue *q, int capacity);
void task_queue_destroy(TaskQueue *q);
int task_queue_push(TaskQueue *q, Task *t);
int task_queue_pop(TaskQueue *q, Task *out);
void task_queue_signal_shutdown(TaskQueue *q);

#endif
