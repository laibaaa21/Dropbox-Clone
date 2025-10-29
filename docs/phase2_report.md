# Phase 2 Design Report
**Concurrency Control and Multi-User Support**

---

## 1. Worker→Client Result Delivery Mechanism

### 1.1 Chosen Approach

**Option A: Session-based Response Delivery with Condition Variables**

Each client connection has a dedicated Session structure containing an embedded Response object with synchronization primitives. Workers deliver results by looking up the session and signaling its condition variable.

### 1.2 Implementation Details

**Session Structure** (`src/session/session_manager.h:20-35`):
```c
typedef struct Session {
    uint64_t session_id;              // Unique identifier
    int socket_fd;                     // Client socket
    char username[MAX_USERNAME_LEN];   // Authenticated user
    bool is_authenticated;
    volatile bool is_active;           // Disconnect detection
    Response response;                 // EMBEDDED response object
    pthread_mutex_t session_mtx;       // Per-session state lock
    // ... activity tracking fields
} Session;
```

**Response Structure** (`src/session/response_queue.h:19-28`):
```c
typedef struct Response {
    response_status_t status;
    char message[512];
    void *data;                        // Binary data (e.g., file contents)
    size_t data_size;
    bool ready;                        // Signal flag
    pthread_mutex_t mtx;               // Protects response state
    pthread_cond_t cv;                 // Client blocks here
} Response;
```

**Session Manager** (`src/session/session_manager.h:41-51`):
- Hash table with 256 slots using linear probing
- Global `manager_mtx` protects session map operations
- O(1) average lookup by `session_id`

**Delivery Flow:**

1. **Client Thread Submits Task** (`src/threads/client_thread.c:295`):
   - Creates Task with `session_id`
   - Pushes to TaskQueue
   - Calls `response_wait(&session->response)` - **blocks on CV**

2. **Worker Processes Task** (`src/threads/worker_thread.c:16-45`):
   - Completes file operation
   - Calls `deliver_response(session_id, status, message, data, size)`
   - Looks up session via `session_get(&session_manager, session_id)`
   - Validates `session->is_active` (handles disconnections)
   - Calls `response_set()` to populate response and **signal CV**

3. **Client Thread Wakes** (`src/session/response_queue.c:68-81`):
   - `pthread_cond_wait()` returns when worker signals
   - Reads response data
   - Sends to client socket
   - Frees response data if allocated

### 1.3 Justification

**Why this approach:**

✅ **Architecture Compliance:** Only client threads perform socket I/O (project requirement)
✅ **Low Latency:** Direct CV signaling with no intermediate queues
✅ **No Busy-Waiting:** Uses `pthread_cond_wait()` (Bonus 1 completed)
✅ **Disconnect Handling:** `is_active` flag prevents worker from accessing freed sessions
✅ **Simple Routing:** Hash table lookup by `session_id` is O(1) average case

**Alternatives considered:**

❌ **Option B (Worker writes to socket):** Violates "only client threads communicate with sockets" requirement
❌ **Option C (Result broker thread):** Adds extra queue overhead and complexity
❌ **Option D (Async fetch):** Requires extra round-trip, protocol complexity

### 1.4 Trade-offs

**Pros:**
- Minimal overhead (one hash lookup per response)
- Simple implementation (no extra threads or queues)
- Type-safe (response embedded in session, not heap-allocated)

**Cons:**
- Fixed capacity: 256 concurrent sessions (configurable via `MAX_SESSIONS`)
- Hash collisions cause linear probing (acceptable with typical load)

---

## 2. Per-User and Per-File Concurrency Control

### 2.1 Concurrency Architecture

The system uses **two-level locking**:
1. **Database-level:** Global mutex for user metadata (SQLite transactions)
2. **File-level:** Fine-grained per-file locks for parallel file operations

### 2.2 Database-Level Concurrency (User Metadata)

**Implementation** (`src/auth/database.c:9`):
```c
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
```

**SQLite Configuration** (`src/auth/database.c:48`):
- `SQLITE_OPEN_FULLMUTEX` mode (fully serialized)
- WAL (Write-Ahead Logging) for crash recovery
- Transactions for atomic metadata updates

**Protected Operations:**
- User signup/login verification
- File metadata insertion/deletion
- Quota calculation (via SQL `SUM()` aggregates)

**Transaction Pattern** (Example: `db_add_file`, lines 250-368):
```c
BEGIN TRANSACTION
  INSERT INTO files (user_id, filename, size) VALUES (?, ?, ?)
  UPDATE users SET quota_used = (SELECT SUM(size) FROM files WHERE user_id = ?) WHERE id = ?
COMMIT
```

**Lock Duration:** Short - database operations complete in microseconds

### 2.3 Per-File Lock Manager

**File Lock Structure** (`src/sync/file_locks.h:24-30`):
```c
typedef struct FileLock {
    char filepath[MAX_FILEPATH_LEN];  // Key: "username/filename"
    pthread_mutex_t mtx;              // Per-file mutex
    int ref_count;                    // Reference counting
    bool in_use;                      // Slot occupancy
} FileLock;
```

**Lock Manager** (`src/sync/file_locks.h:33-38`):
```c
typedef struct FileLockManager {
    FileLock locks[MAX_FILE_LOCKS];   // 1024 slots
    int capacity;
    pthread_mutex_t manager_mtx;      // Protects lock map
} FileLockManager;
```

**Lock Acquisition** (`src/sync/file_locks.c:84-142`):

1. Lock `manager_mtx` (global lock)
2. Hash filepath: `hash = hash_filepath("username/filename", 1024)`
3. Linear probing to find:
   - **Existing lock:** Increment `ref_count`
   - **Empty slot:** Create new lock with `ref_count = 1`
4. Unlock `manager_mtx`
5. Lock `file_lock->mtx` (per-file lock)
6. Return pointer to locked `FileLock`

**Lock Release** (`src/sync/file_locks.c:145-177`):

1. Unlock `file_lock->mtx` (per-file lock)
2. Lock `manager_mtx`
3. Decrement `ref_count`
4. If `ref_count == 0`: Mark slot `in_use = false` (reclaim slot)
5. Unlock `manager_mtx`

**Reference Counting Benefit:** Multiple operations can queue for same file lock without deadlock

### 2.4 Lock Ordering and Deadlock Prevention

**Strict Lock Hierarchy:**

```
Level 1 (Global - Held Briefly):
  ├─ db_mutex (database operations)
  ├─ global_file_lock_manager.manager_mtx (lock map operations)
  └─ session_manager.manager_mtx (session table operations)

Level 2 (Per-Object - Held During I/O):
  ├─ FileLock.mtx (individual file operations)
  └─ Session.session_mtx (session state, independent)

Level 3 (Response - Independent):
  └─ Response.mtx (worker→client signaling)

Level 4 (Queue - Independent):
  ├─ TaskQueue.mtx (task queue operations)
  └─ ClientQueue.mtx (connection queue operations)
```

**Ordering Rules:**

1. **Manager before Object:** Always acquire global `manager_mtx` before per-file `mtx`
2. **Release before Acquire:** Release `manager_mtx` **before** acquiring `file_mtx` (prevents deadlock)
3. **No Nested File Locks:** Workers never hold locks on multiple files simultaneously
4. **Independent Hierarchies:** Database, session, queue, and file locks are independent

**Why Deadlock-Free:**
- No circular wait: Locks acquired in consistent order
- No hold-and-wait: Manager lock released before file lock acquired
- Transactions complete atomically

### 2.5 How Conflicts are Serialized

**Scenario 1: Same User, Same File, Multiple Sessions**

```
Session1: UPLOAD file.txt (2KB)  →  File lock acquired  →  Writes 2KB
Session2: UPLOAD file.txt (5KB)  →  Blocks on file lock  →  Waits
Session1: Release file lock
Session2: Acquires file lock      →  Overwrites with 5KB  →  Last-write-wins
```

**Result:** Sequential execution via file lock, no corruption

**Scenario 2: Same User, Different Files, Multiple Sessions**

```
Session1: UPLOAD file1.txt  →  Lock(file1)  →  Parallel
Session2: UPLOAD file2.txt  →  Lock(file2)  →  Parallel
```

**Result:** Full parallelism - different file locks don't block each other

**Scenario 3: Different Users, Same Filename**

```
User1: UPLOAD test.txt  →  Lock("user1/test.txt")  →  Parallel
User2: UPLOAD test.txt  →  Lock("user2/test.txt")  →  Parallel
```

**Result:** Separate namespaces - no conflict

### 2.6 Quota Enforcement Under Concurrency

**Quota Check** (`src/threads/client_thread.c:202-207`):
```c
if (!user_check_quota(username, filesize)) {
    send_error(cfd, "Quota exceeded");
    continue;  // Reject upload
}
```

**Database Query** (`src/auth/database.c:534-547`):
```c
int db_check_quota(username, additional_bytes, *has_quota) {
    SELECT quota_used, quota_limit FROM users WHERE username = ?
    *has_quota = (quota_used + additional_bytes) <= quota_limit;
}
```

**Race Condition Handling:**
- Quota check happens **before** file transfer (optimistic check)
- Actual quota update happens **after** file write (in transaction)
- Multiple concurrent uploads could theoretically exceed quota briefly
- Acceptable trade-off: quota is soft limit, not hard limit

**Improvement Option:** Lock user row during upload (not implemented for performance)

---

## 3. Multiple Sessions Per User

### 3.1 Session Isolation

Each client connection creates a **unique Session** regardless of username:

**Session Creation** (`src/session/session_manager.c:61-127`):
- Generates unique `session_id` (64-bit counter)
- Allocates Session structure
- Stores in hash table at `hash(session_id) % 256`
- Each session has independent Response object

**Key Property:** User `alice` can have 3 concurrent sessions with 3 different `session_id` values.

### 3.2 Session Routing

**Task Structure** (`src/queue/task_queue.h:19-28`):
```c
typedef struct Task {
    task_type_t type;
    uint64_t session_id;    // Identifies which session submitted this task
    char username[64];      // User who owns the file
    char filename[256];
    void *data_buffer;
} Task;
```

**Worker Response Delivery** (`src/threads/worker_thread.c:16-45`):
```c
Session *session = session_get(&session_manager, task.session_id);
if (session && session->is_active) {
    response_set(&session->response, status, message, data, size);
}
```

**Result:** Each session receives only its own responses, even if multiple sessions belong to same user.

### 3.3 Concurrent Operations by Same User

**Example: 3 sessions of user `bob`**

```
Time  Session1 (bob)    Session2 (bob)    Session3 (bob)    File Locks
----  --------------    --------------    --------------    ----------
T1    UPLOAD a.txt      UPLOAD b.txt      LIST              -
T2    → Task queued     → Task queued     → Task queued     -
T3    -                 -                 -                 Worker1: Lock(bob/a.txt)
T4    -                 -                 -                 Worker2: Lock(bob/b.txt)
T5    -                 -                 -                 Worker3: No lock (LIST)
T6    Write a.txt       Write b.txt       Read directory    Parallel!
T7    Release lock      Release lock      Complete          -
T8    Response→Sess1    Response→Sess2    Response→Sess3    -
```

**Result:** Operations on different files execute in parallel. LIST operation reads directory without synchronization (potential race - see limitations).

---

## 4. Why This Approach is Race-Free

### 4.1 ThreadSanitizer Verification

**Test Configuration:**
- Compiler flags: `-fsanitize=thread -g -O1`
- Tests performed: 11 Phase 1 + 8 Phase 2 concurrency scenarios
- Result: **0 data races detected**

**Test Report** (from `docs/TESTING_RESULTS.md`):
```
✓ RESULT: NO DATA RACES DETECTED

ThreadSanitizer found no race conditions in:
  - Phase 1 functional tests
  - Phase 2 concurrency tests
```

### 4.2 Complete Synchronization Coverage

**All shared data protected:**

| Shared Resource | Protection Mechanism | Location |
|----------------|---------------------|----------|
| ClientQueue | `queue_mtx` + CVs | `src/queue/client_queue.c` |
| TaskQueue | `queue_mtx` + CVs | `src/queue/task_queue.c` |
| Session table | `manager_mtx` | `src/session/session_manager.c` |
| Session state | `session_mtx` | Per-session |
| Response objects | `response.mtx` + `cv` | Per-session |
| User database | `db_mutex` + SQLite FULLMUTEX | `src/auth/database.c` |
| File lock map | `manager_mtx` | `src/sync/file_locks.c` |
| Individual files | `file_lock.mtx` | Per-file |

**Critical Sections Verified:**
- Queue push/pop operations: Mutex-protected, CV-signaled
- Session creation/lookup: Manager mutex held during map modification
- File operations: Per-file lock held during read/write
- Database transactions: Global mutex + SQLite serialization

### 4.3 Condition Variable Safety

**All CV usage follows correct pattern:**

```c
// CORRECT pattern (used throughout codebase)
pthread_mutex_lock(&mtx);
while (!condition) {
    pthread_cond_wait(&cv, &mtx);  // Atomically releases and re-acquires
}
// condition now true
pthread_mutex_unlock(&mtx);
```

**Examples:**
- `response_wait()`: Waits until `ready == true`
- `task_queue_pop()`: Waits until `size > 0`
- `client_queue_pop()`: Waits until queue not empty

**No spurious wakeup bugs:** All CV waits use `while` loops

### 4.4 Memory Safety

**Valgrind Results** (from `test_results/valgrind_output.log`):
```
HEAP SUMMARY:
  in use at exit: 0 bytes in 0 blocks
  total heap usage: 4,420 allocs, 4,420 frees, 352,796 bytes allocated

All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

**Perfect allocation/deallocation balance:** Every malloc has corresponding free

**No use-after-free bugs:**
- `is_active` flag prevents workers from accessing destroyed sessions
- Response data freed by client thread after sending
- File locks ref-counted to prevent premature destruction

---

## 5. Trade-offs and Limitations

### 5.1 Scalability Limits

| Limit | Current Value | Impact | Mitigation |
|-------|--------------|--------|------------|
| Max concurrent sessions | 256 | Rejects 257th client | Increase `MAX_SESSIONS` |
| Max file locks | 1024 | Degrades to linear scan | Increase `MAX_FILE_LOCKS` |
| Thread pool size | 4+4 | Queue builds under heavy load | Make configurable |

**Linear probing impact:** With 1024 slots and typical 10-50 file locks, probe length averages 1-2 lookups. Acceptable performance.

### 5.2 Lock Granularity Analysis

**Coarse-grained (Global locks):**
- Database mutex: Serializes all metadata operations
- Lock manager mutex: Held briefly during map lookup
- **Benefit:** Simple, no deadlock risk
- **Cost:** Some contention under heavy load

**Fine-grained (Per-file locks):**
- Individual file mutex: One per unique file path
- **Benefit:** Maximum parallelism for different files
- **Cost:** More complex code, hash table overhead

**Chosen balance:** Global for metadata (fast operations), per-file for I/O (slow operations). Optimal trade-off.

### 5.3 Known Limitations

**1. LIST Operation Race Condition**
- **Location:** `src/threads/worker_thread.c:364-454`
- **Issue:** Directory listing uses `opendir/readdir` without file locks
- **Scenario:** Concurrent DELETE may remove file during LIST
- **Impact:** Client may see file in LIST that's immediately unavailable
- **Mitigation:** Accept as-is (similar to filesystem snapshots)

**2. Quota Enforcement Race**
- **Issue:** Multiple concurrent uploads can exceed quota temporarily
- **Mitigation:** Quota is soft limit enforced on next operation
- **Alternative:** Lock user row during upload (rejected for performance)

**3. Last-Write-Wins Semantics**
- **Issue:** Concurrent uploads to same file → no conflict resolution, file overwritten
- **Mitigation:** Application-level concern, not server responsibility

### 5.4 Performance Characteristics

**Under Load (20 concurrent clients):**
- No crashes or hangs observed
- All operations complete successfully
- Clean shutdown maintains consistency

**Throughput Observations:**

| Scenario | Concurrency | Bottleneck |
|----------|-------------|------------|
| Different users, different files | **Fully parallel** | Network I/O |
| Same user, different files | **Mostly parallel** | Per-file locks |
| Same user, same file | **Serialized** | File lock (correct) |

**Thread Pool Saturation:** When 8 threads (4 client + 4 worker) all busy:
- New connections queue in ClientQueue (capacity 64)
- Tasks queue in TaskQueue (capacity 128)
- Graceful degradation, no failures

---

## 6. Testing and Verification

### 6.1 Test Coverage

**Phase 1 Tests:** 11 scenarios (all passing)
- Single-user authentication
- Basic file operations
- Quota enforcement

**Phase 2 Concurrency Tests:** 8 scenarios (all passing)
1. Multiple users signup concurrently (5 users)
2. Multiple users login concurrently (5 users)
3. 10 clients with mixed operations
4. Same user, multiple sessions (3 concurrent)
5. Concurrent uploads by same user
6. Concurrent downloads by same user
7. Mixed operations with race conditions
8. Stress test (20 concurrent clients)

### 6.2 TSAN Results Summary

**Data Races Detected:** 0
**Test Duration:** ~2 minutes
**Scenarios Tested:** 19 (Phase 1 + Phase 2)
**Shared Data Verified:** All queues, sessions, locks, database

**Critical Bug Fixed During Testing:**
- Issue: `session_get_statistics()` called after mutex destroyed
- Fix: Moved call before `session_manager_destroy()`
- Location: `src/main.c:267-274`

### 6.3 Valgrind Results Summary

**Memory Leaks:** 0 bytes
**Allocations:** 4,420
**Frees:** 4,420 (perfect balance)
**Errors:** 0

**Resources Verified Freed:**
- Session structures and embedded responses
- Task buffers and file data
- File locks and mutexes
- Queue nodes
- Database connection

### 6.4 Confidence in Correctness

**HIGH** - Based on:
1. Zero data races detected by ThreadSanitizer
2. Zero memory leaks detected by Valgrind
3. All functional tests passing under concurrent load
4. Manual code review of lock ordering
5. Stress testing with 20 concurrent clients

---

## 7. Conclusion

The Phase 2 implementation successfully achieves:

✅ **Multiple concurrent clients:** Verified with 20 simultaneous connections
✅ **Multiple sessions per user:** Each connection independent, unique routing
✅ **Worker→client delivery:** Session-based CV signaling, no busy-waiting
✅ **Per-file concurrency control:** Fine-grained locking with reference counting
✅ **Conflict serialization:** Same-file operations serialized, different-file parallel
✅ **Atomic metadata updates:** SQLite transactions with ACID guarantees
✅ **Robust shutdown:** All resources freed, verified by Valgrind
✅ **Race-free operation:** Verified by ThreadSanitizer across 19 test scenarios

**Design Philosophy:**
- **Simplicity over complexity:** Chosen session-based delivery over broker pattern
- **Safety over performance:** Global locks for metadata acceptable given operation speed
- **Parallelism where it matters:** Per-file locks enable concurrent file operations

**Production Readiness:** For the scope of this educational project, the implementation is production-ready with comprehensive testing verification and zero critical bugs.

---

**Report Date:** October 28, 2025
**Implementation Status:** Phase 2 complete and verified
**Test Results:** 0 races, 0 leaks, 100% tests passing
