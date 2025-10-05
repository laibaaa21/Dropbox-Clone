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
#include "queue.h"

#define DEFAULT_PORT "12345"
#define DEFAULT_QUEUE_CAPACITY 64
#define LISTEN_BACKLOG 128
#define CLIENT_THREAD_COUNT 4

static volatile sig_atomic_t keep_running = 1;
static ClientQueue client_queue;
static int listen_fd = -1;
static pthread_t client_threads[CLIENT_THREAD_COUNT];

/*shutdown on Ctrl+C */
static void int_handler(int signo)
{
    (void)signo;
    keep_running = 0;
    if (listen_fd >= 0)
        close(listen_fd);
    client_queue_signal_shutdown(&client_queue);
}

/* Client Threadpool Worker*/
void *client_worker(void *arg)
{
    (void)arg;
    while (1)
    {
        int cfd = client_queue_pop(&client_queue);
        if (cfd < 0)
        {
            // Queue shutting down
            break;
        }

        // client info
        struct sockaddr_storage addr;
        socklen_t len = sizeof(addr);
        getpeername(cfd, (struct sockaddr *)&addr, &len);
        char host[NI_MAXHOST], serv[NI_MAXSERV];
        getnameinfo((struct sockaddr *)&addr, len, host, sizeof(host), serv, sizeof(serv),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        printf("[Thread %lu] Handling client %s:%s (fd=%d)\n",
               pthread_self(), host, serv, cfd);

        const char *msg = "Welcome to Dropbox Clone Server :))\n";
        send(cfd, msg, strlen(msg), 0);

        close(cfd);
    }
    printf("[Thread %lu] Exiting...\n", pthread_self());
    return NULL;
}

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

    if (client_queue_init(&client_queue, queue_capacity) != 0)
    {
        fprintf(stderr, "Failed to initialize client queue\n");
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
        client_queue_destroy(&client_queue);
        return 1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1)
            continue;

        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(listen_fd);
        listen_fd = -1;
    }

    if (rp == NULL)
    {
        fprintf(stderr, "Could not bind to port %s\n", port);
        freeaddrinfo(res);
        client_queue_destroy(&client_queue);
        return 1;
    }
    freeaddrinfo(res);

    if (listen(listen_fd, LISTEN_BACKLOG) != 0)
    {
        perror("listen");
        close(listen_fd);
        client_queue_destroy(&client_queue);
        return 1;
    }

    // install signal handler
    struct sigaction sa;
    sa.sa_handler = int_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Server listening on port %s (queue capacity=%d)\n",
           port, queue_capacity);

    // Create client worker threads
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
    {
        pthread_create(&client_threads[i], NULL, client_worker, NULL);
    }

    // Accept loop
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

        char host[NI_MAXHOST], serv[NI_MAXSERV];
        if (getnameinfo((struct sockaddr *)&cli_addr, cli_len,
                        host, sizeof(host), serv, sizeof(serv),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            printf("Accepted connection from %s:%s -> fd=%d\n", host, serv, cfd);
        else
            printf("Accepted connection -> fd=%d\n", cfd);

        if (client_queue_push(&client_queue, cfd) != 0)
        {
            fprintf(stderr, "Queue shutdown or error closing fd=%d\n", cfd);
            close(cfd);
            break;
        }
    }

    printf("Server shutting down...\n");
    if (listen_fd >= 0)
        close(listen_fd);
    client_queue_signal_shutdown(&client_queue);

    // Wait for client threads to exit
    for (int i = 0; i < CLIENT_THREAD_COUNT; i++)
    {
        pthread_join(client_threads[i], NULL);
    }

    client_queue_destroy(&client_queue);
    return 0;
}
