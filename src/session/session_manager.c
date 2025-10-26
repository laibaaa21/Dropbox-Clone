#include "session_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

/* Hash function for session IDs */
static uint32_t hash_session_id(uint64_t session_id)
{
    return (uint32_t)(session_id % MAX_SESSIONS);
}

/* -------------------- SessionManager Functions -------------------- */

int session_manager_init(SessionManager *mgr)
{
    if (!mgr)
        return -1;

    /* Initialize session array to NULL */
    memset(mgr->sessions, 0, sizeof(mgr->sessions));

    /* Initialize manager mutex */
    if (pthread_mutex_init(&mgr->manager_mtx, NULL) != 0)
    {
        perror("[SessionManager] Failed to init mutex");
        return -1;
    }

    /* Initialize session ID counter (start from 1, 0 is reserved for errors) */
    mgr->next_session_id = 1;

    /* Initialize statistics (Phase 2.9) */
    mgr->total_sessions_created = 0;
    mgr->active_session_count = 0;
    mgr->peak_session_count = 0;

    printf("[SessionManager] Initialized (max sessions: %d)\n", MAX_SESSIONS);
    return 0;
}

void session_manager_destroy(SessionManager *mgr)
{
    if (!mgr)
        return;

    pthread_mutex_lock(&mgr->manager_mtx);

    /* Clean up all remaining sessions (Phase 2.7: Enhanced cleanup) */
    int active_sessions = 0;
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (mgr->sessions[i])
        {
            Session *session = mgr->sessions[i];
            active_sessions++;

            /* Mark inactive and destroy response */
            session->is_active = false;
            response_destroy(&session->response);
            pthread_mutex_destroy(&session->session_mtx);

            /* Close socket if still open */
            if (session->socket_fd >= 0)
            {
                printf("[SessionManager] Closing socket %d for session %lu\n",
                       session->socket_fd, session->session_id);
                shutdown(session->socket_fd, SHUT_RDWR);
                close(session->socket_fd);
                session->socket_fd = -1;
            }

            free(session);
            mgr->sessions[i] = NULL;
        }
    }

    pthread_mutex_unlock(&mgr->manager_mtx);
    pthread_mutex_destroy(&mgr->manager_mtx);

    printf("[SessionManager] Destroyed (%d active sessions cleaned up)\n", active_sessions);
}

uint64_t session_create(SessionManager *mgr, int socket_fd)
{
    if (!mgr || socket_fd < 0)
        return 0;

    /* Allocate session structure */
    Session *session = (Session *)calloc(1, sizeof(Session));
    if (!session)
    {
        perror("[SessionManager] Failed to allocate session");
        return 0;
    }

    pthread_mutex_lock(&mgr->manager_mtx);

    /* Generate unique session ID */
    uint64_t session_id = mgr->next_session_id++;
    
    /* Find slot in hash table (linear probing) */
    uint32_t index = hash_session_id(session_id);
    uint32_t probe_count = 0;
    
    while (mgr->sessions[index] != NULL && probe_count < MAX_SESSIONS)
    {
        index = (index + 1) % MAX_SESSIONS;
        probe_count++;
    }

    if (probe_count >= MAX_SESSIONS)
    {
        /* Table is full */
        pthread_mutex_unlock(&mgr->manager_mtx);
        free(session);
        fprintf(stderr, "[SessionManager] Session table full\n");
        return 0;
    }

    /* Initialize session */
    session->session_id = session_id;
    session->socket_fd = socket_fd;
    session->is_authenticated = false;
    session->is_active = true;
    memset(session->username, 0, sizeof(session->username));

    /* Initialize session lifetime tracking (Phase 2.9) */
    time_t now = time(NULL);
    session->created_at = now;
    session->authenticated_at = 0;
    session->last_activity = now;
    session->operations_count = 0;

    /* Initialize response structure */
    if (response_init(&session->response) != 0)
    {
        pthread_mutex_unlock(&mgr->manager_mtx);
        free(session);
        fprintf(stderr, "[SessionManager] Failed to init response\n");
        return 0;
    }

    /* Initialize per-session mutex */
    if (pthread_mutex_init(&session->session_mtx, NULL) != 0)
    {
        pthread_mutex_unlock(&mgr->manager_mtx);
        response_destroy(&session->response);
        free(session);
        fprintf(stderr, "[SessionManager] Failed to init session mutex\n");
        return 0;
    }

    /* Insert into hash table */
    mgr->sessions[index] = session;

    /* Update statistics (Phase 2.9) */
    mgr->total_sessions_created++;
    mgr->active_session_count++;
    if (mgr->active_session_count > mgr->peak_session_count)
    {
        mgr->peak_session_count = mgr->active_session_count;
    }

    pthread_mutex_unlock(&mgr->manager_mtx);

    printf("[SessionManager] Created session %lu (fd=%d, slot=%u, active=%lu/%lu peak)\n",
           session_id, socket_fd, index, mgr->active_session_count, mgr->peak_session_count);

    return session_id;
}

Session *session_get(SessionManager *mgr, uint64_t session_id)
{
    if (!mgr || session_id == 0)
        return NULL;

    pthread_mutex_lock(&mgr->manager_mtx);

    /* Search hash table (linear probing) */
    uint32_t index = hash_session_id(session_id);
    uint32_t probe_count = 0;

    while (probe_count < MAX_SESSIONS)
    {
        Session *session = mgr->sessions[index];
        
        if (session == NULL)
        {
            /* Empty slot, session not found */
            break;
        }
        
        if (session->session_id == session_id)
        {
            /* Found the session, check if active */
            if (session->is_active)
            {
                pthread_mutex_unlock(&mgr->manager_mtx);
                return session;
            }
            else
            {
                /* Session exists but is inactive */
                pthread_mutex_unlock(&mgr->manager_mtx);
                return NULL;
            }
        }
        
        index = (index + 1) % MAX_SESSIONS;
        probe_count++;
    }

    pthread_mutex_unlock(&mgr->manager_mtx);
    return NULL;
}

void session_mark_inactive(SessionManager *mgr, uint64_t session_id)
{
    if (!mgr || session_id == 0)
        return;

    pthread_mutex_lock(&mgr->manager_mtx);

    /* Find session in hash table */
    uint32_t index = hash_session_id(session_id);
    uint32_t probe_count = 0;

    while (probe_count < MAX_SESSIONS)
    {
        Session *session = mgr->sessions[index];
        
        if (session == NULL)
        {
            break;
        }
        
        if (session->session_id == session_id)
        {
            /* Mark session as inactive */
            session->is_active = false;
            
            printf("[SessionManager] Session %lu marked inactive\n", session_id);
            pthread_mutex_unlock(&mgr->manager_mtx);
            return;
        }
        
        index = (index + 1) % MAX_SESSIONS;
        probe_count++;
    }

    pthread_mutex_unlock(&mgr->manager_mtx);
}

void session_destroy(SessionManager *mgr, uint64_t session_id)
{
    if (!mgr || session_id == 0)
        return;

    pthread_mutex_lock(&mgr->manager_mtx);

    /* Find session in hash table */
    uint32_t index = hash_session_id(session_id);
    uint32_t probe_count = 0;

    while (probe_count < MAX_SESSIONS)
    {
        Session *session = mgr->sessions[index];
        
        if (session == NULL)
        {
            break;
        }
        
        if (session->session_id == session_id)
        {
            /* Mark inactive first */
            session->is_active = false;
            
            /* Destroy response (frees any data) */
            response_destroy(&session->response);
            
            /* Destroy session mutex */
            pthread_mutex_destroy(&session->session_mtx);
            
            /* Close socket if not already closed */
            if (session->socket_fd >= 0)
            {
                close(session->socket_fd);
                session->socket_fd = -1;
            }
            
            /* Update statistics (Phase 2.9) */
            if (mgr->active_session_count > 0)
            {
                mgr->active_session_count--;
            }

            /* Free session and remove from table */
            free(session);
            mgr->sessions[index] = NULL;

            printf("[SessionManager] Session %lu destroyed (active=%lu)\n",
                   session_id, mgr->active_session_count);
            pthread_mutex_unlock(&mgr->manager_mtx);
            return;
        }
        
        index = (index + 1) % MAX_SESSIONS;
        probe_count++;
    }

    pthread_mutex_unlock(&mgr->manager_mtx);
}

void session_set_username(Session *session, const char *username)
{
    if (!session || !username)
        return;

    pthread_mutex_lock(&session->session_mtx);

    strncpy(session->username, username, MAX_USERNAME_LEN - 1);
    session->username[MAX_USERNAME_LEN - 1] = '\0';
    session->is_authenticated = true;

    /* Record authentication timestamp (Phase 2.9) */
    session->authenticated_at = time(NULL);
    session->last_activity = session->authenticated_at;

    pthread_mutex_unlock(&session->session_mtx);

    printf("[SessionManager] Session %lu authenticated as '%s'\n",
           session->session_id, username);
}

/* -------------------- Phase 2.9 Enhancement Functions -------------------- */

void session_update_activity(Session *session)
{
    if (!session)
        return;

    pthread_mutex_lock(&session->session_mtx);
    session->last_activity = time(NULL);
    pthread_mutex_unlock(&session->session_mtx);
}

void session_increment_operations(Session *session)
{
    if (!session)
        return;

    pthread_mutex_lock(&session->session_mtx);
    session->operations_count++;
    session->last_activity = time(NULL);
    pthread_mutex_unlock(&session->session_mtx);
}

void session_get_statistics(SessionManager *mgr, uint64_t *active_count,
                           uint64_t *total_created, uint64_t *peak_count)
{
    if (!mgr)
        return;

    pthread_mutex_lock(&mgr->manager_mtx);

    if (active_count)
        *active_count = mgr->active_session_count;
    if (total_created)
        *total_created = mgr->total_sessions_created;
    if (peak_count)
        *peak_count = mgr->peak_session_count;

    pthread_mutex_unlock(&mgr->manager_mtx);
}

void session_print_active(SessionManager *mgr)
{
    if (!mgr)
        return;

    pthread_mutex_lock(&mgr->manager_mtx);

    printf("\n[SessionManager] ========== Active Sessions ==========\n");
    printf("[SessionManager] Total created: %lu, Active: %lu, Peak: %lu\n",
           mgr->total_sessions_created, mgr->active_session_count, mgr->peak_session_count);
    printf("[SessionManager] ------------------------------------------------\n");

    time_t now = time(NULL);
    int count = 0;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        Session *session = mgr->sessions[i];
        if (session && session->is_active)
        {
            count++;
            pthread_mutex_lock(&session->session_mtx);

            time_t session_age = now - session->created_at;
            time_t idle_time = now - session->last_activity;

            printf("[SessionManager] Session %lu:\n", session->session_id);
            printf("                 User: %s (auth: %s)\n",
                   session->is_authenticated ? session->username : "<not authenticated>",
                   session->is_authenticated ? "yes" : "no");
            printf("                 Socket: %d, Active: %s\n",
                   session->socket_fd, session->is_active ? "yes" : "no");
            printf("                 Age: %lds, Idle: %lds, Operations: %lu\n",
                   (long)session_age, (long)idle_time, session->operations_count);

            pthread_mutex_unlock(&session->session_mtx);
        }
    }

    printf("[SessionManager] ================================================\n");
    printf("[SessionManager] Total active sessions listed: %d\n\n", count);

    pthread_mutex_unlock(&mgr->manager_mtx);
}

bool session_is_idle(Session *session, time_t timeout_seconds)
{
    if (!session)
        return true;

    pthread_mutex_lock(&session->session_mtx);

    time_t now = time(NULL);
    time_t idle_time = now - session->last_activity;
    bool is_idle = (idle_time > timeout_seconds);

    pthread_mutex_unlock(&session->session_mtx);

    return is_idle;
}

