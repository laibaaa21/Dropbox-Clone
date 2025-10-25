#include "file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <dirent.h>

#define STORAGE_ROOT "storage"

static void ensure_user_dir(const char *username)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_ROOT, username);
    mkdir(STORAGE_ROOT, 0777); // create root if missing
    mkdir(path, 0777);         // create user folder if missing
}

/* UPLOAD handler - currently not used, will be refactored in Sprint 2 */
int handle_upload(Task *t)
{
    ensure_user_dir(t->username);
    char dest[512];
    snprintf(dest, sizeof(dest), "%s/%s/%s", STORAGE_ROOT, t->username, t->filename);

    FILE *fp = fopen(dest, "wb");
    if (!fp)
    {
        perror("fopen upload");
        send(t->client_fd, "UPLOAD FAILED\n", 14, 0);
        return -1;
    }

    size_t remaining = t->filesize;
    char buf[1024];
    while (remaining > 0)
    {
        ssize_t n = recv(t->client_fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        fwrite(buf, 1, n, fp);
        remaining -= n;
    }

    fclose(fp);
    send(t->client_fd, "UPLOAD OK\n", 10, 0);
    return 0;
}

/* DOWNLOAD handler */
int handle_download(Task *t)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%s", STORAGE_ROOT, t->username, t->filename);
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        send(t->client_fd, "DOWNLOAD FAILED\n", 16, 0);
        return -1;
    }

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        send(t->client_fd, buf, n, 0);
    }
    fclose(fp);
    return 0;
}

/* DELETE handler */
int handle_delete(Task *t)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%s", STORAGE_ROOT, t->username, t->filename);
    if (unlink(path) == 0)
    {
        send(t->client_fd, "DELETE OK\n", 10, 0);
    }
    else
    {
        send(t->client_fd, "DELETE FAILED\n", 14, 0);
    }
    return 0;
}

/* LIST handler */
int handle_list(Task *t)
{
    char dirpath[256];
    snprintf(dirpath, sizeof(dirpath), "%s/%s", STORAGE_ROOT, t->username);
    ensure_user_dir(t->username);

    DIR *dir = opendir(dirpath);
    if (!dir)
    {
        send(t->client_fd, "LIST FAILED\n", 12, 0);
        return -1;
    }

    struct dirent *entry;
    char line[512];
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(line, sizeof(line), "%s\n", entry->d_name);
        send(t->client_fd, line, strlen(line), 0);
    }
    closedir(dir);
    return 0;
}
