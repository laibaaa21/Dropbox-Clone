#include "client_thread.h"
#include "../server.h"
#include "../queue/client_queue.h"
#include "../queue/task_queue.h"
#include "../session/response_queue.h"
#include "../session/session_manager.h"
#include "../auth/auth.h"
#include "../auth/user_metadata.h"
#include "../utils/network_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
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
        if (send_success(cfd, welcome_msg) != 0)
        {
            fprintf(stderr, "[ClientThread] Session %lu: failed to send welcome message\n", session_id);
            session_mark_inactive(&session_manager, session_id);
            session_destroy(&session_manager, session_id);
            goto next_client;
        }

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
                    if (send_success(cfd, "SIGNUP OK\n") != 0)
                    {
                        fprintf(stderr, "[ClientThread] Session %lu: failed to send SIGNUP OK\n", session_id);
                        session_mark_inactive(&session_manager, session_id);
                        session_destroy(&session_manager, session_id);
                        goto next_client;
                    }
                    session_set_username(session, username);
                }
                else if (result == -2)
                {
                    send_error(cfd, "SIGNUP ERROR: User already exists\n");
                }
                else
                {
                    send_error(cfd, "SIGNUP ERROR: Database operation failed\n");
                }
            }
            /* Handle LOGIN */
            else if (sscanf(cmd, "LOGIN %63s %255s", username, password) == 2)
            {
                int result = user_login(username, password);
                if (result == 0)
                {
                    if (send_success(cfd, "LOGIN OK\n") != 0)
                    {
                        fprintf(stderr, "[ClientThread] Session %lu: failed to send LOGIN OK\n", session_id);
                        session_mark_inactive(&session_manager, session_id);
                        session_destroy(&session_manager, session_id);
                        goto next_client;
                    }
                    session_set_username(session, username);
                }
                else if (result == -2)
                {
                    send_error(cfd, "LOGIN ERROR: User not found\n");
                }
                else if (result == -3)
                {
                    send_error(cfd, "LOGIN ERROR: Invalid password\n");
                }
                else
                {
                    send_error(cfd, "LOGIN ERROR: Database operation failed\n");
                }
            }
            else
            {
                send_error(cfd, "ERROR: Please SIGNUP or LOGIN first\n");
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
        if (send_success(cfd, file_menu) != 0)
        {
            fprintf(stderr, "[ClientThread] Session %lu: failed to send file menu\n", session_id);
            session_mark_inactive(&session_manager, session_id);
            session_destroy(&session_manager, session_id);
            goto next_client;
        }

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
                send_success(cfd, "Goodbye!\n");  /* Best effort, ignore error */
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
            t.username[sizeof(t.username) - 1] = '\0';  /* Ensure null-termination */
            t.data_buffer = NULL;

            /* Parse command */
            if (sscanf(cmd, "UPLOAD %255s %zu", t.filename, &t.filesize) == 2)
            {
                /* Check quota before receiving data */
                UserMetadata *user = user_get(&global_user_db, session->username);
                if (!user)
                {
                    send_error(cfd, "UPLOAD ERROR: User not found\n");
                    continue;
                }
                if (!user_check_quota(user, t.filesize))
                {
                    send_error(cfd, "UPLOAD ERROR: Quota exceeded\n");
                    continue;
                }

                t.type = TASK_UPLOAD;
                printf("[ClientThread] Session %lu: Receiving %zu bytes for %s\n", 
                       session_id, t.filesize, t.filename);

                /* Allocate buffer */
                t.data_buffer = malloc(t.filesize);
                if (!t.data_buffer)
                {
                    send_error(cfd, "UPLOAD ERROR: Server memory allocation failed\n");
                    fprintf(stderr, "[ClientThread] Session %lu: malloc failed for %zu bytes\n",
                           session_id, t.filesize);
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

                /* Read remaining bytes using recv_full */
                if (received < t.filesize)
                {
                    ssize_t bytes = recv_full(cfd, (char *)t.data_buffer + received,
                                             t.filesize - received);
                    if (bytes < 0)
                    {
                        fprintf(stderr, "[ClientThread] Session %lu: recv_full error: %s\n",
                               session_id, strerror(errno));
                        send_error(cfd, "UPLOAD ERROR: Network receive error\n");
                        free(t.data_buffer);
                        continue;
                    }
                    else if (bytes != (ssize_t)(t.filesize - received))
                    {
                        fprintf(stderr, "[ClientThread] Session %lu: Upload incomplete (received %zu/%zu)\n",
                               session_id, received + bytes, t.filesize);
                        send_error(cfd, "UPLOAD ERROR: Incomplete data transfer\n");
                        free(t.data_buffer);
                        continue;
                    }
                    received += bytes;
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
                send_error(cfd, "ERROR: Invalid command\n");
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
                send_error(cfd, "ERROR: Server busy, please try again\n");
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
                ssize_t sent = send_full(cfd, session->response.data, session->response.data_size);
                if (sent != (ssize_t)session->response.data_size)
                {
                    fprintf(stderr, "[ClientThread] Session %lu: failed to send response data (%zd/%zu bytes)\n",
                           session_id, sent, session->response.data_size);
                    /* Connection may be broken, disconnect */
                    session_mark_inactive(&session_manager, session_id);
                    session_destroy(&session_manager, session_id);
                    goto next_client;
                }
            }
            if (strlen(session->response.message) > 0)
            {
                if (send_full(cfd, session->response.message, strlen(session->response.message)) < 0)
                {
                    fprintf(stderr, "[ClientThread] Session %lu: failed to send response message\n", session_id);
                    /* Connection may be broken, disconnect */
                    session_mark_inactive(&session_manager, session_id);
                    session_destroy(&session_manager, session_id);
                    goto next_client;
                }
            }
        }

    next_client:
        continue;
    }

    printf("[ClientThread %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}
