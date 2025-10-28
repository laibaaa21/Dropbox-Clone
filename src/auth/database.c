#include "database.h"
#include "user_metadata.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* Global database connection (thread-safe with FULLMUTEX mode) */
static sqlite3 *db = NULL;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Database schema */
static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username TEXT UNIQUE NOT NULL,"
    "  password_hash TEXT NOT NULL,"
    "  quota_used INTEGER DEFAULT 0,"
    "  quota_limit INTEGER DEFAULT 104857600,"
    "  created_at INTEGER DEFAULT (strftime('%s', 'now'))"
    ");"
    ""
    "CREATE TABLE IF NOT EXISTS files ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  user_id INTEGER NOT NULL,"
    "  filename TEXT NOT NULL,"
    "  size INTEGER NOT NULL,"
    "  timestamp INTEGER DEFAULT (strftime('%s', 'now')),"
    "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,"
    "  UNIQUE(user_id, filename)"
    ");"
    ""
    "CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);"
    "CREATE INDEX IF NOT EXISTS idx_files_user_id ON files(user_id);"
    "CREATE INDEX IF NOT EXISTS idx_files_composite ON files(user_id, filename);";

int db_init(const char *db_path)
{
    if (!db_path)
    {
        fprintf(stderr, "[Database] NULL database path\n");
        return -1;
    }

    pthread_mutex_lock(&db_mutex);

    /* Open database with full mutex (thread-safe) */
    int rc = sqlite3_open_v2(db_path, &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             NULL);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Cannot open database: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* Enable WAL mode for better concurrency */
    char *err_msg = NULL;
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] WAL mode failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        /* Continue anyway - not critical */
    }

    /* Execute schema */
    rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Schema creation failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        db = NULL;
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    pthread_mutex_unlock(&db_mutex);
    printf("[Database] Initialized successfully at %s\n", db_path);
    return 0;
}

void db_close(void)
{
    pthread_mutex_lock(&db_mutex);
    if (db)
    {
        sqlite3_close(db);
        db = NULL;
        printf("[Database] Closed successfully\n");
    }
    pthread_mutex_unlock(&db_mutex);
}

sqlite3* db_get_connection(void)
{
    return db;
}

int db_create_user(const char *username, const char *password_hash)
{
    if (!db || !username || !password_hash)
        return -1;

    const char *sql = "INSERT INTO users (username, password_hash) VALUES (?, ?)";

    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (create_user): %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password_hash, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);

    if (rc == SQLITE_DONE)
    {
        printf("[Database] User '%s' created\n", username);
        return 0;
    }
    else if (rc == SQLITE_CONSTRAINT)
    {
        /* User already exists */
        return -2;
    }
    else
    {
        fprintf(stderr, "[Database] Insert failed (create_user): %s\n", sqlite3_errmsg(db));
        return -1;
    }
}

int db_user_exists(const char *username, bool *exists)
{
    if (!db || !username || !exists)
        return -1;

    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1";

    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (user_exists): %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    *exists = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return 0;
}

int db_verify_password(const char *username, const char *password_hash, bool *valid)
{
    if (!db || !username || !password_hash || !valid)
        return -1;

    const char *sql = "SELECT password_hash FROM users WHERE username = ?";

    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (verify_password): %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        const char *stored_hash = (const char*)sqlite3_column_text(stmt, 0);
        *valid = (strcmp(stored_hash, password_hash) == 0);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    else
    {
        /* User not found */
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -2;
    }
}

int db_get_user_quota(const char *username, size_t *quota_used, size_t *quota_limit)
{
    if (!db || !username || !quota_used || !quota_limit)
        return -1;

    const char *sql = "SELECT quota_used, quota_limit FROM users WHERE username = ?";

    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (get_user_quota): %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        *quota_used = sqlite3_column_int64(stmt, 0);
        *quota_limit = sqlite3_column_int64(stmt, 1);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    else
    {
        /* User not found */
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -2;
    }
}

int db_add_or_update_file(const char *username, const char *filename, size_t size)
{
    if (!db || !username || !filename)
        return -1;

    pthread_mutex_lock(&db_mutex);

    /* Begin transaction for atomicity */
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] BEGIN failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* Get user_id */
    const char *sql_get_id = "SELECT id FROM users WHERE username = ?";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql_get_id, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (get user_id): %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW)
    {
        fprintf(stderr, "[Database] User not found: %s\n", username);
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -2;
    }

    int user_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    /* Insert or replace file */
    const char *sql_upsert =
        "INSERT INTO files (user_id, filename, size, timestamp) "
        "VALUES (?, ?, ?, strftime('%s', 'now')) "
        "ON CONFLICT(user_id, filename) DO UPDATE SET "
        "size = excluded.size, timestamp = excluded.timestamp";

    rc = sqlite3_prepare_v2(db, sql_upsert, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (upsert file): %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, size);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "[Database] File upsert failed: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* Update quota_used */
    const char *sql_quota =
        "UPDATE users SET quota_used = "
        "(SELECT COALESCE(SUM(size), 0) FROM files WHERE user_id = ?) "
        "WHERE id = ?";

    rc = sqlite3_prepare_v2(db, sql_quota, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (update quota): %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, user_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "[Database] Quota update failed: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* Commit transaction */
    rc = sqlite3_exec(db, "COMMIT", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] COMMIT failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    pthread_mutex_unlock(&db_mutex);
    return 0;
}

int db_remove_file(const char *username, const char *filename)
{
    if (!db || !username || !filename)
        return -1;

    pthread_mutex_lock(&db_mutex);

    /* Begin transaction */
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] BEGIN failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* Get user_id */
    const char *sql_get_id = "SELECT id FROM users WHERE username = ?";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql_get_id, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (get user_id): %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW)
    {
        fprintf(stderr, "[Database] User not found: %s\n", username);
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -2;
    }

    int user_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    /* Delete file */
    const char *sql_delete = "DELETE FROM files WHERE user_id = ? AND filename = ?";
    rc = sqlite3_prepare_v2(db, sql_delete, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (delete file): %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "[Database] File delete failed: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    if (changes == 0)
    {
        /* File not found */
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -3;
    }

    /* Update quota_used */
    const char *sql_quota =
        "UPDATE users SET quota_used = "
        "(SELECT COALESCE(SUM(size), 0) FROM files WHERE user_id = ?) "
        "WHERE id = ?";

    rc = sqlite3_prepare_v2(db, sql_quota, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (update quota): %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, user_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "[Database] Quota update failed: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* Commit transaction */
    rc = sqlite3_exec(db, "COMMIT", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] COMMIT failed: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    pthread_mutex_unlock(&db_mutex);
    return 0;
}

int db_get_file_size(const char *username, const char *filename, size_t *size)
{
    if (!db || !username || !filename || !size)
        return -1;

    const char *sql =
        "SELECT f.size FROM files f "
        "JOIN users u ON f.user_id = u.id "
        "WHERE u.username = ? AND f.filename = ?";

    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (get_file_size): %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        *size = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    else
    {
        /* File not found */
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -2;
    }
}

int db_check_quota(const char *username, size_t additional_bytes, bool *has_quota)
{
    if (!db || !username || !has_quota)
        return -1;

    size_t quota_used, quota_limit;
    int result = db_get_user_quota(username, &quota_used, &quota_limit);

    if (result != 0)
        return result;

    *has_quota = (quota_used + additional_bytes) <= quota_limit;
    return 0;
}

int db_update_user_quota(const char *username)
{
    if (!db || !username)
        return -1;

    const char *sql =
        "UPDATE users SET quota_used = "
        "(SELECT COALESCE(SUM(size), 0) FROM files WHERE user_id = users.id) "
        "WHERE username = ?";

    pthread_mutex_lock(&db_mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[Database] Prepare failed (update_user_quota): %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&db_mutex);

    return (rc == SQLITE_DONE) ? 0 : -1;
}
