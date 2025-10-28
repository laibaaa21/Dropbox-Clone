#ifndef USER_METADATA_H
#define USER_METADATA_H

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_HASH_LEN 65  // SHA256 hex + null terminator
#define MAX_FILES_PER_USER 1024
#define DEFAULT_QUOTA_LIMIT (100 * 1024 * 1024)  // 100MB

/* File metadata structure */
typedef struct FileMetadata
{
    char filename[256];
    size_t size;
    time_t timestamp;
} FileMetadata;

/* User metadata structure */
typedef struct UserMetadata
{
    char username[MAX_USERNAME_LEN];
    char password_hash[MAX_PASSWORD_HASH_LEN];
    FileMetadata *files;      // Dynamic array of files
    int file_count;
    int file_capacity;
    size_t quota_used;        // Bytes used
    size_t quota_limit;       // Bytes allowed
    pthread_mutex_t mtx;      // Per-user lock (for Phase 2)
    bool loaded;              // Metadata loaded from disk
} UserMetadata;

/* User database (thread-safe hash map) */
typedef struct UserDatabase
{
    UserMetadata **users;     // Array of user pointers (simple hash table)
    int capacity;             // Hash table size
    int count;                // Number of users
    pthread_mutex_t mtx;      // Protects user database
} UserDatabase;

/* Initialize user database */
int user_database_init(UserDatabase *db, int capacity);

/* Destroy user database */
void user_database_destroy(UserDatabase *db);

/* Create new user (for signup) */
UserMetadata *user_create(const char *username, const char *password_hash);

/* Get user by username */
UserMetadata *user_get(UserDatabase *db, const char *username);

/* Add user to database */
int user_database_add(UserDatabase *db, UserMetadata *user);

/* Add file to user's file list */
int user_add_file(UserMetadata *user, const char *filename, size_t size);

/* Remove file from user's file list */
int user_remove_file(UserMetadata *user, const char *filename);

/* Check if user has enough quota */
bool user_check_quota(UserMetadata *user, size_t additional_bytes);

/* Save user metadata to disk */
int user_save_metadata(UserMetadata *user);

/* Load user metadata from disk */
int user_load_metadata(UserMetadata *user, const char *username);

/* Free user metadata structure */
void user_free(UserMetadata *user);

/* Global user database */
extern UserDatabase global_user_db;

#endif /* USER_METADATA_H */
