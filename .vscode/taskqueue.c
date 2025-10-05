#include "taskqueue.h"
#include <stdlib.h>
#include <string.h>

int task_queue_init(TaskQueue *q, int capacity)
{
    if (!q || capacity <= 0)
        return -1;
    q->tasks = calloc(capacity, sizeof(Task));
    if (!q->tasks)
        return -1;
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->size = 0;
    q->shutdown = false;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return 0;
}

void task_queue_destroy(TaskQueue *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->mtx);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->tasks);
}

int task_queue_push(TaskQueue *q, Task *t)
{
    if (!q || !t)
        return -1;
    pthread_mutex_lock(&q->mtx);
    while (q->size == q->capacity && !q->shutdown)
        pthread_cond_wait(&q->not_full, &q->mtx);
    if (q->shutdown)
    {
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }
    q->tasks[q->tail] = *t;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

int task_queue_pop(TaskQueue *q, Task *out)
{
    if (!q || !out)
        return -1;
    pthread_mutex_lock(&q->mtx);
    while (q->size == 0 && !q->shutdown)
        pthread_cond_wait(&q->not_empty, &q->mtx);
    if (q->size == 0 && q->shutdown)
    {
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }
    *out = q->tasks[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
    return 0;
}

void task_queue_signal_shutdown(TaskQueue *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->mtx);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
}
