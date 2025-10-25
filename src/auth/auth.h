#ifndef AUTH_H
#define AUTH_H

#include "user_metadata.h"

/* Hash password using SHA256 */
void hash_password(const char *password, char *output_hash);

/* Signup new user */
int user_signup(const char *username, const char *password);

/* Login existing user */
int user_login(const char *username, const char *password);

#endif /* AUTH_H */
