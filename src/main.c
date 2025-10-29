#include "server.h"
#include "threads/client_thread.h"
#include "threads/worker_thread.h"
#include "queue/client_queue.h"
#include "queue/task_queue.h"
#include "auth/user_metadata.h"
#include "sync/file_locks.h"
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
SessionManager session_manager;  /* Global session manager (Phase 2.1) */

pthread_t client_threads[CLIENT_THREAD_COUNT];
pthread_t worker_threads[WORKER_THREAD_COUNT];

/* -------------------- Signal Handler (Phase 2.7) -------------------- */
void int_handler(int signo)
{
    (void)signo;

    /* Prevent re-entry */
    static volatile sig_atomic_t handler_called = 0;
    if (handler_called)
        return;
    handler_called = 1;

    printf("\n[Signal] Received SIGINT, initiating graceful shutdown...\n");

    /* Step 1: Stop accepting new connections */
    keep_running = 0;
    if (listen_fd >= 0)
    {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
        listen_fd = -1;
    }

    /* Step 2: Signal queues to stop accepting new items */
    client_queue_signal_shutdown(&client_queue);
    task_queue_signal_shutdown(&task_queue);

    printf("[Signal] Shutdown signal sent to all queues\n");
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

    /* Initialize session manager (Phase 2.1) */
    if (session_manager_init(&session_manager) != 0)
    {
        fprintf(stderr, "Session manager initialization failed\n");
        client_queue_destroy(&client_queue);
        task_queue_destroy(&task_queue);
        return 1;
    }

    /* Initialize user metadata system with SQLite database */
    if (user_metadata_init("storage/stash.db") != 0)
    {
        fprintf(stderr, "User metadata initialization failed\n");
        session_manager_destroy(&session_manager);
        client_queue_destroy(&client_queue);
        task_queue_destroy(&task_queue);
        return 1;
    }
    printf("User metadata system initialized\n");

    /* Initialize file lock manager (Phase 2.5) */
    if (file_lock_manager_init(&global_file_lock_manager, MAX_FILE_LOCKS) != 0)
    {
        fprintf(stderr, "File lock manager initialization failed\n");
        user_metadata_cleanup();
        session_manager_destroy(&session_manager);
        client_queue_destroy(&client_queue);
        task_queue_destroy(&task_queue);
        return 1;
    }
    printf("File lock manager initialized\n");

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
        fprintf(stderr, "[Main] Failed to bind to port %s\n", port);
        perror("bind");
        file_lock_manager_destroy(&global_file_lock_manager);
        user_metadata_cleanup();
        session_manager_destroy(&session_manager);
        client_queue_destroy(&client_queue);
        task_queue_destroy(&task_queue);
        return 1;
    }

    if (listen(listen_fd, LISTEN_BACKLOG) != 0)
    {
        fprintf(stderr, "[Main] Failed to listen on socket\n");
        perror("listen");
        close(listen_fd);
        file_lock_manager_destroy(&global_file_lock_manager);
        user_metadata_cleanup();
        session_manager_destroy(&session_manager);
        client_queue_destroy(&client_queue);
        task_queue_destroy(&task_queue);
        return 1;
    }

    /* Setup signal handlers (Phase 2.7) */
    struct sigaction sa;
    sa.sa_handler = int_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        perror("[Main] Failed to setup SIGINT handler");
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0)
    {
        perror("[Main] Failed to setup SIGTERM handler");
    }

    /* Ignore SIGPIPE (write to closed socket) - handle errors instead */
    signal(SIGPIPE, SIG_IGN);

    printf("Server listening on port %s\n", port);

    /* Create thread pools */
    printf("[Main] Creating worker thread pool (%d threads)...\n", WORKER_THREAD_COUNT);
    for (int i = 0; i < WORKER_THREAD_COUNT; i++)
    {
        int rc = pthread_create(&worker_threads[i], NULL, worker_worker, NULL);
        if (rc != 0)
        {
            fprintf(stderr, "[Main] Failed to create worker thread %d: %s\n", i, strerror(rc));
            /* Continue with fewer threads rather than failing completely */
        }
    }

    printf("[Main] Creating client thread pool (%d threads)...\n", CLIENT_THREAD_COUNT);
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
    {
        int rc = pthread_create(&client_threads[i], NULL, client_worker, NULL);
        if (rc != 0)
        {
            fprintf(stderr, "[Main] Failed to create client thread %d: %s\n", i, strerror(rc));
            /* Continue with fewer threads rather than failing completely */
        }
    }

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
            fprintf(stderr, "[Main] Client queue full, rejecting connection\n");
            const char *reject_msg = "ERROR: Server busy, please try again later\n";
            send(cfd, reject_msg, strlen(reject_msg), 0);
            close(cfd);
        }
    }

    /* -------------------- Shutdown Sequence (Phase 2.7) -------------------- */
    printf("\n[Main] ========================================\n");
    printf("[Main] GRACEFUL SHUTDOWN INITIATED\n");
    printf("[Main] ========================================\n");

    /* Ensure queues are signaled (may have been done by signal handler) */
    client_queue_signal_shutdown(&client_queue);
    task_queue_signal_shutdown(&task_queue);

    /* Wait for client threads to finish processing their current clients */
    printf("[Main] Step 1: Waiting for client threads to finish...\n");
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
    {
        void *retval;
        int rc = pthread_join(client_threads[i], &retval);
        if (rc == 0)
            printf("[Main]   Client thread %d joined successfully\n", i);
        else
            fprintf(stderr, "[Main]   Error joining client thread %d: %d\n", i, rc);
    }
    printf("[Main] All client threads terminated\n");

    /* Wait for worker threads to finish processing their current tasks */
    printf("[Main] Step 2: Waiting for worker threads to finish...\n");
    for (int i = 0; i < WORKER_THREAD_COUNT; i++)
    {
        void *retval;
        int rc = pthread_join(worker_threads[i], &retval);
        if (rc == 0)
            printf("[Main]   Worker thread %d joined successfully\n", i);
        else
            fprintf(stderr, "[Main]   Error joining worker thread %d: %d\n", i, rc);
    }
    printf("[Main] All worker threads terminated\n");

    /* Clean up resources in reverse order of initialization */
    printf("[Main] Step 3: Cleaning up resources...\n");

    printf("[Main]   Destroying file lock manager...\n");
    file_lock_manager_destroy(&global_file_lock_manager);

    /* Print final statistics (Phase 2.9) - BEFORE destroying session manager */
    uint64_t total_created, peak_count;
    session_get_statistics(&session_manager, NULL, &total_created, &peak_count);
    printf("[Main] Session statistics: %lu total created, %lu peak concurrent\n",
           total_created, peak_count);

    printf("[Main]   Destroying session manager...\n");
    session_manager_destroy(&session_manager);

    printf("[Main]   Destroying client queue...\n");
    client_queue_destroy(&client_queue);

    printf("[Main]   Destroying task queue...\n");
    task_queue_destroy(&task_queue);

    printf("[Main]   Cleaning up user metadata system...\n");
    user_metadata_cleanup();

    printf("[Main] ========================================\n");
    printf("[Main] SERVER SHUTDOWN COMPLETE\n");
    printf("[Main] ========================================\n");
    return 0;
}
