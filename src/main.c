#include "server.h"
#include "threads/client_thread.h"
#include "threads/worker_thread.h"
#include "queue/client_queue.h"
#include "queue/task_queue.h"
#include "auth/user_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <signal.h>

/* -------------------- Global Variable Definitions -------------------- */
volatile sig_atomic_t keep_running = 1;
int listen_fd = -1;

ClientQueue client_queue;
TaskQueue task_queue;

pthread_t client_threads[CLIENT_THREAD_COUNT];
pthread_t worker_threads[WORKER_THREAD_COUNT];

/* -------------------- Signal Handler -------------------- */
void int_handler(int signo)
{
    (void)signo;
    keep_running = 0;
    if (listen_fd >= 0)
        close(listen_fd);
    client_queue_signal_shutdown(&client_queue);
    task_queue_signal_shutdown(&task_queue);
}

/* -------------------- Main Function -------------------- */
int main(int argc, char *argv[])
{
    const char *port = DEFAULT_PORT;
    int queue_capacity = DEFAULT_QUEUE_CAPACITY;

    if (argc >= 2)
        port = argv[1];
    if (argc >= 3)
        queue_capacity = atoi(argv[2]);
    if (queue_capacity <= 0)
        queue_capacity = DEFAULT_QUEUE_CAPACITY;

    /* Initialize queues */
    if (client_queue_init(&client_queue, queue_capacity) != 0 ||
        task_queue_init(&task_queue, TASK_QUEUE_CAPACITY) != 0)
    {
        fprintf(stderr, "Queue initialization failed\n");
        return 1;
    }

    /* Initialize user database */
    if (user_database_init(&global_user_db, 256) != 0)
    {
        fprintf(stderr, "User database initialization failed\n");
        return 1;
    }
    printf("User database initialized\n");

    /* Setup server socket */
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int s = getaddrinfo(NULL, port, &hints, &res);
    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return 1;
    }

    for (rp = res; rp; rp = rp->ai_next)
    {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1)
            continue;
        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(listen_fd);
    }

    freeaddrinfo(res);
    if (rp == NULL)
    {
        perror("bind");
        return 1;
    }

    listen(listen_fd, LISTEN_BACKLOG);

    /* Setup signal handler */
    struct sigaction sa;
    sa.sa_handler = int_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    printf("Server listening on port %s\n", port);

    /* Create thread pools */
    for (int i = 0; i < WORKER_THREAD_COUNT; i++)
        pthread_create(&worker_threads[i], NULL, worker_worker, NULL);
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
        pthread_create(&client_threads[i], NULL, client_worker, NULL);

    /* Accept loop */
    while (keep_running)
    {
        struct sockaddr_storage cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cfd < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        if (client_queue_push(&client_queue, cfd) != 0)
        {
            fprintf(stderr, "Client queue full\n");
            close(cfd);
        }
    }

    /* Shutdown sequence */
    client_queue_signal_shutdown(&client_queue);
    task_queue_signal_shutdown(&task_queue);
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
        pthread_join(client_threads[i], NULL);
    for (int i = 0; i < WORKER_THREAD_COUNT; i++)
        pthread_join(worker_threads[i], NULL);

    client_queue_destroy(&client_queue);
    task_queue_destroy(&task_queue);
    user_database_destroy(&global_user_db);
    return 0;
}
