#include "client_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int client_queue_init(ClientQueue *q, int capacity)
{
    if (!q || capacity <= 0)
        return -1;
    q->fds = calloc(capacity, sizeof(int));
    if (!q->fds)
        return -1;
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->size = 0;
    q->shutdown = false;
    if (pthread_mutex_init(&q->mtx, NULL) != 0)
    {
        free(q->fds);
        return -1;
    }
    if (pthread_cond_init(&q->not_empty, NULL) != 0)
    {
        pthread_mutex_destroy(&q->mtx);
        free(q->fds);
        return -1;
    }
    if (pthread_cond_init(&q->not_full, NULL) != 0)
    {
        pthread_cond_destroy(&q->not_empty);
        pthread_mutex_destroy(&q->mtx);
        free(q->fds);
        return -1;
    }
    return 0;
}

void client_queue_destroy(ClientQueue *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->mtx);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mtx);

    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->mtx);
    free(q->fds);
    q->fds = NULL;
}

int client_queue_push(ClientQueue *q, int fd)
{
    if (!q)
        return -1;
    int err = 0;
    pthread_mutex_lock(&q->mtx);
    while (q->size == q->capacity && !q->shutdown)
    {
        // wait until not full
        pthread_cond_wait(&q->not_full, &q->mtx);
    }
    if (q->shutdown)
    {
        err = -1;
    }
    else
    {
        q->fds[q->tail] = fd;
        q->tail = (q->tail + 1) % q->capacity;
        q->size++;
        pthread_cond_signal(&q->not_empty);
        err = 0;
    }
    pthread_mutex_unlock(&q->mtx);
    return err;
}

int client_queue_pop(ClientQueue *q)
{
    if (!q)
        return -1;
    int fd = -1;
    pthread_mutex_lock(&q->mtx);
    while (q->size == 0 && !q->shutdown)
    {
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }
    if (q->size == 0 && q->shutdown)
    {
        // shutdown signaled and nothing to pop
        fd = -1;
    }
    else
    {
        fd = q->fds[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->size--;
        pthread_cond_signal(&q->not_full);
    }
    pthread_mutex_unlock(&q->mtx);
    return fd;
}

void client_queue_signal_shutdown(ClientQueue *q)
{
    if (!q)
        return;
    pthread_mutex_lock(&q->mtx);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
}
