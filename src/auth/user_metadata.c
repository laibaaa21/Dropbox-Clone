#include "user_metadata.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

/* Global user database */
UserDatabase global_user_db;

/* Simple hash function for username */
static unsigned int hash_username(const char *username, int capacity)
{
    unsigned int hash = 0;
    while (*username)
    {
        hash = (hash * 31) + *username;
        username++;
    }
    return hash % capacity;
}

int user_database_init(UserDatabase *db, int capacity)
{
    if (!db || capacity <= 0)
        return -1;

    db->users = calloc(capacity, sizeof(UserMetadata *));
    if (!db->users)
        return -1;

    db->capacity = capacity;
    db->count = 0;

    if (pthread_mutex_init(&db->mtx, NULL) != 0)
    {
        free(db->users);
        return -1;
    }

    return 0;
}

void user_database_destroy(UserDatabase *db)
{
    if (!db)
        return;

    pthread_mutex_lock(&db->mtx);
    for (int i = 0; i < db->capacity; i++)
    {
        if (db->users[i])
        {
            user_free(db->users[i]);
        }
    }
    free(db->users);
    pthread_mutex_unlock(&db->mtx);
    pthread_mutex_destroy(&db->mtx);
}

UserMetadata *user_create(const char *username, const char *password_hash)
{
    if (!username || !password_hash)
        return NULL;

    UserMetadata *user = calloc(1, sizeof(UserMetadata));
    if (!user)
        return NULL;

    strncpy(user->username, username, MAX_USERNAME_LEN - 1);
    strncpy(user->password_hash, password_hash, MAX_PASSWORD_HASH_LEN - 1);

    user->file_capacity = 16;  // Initial capacity
    user->files = calloc(user->file_capacity, sizeof(FileMetadata));
    if (!user->files)
    {
        free(user);
        return NULL;
    }

    user->file_count = 0;
    user->quota_used = 0;
    user->quota_limit = DEFAULT_QUOTA_LIMIT;
    user->loaded = false;

    if (pthread_mutex_init(&user->mtx, NULL) != 0)
    {
        free(user->files);
        free(user);
        return NULL;
    }

    return user;
}

UserMetadata *user_get(UserDatabase *db, const char *username)
{
    if (!db || !username)
        return NULL;

    unsigned int index = hash_username(username, db->capacity);

    pthread_mutex_lock(&db->mtx);

    /* Linear probing for collision resolution */
    for (int i = 0; i < db->capacity; i++)
    {
        int probe = (index + i) % db->capacity;
        if (db->users[probe] == NULL)
        {
            pthread_mutex_unlock(&db->mtx);
            return NULL;  // Not found
        }
        if (strcmp(db->users[probe]->username, username) == 0)
        {
            UserMetadata *user = db->users[probe];
            pthread_mutex_unlock(&db->mtx);
            return user;
        }
    }

    pthread_mutex_unlock(&db->mtx);
    return NULL;
}

int user_database_add(UserDatabase *db, UserMetadata *user)
{
    if (!db || !user)
        return -1;

    pthread_mutex_lock(&db->mtx);

    if (db->count >= db->capacity * 0.75)  // Load factor check
    {
        pthread_mutex_unlock(&db->mtx);
        return -1;  // Database full
    }

    unsigned int index = hash_username(user->username, db->capacity);

    /* Linear probing to find empty slot */
    for (int i = 0; i < db->capacity; i++)
    {
        int probe = (index + i) % db->capacity;
        if (db->users[probe] == NULL)
        {
            db->users[probe] = user;
            db->count++;
            pthread_mutex_unlock(&db->mtx);
            return 0;
        }
    }

    pthread_mutex_unlock(&db->mtx);
    return -1;  // No empty slot found
}

int user_add_file(UserMetadata *user, const char *filename, size_t size)
{
    if (!user || !filename)
        return -1;

    pthread_mutex_lock(&user->mtx);

    /* Check if file already exists (update size if so) */
    for (int i = 0; i < user->file_count; i++)
    {
        if (strcmp(user->files[i].filename, filename) == 0)
        {
            size_t old_size = user->files[i].size;
            user->files[i].size = size;
            user->files[i].timestamp = time(NULL);
            user->quota_used = user->quota_used - old_size + size;
            pthread_mutex_unlock(&user->mtx);
            return 0;
        }
    }

    /* Expand array if needed */
    if (user->file_count >= user->file_capacity)
    {
        int new_capacity = user->file_capacity * 2;
        FileMetadata *new_files = realloc(user->files, new_capacity * sizeof(FileMetadata));
        if (!new_files)
        {
            pthread_mutex_unlock(&user->mtx);
            return -1;
        }
        user->files = new_files;
        user->file_capacity = new_capacity;
    }

    /* Add new file */
    strncpy(user->files[user->file_count].filename, filename, 255);
    user->files[user->file_count].size = size;
    user->files[user->file_count].timestamp = time(NULL);
    user->file_count++;
    user->quota_used += size;

    pthread_mutex_unlock(&user->mtx);
    return 0;
}

int user_remove_file(UserMetadata *user, const char *filename)
{
    if (!user || !filename)
        return -1;

    pthread_mutex_lock(&user->mtx);

    for (int i = 0; i < user->file_count; i++)
    {
        if (strcmp(user->files[i].filename, filename) == 0)
        {
            user->quota_used -= user->files[i].size;

            /* Shift remaining files */
            for (int j = i; j < user->file_count - 1; j++)
            {
                user->files[j] = user->files[j + 1];
            }
            user->file_count--;

            pthread_mutex_unlock(&user->mtx);
            return 0;
        }
    }

    pthread_mutex_unlock(&user->mtx);
    return -1;  // File not found
}

bool user_check_quota(UserMetadata *user, size_t additional_bytes)
{
    if (!user)
        return false;

    pthread_mutex_lock(&user->mtx);
    bool has_quota = (user->quota_used + additional_bytes) <= user->quota_limit;
    pthread_mutex_unlock(&user->mtx);

    return has_quota;
}

int user_save_metadata(UserMetadata *user)
{
    if (!user)
        return -1;

    char dir_path[512];
    char file_path[512];
    char temp_path[512];

    snprintf(dir_path, sizeof(dir_path), "storage/%s", user->username);
    snprintf(temp_path, sizeof(temp_path), "storage/%s/metadata.tmp", user->username);
    snprintf(file_path, sizeof(file_path), "storage/%s/metadata.txt", user->username);

    /* Create user directory */
    mkdir("storage", 0777);
    mkdir(dir_path, 0777);

    pthread_mutex_lock(&user->mtx);

    FILE *fp = fopen(temp_path, "w");
    if (!fp)
    {
        pthread_mutex_unlock(&user->mtx);
        return -1;
    }

    /* Write metadata */
    fprintf(fp, "username=%s\n", user->username);
    fprintf(fp, "password_hash=%s\n", user->password_hash);
    fprintf(fp, "quota_used=%zu\n", user->quota_used);
    fprintf(fp, "quota_limit=%zu\n", user->quota_limit);
    fprintf(fp, "file_count=%d\n", user->file_count);

    for (int i = 0; i < user->file_count; i++)
    {
        fprintf(fp, "file=%s,%zu,%ld\n",
                user->files[i].filename,
                user->files[i].size,
                (long)user->files[i].timestamp);
    }

    fclose(fp);
    pthread_mutex_unlock(&user->mtx);

    /* Atomically rename */
    if (rename(temp_path, file_path) != 0)
    {
        return -1;
    }

    return 0;
}

int user_load_metadata(UserMetadata *user, const char *username)
{
    if (!user || !username)
        return -1;

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "storage/%s/metadata.txt", username);

    FILE *fp = fopen(file_path, "r");
    if (!fp)
        return -1;  // File doesn't exist (new user)

    pthread_mutex_lock(&user->mtx);

    char line[1024];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "username=", 9) == 0)
        {
            sscanf(line, "username=%63s", user->username);
        }
        else if (strncmp(line, "password_hash=", 14) == 0)
        {
            sscanf(line, "password_hash=%64s", user->password_hash);
        }
        else if (strncmp(line, "quota_used=", 11) == 0)
        {
            sscanf(line, "quota_used=%zu", &user->quota_used);
        }
        else if (strncmp(line, "quota_limit=", 12) == 0)
        {
            sscanf(line, "quota_limit=%zu", &user->quota_limit);
        }
        else if (strncmp(line, "file_count=", 11) == 0)
        {
            sscanf(line, "file_count=%d", &user->file_count);
        }
        else if (strncmp(line, "file=", 5) == 0)
        {
            char filename[256];
            size_t size;
            long timestamp;
            if (sscanf(line, "file=%255[^,],%zu,%ld", filename, &size, &timestamp) == 3)
            {
                if (user->file_count < user->file_capacity)
                {
                    int idx = user->file_count;
                    strncpy(user->files[idx].filename, filename, 255);
                    user->files[idx].size = size;
                    user->files[idx].timestamp = (time_t)timestamp;
                }
            }
        }
    }

    user->loaded = true;
    fclose(fp);
    pthread_mutex_unlock(&user->mtx);

    return 0;
}

void user_free(UserMetadata *user)
{
    if (!user)
        return;

    pthread_mutex_destroy(&user->mtx);
    free(user->files);
    free(user);
}
