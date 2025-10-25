#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -------------------- Task Types -------------------- */
typedef enum
{
    TASK_UPLOAD,
    TASK_DOWNLOAD,
    TASK_DELETE,
    TASK_LIST
} task_type_t;

/* -------------------- Task Definition -------------------- */
typedef struct Task
{
    task_type_t type;
    uint64_t session_id; // session ID for result delivery (Phase 2.1)
    char username[64];   // username (authenticated user)
    char filename[256];  // file name for upload/download/delete
    char temp_path[512]; // optional temp path for upload
    size_t filesize;     // file size for upload/download
    void *data_buffer;   // buffer for upload data (for UPLOAD tasks)
} Task;

/* -------------------- Queue Struct -------------------- */
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

/* -------------------- Function Prototypes -------------------- */
int task_queue_init(TaskQueue *q, int capacity);
void task_queue_destroy(TaskQueue *q);
int task_queue_push(TaskQueue *q, Task *t);
int task_queue_pop(TaskQueue *q, Task *out);
void task_queue_signal_shutdown(TaskQueue *q);

#endif
