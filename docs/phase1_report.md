# Phase 1 Design Report
**Multi-threaded File-Storage Server**

---

## 1. Threadpool and Queue Architecture

### 1.1 Three-Layer Design

The system implements the required three-layer architecture:

**Layer 1 - Main/Accept Thread** (`src/main.c:205-224`):
- Listens on TCP port 10985 (configurable)
- Accepts incoming connections
- Pushes connected socket descriptors into thread-safe **ClientQueue** (capacity: 64)

**Layer 2 - Client Thread Pool** (`src/threads/client_thread.c`, 4 threads):
- Dequeues socket descriptors from ClientQueue
- Handles user authentication (SIGNUP/LOGIN with SHA256 hashing)
- Parses textual commands: UPLOAD, DOWNLOAD, DELETE, LIST
- **Does not perform file I/O directly** - packages operations as Task structures
- Pushes tasks into thread-safe **TaskQueue** (capacity: 128)
- Waits for worker response using condition variables (no busy-waiting)
- Sends responses back to client sockets

**Layer 3 - Worker Thread Pool** (`src/threads/worker_thread.c`, 4 threads):
- Dequeues tasks from TaskQueue
- Performs file I/O operations (read/write/delete files)
- Updates user metadata and quota information
- Delivers results back to client threads via session-based response mechanism

---

## 2. Queue Design and Synchronization

### 2.1 ClientQueue (Connection Queue)

**Implementation:** `src/queue/client_queue.c`

**Data Structure:** Circular buffer storing socket file descriptors

**Synchronization Primitives:**
- `pthread_mutex_t mtx` - protects queue state (head, tail, size)
- `pthread_cond_t not_empty` - signals when queue has connections (client threads wait)
- `pthread_cond_t not_full` - signals when queue has space (main thread waits)

**Operation:**
- Main thread blocks on `not_full` CV when queue is full
- Client threads block on `not_empty` CV when queue is empty
- Shutdown broadcasts both CVs to wake all threads for graceful termination

### 2.2 TaskQueue (Worker Task Queue)

**Implementation:** `src/queue/task_queue.c`

**Data Structure:** Circular buffer storing Task structures (by copy)

**Task Structure:** Contains operation type, session_id, username, filename, file data buffer

**Synchronization Primitives:**
- `pthread_mutex_t mtx` - protects queue state
- `pthread_cond_t not_empty` - signals workers when tasks available
- `pthread_cond_t not_full` - signals client threads when space available

**Operation:**
- Client threads block on `not_full` CV when queue is full
- Worker threads block on `not_empty` CV when queue is empty
- All access protected by mutex to prevent race conditions

---

## 3. Worker→Client Response Delivery (No Busy-Waiting)

**Design Choice:** Session-based response delivery with condition variables

**Implementation:** `src/session/response_queue.c`

**Response Structure:**
```c
typedef struct Response {
    response_status_t status;
    char message[512];
    void *data;
    size_t data_size;
    bool ready;                 // Signals when response is available
    pthread_mutex_t mtx;        // Protects response state
    pthread_cond_t cv;          // Client thread waits here
} Response;
```

**Flow:**

1. Each client connection has a Session structure containing an embedded Response
2. Sessions are tracked in a SessionManager hash table (capacity: 256)
3. When client thread queues a task, it includes its `session_id`
4. Client thread calls `response_wait()` which **blocks on CV** (not busy-wait)
5. Worker completes operation and calls `response_set()`:
   - Looks up session by `session_id`
   - Sets response data and status
   - Sets `ready = true`
   - **Signals CV to wake client thread**
6. Client thread wakes, reads response, sends data to socket

**Bonus Feature Achieved:** No busy-waiting - client threads use `pthread_cond_wait()` to block until response ready.

---

## 4. Authentication System

**Implementation:** `src/auth/auth.c`, `src/auth/database.c`

**Protocol:**
- SIGNUP username password - creates new user
- LOGIN username password - authenticates existing user

**Password Storage:**
- Passwords hashed using **SHA256** (OpenSSL implementation)
- Hash output: 64 hexadecimal characters
- No plaintext passwords stored

**User Database:**
- SQLite database: `storage/dropbox.db`
- Users table: username (unique), password_hash, quota_used, quota_limit
- Files table: user_id, filename (unique per user), size, timestamp
- Thread-safe: all database access protected by `db_mutex`

---

## 5. Quota Management and Metadata Persistence

**Default Quota:** 100MB per user (`DEFAULT_QUOTA_LIMIT` in `user_metadata.h`)

**Quota Enforcement:**
- Before accepting UPLOAD, client thread calls `user_check_quota(username, filesize)`
- Rejects upload with "Quota exceeded" error if insufficient space
- Quota calculation: `(current_usage + new_file_size) <= 100MB`

**Metadata Persistence:**
- SQLite database stores all user and file metadata
- Atomic transactions for metadata updates:
  1. BEGIN TRANSACTION
  2. Insert/update file record
  3. Recalculate quota_used: `SUM(size) FROM files WHERE user_id = ?`
  4. Update users.quota_used
  5. COMMIT
- WAL (Write-Ahead Logging) enabled for crash recovery
- Foreign key constraints ensure data consistency

**File Storage:**
- Files stored on disk: `storage/{username}/{filename}`
- Metadata in database synchronized with disk operations
- File deletions update both disk and database atomically

---

## 6. Synchronization Choices and Justification

**Key Design Decisions:**

1. **Condition Variables for Queues:**
   - **Choice:** Use pthread_cond_wait/signal instead of polling
   - **Justification:** Eliminates CPU waste from busy-waiting, threads sleep when no work available
   - **Implementation:** Both queues use `not_empty` and `not_full` CVs

2. **Session-based Response Delivery:**
   - **Choice:** Each session has embedded Response with CV
   - **Justification:**
     - Keeps socket I/O in client threads (architecture requirement)
     - Direct CV signaling = low latency
     - Hash table lookup by session_id = O(1) routing
   - **Alternative rejected:** Worker writing directly to socket (violates design requirement)

3. **Mutex Granularity:**
   - **Global mutexes:** ClientQueue, TaskQueue, SessionManager, database access
   - **Per-session mutexes:** Each Session has own mutex for response state
   - **Justification:** Simple lock hierarchy prevents deadlocks, sufficient for Phase 1 requirements

4. **Database as Metadata Store:**
   - **Choice:** SQLite with transactions
   - **Justification:** ACID guarantees, automatic quota calculation via SQL SUM(), handles crashes

---

## 7. Phase 1 Testing and Validation

**Test Suite:** `tests/test_phase1.sh` (11 tests, all passing)

**Scenarios Tested:**
- Server startup and shutdown
- User signup and login
- File upload, download, delete, list operations
- Quota enforcement
- Authentication failure handling

**Valgrind Results:** 0 bytes leaked in 0 blocks (all memory freed on shutdown)

**ThreadSanitizer:** No data races detected in Phase 1 tests

---

**Report Date:** October 28, 2025
**Implementation Status:** Phase 1 complete and verified
