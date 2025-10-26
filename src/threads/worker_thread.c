#include "worker_thread.h"
#include "../server.h"
#include "../queue/task_queue.h"
#include "../session/response_queue.h"
#include "../session/session_manager.h"
#include "../auth/user_metadata.h"
#include "../sync/file_locks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

/* Helper function to safely deliver response to session (Phase 2.1) */
static void deliver_response(uint64_t session_id, response_status_t status,
                            const char *message, void *data, size_t data_size)
{
    /* Look up session by ID */
    Session *session = session_get(&session_manager, session_id);

    if (!session)
    {
        /* Session not found or inactive - client disconnected */
        printf("[Worker %lu] Session %lu not found or inactive, dropping response\n",
               (unsigned long)pthread_self(), session_id);

        /* Free data if allocated */
        if (data)
        {
            free(data);
        }
        return;
    }

    /* Session is active, deliver response */
    response_set(&session->response, status, message, data, data_size);

    /* Update session activity tracking (Phase 2.9) */
    session_increment_operations(session);

    printf("[Worker %lu] Response delivered to session %lu (ops=%lu)\n",
           (unsigned long)pthread_self(), session_id, session->operations_count);
}

/* Worker thread: handles ALL file operations including UPLOAD */
void *worker_worker(void *arg)
{
    (void)arg;
    Task task;

    while (task_queue_pop(&task_queue, &task) == 0)
    {
        printf("[Worker %lu] Processing task type=%d for session=%lu user=%s\n",
               (unsigned long)pthread_self(), task.type, task.session_id, task.username);

        char path[512];
        mkdir("storage", 0777);
        snprintf(path, sizeof(path), "storage/%s", task.username);
        mkdir(path, 0777);

        switch (task.type)
        {
        case TASK_UPLOAD:
        {
            /* Phase 2.4: Get user and acquire per-user lock */
            UserMetadata *user = user_get(&global_user_db, task.username);
            if (!user)
            {
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "UPLOAD FAILED: User not found\n", NULL, 0);
                if (task.data_buffer)
                    free(task.data_buffer);
                break;
            }

            /* Acquire per-user lock for entire upload operation */
            pthread_mutex_lock(&user->mtx);

            /* Phase 2.5: Acquire per-file lock */
            FileLock *file_lock = file_lock_acquire(&global_file_lock_manager, task.username, task.filename);
            if (!file_lock)
            {
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "UPLOAD FAILED: Could not acquire file lock\n", NULL, 0);
                if (task.data_buffer)
                    free(task.data_buffer);
                break;
            }

            /* Handle UPLOAD - write data_buffer to disk */
            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            FILE *fp = fopen(path, "wb");
            if (!fp)
            {
                fprintf(stderr, "[Worker] fopen failed for upload '%s': %s\n",
                       path, strerror(errno));
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);

                /* Provide specific error message based on errno */
                if (errno == EACCES || errno == EPERM)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "UPLOAD ERROR: Permission denied\n", NULL, 0);
                }
                else if (errno == ENOSPC)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "UPLOAD ERROR: No space left on device\n", NULL, 0);
                }
                else if (errno == ENAMETOOLONG)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "UPLOAD ERROR: Filename too long\n", NULL, 0);
                }
                else
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "UPLOAD ERROR: Cannot create file\n", NULL, 0);
                }

                if (task.data_buffer)
                    free(task.data_buffer);
                break;
            }

            size_t written = fwrite(task.data_buffer, 1, task.filesize, fp);
            int fwrite_error = ferror(fp);
            if (fclose(fp) != 0)
            {
                fprintf(stderr, "[Worker] fclose failed for upload '%s': %s\n",
                       path, strerror(errno));
            }

            if (fwrite_error || written != task.filesize)
            {
                fprintf(stderr, "[Worker] Upload incomplete: wrote %zu/%zu bytes, ferror=%d\n",
                        written, task.filesize, fwrite_error);
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);

                /* Try to remove incomplete file */
                if (remove(path) != 0)
                {
                    fprintf(stderr, "[Worker] Failed to remove incomplete file '%s': %s\n",
                           path, strerror(errno));
                }

                deliver_response(task.session_id, RESPONSE_ERROR,
                                "UPLOAD ERROR: File write failed\n", NULL, 0);
            }
            else
            {
                printf("[Worker] Upload complete: %s (%zu bytes)\n", task.filename, written);

                /* Update user metadata (already holding user lock) */
                /* Check if file already exists (update size if so) */
                bool file_exists = false;
                for (int i = 0; i < user->file_count; i++)
                {
                    if (strcmp(user->files[i].filename, task.filename) == 0)
                    {
                        size_t old_size = user->files[i].size;
                        user->files[i].size = task.filesize;
                        time_t now = time(NULL);
                        if (now == (time_t)-1)
                        {
                            fprintf(stderr, "[Worker] time() failed: %s\n", strerror(errno));
                            now = 0;
                        }
                        user->files[i].timestamp = now;
                        user->quota_used = user->quota_used - old_size + task.filesize;
                        file_exists = true;
                        break;
                    }
                }

                /* Add new file if it doesn't exist */
                if (!file_exists)
                {
                    /* Expand array if needed */
                    if (user->file_count >= user->file_capacity)
                    {
                        int new_capacity = user->file_capacity * 2;
                        FileMetadata *new_files = realloc(user->files, new_capacity * sizeof(FileMetadata));
                        if (new_files)
                        {
                            user->files = new_files;
                            user->file_capacity = new_capacity;
                        }
                    }

                    if (user->file_count < user->file_capacity)
                    {
                        strncpy(user->files[user->file_count].filename, task.filename, 255);
                        user->files[user->file_count].filename[255] = '\0';  /* Ensure null-termination */
                        user->files[user->file_count].size = task.filesize;
                        time_t now = time(NULL);
                        if (now == (time_t)-1)
                        {
                            fprintf(stderr, "[Worker] time() failed: %s\n", strerror(errno));
                            now = 0;
                        }
                        user->files[user->file_count].timestamp = now;
                        user->file_count++;
                        user->quota_used += task.filesize;
                    }
                    else
                    {
                        fprintf(stderr, "[Worker] Cannot add file: file_capacity exceeded\n");
                    }
                }

                /* Save metadata (already holding lock) */
                FILE *meta_fp = NULL;
                char meta_path[512];
                snprintf(meta_path, sizeof(meta_path), "storage/%s/metadata.txt", user->username);
                char temp_path[512];
                snprintf(temp_path, sizeof(temp_path), "storage/%s/metadata.tmp", user->username);

                meta_fp = fopen(temp_path, "w");
                if (meta_fp)
                {
                    fprintf(meta_fp, "username=%s\n", user->username);
                    fprintf(meta_fp, "quota_used=%zu\n", user->quota_used);
                    fprintf(meta_fp, "quota_limit=%zu\n", user->quota_limit);
                    fprintf(meta_fp, "file_count=%d\n", user->file_count);
                    for (int i = 0; i < user->file_count; i++)
                    {
                        fprintf(meta_fp, "file=%s,%zu,%ld\n",
                                user->files[i].filename,
                                user->files[i].size,
                                (long)user->files[i].timestamp);
                    }
                    if (fclose(meta_fp) != 0)
                    {
                        fprintf(stderr, "[Worker] fclose failed for metadata file (UPLOAD): %s\n",
                               strerror(errno));
                    }

                    /* Atomic rename */
                    if (rename(temp_path, meta_path) != 0)
                    {
                        fprintf(stderr, "[Worker] rename failed for metadata (UPLOAD): %s\n",
                               strerror(errno));
                    }
                }
                else
                {
                    fprintf(stderr, "[Worker] fopen failed for metadata temp file (UPLOAD): %s\n",
                           strerror(errno));
                }

                /* Phase 2.5: Release file lock, then user lock */
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_SUCCESS,
                                "UPLOAD OK\n", NULL, 0);
            }

            /* Free the data buffer */
            if (task.data_buffer)
                free(task.data_buffer);
            break;
        }

        case TASK_DOWNLOAD:
        {
            /* Phase 2.4: Get user and acquire per-user lock */
            UserMetadata *user = user_get(&global_user_db, task.username);
            if (!user)
            {
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD FAILED: User not found\n", NULL, 0);
                break;
            }

            /* Acquire per-user lock for entire download operation */
            pthread_mutex_lock(&user->mtx);

            /* Phase 2.5: Acquire per-file lock */
            FileLock *file_lock = file_lock_acquire(&global_file_lock_manager, task.username, task.filename);
            if (!file_lock)
            {
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD FAILED: Could not acquire file lock\n", NULL, 0);
                break;
            }

            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            FILE *fp = fopen(path, "rb");
            if (!fp)
            {
                fprintf(stderr, "[Worker] fopen failed for download '%s': %s\n",
                       path, strerror(errno));
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);

                /* Provide specific error message */
                if (errno == ENOENT)
                {
                    deliver_response(task.session_id, RESPONSE_FILE_NOT_FOUND,
                                    "DOWNLOAD ERROR: File not found\n", NULL, 0);
                }
                else if (errno == EACCES)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "DOWNLOAD ERROR: Permission denied\n", NULL, 0);
                }
                else
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "DOWNLOAD ERROR: Cannot open file\n", NULL, 0);
                }
                break;
            }

            /* Get file size */
            if (fseek(fp, 0, SEEK_END) != 0)
            {
                fprintf(stderr, "[Worker] fseek failed for download '%s': %s\n",
                       path, strerror(errno));
                fclose(fp);
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD ERROR: Cannot determine file size\n", NULL, 0);
                break;
            }

            long file_size = ftell(fp);
            if (file_size < 0)
            {
                fprintf(stderr, "[Worker] ftell failed for download '%s': %s\n",
                       path, strerror(errno));
                fclose(fp);
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD ERROR: Cannot determine file size\n", NULL, 0);
                break;
            }

            if (fseek(fp, 0, SEEK_SET) != 0)
            {
                fprintf(stderr, "[Worker] fseek to start failed for download '%s': %s\n",
                       path, strerror(errno));
                fclose(fp);
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD ERROR: File seek error\n", NULL, 0);
                break;
            }

            /* Allocate buffer for file data */
            void *file_data = malloc(file_size);
            if (!file_data)
            {
                fclose(fp);
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD FAILED: Memory allocation failed\n", NULL, 0);
                break;
            }

            /* Read entire file */
            size_t read_bytes = fread(file_data, 1, file_size, fp);
            int fread_error = ferror(fp);
            if (fclose(fp) != 0)
            {
                fprintf(stderr, "[Worker] fclose failed for download '%s': %s\n",
                       path, strerror(errno));
            }

            /* Phase 2.5: Release locks after reading */
            file_lock_release(&global_file_lock_manager, file_lock);
            pthread_mutex_unlock(&user->mtx);

            if (fread_error || read_bytes != (size_t)file_size)
            {
                fprintf(stderr, "[Worker] fread incomplete: read %zu/%ld bytes, ferror=%d\n",
                       read_bytes, file_size, fread_error);
                free(file_data);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DOWNLOAD ERROR: File read error\n", NULL, 0);
                break;
            }

            printf("[Worker] Download complete: %s (%ld bytes)\n", task.filename, file_size);
            deliver_response(task.session_id, RESPONSE_SUCCESS,
                            "\nDOWNLOAD OK\n", file_data, read_bytes);
            break;
        }

        case TASK_DELETE:
        {
            /* Phase 2.4: Get user and acquire per-user lock */
            UserMetadata *user = user_get(&global_user_db, task.username);
            if (!user)
            {
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DELETE FAILED: User not found\n", NULL, 0);
                break;
            }

            /* Acquire per-user lock for entire delete operation */
            pthread_mutex_lock(&user->mtx);

            /* Phase 2.5: Acquire per-file lock */
            FileLock *file_lock = file_lock_acquire(&global_file_lock_manager, task.username, task.filename);
            if (!file_lock)
            {
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "DELETE FAILED: Could not acquire file lock\n", NULL, 0);
                break;
            }

            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            int remove_result = remove(path);
            if (remove_result == 0)
            {
                printf("[Worker] Delete complete: %s\n", task.filename);

                /* Update user metadata (already holding lock) */
                for (int i = 0; i < user->file_count; i++)
                {
                    if (strcmp(user->files[i].filename, task.filename) == 0)
                    {
                        user->quota_used -= user->files[i].size;

                        /* Shift remaining files */
                        for (int j = i; j < user->file_count - 1; j++)
                        {
                            user->files[j] = user->files[j + 1];
                        }
                        user->file_count--;
                        break;
                    }
                }

                /* Save metadata (already holding lock) */
                FILE *meta_fp = NULL;
                char meta_path[512];
                snprintf(meta_path, sizeof(meta_path), "storage/%s/metadata.txt", user->username);
                char temp_path[512];
                snprintf(temp_path, sizeof(temp_path), "storage/%s/metadata.tmp", user->username);

                meta_fp = fopen(temp_path, "w");
                if (meta_fp)
                {
                    fprintf(meta_fp, "username=%s\n", user->username);
                    fprintf(meta_fp, "quota_used=%zu\n", user->quota_used);
                    fprintf(meta_fp, "quota_limit=%zu\n", user->quota_limit);
                    fprintf(meta_fp, "file_count=%d\n", user->file_count);
                    for (int i = 0; i < user->file_count; i++)
                    {
                        fprintf(meta_fp, "file=%s,%zu,%ld\n",
                                user->files[i].filename,
                                user->files[i].size,
                                (long)user->files[i].timestamp);
                    }
                    if (fclose(meta_fp) != 0)
                    {
                        fprintf(stderr, "[Worker] fclose failed for metadata file (DELETE): %s\n",
                               strerror(errno));
                    }

                    /* Atomic rename */
                    if (rename(temp_path, meta_path) != 0)
                    {
                        fprintf(stderr, "[Worker] rename failed for metadata (DELETE): %s\n",
                               strerror(errno));
                    }
                }
                else
                {
                    fprintf(stderr, "[Worker] fopen failed for metadata temp file (DELETE): %s\n",
                           strerror(errno));
                }

                /* Phase 2.5: Release locks */
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);
                deliver_response(task.session_id, RESPONSE_SUCCESS,
                                "DELETE OK\n", NULL, 0);
            }
            else
            {
                fprintf(stderr, "[Worker] remove failed for '%s': %s\n",
                       path, strerror(errno));
                file_lock_release(&global_file_lock_manager, file_lock);
                pthread_mutex_unlock(&user->mtx);

                /* Provide specific error message */
                if (errno == ENOENT)
                {
                    deliver_response(task.session_id, RESPONSE_FILE_NOT_FOUND,
                                    "DELETE ERROR: File not found\n", NULL, 0);
                }
                else if (errno == EACCES || errno == EPERM)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "DELETE ERROR: Permission denied\n", NULL, 0);
                }
                else
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "DELETE ERROR: Cannot delete file\n", NULL, 0);
                }
            }
            break;
        }

        case TASK_LIST:
        {
            /* Phase 2.4: Get user and acquire per-user lock */
            UserMetadata *user = user_get(&global_user_db, task.username);
            if (!user)
            {
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "LIST FAILED: User not found\n", NULL, 0);
                break;
            }

            /* Acquire per-user lock for entire list operation */
            pthread_mutex_lock(&user->mtx);

            snprintf(path, sizeof(path), "storage/%s", task.username);
            DIR *dir = opendir(path);
            if (!dir)
            {
                fprintf(stderr, "[Worker] opendir failed for '%s': %s\n",
                       path, strerror(errno));
                pthread_mutex_unlock(&user->mtx);

                /* Provide specific error message */
                if (errno == ENOENT)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "LIST ERROR: User directory not found\n", NULL, 0);
                }
                else if (errno == EACCES)
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "LIST ERROR: Permission denied\n", NULL, 0);
                }
                else
                {
                    deliver_response(task.session_id, RESPONSE_ERROR,
                                    "LIST ERROR: Cannot open directory\n", NULL, 0);
                }
                break;
            }

            /* Build list of files */
            char list_buffer[4096] = {0};
            struct dirent *entry;
            errno = 0;  /* Reset errno before readdir */
            while ((entry = readdir(dir)) != NULL)
            {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                /* Skip metadata file */
                if (strcmp(entry->d_name, "metadata.txt") == 0 || strcmp(entry->d_name, "metadata.tmp") == 0)
                    continue;

                /* Check buffer space before adding */
                size_t remaining = sizeof(list_buffer) - strlen(list_buffer) - 1;
                size_t needed = strlen(entry->d_name) + 1; /* +1 for newline */
                if (needed >= remaining)
                {
                    fprintf(stderr, "[Worker] LIST: buffer full, truncating file list\n");
                    break;
                }

                strncat(list_buffer, entry->d_name, remaining);
                strncat(list_buffer, "\n", remaining - strlen(entry->d_name));
                errno = 0;  /* Reset errno for next iteration */
            }

            /* Check for readdir error */
            if (errno != 0)
            {
                fprintf(stderr, "[Worker] readdir failed: %s\n", strerror(errno));
            }

            if (closedir(dir) != 0)
            {
                fprintf(stderr, "[Worker] closedir failed: %s\n", strerror(errno));
            }

            pthread_mutex_unlock(&user->mtx);

            strncat(list_buffer, "LIST END\n", sizeof(list_buffer) - strlen(list_buffer) - 1);

            /* Allocate and copy list data */
            size_t list_len = strlen(list_buffer);
            char *list_data = malloc(list_len + 1);
            if (list_data)
            {
                strcpy(list_data, list_buffer);
                deliver_response(task.session_id, RESPONSE_SUCCESS,
                                "", list_data, list_len);
            }
            else
            {
                fprintf(stderr, "[Worker] malloc failed for list data (%zu bytes)\n", list_len + 1);
                deliver_response(task.session_id, RESPONSE_ERROR,
                                "LIST ERROR: Server memory allocation failed\n", NULL, 0);
            }
            break;
        }

        default:
            deliver_response(task.session_id, RESPONSE_ERROR,
                            "UNKNOWN COMMAND\n", NULL, 0);
            break;
        }
    }

    printf("[Worker %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}
