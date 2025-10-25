#ifndef FILE_LOCKS_H
#define FILE_LOCKS_H

#include <pthread.h>
#include <stdbool.h>

/*
 * Phase 2.5: Per-File Concurrency Control
 *
 * This module provides fine-grained file-level locking to prevent conflicting
 * operations on the same file (e.g., simultaneous upload/delete).
 *
 * Design:
 * - Global hash table maps (username, filename) -> FileLock
 * - Each FileLock has a mutex and reference count
 * - Locks are created on-demand and destroyed when ref_count reaches 0
 * - Global mutex protects the lock map itself
 */

#define MAX_FILE_LOCKS 1024
#define MAX_FILEPATH_LEN 320  // username(64) + "/" + filename(256)

/* File lock structure */
typedef struct FileLock
{
    char filepath[MAX_FILEPATH_LEN];  /* Key: "username/filename" */
    pthread_mutex_t mtx;              /* Protects access to this specific file */
    int ref_count;                    /* Number of operations holding this lock */
    bool in_use;                      /* Whether this slot is occupied */
} FileLock;

/* File lock manager (global) */
typedef struct FileLockManager
{
    FileLock locks[MAX_FILE_LOCKS];   /* Array of file locks (simple hash table) */
    int capacity;                     /* Size of the array */
    pthread_mutex_t manager_mtx;      /* Protects the lock map */
} FileLockManager;

/* Initialize the file lock manager */
int file_lock_manager_init(FileLockManager *manager, int capacity);

/* Destroy the file lock manager */
void file_lock_manager_destroy(FileLockManager *manager);

/* Acquire a file lock (creates if doesn't exist)
 * Returns: pointer to the acquired FileLock, or NULL on error
 *
 * This function:
 * 1. Locks the global manager mutex
 * 2. Looks up or creates a FileLock for the given username/filename
 * 3. Increments ref_count
 * 4. Unlocks the global manager mutex
 * 5. Locks the file-specific mutex
 * 6. Returns the FileLock pointer
 */
FileLock *file_lock_acquire(FileLockManager *manager, const char *username, const char *filename);

/* Release a file lock
 *
 * This function:
 * 1. Unlocks the file-specific mutex
 * 2. Locks the global manager mutex
 * 3. Decrements ref_count
 * 4. If ref_count == 0, marks the lock slot as unused
 * 5. Unlocks the global manager mutex
 */
void file_lock_release(FileLockManager *manager, FileLock *file_lock);

/* Global file lock manager */
extern FileLockManager global_file_lock_manager;

#endif /* FILE_LOCKS_H */
