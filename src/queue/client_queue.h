#ifndef CLIENT_QUEUE_H
#define CLIENT_QUEUE_H

#include <pthread.h>
#include <stdbool.h>

typedef struct ClientQueue
{
    int *fds;     // circular buffer of file descriptors
    int capacity; // max number of entries
    int head;     // index of next pop
    int tail;     // index of next push
    int size;     // current number of elements
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool shutdown; // set to true to wake all waiting threads during shutdown
} ClientQueue;

// Initialize queue; returns 0 on success, -1 on error
int client_queue_init(ClientQueue *q, int capacity);

// Destroy queue resources
void client_queue_destroy(ClientQueue *q);

// Push an fd into the queue (blocks if full). Returns 0 on success, -1 if shutdown.
int client_queue_push(ClientQueue *q, int fd);

// Pop an fd from the queue (blocks if empty). On success returns fd >= 0.
// If shutdown and empty, returns -1.
int client_queue_pop(ClientQueue *q);

// Signal shutdown to wake any waiting producers/consumers
void client_queue_signal_shutdown(ClientQueue *q);

#endif /* CLIENT_QUEUE_H */
