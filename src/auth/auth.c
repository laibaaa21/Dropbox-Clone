#include "auth.h"
#include "user_metadata.h"
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
    {
        fprintf(stderr, "[Auth] Invalid parameters for signup\n");
        return -1;
    }

    /* Check if user already exists */
    if (user_exists(username))
    {
        printf("[Auth] Signup failed: User '%s' already exists\n", username);
        return -2;  /* User already exists */
    }

    /* Hash password */
    char password_hash[MAX_PASSWORD_HASH_LEN];
    hash_password(password, password_hash);

    /* Create user in database */
    int result = user_create(username, password_hash);

    if (result == 0)
    {
        printf("[Auth] User '%s' signed up successfully\n", username);
    }
    else if (result == -2)
    {
        /* Rare case: user was created between exists check and create */
        printf("[Auth] Signup failed: User '%s' already exists\n", username);
    }
    else
    {
        fprintf(stderr, "[Auth] Signup failed for user '%s': database error\n", username);
    }

    return result;
}

int user_login(const char *username, const char *password)
{
    if (!username || !password)
    {
        fprintf(stderr, "[Auth] Invalid parameters for login\n");
        return -1;
    }

    /* Hash provided password */
    char password_hash[MAX_PASSWORD_HASH_LEN];
    hash_password(password, password_hash);

    /* Verify password in database */
    int result = user_verify_password(username, password_hash);

    if (result == 0)
    {
        printf("[Auth] User '%s' logged in successfully\n", username);
    }
    else if (result == -2)
    {
        printf("[Auth] Login failed: User '%s' not found\n", username);
    }
    else if (result == -3)
    {
        printf("[Auth] Login failed: Invalid password for user '%s'\n", username);
    }
    else
    {
        fprintf(stderr, "[Auth] Login failed for user '%s': database error\n", username);
    }

    return result;
}
