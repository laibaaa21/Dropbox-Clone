#include "response_queue.h"
#include <string.h>
#include <stdlib.h>

int response_init(Response *resp)
{
    if (!resp)
        return -1;

    resp->status = RESPONSE_SUCCESS;
    memset(resp->message, 0, sizeof(resp->message));
    resp->data = NULL;
    resp->data_size = 0;
    resp->ready = false;

    if (pthread_mutex_init(&resp->mtx, NULL) != 0)
        return -1;

    if (pthread_cond_init(&resp->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&resp->mtx);
        return -1;
    }

    return 0;
}

void response_destroy(Response *resp)
{
    if (!resp)
        return;

    pthread_mutex_lock(&resp->mtx);
    if (resp->data)
    {
        free(resp->data);
        resp->data = NULL;
    }
    pthread_mutex_unlock(&resp->mtx);

    pthread_mutex_destroy(&resp->mtx);
    pthread_cond_destroy(&resp->cv);
}

void response_set(Response *resp, response_status_t status, const char *message,
                  void *data, size_t data_size)
{
    if (!resp)
        return;

    pthread_mutex_lock(&resp->mtx);

    resp->status = status;
    if (message)
    {
        strncpy(resp->message, message, sizeof(resp->message) - 1);
        resp->message[sizeof(resp->message) - 1] = '\0';
    }

    resp->data = data;
    resp->data_size = data_size;
    resp->ready = true;

    pthread_cond_signal(&resp->cv);
    pthread_mutex_unlock(&resp->mtx);
}

int response_wait(Response *resp)
{
    if (!resp)
        return -1;

    pthread_mutex_lock(&resp->mtx);
    while (!resp->ready)
    {
        pthread_cond_wait(&resp->cv, &resp->mtx);
    }
    pthread_mutex_unlock(&resp->mtx);

    return 0;
}
