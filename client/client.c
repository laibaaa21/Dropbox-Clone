#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define BUFFER_SIZE 4096

/* Simple test client for Dropbox Clone server */

int connect_to_server(const char *host, const char *port)
{
    struct addrinfo hints, *res, *rp;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int s = getaddrinfo(host, port, &hints, &res);
    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        close(sockfd);
    }

    freeaddrinfo(res);

    if (rp == NULL)
    {
        fprintf(stderr, "Could not connect\n");
        return -1;
    }

    return sockfd;
}

void send_upload_command(int sockfd, const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        perror("fopen");
        return;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Send UPLOAD command */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "UPLOAD %s %ld\n", filename, filesize);
    send(sockfd, cmd, strlen(cmd), 0);

    /* Send file data */
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        send(sockfd, buf, n, 0);
    }

    fclose(fp);

    /* Receive response */
    char response[BUFFER_SIZE];
    ssize_t bytes = recv(sockfd, response, sizeof(response) - 1, 0);
    if (bytes > 0)
    {
        response[bytes] = '\0';
        printf("Server response:\n%s\n", response);
    }
}

void send_download_command(int sockfd, const char *filename)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", filename);
    send(sockfd, cmd, strlen(cmd), 0);

    /* Receive file data */
    char buf[BUFFER_SIZE];
    FILE *fp = fopen(filename, "wb");
    if (!fp)
    {
        perror("fopen download");
        return;
    }

    ssize_t bytes;
    while ((bytes = recv(sockfd, buf, sizeof(buf), 0)) > 0)
    {
        fwrite(buf, 1, bytes, fp);
    }

    fclose(fp);
    printf("Downloaded: %s\n", filename);
}

void send_delete_command(int sockfd, const char *filename)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", filename);
    send(sockfd, cmd, strlen(cmd), 0);

    char response[BUFFER_SIZE];
    ssize_t bytes = recv(sockfd, response, sizeof(response) - 1, 0);
    if (bytes > 0)
    {
        response[bytes] = '\0';
        printf("Server response: %s\n", response);
    }
}

void send_list_command(int sockfd)
{
    char cmd[] = "LIST\n";
    send(sockfd, cmd, strlen(cmd), 0);

    char response[BUFFER_SIZE];
    ssize_t bytes = recv(sockfd, response, sizeof(response) - 1, 0);
    if (bytes > 0)
    {
        response[bytes] = '\0';
        printf("Server response:\n%s\n", response);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <host> <port> [command] [args...]\n", argv[0]);
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  upload <filename>\n");
        fprintf(stderr, "  download <filename>\n");
        fprintf(stderr, "  delete <filename>\n");
        fprintf(stderr, "  list\n");
        return 1;
    }

    const char *host = argv[1];
    const char *port = argv[2];

    int sockfd = connect_to_server(host, port);
    if (sockfd < 0)
    {
        return 1;
    }

    /* Read welcome message */
    char welcome[BUFFER_SIZE];
    ssize_t bytes = recv(sockfd, welcome, sizeof(welcome) - 1, 0);
    if (bytes > 0)
    {
        welcome[bytes] = '\0';
        printf("%s\n", welcome);
    }

    /* Execute command */
    if (argc >= 4)
    {
        const char *command = argv[3];

        if (strcmp(command, "upload") == 0 && argc >= 5)
        {
            send_upload_command(sockfd, argv[4]);
        }
        else if (strcmp(command, "download") == 0 && argc >= 5)
        {
            send_download_command(sockfd, argv[4]);
        }
        else if (strcmp(command, "delete") == 0 && argc >= 5)
        {
            send_delete_command(sockfd, argv[4]);
        }
        else if (strcmp(command, "list") == 0)
        {
            send_list_command(sockfd);
        }
        else
        {
            fprintf(stderr, "Unknown command: %s\n", command);
        }
    }

    close(sockfd);
    return 0;
}
