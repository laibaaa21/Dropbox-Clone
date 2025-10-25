#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "response_queue.h"

#define MAX_USERNAME_LEN 64
#define MAX_SESSIONS 256

/* Forward declarations */
typedef struct Session Session;
typedef struct SessionManager SessionManager;

/**
 * Session structure - represents a single client connection
 * Contains all state needed for worker→client communication
 */
typedef struct Session
{
    uint64_t session_id;                 /* Unique session identifier */
    int socket_fd;                        /* Client socket descriptor */
    char username[MAX_USERNAME_LEN];      /* Authenticated username (empty until auth) */
    bool is_authenticated;                /* Authentication status */
    volatile bool is_active;              /* Session active flag (checked by workers) */
    Response response;                    /* Response structure for this session */
    pthread_mutex_t session_mtx;          /* Mutex for per-session operations */
} Session;

/**
 * SessionManager structure - manages all active sessions
 * Thread-safe hash table mapping session_id → Session*
 */
typedef struct SessionManager
{
    Session *sessions[MAX_SESSIONS];      /* Hash table of sessions */
    pthread_mutex_t manager_mtx;          /* Protects the session map */
    uint64_t next_session_id;             /* Counter for session ID generation */
} SessionManager;

/* -------------------- Function Prototypes -------------------- */

/**
 * Initialize the session manager
 * Returns 0 on success, -1 on error
 */
int session_manager_init(SessionManager *mgr);

/**
 * Destroy the session manager and clean up all sessions
 */
void session_manager_destroy(SessionManager *mgr);

/**
 * Create a new session for a client connection
 * @param mgr Session manager
 * @param socket_fd Client socket descriptor
 * @return session_id on success, 0 on error
 */
uint64_t session_create(SessionManager *mgr, int socket_fd);

/**
 * Get a session by ID (thread-safe)
 * @param mgr Session manager
 * @param session_id Session ID to look up
 * @return Session pointer if found and active, NULL otherwise
 * 
 * IMPORTANT: Caller must hold reference while using the session.
 * The session remains valid as long as is_active is true.
 */
Session *session_get(SessionManager *mgr, uint64_t session_id);

/**
 * Mark a session as inactive (client disconnected)
 * This signals to workers that they should not deliver results
 * @param mgr Session manager
 * @param session_id Session ID to mark inactive
 */
void session_mark_inactive(SessionManager *mgr, uint64_t session_id);

/**
 * Destroy and remove a session from the manager
 * This should be called after the client thread finishes
 * @param mgr Session manager
 * @param session_id Session ID to destroy
 */
void session_destroy(SessionManager *mgr, uint64_t session_id);

/**
 * Set authenticated username for a session
 * @param session Session pointer
 * @param username Username to set
 */
void session_set_username(Session *session, const char *username);

#endif /* SESSION_MANAGER_H */
