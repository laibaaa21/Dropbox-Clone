#include "file_locks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global file lock manager instance */
FileLockManager global_file_lock_manager;

/* Simple hash function for filepath */
static unsigned int hash_filepath(const char *filepath, int capacity)
{
    unsigned int hash = 5381;
    int c;

    while ((c = *filepath++))
    {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash % capacity;
}

/* Initialize the file lock manager */
int file_lock_manager_init(FileLockManager *manager, int capacity)
{
    if (!manager || capacity <= 0)
        return -1;

    manager->capacity = capacity;

    /* Initialize all lock slots as unused */
    for (int i = 0; i < capacity; i++)
    {
        manager->locks[i].filepath[0] = '\0';
        manager->locks[i].ref_count = 0;
        manager->locks[i].in_use = false;

        /* Initialize the file-specific mutex */
        if (pthread_mutex_init(&manager->locks[i].mtx, NULL) != 0)
        {
            /* Clean up previously initialized mutexes */
            for (int j = 0; j < i; j++)
            {
                pthread_mutex_destroy(&manager->locks[j].mtx);
            }
            return -1;
        }
    }

    /* Initialize the global manager mutex */
    if (pthread_mutex_init(&manager->manager_mtx, NULL) != 0)
    {
        /* Clean up all file mutexes */
        for (int i = 0; i < capacity; i++)
        {
            pthread_mutex_destroy(&manager->locks[i].mtx);
        }
        return -1;
    }

    printf("[FileLockManager] Initialized with capacity %d\n", capacity);
    return 0;
}

/* Destroy the file lock manager */
void file_lock_manager_destroy(FileLockManager *manager)
{
    if (!manager)
        return;

    /* Destroy all lock mutexes */
    for (int i = 0; i < manager->capacity; i++)
    {
        pthread_mutex_destroy(&manager->locks[i].mtx);
    }

    /* Destroy the global manager mutex */
    pthread_mutex_destroy(&manager->manager_mtx);

    printf("[FileLockManager] Destroyed\n");
}

/* Acquire a file lock (creates if doesn't exist) */
FileLock *file_lock_acquire(FileLockManager *manager, const char *username, const char *filename)
{
    if (!manager || !username || !filename)
        return NULL;

    /* Build filepath key: "username/filename" */
    char filepath[MAX_FILEPATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s", username, filename);

    /* Lock the global manager */
    pthread_mutex_lock(&manager->manager_mtx);

    /* Look for existing lock or empty slot */
    unsigned int hash = hash_filepath(filepath, manager->capacity);
    unsigned int index = hash;
    FileLock *lock = NULL;
    int attempts = 0;

    while (attempts < manager->capacity)
    {
        if (manager->locks[index].in_use && strcmp(manager->locks[index].filepath, filepath) == 0)
        {
            /* Found existing lock for this file */
            lock = &manager->locks[index];
            lock->ref_count++;
            break;
        }
        else if (!manager->locks[index].in_use)
        {
            /* Found empty slot - create new lock */
            lock = &manager->locks[index];
            strncpy(lock->filepath, filepath, MAX_FILEPATH_LEN - 1);
            lock->filepath[MAX_FILEPATH_LEN - 1] = '\0';
            lock->ref_count = 1;
            lock->in_use = true;
            break;
        }

        /* Linear probing */
        index = (index + 1) % manager->capacity;
        attempts++;
    }

    /* Unlock the global manager */
    pthread_mutex_unlock(&manager->manager_mtx);

    if (!lock)
    {
        fprintf(stderr, "[FileLockManager] ERROR: Lock table full (capacity=%d)\n", manager->capacity);
        return NULL;
    }

    /* Acquire the file-specific lock */
    pthread_mutex_lock(&lock->mtx);

    printf("[FileLockManager] Acquired lock for '%s' (ref_count=%d)\n", filepath, lock->ref_count);

    return lock;
}

/* Release a file lock */
void file_lock_release(FileLockManager *manager, FileLock *file_lock)
{
    if (!manager || !file_lock)
        return;

    char filepath_copy[MAX_FILEPATH_LEN];
    strncpy(filepath_copy, file_lock->filepath, MAX_FILEPATH_LEN - 1);
    filepath_copy[MAX_FILEPATH_LEN - 1] = '\0';

    /* Unlock the file-specific mutex first */
    pthread_mutex_unlock(&file_lock->mtx);

    /* Lock the global manager */
    pthread_mutex_lock(&manager->manager_mtx);

    /* Decrement ref_count */
    file_lock->ref_count--;

    /* If no more references, mark slot as unused */
    if (file_lock->ref_count <= 0)
    {
        file_lock->in_use = false;
        file_lock->filepath[0] = '\0';
        printf("[FileLockManager] Released lock for '%s' (freed)\n", filepath_copy);
    }
    else
    {
        printf("[FileLockManager] Released lock for '%s' (ref_count=%d)\n", filepath_copy, file_lock->ref_count);
    }

    /* Unlock the global manager */
    pthread_mutex_unlock(&manager->manager_mtx);
}
