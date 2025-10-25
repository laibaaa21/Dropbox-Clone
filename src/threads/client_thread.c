#include "client_thread.h"
#include "../server.h"
#include "../queue/client_queue.h"
#include "../queue/task_queue.h"
#include "../session/response_queue.h"
#include "../auth/auth.h"
#include "../auth/user_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

/* Client thread: handles authentication, then queues file operations to workers */
void *client_worker(void *arg)
{
    (void)arg;

    while (1)
    {
        int cfd = client_queue_pop(&client_queue);
        if (cfd < 0)
            break;

        char cmd[512];
        char authenticated_user[MAX_USERNAME_LEN] = {0};
        bool is_authenticated = false;

        /* Send welcome message */
        const char *welcome_msg =
            "Welcome to Dropbox Clone Server :))\n"
            "Please authenticate first:\n"
            "SIGNUP <username> <password>\n"
            "LOGIN <username> <password>\n";
        send(cfd, welcome_msg, strlen(welcome_msg), 0);

        /* Authentication loop */
        while (!is_authenticated)
        {
            ssize_t n = recv(cfd, cmd, sizeof(cmd) - 1, 0);
            if (n <= 0)
            {
                close(cfd);
                goto next_client;
            }
            cmd[n] = '\0';

            /* Remove newline */
            char *newline = strchr(cmd, '\n');
            if (newline)
                *newline = '\0';

            printf("[ClientThread] Auth command: %s\n", cmd);

            char username[MAX_USERNAME_LEN];
            char password[256];

            /* Handle SIGNUP */
            if (sscanf(cmd, "SIGNUP %63s %255s", username, password) == 2)
            {
                int result = user_signup(username, password);
                if (result == 0)
                {
                    send(cfd, "SIGNUP OK\n", 10, 0);
                    strncpy(authenticated_user, username, MAX_USERNAME_LEN - 1);
                    is_authenticated = true;
                }
                else if (result == -2)
                {
                    send(cfd, "SIGNUP ERROR: User already exists\n", 35, 0);
                }
                else
                {
                    send(cfd, "SIGNUP ERROR: Failed\n", 21, 0);
                }
            }
            /* Handle LOGIN */
            else if (sscanf(cmd, "LOGIN %63s %255s", username, password) == 2)
            {
                int result = user_login(username, password);
                if (result == 0)
                {
                    send(cfd, "LOGIN OK\n", 9, 0);
                    strncpy(authenticated_user, username, MAX_USERNAME_LEN - 1);
                    is_authenticated = true;
                }
                else if (result == -2)
                {
                    send(cfd, "LOGIN ERROR: User not found\n", 28, 0);
                }
                else if (result == -3)
                {
                    send(cfd, "LOGIN ERROR: Invalid password\n", 30, 0);
                }
                else
                {
                    send(cfd, "LOGIN ERROR: Failed\n", 20, 0);
                }
            }
            else
            {
                send(cfd, "ERROR: Please SIGNUP or LOGIN first\n", 37, 0);
            }
        }

        /* User is now authenticated, show file commands */
        const char *file_menu =
            "\nAuthenticated! Available commands:\n"
            "UPLOAD <filename> <size>\n"
            "DOWNLOAD <filename>\n"
            "DELETE <filename>\n"
            "LIST\n"
            "QUIT\n";
        send(cfd, file_menu, strlen(file_menu), 0);

        /* File operation loop */
        while (1)
        {
            ssize_t n = recv(cfd, cmd, sizeof(cmd) - 1, 0);
            if (n <= 0)
            {
                close(cfd);
                goto next_client;
            }
            cmd[n] = '\0';
            printf("[ClientThread] File command from %s: %s\n", authenticated_user, cmd);

            /* Handle QUIT */
            if (strncmp(cmd, "QUIT", 4) == 0)
            {
                send(cfd, "Goodbye!\n", 9, 0);
                close(cfd);
                goto next_client;
            }

            Task t;
            memset(&t, 0, sizeof(t));
            strncpy(t.username, authenticated_user, sizeof(t.username) - 1);
            t.client_fd = cfd;
            t.data_buffer = NULL;

            /* Create response structure */
            Response response;
            if (response_init(&response) != 0)
            {
                fprintf(stderr, "[ClientThread] Failed to init response\n");
                send(cfd, "SERVER ERROR\n", 13, 0);
                close(cfd);
                goto next_client;
            }
            t.response = &response;

            /* Parse command */
            if (sscanf(cmd, "UPLOAD %255s %zu", t.filename, &t.filesize) == 2)
            {
                /* Check quota before receiving data */
                UserMetadata *user = user_get(&global_user_db, authenticated_user);
                if (user && !user_check_quota(user, t.filesize))
                {
                    send(cfd, "UPLOAD ERROR: Quota exceeded\n", 29, 0);
                    response_destroy(&response);
                    continue;
                }

                t.type = TASK_UPLOAD;
                printf("[ClientThread] Receiving %zu bytes for %s\n", t.filesize, t.filename);

                /* Allocate buffer */
                t.data_buffer = malloc(t.filesize);
                if (!t.data_buffer)
                {
                    send(cfd, "UPLOAD FAILED: Memory allocation failed\n", 40, 0);
                    response_destroy(&response);
                    continue;
                }

                /* Find leftover bytes */
                char *newline = strchr(cmd, '\n');
                size_t received = 0;
                if (newline && *(newline + 1) != '\0')
                {
                    char *extra = newline + 1;
                    size_t extra_len = n - (extra - cmd);
                    if (extra_len > t.filesize)
                        extra_len = t.filesize;
                    memcpy(t.data_buffer, extra, extra_len);
                    received = extra_len;
                }

                /* Read remaining bytes */
                while (received < t.filesize)
                {
                    ssize_t bytes = recv(cfd, (char *)t.data_buffer + received,
                                         t.filesize - received, 0);
                    if (bytes <= 0)
                        break;
                    received += bytes;
                }

                if (received != t.filesize)
                {
                    fprintf(stderr, "[ClientThread] Upload incomplete\n");
                    send(cfd, "UPLOAD FAILED: Incomplete data\n", 31, 0);
                    free(t.data_buffer);
                    response_destroy(&response);
                    continue;
                }

                printf("[ClientThread] Received all %zu bytes, queueing\n", received);
            }
            else if (sscanf(cmd, "DOWNLOAD %255s", t.filename) == 1)
            {
                t.type = TASK_DOWNLOAD;
            }
            else if (sscanf(cmd, "DELETE %255s", t.filename) == 1)
            {
                t.type = TASK_DELETE;
            }
            else if (strncmp(cmd, "LIST", 4) == 0)
            {
                t.type = TASK_LIST;
            }
            else
            {
                send(cfd, "Invalid command\n", 16, 0);
                response_destroy(&response);
                continue;
            }

            /* Queue task to workers */
            if (task_queue_push(&task_queue, &t) != 0)
            {
                fprintf(stderr, "Task queue full\n");
                send(cfd, "SERVER BUSY\n", 12, 0);
                if (t.data_buffer)
                    free(t.data_buffer);
                response_destroy(&response);
                continue;
            }

            /* Wait for worker response */
            printf("[ClientThread] Waiting for worker...\n");
            response_wait(&response);
            printf("[ClientThread] Got response: %s\n", response.message);

            /* Send response to client */
            if (response.data && response.data_size > 0)
            {
                send(cfd, response.data, response.data_size, 0);
            }
            if (strlen(response.message) > 0)
            {
                send(cfd, response.message, strlen(response.message), 0);
            }

            response_destroy(&response);
        }

    next_client:
        continue;
    }

    printf("[ClientThread %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}
