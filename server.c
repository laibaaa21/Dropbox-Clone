#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>

#include "queue.h"
#include "taskqueue.h"

#define DEFAULT_PORT "12345"
#define DEFAULT_QUEUE_CAPACITY 64
#define LISTEN_BACKLOG 128
#define CLIENT_THREAD_COUNT 4
#define WORKER_THREAD_COUNT 4
#define TASK_QUEUE_CAPACITY 128

static volatile sig_atomic_t keep_running = 1;
static int listen_fd = -1;

static ClientQueue client_queue;
static TaskQueue task_queue;

static pthread_t client_threads[CLIENT_THREAD_COUNT];
static pthread_t worker_threads[WORKER_THREAD_COUNT];

static void int_handler(int signo)
{
    (void)signo;
    keep_running = 0;
    if (listen_fd >= 0)
        close(listen_fd);
    client_queue_signal_shutdown(&client_queue);
    task_queue_signal_shutdown(&task_queue);
}

/* Worker thread: handles only LIST, DOWNLOAD, DELETE */
void *worker_worker(void *arg)
{
    (void)arg;
    Task task;

    while (task_queue_pop(&task_queue, &task) == 0)
    {
        printf("[Worker %lu] Processing %d task for %s\n",
               (unsigned long)pthread_self(), task.type, task.filename);

        char path[512];
        mkdir("storage", 0777);
        mkdir("storage/user1", 0777);

        switch (task.type)
        {
        case TASK_DOWNLOAD:
        {
            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            FILE *fp = fopen(path, "rb");
            if (!fp)
            {
                send(task.client_fd, "DOWNLOAD FAILED\n", 16, 0);
                close(task.client_fd);
                break;
            }

            char buf[1024];
            size_t nbytes;
            while ((nbytes = fread(buf, 1, sizeof(buf), fp)) > 0)
                send(task.client_fd, buf, nbytes, 0);

            fclose(fp);
            send(task.client_fd, "\nDOWNLOAD OK\n", 13, 0);
            close(task.client_fd);
            break;
        }

        case TASK_DELETE:
        {
            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            if (remove(path) == 0)
                send(task.client_fd, "DELETE OK\n", 10, 0);
            else
                send(task.client_fd, "DELETE FAILED\n", 14, 0);
            close(task.client_fd);
            break;
        }

        case TASK_LIST:
        {
            snprintf(path, sizeof(path), "storage/%s", task.username);
            DIR *dir = opendir(path);
            if (!dir)
            {
                send(task.client_fd, "LIST FAILED\n", 12, 0);
                close(task.client_fd);
                break;
            }

            struct dirent *entry;
            char line[512];
            while ((entry = readdir(dir)) != NULL)
            {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                snprintf(line, sizeof(line), "%s\n", entry->d_name);
                send(task.client_fd, line, strlen(line), 0);
            }
            closedir(dir);
            send(task.client_fd, "LIST END\n", 9, 0);
            close(task.client_fd);
            break;
        }

        default:
            close(task.client_fd);
            break;
        }
    }

    printf("[Worker %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}

/* Client thread: handles UPLOAD immediately, queues others */
void *client_worker(void *arg)
{
    (void)arg;

    while (1)
    {
        int cfd = client_queue_pop(&client_queue);
        if (cfd < 0)
            break;
        char cmd[512];

        const char *msg =
            "Welcome to Dropbox Clone Server :))\n"
            "Commands:\n"
            "UPLOAD <filename> <size>\n"
            "DOWNLOAD <filename>\n"
            "DELETE <filename>\n"
            "LIST\n";

        ssize_t peek_bytes = recv(cfd, cmd, sizeof(cmd) - 1, MSG_PEEK);
        if (peek_bytes > 0)
        {
            cmd[peek_bytes] = '\0';
            if (strncmp(cmd, "DOWNLOAD", 8) != 0)
                send(cfd, msg, strlen(msg), 0);
        }

        // reads the actual command
        ssize_t n = recv(cfd, cmd, sizeof(cmd) - 1, 0);
        if (n <= 0)
        {
            close(cfd);
            continue;
        }
        cmd[n] = '\0';
        printf("[ClientThread] Received: %s\n", cmd);

        Task t;
        memset(&t, 0, sizeof(t));
        strcpy(t.username, "user1");
        t.client_fd = cfd;

        /* Handle UPLOAD immediately */
        if (sscanf(cmd, "UPLOAD %255s %zu", t.filename, &t.filesize) == 2)
        {
            mkdir("storage", 0777);
            mkdir("storage/user1", 0777);

            char dest[512];
            snprintf(dest, sizeof(dest), "storage/user1/%s", t.filename);
            FILE *fp = fopen(dest, "wb");
            if (!fp)
            {
                send(cfd, "UPLOAD FAILED\n", 14, 0);
                close(cfd);
                continue;
            }

            printf("[ClientThread] Receiving %zu bytes for %s\n", t.filesize, t.filename);

            /*Find leftover bytes (file data that came with command) */
            char *newline = strchr(cmd, '\n');
            size_t received = 0;
            if (newline && *(newline + 1) != '\0')
            {
                char *extra = newline + 1;
                size_t extra_len = strlen(extra);
                fwrite(extra, 1, extra_len, fp);
                received += extra_len;
            }

            /*Continue reading remaining bytes */
            char buf[1024];
            ssize_t bytes;
            while (received < t.filesize && (bytes = recv(cfd, buf, sizeof(buf), 0)) > 0)
            {
                fwrite(buf, 1, bytes, fp);
                received += bytes;
            }

            fclose(fp);

            printf("[ClientThread] Upload done: %s (%zu bytes)\n", t.filename, received);
            send(cfd, "UPLOAD OK\n", 10, 0);
            close(cfd);
            continue;
        }

        /* Queue other commands for workers */
        else if (sscanf(cmd, "DOWNLOAD %255s", t.filename) == 1)
            t.type = TASK_DOWNLOAD;
        else if (sscanf(cmd, "DELETE %255s", t.filename) == 1)
            t.type = TASK_DELETE;
        else if (strncmp(cmd, "LIST", 4) == 0)
            t.type = TASK_LIST;
        else
        {
            send(cfd, "Invalid command\n", 16, 0);
            close(cfd);
            continue;
        }

        if (task_queue_push(&task_queue, &t) != 0)
        {
            fprintf(stderr, "Task queue full. Closing fd=%d\n", cfd);
            close(cfd);
        }
    }

    printf("[ClientThread %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}

/* Main setup */
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

    if (client_queue_init(&client_queue, queue_capacity) != 0 ||
        task_queue_init(&task_queue, TASK_QUEUE_CAPACITY) != 0)
    {
        fprintf(stderr, "Queue initialization failed\n");
        return 1;
    }

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

    struct sigaction sa;
    sa.sa_handler = int_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    printf("Server listening on port %s\n", port);

    for (int i = 0; i < WORKER_THREAD_COUNT; i++)
        pthread_create(&worker_threads[i], NULL, worker_worker, NULL);
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
        pthread_create(&client_threads[i], NULL, client_worker, NULL);

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

    client_queue_signal_shutdown(&client_queue);
    task_queue_signal_shutdown(&task_queue);
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
        pthread_join(client_threads[i], NULL);
    for (int i = 0; i < WORKER_THREAD_COUNT; i++)
        pthread_join(worker_threads[i], NULL);

    client_queue_destroy(&client_queue);
    task_queue_destroy(&task_queue);
    return 0;
}
