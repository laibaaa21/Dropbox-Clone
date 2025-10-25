#include "worker_thread.h"
#include "../server.h"
#include "../queue/task_queue.h"
#include "../session/response_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

/* Worker thread: handles ALL file operations including UPLOAD */
void *worker_worker(void *arg)
{
    (void)arg;
    Task task;

    while (task_queue_pop(&task_queue, &task) == 0)
    {
        printf("[Worker %lu] Processing %d task for %s\n",
               (unsigned long)pthread_self(), task.type, task.filename);

        char path[512];
        mkdir("storage", 0777);
        snprintf(path, sizeof(path), "storage/%s", task.username);
        mkdir(path, 0777);

        switch (task.type)
        {
        case TASK_UPLOAD:
        {
            /* Handle UPLOAD - write data_buffer to disk */
            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            FILE *fp = fopen(path, "wb");
            if (!fp)
            {
                perror("[Worker] fopen upload");
                response_set(task.response, RESPONSE_ERROR,
                             "UPLOAD FAILED: Cannot create file\n", NULL, 0);
                if (task.data_buffer)
                    free(task.data_buffer);
                break;
            }

            size_t written = fwrite(task.data_buffer, 1, task.filesize, fp);
            fclose(fp);

            if (written != task.filesize)
            {
                fprintf(stderr, "[Worker] Upload incomplete: wrote %zu, expected %zu\n",
                        written, task.filesize);
                response_set(task.response, RESPONSE_ERROR,
                             "UPLOAD FAILED: Write error\n", NULL, 0);
            }
            else
            {
                printf("[Worker] Upload complete: %s (%zu bytes)\n", task.filename, written);
                response_set(task.response, RESPONSE_SUCCESS,
                             "UPLOAD OK\n", NULL, 0);
            }

            /* Free the data buffer */
            if (task.data_buffer)
                free(task.data_buffer);
            break;
        }

        case TASK_DOWNLOAD:
        {
            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            FILE *fp = fopen(path, "rb");
            if (!fp)
            {
                response_set(task.response, RESPONSE_FILE_NOT_FOUND,
                             "DOWNLOAD FAILED: File not found\n", NULL, 0);
                break;
            }

            /* Get file size */
            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            /* Allocate buffer for file data */
            void *file_data = malloc(file_size);
            if (!file_data)
            {
                fclose(fp);
                response_set(task.response, RESPONSE_ERROR,
                             "DOWNLOAD FAILED: Memory allocation failed\n", NULL, 0);
                break;
            }

            /* Read entire file */
            size_t read_bytes = fread(file_data, 1, file_size, fp);
            fclose(fp);

            if (read_bytes != (size_t)file_size)
            {
                free(file_data);
                response_set(task.response, RESPONSE_ERROR,
                             "DOWNLOAD FAILED: Read error\n", NULL, 0);
                break;
            }

            printf("[Worker] Download complete: %s (%ld bytes)\n", task.filename, file_size);
            response_set(task.response, RESPONSE_SUCCESS,
                         "\nDOWNLOAD OK\n", file_data, read_bytes);
            break;
        }

        case TASK_DELETE:
        {
            snprintf(path, sizeof(path), "storage/%s/%s", task.username, task.filename);
            if (remove(path) == 0)
            {
                printf("[Worker] Delete complete: %s\n", task.filename);
                response_set(task.response, RESPONSE_SUCCESS,
                             "DELETE OK\n", NULL, 0);
            }
            else
            {
                response_set(task.response, RESPONSE_ERROR,
                             "DELETE FAILED: File not found or error\n", NULL, 0);
            }
            break;
        }

        case TASK_LIST:
        {
            snprintf(path, sizeof(path), "storage/%s", task.username);
            DIR *dir = opendir(path);
            if (!dir)
            {
                response_set(task.response, RESPONSE_ERROR,
                             "LIST FAILED: Cannot open directory\n", NULL, 0);
                break;
            }

            /* Build list of files */
            char list_buffer[4096] = {0};
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
            {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                strncat(list_buffer, entry->d_name, sizeof(list_buffer) - strlen(list_buffer) - 1);
                strncat(list_buffer, "\n", sizeof(list_buffer) - strlen(list_buffer) - 1);
            }
            closedir(dir);

            strncat(list_buffer, "LIST END\n", sizeof(list_buffer) - strlen(list_buffer) - 1);

            /* Allocate and copy list data */
            size_t list_len = strlen(list_buffer);
            char *list_data = malloc(list_len + 1);
            if (list_data)
            {
                strcpy(list_data, list_buffer);
                response_set(task.response, RESPONSE_SUCCESS,
                             "", list_data, list_len);
            }
            else
            {
                response_set(task.response, RESPONSE_ERROR,
                             "LIST FAILED: Memory allocation failed\n", NULL, 0);
            }
            break;
        }

        default:
            response_set(task.response, RESPONSE_ERROR,
                         "UNKNOWN COMMAND\n", NULL, 0);
            break;
        }
    }

    printf("[Worker %lu] Exiting...\n", (unsigned long)pthread_self());
    return NULL;
}
