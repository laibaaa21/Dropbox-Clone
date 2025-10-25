#include "client_thread.h"
#include "../server.h"
#include "../queue/client_queue.h"
#include "../queue/task_queue.h"
#include "../session/response_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

/* Client thread: queues ALL operations (including UPLOAD) to workers */
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

        // Read the actual command
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
        t.data_buffer = NULL;

        /* Create response structure for this task */
        Response response;
        if (response_init(&response) != 0)
        {
            fprintf(stderr, "[ClientThread] Failed to init response\n");
            send(cfd, "SERVER ERROR\n", 13, 0);
            close(cfd);
            continue;
        }
        t.response = &response;

        /* Parse command and prepare task */
        if (sscanf(cmd, "UPLOAD %255s %zu", t.filename, &t.filesize) == 2)
        {
            t.type = TASK_UPLOAD;

            printf("[ClientThread] Receiving %zu bytes for %s\n", t.filesize, t.filename);

            /* Allocate buffer for file data */
            t.data_buffer = malloc(t.filesize);
            if (!t.data_buffer)
            {
                send(cfd, "UPLOAD FAILED: Memory allocation failed\n", 40, 0);
                close(cfd);
                response_destroy(&response);
                continue;
            }

            /* Find leftover bytes (file data that came with command) */
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

            /* Continue reading remaining bytes */
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
                fprintf(stderr, "[ClientThread] Upload incomplete: got %zu, expected %zu\n",
                        received, t.filesize);
                send(cfd, "UPLOAD FAILED: Incomplete data\n", 31, 0);
                close(cfd);
                free(t.data_buffer);
                response_destroy(&response);
                continue;
            }

            printf("[ClientThread] Received all %zu bytes, queueing task to worker\n", received);
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
            close(cfd);
            response_destroy(&response);
            continue;
        }

        /* Queue task to workers */
        if (task_queue_push(&task_queue, &t) != 0)
        {
            fprintf(stderr, "Task queue full. Closing fd=%d\n", cfd);
            send(cfd, "SERVER BUSY\n", 12, 0);
            close(cfd);
            if (t.data_buffer)
                free(t.data_buffer);
            response_destroy(&response);
            continue;
        }

        /* Wait for worker to complete task (non-busy-wait) */
        printf("[ClientThread] Waiting for worker response...\n");
        response_wait(&response);
        printf("[ClientThread] Got response: %s\n", response.message);

        /* Send response to client */
        if (response.status == RESPONSE_SUCCESS)
        {
            if (response.data && response.data_size > 0)
            {
                /* DOWNLOAD: send file data */
                send(cfd, response.data, response.data_size, 0);
            }
            send(cfd, response.message, strlen(response.message), 0);
        }
        else
        {
            /* Error response */
            send(cfd, response.message, strlen(response.message), 0);
        }

        close(cfd);
        response_destroy(&response);
    }

    printf("[ClientThread %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}
