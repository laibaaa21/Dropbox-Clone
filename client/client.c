#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <stdbool.h>
#include "client_ui.h"

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

bool authenticate(int sockfd, char *authenticated_username, size_t username_bufsize)
{
    char command[CMD_BUFFER_SIZE];
    char username[64];
    char password[256];
    char response[BUFFER_SIZE];

    /* Read welcome message (silently) */
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    (void)bytes;

    while (1)
    {
        int choice = ui_show_auth_menu();

        if (choice == 3 || choice < 0)
        {
            return false;
        }
        else if (choice == 1 || choice == 2)
        {
            if (!ui_prompt_username(username, sizeof(username)))
                return false;

            if (!ui_prompt_password(password, sizeof(password)))
                return false;

            /* Send authentication command */
            if (choice == 1)
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
                /* Check if authentication succeeded */
                bool success = (strstr(response, "SIGNUP OK") != NULL ||
                               strstr(response, "LOGIN OK") != NULL);

                ui_show_auth_result(success, response);

                if (success)
                {
                    /* Read the file menu message (silently) */
                    recv_response(sockfd, response, sizeof(response));

                    /* Store username for display */
                    if (authenticated_username && username_bufsize > 0)
                    {
                        strncpy(authenticated_username, username, username_bufsize - 1);
                        authenticated_username[username_bufsize - 1] = '\0';
                    }

                    return true;
                }
            }
            else
            {
                ui_show_error("Connection lost");
                return false;
            }
        }
    }

    return false;
}

void handle_upload(int sockfd, const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        ui_show_error("Cannot open file '%s': %s", filename, strerror(errno));
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

    ui_show_upload_start(basename, (size_t)filesize);

    /* Send UPLOAD command with size (use basename only) */
    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "UPLOAD %s %ld\n", basename, filesize);
    send(sockfd, cmd, strlen(cmd), 0);

    /* Send file data in chunks */
    char buf[4096];
    size_t total_sent = 0;
    size_t n;

    /* Initial progress display */
    ui_show_upload_progress(0, (size_t)filesize);

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        ssize_t sent = send(sockfd, buf, n, 0);
        if (sent < 0)
        {
            ui_show_error("Error sending file data: %s", strerror(errno));
            fclose(fp);
            return;
        }
        total_sent += sent;

        /* Update progress every 4KB */
        ui_show_upload_progress(total_sent, (size_t)filesize);
    }

    fclose(fp);

    /* Receive response */
    char response[BUFFER_SIZE];
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    bool success = false;
    if (bytes > 0)
    {
        success = (strstr(response, "UPLOAD OK") != NULL);
    }

    ui_show_upload_result(success, response, total_sent);
}

void handle_download(int sockfd, const char *filename)
{
    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", filename);
    send(sockfd, cmd, strlen(cmd), 0);

    ui_show_download_start(filename);

    /* Receive file data and response */
    char buf[BUFFER_SIZE];
    FILE *fp = fopen(filename, "wb");
    if (!fp)
    {
        ui_show_error("Cannot create file '%s': %s", filename, strerror(errno));
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
    char *server_message = NULL;

    /* Initial progress display */
    ui_show_download_progress(0, 0);

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
            server_message = end_marker;
            found_end = true;
            break;
        }
        else
        {
            /* Write all data */
            fwrite(buf, 1, bytes, fp);
            total_received += bytes;

            /* Update progress */
            ui_show_download_progress(total_received, 0);
        }
    }

    fclose(fp);

    bool success = (found_end && total_received > 0 &&
                   server_message && strstr(server_message, "DOWNLOAD OK"));

    if (!found_end)
    {
        ui_show_download_result(false, "Connection closed unexpectedly", total_received);
    }
    else
    {
        ui_show_download_result(success, server_message ? server_message : "", total_received);
    }
}

void handle_delete(int sockfd, const char *filename)
{
    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", filename);
    send(sockfd, cmd, strlen(cmd), 0);

    char response[BUFFER_SIZE];
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    bool success = false;
    if (bytes > 0)
    {
        success = (strstr(response, "DELETE OK") != NULL);
    }

    ui_show_delete_result(success, filename, response);
}

void handle_list(int sockfd)
{
    char cmd[] = "LIST\n";
    send(sockfd, cmd, strlen(cmd), 0);

    char response[BUFFER_SIZE];
    ssize_t bytes = recv_response(sockfd, response, sizeof(response));
    if (bytes > 0)
    {
        /* Parse the LIST response - simple display for now */
        /* In a real implementation, you'd parse file info properly */
        if (strstr(response, "No files") || strlen(response) < 5)
        {
            ui_show_file_list_header();
            ui_show_file_list_empty();
        }
        else
        {
            ui_show_file_list_header();
            /* For now, just display the raw response */
            /* A better implementation would parse each file entry */
            printf("%s", response);
            printf("\n");
        }
    }
}

void interactive_session(int sockfd, const char *username)
{
    char line[CMD_BUFFER_SIZE];
    char command[64];
    char arg1[256];

    ui_show_session_header(username);

    while (1)
    {
        ui_show_prompt();

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
            ui_show_help();
        }
        else if (strcmp(command, "upload") == 0)
        {
            if (strlen(arg1) == 0)
            {
                ui_show_usage_error("upload", "upload <filename>");
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
                ui_show_usage_error("download", "download <filename>");
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
                ui_show_usage_error("delete", "delete <filename>");
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
            ui_show_info("Sending QUIT command...");
            send(sockfd, "QUIT\n", 5, 0);
            break;
        }
        else
        {
            ui_show_error("Unknown command: '%s'. Type 'help' for available commands.", command);
        }
    }

    ui_show_session_end();
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
    char username[64] = {0};

    /* Show banner */
    ui_show_banner();

    /* Connect to server */
    ui_show_connecting(host, port);

    int sockfd = connect_to_server(host, port);
    if (sockfd < 0)
    {
        ui_show_connection_error("Unable to connect");
        return 1;
    }

    ui_show_connected();

    /* Authenticate first */
    if (!authenticate(sockfd, username, sizeof(username)))
    {
        ui_show_goodbye();
        close(sockfd);
        return 1;
    }

    /* Start interactive session */
    interactive_session(sockfd, username);

    ui_show_goodbye();
    close(sockfd);
    return 0;
}
