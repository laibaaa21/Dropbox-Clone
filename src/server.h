#ifndef SERVER_H
#define SERVER_H

#include <signal.h>
#include <pthread.h>
#include "queue/client_queue.h"
#include "queue/task_queue.h"
#include "session/session_manager.h"

/* -------------------- Configuration Constants -------------------- */
#define DEFAULT_PORT "10985"
#define DEFAULT_QUEUE_CAPACITY 64
#define LISTEN_BACKLOG 128
#define CLIENT_THREAD_COUNT 4
#define WORKER_THREAD_COUNT 4
#define TASK_QUEUE_CAPACITY 128

/* -------------------- Global Variables -------------------- */
extern volatile sig_atomic_t keep_running;
extern int listen_fd;

extern ClientQueue client_queue;
extern TaskQueue task_queue;
extern SessionManager session_manager;  /* Global session manager (Phase 2.1) */

extern pthread_t client_threads[CLIENT_THREAD_COUNT];
extern pthread_t worker_threads[WORKER_THREAD_COUNT];

/* -------------------- Signal Handler -------------------- */
void int_handler(int signo);

#endif /* SERVER_H */
