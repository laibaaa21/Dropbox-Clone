#include "client_thread.h"
#include "../server.h"
#include "../queue/client_queue.h"
#include "../queue/task_queue.h"
#include "../session/response_queue.h"
#include "../session/session_manager.h"
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

        /* Create session for this client connection (Phase 2.1) */
        uint64_t session_id = session_create(&session_manager, cfd);
        if (session_id == 0)
        {
            fprintf(stderr, "[ClientThread] Failed to create session\n");
            close(cfd);
            continue;
        }

        /* Get session pointer */
        Session *session = session_get(&session_manager, session_id);
        if (!session)
        {
            fprintf(stderr, "[ClientThread] Failed to get session %lu\n", session_id);
            close(cfd);
            session_destroy(&session_manager, session_id);
            continue;
        }

        printf("[ClientThread] Session %lu created (fd=%d)\n", session_id, cfd);

        char cmd[512];

        /* Send welcome message */
        const char *welcome_msg =
            "Welcome to Dropbox Clone Server :))\n"
            "Please authenticate first:\n"
            "SIGNUP <username> <password>\n"
            "LOGIN <username> <password>\n";
        send(cfd, welcome_msg, strlen(welcome_msg), 0);

        /* Authentication loop */
        while (!session->is_authenticated)
        {
            ssize_t n = recv(cfd, cmd, sizeof(cmd) - 1, 0);
            if (n <= 0)
            {
                /* Client disconnected during auth */
                printf("[ClientThread] Session %lu: client disconnected during auth\n", session_id);
                session_mark_inactive(&session_manager, session_id);
                session_destroy(&session_manager, session_id);
                goto next_client;
            }
            cmd[n] = '\0';

            /* Remove newline */
            char *newline = strchr(cmd, '\n');
            if (newline)
                *newline = '\0';

            printf("[ClientThread] Session %lu: Auth command: %s\n", session_id, cmd);

            char username[MAX_USERNAME_LEN];
            char password[256];

            /* Handle SIGNUP */
            if (sscanf(cmd, "SIGNUP %63s %255s", username, password) == 2)
            {
                int result = user_signup(username, password);
                if (result == 0)
                {
                    send(cfd, "SIGNUP OK\n", 10, 0);
                    session_set_username(session, username);
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
                    session_set_username(session, username);
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

        printf("[ClientThread] Session %lu: User '%s' authenticated\n", session_id, session->username);

        /* File operation loop */
        while (1)
        {
            ssize_t n = recv(cfd, cmd, sizeof(cmd) - 1, 0);
            if (n <= 0)
            {
                /* Client disconnected */
                printf("[ClientThread] Session %lu: client disconnected\n", session_id);
                session_mark_inactive(&session_manager, session_id);
                session_destroy(&session_manager, session_id);
                goto next_client;
            }
            cmd[n] = '\0';
            printf("[ClientThread] Session %lu: File command: %s\n", session_id, cmd);

            /* Handle QUIT */
            if (strncmp(cmd, "QUIT", 4) == 0)
            {
                send(cfd, "Goodbye!\n", 9, 0);
                printf("[ClientThread] Session %lu: user quit\n", session_id);
                session_mark_inactive(&session_manager, session_id);
                session_destroy(&session_manager, session_id);
                goto next_client;
            }

            /* Prepare task structure (Phase 2.1: use session_id instead of Response*) */
            Task t;
            memset(&t, 0, sizeof(t));
            t.session_id = session_id;
            strncpy(t.username, session->username, sizeof(t.username) - 1);
            t.data_buffer = NULL;

            /* Parse command */
            if (sscanf(cmd, "UPLOAD %255s %zu", t.filename, &t.filesize) == 2)
            {
                /* Check quota before receiving data */
                UserMetadata *user = user_get(&global_user_db, session->username);
                if (user && !user_check_quota(user, t.filesize))
                {
                    send(cfd, "UPLOAD ERROR: Quota exceeded\n", 29, 0);
                    continue;
                }

                t.type = TASK_UPLOAD;
                printf("[ClientThread] Session %lu: Receiving %zu bytes for %s\n", 
                       session_id, t.filesize, t.filename);

                /* Allocate buffer */
                t.data_buffer = malloc(t.filesize);
                if (!t.data_buffer)
                {
                    send(cfd, "UPLOAD FAILED: Memory allocation failed\n", 40, 0);
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
                    fprintf(stderr, "[ClientThread] Session %lu: Upload incomplete\n", session_id);
                    send(cfd, "UPLOAD FAILED: Incomplete data\n", 31, 0);
                    free(t.data_buffer);
                    continue;
                }

                printf("[ClientThread] Session %lu: Received all %zu bytes, queueing\n", 
                       session_id, received);
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
                continue;
            }

            /* Reset response for this task (Phase 2.1: reuse session response) */
            pthread_mutex_lock(&session->response.mtx);
            session->response.ready = false;
            session->response.status = RESPONSE_SUCCESS;
            memset(session->response.message, 0, sizeof(session->response.message));
            if (session->response.data)
            {
                free(session->response.data);
                session->response.data = NULL;
            }
            session->response.data_size = 0;
            pthread_mutex_unlock(&session->response.mtx);

            /* Queue task to workers (Phase 2.1: task contains session_id) */
            if (task_queue_push(&task_queue, &t) != 0)
            {
                fprintf(stderr, "[ClientThread] Session %lu: Task queue full\n", session_id);
                send(cfd, "SERVER BUSY\n", 12, 0);
                if (t.data_buffer)
                    free(t.data_buffer);
                continue;
            }

            /* Wait for worker response (Phase 2.1: wait on session response) */
            printf("[ClientThread] Session %lu: Waiting for worker...\n", session_id);
            response_wait(&session->response);
            
            /* Check if session is still active (worker may have found inactive session) */
            if (!session->is_active)
            {
                printf("[ClientThread] Session %lu: became inactive while waiting\n", session_id);
                session_destroy(&session_manager, session_id);
                goto next_client;
            }
            
            printf("[ClientThread] Session %lu: Got response: %s\n", 
                   session_id, session->response.message);

            /* Send response to client */
            if (session->response.data && session->response.data_size > 0)
            {
                send(cfd, session->response.data, session->response.data_size, 0);
            }
            if (strlen(session->response.message) > 0)
            {
                send(cfd, session->response.message, strlen(session->response.message), 0);
            }
        }

    next_client:
        continue;
    }

    printf("[ClientThread %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}
