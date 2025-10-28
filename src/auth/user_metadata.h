#ifndef USER_METADATA_H
#define USER_METADATA_H

#include <stddef.h>
#include <stdbool.h>

#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_HASH_LEN 65  // SHA256 hex + null terminator
#define DEFAULT_QUOTA_LIMIT (100 * 1024 * 1024)  // 100MB

/* Initialize user metadata system (initializes database) */
int user_metadata_init(const char *db_path);

/* Cleanup user metadata system (closes database) */
void user_metadata_cleanup(void);

/* Create new user */
int user_create(const char *username, const char *password_hash);

/* Check if user exists */
bool user_exists(const char *username);

/* Verify user password */
int user_verify_password(const char *username, const char *password_hash);

/* Check if user has enough quota for additional bytes */
bool user_check_quota(const char *username, size_t additional_bytes);

/* Add or update file in user's metadata */
int user_add_file(const char *username, const char *filename, size_t size);

/* Remove file from user's metadata */
int user_remove_file(const char *username, const char *filename);

/* Get file size */
int user_get_file_size(const char *username, const char *filename, size_t *size);

/* Get user quota information */
int user_get_quota(const char *username, size_t *quota_used, size_t *quota_limit);

#endif /* USER_METADATA_H */
