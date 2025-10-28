#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* Initialize database and create schema */
int db_init(const char *db_path);

/* Close database connection */
void db_close(void);

/* Get thread-safe database connection */
sqlite3* db_get_connection(void);

/* User operations */
int db_create_user(const char *username, const char *password_hash);
int db_user_exists(const char *username, bool *exists);
int db_verify_password(const char *username, const char *password_hash, bool *valid);
int db_get_user_quota(const char *username, size_t *quota_used, size_t *quota_limit);

/* File operations */
int db_add_or_update_file(const char *username, const char *filename, size_t size);
int db_remove_file(const char *username, const char *filename);
int db_get_file_size(const char *username, const char *filename, size_t *size);

/* Quota operations */
int db_check_quota(const char *username, size_t additional_bytes, bool *has_quota);
int db_update_user_quota(const char *username);

#endif /* DATABASE_H */
