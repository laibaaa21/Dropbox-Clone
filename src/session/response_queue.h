#ifndef RESPONSE_QUEUE_H
#define RESPONSE_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/* Response status codes */
typedef enum
{
    RESPONSE_SUCCESS = 0,
    RESPONSE_ERROR = -1,
    RESPONSE_FILE_NOT_FOUND = -2,
    RESPONSE_QUOTA_EXCEEDED = -3,
    RESPONSE_PERMISSION_DENIED = -4
} response_status_t;

/* Response structure for worker->client communication */
typedef struct Response
{
    response_status_t status;
    char message[512];     // Error message or info
    void *data;            // Optional data (for download)
    size_t data_size;      // Size of data
    bool ready;            // Result is ready
    pthread_mutex_t mtx;
    pthread_cond_t cv;     // Client waits on this
} Response;

/* Initialize a response structure */
int response_init(Response *resp);

/* Destroy a response structure */
void response_destroy(Response *resp);

/* Worker fills response and signals client */
void response_set(Response *resp, response_status_t status, const char *message,
                  void *data, size_t data_size);

/* Client waits for response (blocks until ready) */
int response_wait(Response *resp);

#endif /* RESPONSE_QUEUE_H */
