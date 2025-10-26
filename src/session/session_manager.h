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

    /* Session lifetime tracking (Phase 2.9 enhancements) */
    time_t created_at;                    /* Session creation timestamp */
    time_t authenticated_at;              /* Authentication timestamp (0 if not authenticated) */
    time_t last_activity;                 /* Last activity timestamp */
    uint64_t operations_count;            /* Number of operations performed */
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

    /* Session statistics (Phase 2.9 enhancements) */
    uint64_t total_sessions_created;      /* Total sessions created since start */
    uint64_t active_session_count;        /* Current number of active sessions */
    uint64_t peak_session_count;          /* Peak concurrent sessions */
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

/**
 * Update last activity timestamp for a session
 * Should be called whenever the session performs an operation
 * @param session Session pointer
 */
void session_update_activity(Session *session);

/**
 * Increment operation counter for a session
 * @param session Session pointer
 */
void session_increment_operations(Session *session);

/**
 * Get session statistics
 * @param mgr Session manager
 * @param active_count Output: current active sessions
 * @param total_created Output: total sessions created
 * @param peak_count Output: peak concurrent sessions
 */
void session_get_statistics(SessionManager *mgr, uint64_t *active_count,
                           uint64_t *total_created, uint64_t *peak_count);

/**
 * Print all active sessions (for debugging)
 * @param mgr Session manager
 */
void session_print_active(SessionManager *mgr);

/**
 * Check if session has been idle for too long
 * @param session Session pointer
 * @param timeout_seconds Idle timeout in seconds
 * @return true if session is idle beyond timeout, false otherwise
 */
bool session_is_idle(Session *session, time_t timeout_seconds);

#endif /* SESSION_MANAGER_H */
