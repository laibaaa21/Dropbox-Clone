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
#include <fcntl.h>
#include "queue.h"

#define DEFAULT_PORT "12345"
#define DEFAULT_QUEUE_CAPACITY 64
#define LISTEN_BACKLOG 128

static volatile sig_atomic_t keep_running = 1;
static ClientQueue client_queue;
static int listen_fd = -1;

static void int_handler(int signo)
{
    (void)signo;
    keep_running = 0;
    // Close listen fd to break accept()
    if (listen_fd >= 0)
        close(listen_fd);
    client_queue_signal_shutdown(&client_queue);
}

static int set_socket_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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
            break; // success

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

    set_socket_nonblocking(listen_fd); // shutdown via close

    struct sigaction sa;
    sa.sa_handler = int_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Server listening on port %s (queue capacity=%d)\n", port, queue_capacity);

    while (keep_running)
    {
        struct sockaddr_storage cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cfd < 0)
        {
            if (errno == EINTR)
            {
                // interrupted by signal, check keep_running
                continue;
            }
            perror("accept");
            break;
        }

        char host[NI_MAXHOST], serv[NI_MAXSERV];
        if (getnameinfo((struct sockaddr *)&cli_addr, cli_len, host, sizeof(host), serv, sizeof(serv),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        {
            printf("Accepted connection from %s:%s -> fd=%d\n", host, serv, cfd);
        }
        else
        {
            printf("Accepted connection -> fd=%d\n", cfd);
        }

        // Push into client queue (blocks if full)
        if (client_queue_push(&client_queue, cfd) != 0)
        {
            // queue is shutting down or error
            fprintf(stderr, "Queue shutdown or error while pushing fd=%d, closing socket\n", cfd);
            close(cfd);
            break;
        }
        else
        {
            printf("Enqueued fd=%d (queue size approx=%d)\n", cfd, client_queue.size);
        }
    }

    printf("Server shutting down...\n");
    if (listen_fd >= 0)
        close(listen_fd);
    client_queue_signal_shutdown(&client_queue);
    client_queue_destroy(&client_queue);
    return 0;
}