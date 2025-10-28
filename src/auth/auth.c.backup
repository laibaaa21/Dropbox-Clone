#include "auth.h"
#include <string.h>
#include <stdio.h>
#include <openssl/sha.h>

void hash_password(const char *password, char *output_hash)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)password, strlen(password), hash);

    /* Convert to hex string */
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(output_hash + (i * 2), "%02x", hash[i]);
    }
    output_hash[64] = '\0';
}

int user_signup(const char *username, const char *password)
{
    if (!username || !password)
        return -1;

    /* Check if user already exists */
    UserMetadata *existing = user_get(&global_user_db, username);
    if (existing)
    {
        return -2;  // User already exists
    }

    /* Hash password */
    char password_hash[MAX_PASSWORD_HASH_LEN];
    hash_password(password, password_hash);

    /* Create user */
    UserMetadata *user = user_create(username, password_hash);
    if (!user)
        return -1;

    /* Add to database */
    if (user_database_add(&global_user_db, user) != 0)
    {
        user_free(user);
        return -1;
    }

    /* Save to disk */
    user_save_metadata(user);

    printf("[Auth] User '%s' signed up successfully\n", username);
    return 0;
}

int user_login(const char *username, const char *password)
{
    if (!username || !password)
        return -1;

    /* Get user from database */
    UserMetadata *user = user_get(&global_user_db, username);
    if (!user)
    {
        /* Try to load from disk */
        user = user_create(username, "");
        if (!user)
            return -1;

        if (user_load_metadata(user, username) != 0)
        {
            user_free(user);
            return -2;  // User not found
        }

        /* Add to in-memory database */
        if (user_database_add(&global_user_db, user) != 0)
        {
            user_free(user);
            return -1;
        }
    }

    /* Hash provided password */
    char password_hash[MAX_PASSWORD_HASH_LEN];
    hash_password(password, password_hash);

    /* Compare hashes */
    if (strcmp(user->password_hash, password_hash) != 0)
    {
        return -3;  // Invalid password
    }

    printf("[Auth] User '%s' logged in successfully\n", username);
    return 0;
}
