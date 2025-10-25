#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <stdbool.h>

#define BUFFER_SIZE 8192
#define CMD_BUFFER_SIZE 512

/* Dropbox Clone Client - Interactive client with authentication support */

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
        fprintf(stderr, "Could not connect to %s:%s\n", host, port);
        return -1;
    }

    return sockfd;
}

ssize_t recv_response(int sockfd, char *buffer, size_t bufsize)
{
    ssize_t bytes = recv(sockfd, buffer, bufsize - 1, 0);
    if (bytes > 0)
    {
        buffer[bytes] = '\0';
    }
    return bytes;
}

bool authenticate(int sockfd)
{
    char command[CMD_BUFFER_SIZE];
    char username[64];
    char password[256];
    char choice[10];
    char response[BUFFER_SIZE];

    /* Read welcome message */
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    if (bytes > 0)
    {
        printf("%s", response);
    }

    while (1)
    {
        printf("\nChoose an option:\n");
        printf("1. SIGNUP (Create new account)\n");
        printf("2. LOGIN (Existing account)\n");
        printf("3. QUIT\n");
        printf("Enter choice: ");
        fflush(stdout);

        if (fgets(choice, sizeof(choice), stdin) == NULL)
            return false;

        choice[strcspn(choice, "\n")] = 0; /* Remove newline */

        if (strcmp(choice, "3") == 0)
        {
            return false;
        }
        else if (strcmp(choice, "1") == 0 || strcmp(choice, "2") == 0)
        {
            printf("Username: ");
            fflush(stdout);
            if (fgets(username, sizeof(username), stdin) == NULL)
                return false;
            username[strcspn(username, "\n")] = 0;

            printf("Password: ");
            fflush(stdout);
            if (fgets(password, sizeof(password), stdin) == NULL)
                return false;
            password[strcspn(password, "\n")] = 0;

            /* Send authentication command */
            if (strcmp(choice, "1") == 0)
            {
                snprintf(command, sizeof(command), "SIGNUP %s %s\n", username, password);
            }
            else
            {
                snprintf(command, sizeof(command), "LOGIN %s %s\n", username, password);
            }

            send(sockfd, command, strlen(command), 0);

            /* Receive response */
            bytes = recv_response(sockfd, response, sizeof(response));
            if (bytes > 0)
            {
                printf("%s", response);

                /* Check if authentication succeeded */
                if (strstr(response, "SIGNUP OK") || strstr(response, "LOGIN OK"))
                {
                    /* Read the file menu message */
                    bytes = recv_response(sockfd, response, sizeof(response));
                    if (bytes > 0)
                    {
                        printf("%s", response);
                    }
                    return true;
                }
            }
            else
            {
                fprintf(stderr, "Connection lost\n");
                return false;
            }
        }
        else
        {
            printf("Invalid choice. Please enter 1, 2, or 3.\n");
        }
    }

    return false;
}

void handle_upload(int sockfd, const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Extract basename from path (just the filename without directory) */
    const char *basename = strrchr(filename, '/');
    if (basename)
        basename++; /* Skip the '/' */
    else
        basename = filename; /* No path, just filename */

    printf("Uploading '%s' (%ld bytes)...\n", basename, filesize);

    /* Send UPLOAD command with size (use basename only) */
    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "UPLOAD %s %ld\n", basename, filesize);
    send(sockfd, cmd, strlen(cmd), 0);

    /* Send file data in chunks */
    char buf[4096];
    size_t total_sent = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        ssize_t sent = send(sockfd, buf, n, 0);
        if (sent < 0)
        {
            fprintf(stderr, "Error sending file data: %s\n", strerror(errno));
            fclose(fp);
            return;
        }
        total_sent += sent;
    }

    fclose(fp);
    printf("Sent %zu bytes\n", total_sent);

    /* Receive response */
    char response[BUFFER_SIZE];
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    if (bytes > 0)
    {
        printf("Server: %s", response);
    }
}

void handle_download(int sockfd, const char *filename)
{
    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", filename);
    send(sockfd, cmd, strlen(cmd), 0);

    printf("Downloading '%s'...\n", filename);

    /* Receive file data and response */
    char buf[BUFFER_SIZE];
    FILE *fp = fopen(filename, "wb");
    if (!fp)
    {
        fprintf(stderr, "Error: Cannot create file '%s': %s\n", filename, strerror(errno));
        /* Still need to drain the response */
        ssize_t bytes;
        while ((bytes = recv(sockfd, buf, sizeof(buf), 0)) > 0)
        {
            if (strstr(buf, "DOWNLOAD") || strstr(buf, "ERROR"))
                break;
        }
        return;
    }

    ssize_t bytes;
    size_t total_received = 0;
    bool found_end = false;

    while ((bytes = recv(sockfd, buf, sizeof(buf), 0)) > 0)
    {
        /* Look for end marker */
        char *end_marker = strstr(buf, "\nDOWNLOAD OK");
        if (!end_marker)
            end_marker = strstr(buf, "DOWNLOAD FAILED");
        if (!end_marker)
            end_marker = strstr(buf, "ERROR");

        if (end_marker)
        {
            /* Write data before the marker */
            size_t data_len = end_marker - buf;
            if (data_len > 0)
            {
                fwrite(buf, 1, data_len, fp);
                total_received += data_len;
            }
            printf("Server: %s", end_marker);
            found_end = true;
            break;
        }
        else
        {
            /* Write all data */
            fwrite(buf, 1, bytes, fp);
            total_received += bytes;
        }
    }

    fclose(fp);

    if (found_end && total_received > 0)
    {
        printf("Downloaded %zu bytes to '%s'\n", total_received, filename);
    }
    else if (!found_end)
    {
        fprintf(stderr, "Download may be incomplete (connection closed)\n");
    }
}

void handle_delete(int sockfd, const char *filename)
{
    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", filename);
    send(sockfd, cmd, strlen(cmd), 0);

    char response[BUFFER_SIZE];
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    if (bytes > 0)
    {
        printf("Server: %s", response);
    }
}

void handle_list(int sockfd)
{
    char cmd[] = "LIST\n";
    send(sockfd, cmd, strlen(cmd), 0);

    char response[BUFFER_SIZE];
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    if (bytes > 0)
    {
        printf("Files in your storage:\n");
        printf("%s", response);
    }
}

void interactive_session(int sockfd)
{
    char line[CMD_BUFFER_SIZE];
    char command[64];
    char arg1[256];

    printf("\n=== Interactive Session Started ===\n");
    printf("Type 'help' for available commands\n\n");

    while (1)
    {
        printf("dbc> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            break;
        }

        /* Remove trailing newline */
        line[strcspn(line, "\n")] = 0;

        /* Skip empty lines */
        if (strlen(line) == 0)
            continue;

        /* Parse command */
        command[0] = '\0';
        arg1[0] = '\0';
        sscanf(line, "%63s %255s", command, arg1);

        /* Handle commands */
        if (strcmp(command, "help") == 0)
        {
            printf("\nAvailable commands:\n");
            printf("  upload <filename>   - Upload a file to server\n");
            printf("  download <filename> - Download a file from server\n");
            printf("  delete <filename>   - Delete a file from server\n");
            printf("  list                - List all your files\n");
            printf("  quit                - Exit the client\n");
            printf("  help                - Show this help message\n\n");
        }
        else if (strcmp(command, "upload") == 0)
        {
            if (strlen(arg1) == 0)
            {
                printf("Usage: upload <filename>\n");
            }
            else
            {
                handle_upload(sockfd, arg1);
            }
        }
        else if (strcmp(command, "download") == 0)
        {
            if (strlen(arg1) == 0)
            {
                printf("Usage: download <filename>\n");
            }
            else
            {
                handle_download(sockfd, arg1);
            }
        }
        else if (strcmp(command, "delete") == 0)
        {
            if (strlen(arg1) == 0)
            {
                printf("Usage: delete <filename>\n");
            }
            else
            {
                handle_delete(sockfd, arg1);
            }
        }
        else if (strcmp(command, "list") == 0)
        {
            handle_list(sockfd);
        }
        else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0)
        {
            printf("Sending QUIT command...\n");
            send(sockfd, "QUIT\n", 5, 0);
            break;
        }
        else
        {
            printf("Unknown command: '%s'. Type 'help' for available commands.\n", command);
        }
    }

    printf("\n=== Session Ended ===\n");
}

void print_usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <host> <port>\n", progname);
    fprintf(stderr, "\nDropbox Clone Client - Interactive Mode\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s localhost 10985\n\n", progname);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *port = argv[2];

    printf("Connecting to %s:%s...\n", host, port);

    int sockfd = connect_to_server(host, port);
    if (sockfd < 0)
    {
        return 1;
    }

    printf("Connected successfully!\n\n");

    /* Authenticate first */
    if (!authenticate(sockfd))
    {
        printf("Authentication failed or cancelled. Goodbye!\n");
        close(sockfd);
        return 1;
    }

    printf("\n=== Authentication Successful! ===\n");

    /* Start interactive session */
    interactive_session(sockfd);

    close(sockfd);
    return 0;
}
