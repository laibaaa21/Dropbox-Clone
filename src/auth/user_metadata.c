#include "user_metadata.h"
#include "database.h"
#include <stdio.h>
#include <string.h>

int user_metadata_init(const char *db_path)
{
    if (!db_path)
    {
        fprintf(stderr, "[UserMetadata] NULL database path\n");
        return -1;
    }

    int result = db_init(db_path);
    if (result == 0)
    {
        printf("[UserMetadata] Initialized with database: %s\n", db_path);
    }
    else
    {
        fprintf(stderr, "[UserMetadata] Failed to initialize database\n");
    }

    return result;
}

void user_metadata_cleanup(void)
{
    db_close();
    printf("[UserMetadata] Cleanup complete\n");
}

int user_create(const char *username, const char *password_hash)
{
    if (!username || !password_hash)
    {
        fprintf(stderr, "[UserMetadata] Invalid parameters for user_create\n");
        return -1;
    }

    int result = db_create_user(username, password_hash);

    if (result == 0)
    {
        printf("[UserMetadata] User '%s' created successfully\n", username);
    }
    else if (result == -2)
    {
        fprintf(stderr, "[UserMetadata] User '%s' already exists\n", username);
    }
    else
    {
        fprintf(stderr, "[UserMetadata] Failed to create user '%s'\n", username);
    }

    return result;
}

bool user_exists(const char *username)
{
    if (!username)
        return false;

    bool exists = false;
    int result = db_user_exists(username, &exists);

    if (result != 0)
    {
        fprintf(stderr, "[UserMetadata] Error checking if user '%s' exists\n", username);
        return false;
    }

    return exists;
}

int user_verify_password(const char *username, const char *password_hash)
{
    if (!username || !password_hash)
    {
        fprintf(stderr, "[UserMetadata] Invalid parameters for user_verify_password\n");
        return -1;
    }

    bool valid = false;
    int result = db_verify_password(username, password_hash, &valid);

    if (result == -2)
    {
        /* User not found */
        return -2;
    }
    else if (result != 0)
    {
        fprintf(stderr, "[UserMetadata] Error verifying password for user '%s'\n", username);
        return -1;
    }

    return valid ? 0 : -3;  /* 0 = valid, -3 = invalid password */
}

bool user_check_quota(const char *username, size_t additional_bytes)
{
    if (!username)
        return false;

    bool has_quota = false;
    int result = db_check_quota(username, additional_bytes, &has_quota);

    if (result != 0)
    {
        fprintf(stderr, "[UserMetadata] Error checking quota for user '%s'\n", username);
        return false;
    }

    return has_quota;
}

int user_add_file(const char *username, const char *filename, size_t size)
{
    if (!username || !filename)
    {
        fprintf(stderr, "[UserMetadata] Invalid parameters for user_add_file\n");
        return -1;
    }

    int result = db_add_or_update_file(username, filename, size);

    if (result == 0)
    {
        printf("[UserMetadata] File '%s' added/updated for user '%s' (%zu bytes)\n",
               filename, username, size);
    }
    else if (result == -2)
    {
        fprintf(stderr, "[UserMetadata] User '%s' not found\n", username);
    }
    else
    {
        fprintf(stderr, "[UserMetadata] Failed to add file '%s' for user '%s'\n",
                filename, username);
    }

    return result;
}

int user_remove_file(const char *username, const char *filename)
{
    if (!username || !filename)
    {
        fprintf(stderr, "[UserMetadata] Invalid parameters for user_remove_file\n");
        return -1;
    }

    int result = db_remove_file(username, filename);

    if (result == 0)
    {
        printf("[UserMetadata] File '%s' removed for user '%s'\n", filename, username);
    }
    else if (result == -2)
    {
        fprintf(stderr, "[UserMetadata] User '%s' not found\n", username);
    }
    else if (result == -3)
    {
        fprintf(stderr, "[UserMetadata] File '%s' not found for user '%s'\n", filename, username);
    }
    else
    {
        fprintf(stderr, "[UserMetadata] Failed to remove file '%s' for user '%s'\n",
                filename, username);
    }

    return result;
}

int user_get_file_size(const char *username, const char *filename, size_t *size)
{
    if (!username || !filename || !size)
    {
        fprintf(stderr, "[UserMetadata] Invalid parameters for user_get_file_size\n");
        return -1;
    }

    int result = db_get_file_size(username, filename, size);

    if (result == -2)
    {
        /* File not found - this is expected in some cases */
        return -2;
    }
    else if (result != 0)
    {
        fprintf(stderr, "[UserMetadata] Error getting file size for '%s/%s'\n",
                username, filename);
    }

    return result;
}

int user_get_quota(const char *username, size_t *quota_used, size_t *quota_limit)
{
    if (!username || !quota_used || !quota_limit)
    {
        fprintf(stderr, "[UserMetadata] Invalid parameters for user_get_quota\n");
        return -1;
    }

    int result = db_get_user_quota(username, quota_used, quota_limit);

    if (result == -2)
    {
        fprintf(stderr, "[UserMetadata] User '%s' not found\n", username);
    }
    else if (result != 0)
    {
        fprintf(stderr, "[UserMetadata] Error getting quota for user '%s'\n", username);
    }

    return result;
}
