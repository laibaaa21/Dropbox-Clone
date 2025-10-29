# StashCLI - Protocol Specification

## Overview

StashCLI uses a text-based command protocol over TCP sockets. The server listens on port **10985** by default. All commands are newline-terminated (`\n`), and responses are sent as plain text with status messages.

## Connection Flow

1. **Client connects** to server via TCP socket
2. **Server sends welcome message** with authentication instructions
3. **Client authenticates** using SIGNUP or LOGIN
4. **Server confirms authentication** and sends file operation menu
5. **Client sends file operations** (UPLOAD, DOWNLOAD, DELETE, LIST, QUIT)
6. **Server processes** operations and sends responses
7. **Client disconnects** with QUIT command or closes socket

---

## Authentication Phase

### Welcome Message

When a client connects, the server immediately sends:

```
Welcome to StashCLI Server :))
Please authenticate first:
SIGNUP <username> <password>
LOGIN <username> <password>
```

### SIGNUP Command

**Format:**
```
SIGNUP <username> <password>\n
```

**Parameters:**
- `username`: Alphanumeric username (max 63 characters)
- `password`: Password string (max 255 characters)

**Server Responses:**

Success:
```
SIGNUP OK\n
```

Failure (user already exists):
```
SIGNUP ERROR: User already exists\n
```

Failure (other error):
```
SIGNUP ERROR: Failed\n
```

**Example:**
```
Client: SIGNUP alice mypassword123\n
Server: SIGNUP OK\n
```

---

### LOGIN Command

**Format:**
```
LOGIN <username> <password>\n
```

**Parameters:**
- `username`: Registered username
- `password`: User's password

**Server Responses:**

Success:
```
LOGIN OK\n
```

Failure (user not found):
```
LOGIN ERROR: User not found\n
```

Failure (wrong password):
```
LOGIN ERROR: Invalid password\n
```

Failure (other error):
```
LOGIN ERROR: Failed\n
```

**Example:**
```
Client: LOGIN alice mypassword123\n
Server: LOGIN OK\n
```

---

### Post-Authentication Menu

After successful authentication, the server sends:

```
Authenticated! Available commands:
UPLOAD <filename> <size>
DOWNLOAD <filename>
DELETE <filename>
LIST
QUIT
```

---

## File Operation Commands

### UPLOAD Command

**Format:**
```
UPLOAD <filename> <size>\n
<binary file data>
```

**Parameters:**
- `filename`: Name of file to upload (max 255 characters, no path separators)
- `size`: File size in bytes (decimal number)
- Binary data follows immediately after the command line

**Flow:**
1. Client sends command line with filename and size
2. Client immediately sends binary file data (exactly `size` bytes)
3. Server receives all data and saves to `storage/<username>/<filename>`
4. Server updates user metadata and quota
5. Server sends response

**Server Responses:**

Success:
```
UPLOAD OK\n
```

Failure (quota exceeded):
```
UPLOAD ERROR: Quota exceeded\n
```

Failure (cannot create file):
```
UPLOAD FAILED: Cannot create file\n
```

Failure (incomplete data):
```
UPLOAD FAILED: Incomplete data\n
```

Failure (write error):
```
UPLOAD FAILED: Write error\n
```

**Example:**
```
Client: UPLOAD test.txt 54\n
Client: <54 bytes of file data>
Server: UPLOAD OK\n
```

**Notes:**
- Quota checking is done BEFORE receiving file data
- Default quota per user: 100 MB (104857600 bytes)
- File data is binary and may contain any byte values

---

### DOWNLOAD Command

**Format:**
```
DOWNLOAD <filename>\n
```

**Parameters:**
- `filename`: Name of file to download

**Server Responses:**

Success:
```
<binary file data>
\nDOWNLOAD OK\n
```

Failure (file not found):
```
DOWNLOAD FAILED: File not found\n
```

Failure (read error):
```
DOWNLOAD FAILED: Read error\n
```

**Example:**
```
Client: DOWNLOAD test.txt\n
Server: <54 bytes of file data>
        \nDOWNLOAD OK\n
```

**Notes:**
- Binary data is sent first, followed by status message
- Client must buffer data and look for end marker `\nDOWNLOAD OK`
- File data may contain newlines and any binary content

---

### DELETE Command

**Format:**
```
DELETE <filename>\n
```

**Parameters:**
- `filename`: Name of file to delete

**Server Responses:**

Success:
```
DELETE OK\n
```

Failure (file not found or error):
```
DELETE FAILED: File not found or error\n
```

**Example:**
```
Client: DELETE test.txt\n
Server: DELETE OK\n
```

**Notes:**
- Deleting a file updates user quota (frees space)
- User metadata is updated after successful deletion

---

### LIST Command

**Format:**
```
LIST\n
```

**Parameters:** None

**Server Response:**

Success:
```
<filename1>\n
<filename2>\n
...
<filenameN>\n
LIST END\n
```

Empty directory:
```
LIST END\n
```

Failure (cannot open directory):
```
LIST FAILED: Cannot open directory\n
```

**Example:**
```
Client: LIST\n
Server: test.txt\n
        document.pdf\n
        image.png\n
        LIST END\n
```

**Notes:**
- Files are listed one per line
- `metadata.txt` file (internal) is included in listing
- Order is not guaranteed
- End marker `LIST END` signals completion

---

### QUIT Command

**Format:**
```
QUIT\n
```

**Parameters:** None

**Server Response:**
```
Goodbye!\n
```

**Example:**
```
Client: QUIT\n
Server: Goodbye!\n
<connection closes>
```

**Notes:**
- Server closes connection after sending goodbye message
- Client can also close connection without QUIT

---

## Error Handling

### Invalid Command

If client sends unrecognized command during authentication:
```
ERROR: Please SIGNUP or LOGIN first\n
```

If client sends unrecognized command after authentication:
```
Invalid command\n
```

### Server Errors

Generic server error:
```
SERVER ERROR\n
```

Server busy (queue full):
```
SERVER BUSY\n
```

---

## Implementation Details

### Character Encoding
- All text is ASCII/UTF-8
- Filenames should be ASCII for maximum compatibility
- Passwords are hashed with SHA256 before storage

### Binary Data Handling
- File data is transmitted as raw bytes
- No encoding (like base64) is used
- Client must send exact byte count specified in UPLOAD
- Server sends exact file contents in DOWNLOAD

### Concurrency
- Server handles multiple concurrent clients
- Each client has dedicated client thread for socket I/O
- File operations are processed by worker thread pool
- Per-user locking prevents metadata corruption (Phase 2)

### Storage Structure
```
storage/
├── <username1>/
│   ├── metadata.txt          # User metadata (internal)
│   ├── file1.txt
│   ├── file2.pdf
│   └── ...
├── <username2>/
│   └── ...
```

### Metadata Format (Internal)

`storage/<username>/metadata.txt`:
```
username=alice
password_hash=5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8
quota_used=12345
quota_limit=104857600
file_count=3
file[0].name=test.txt
file[0].size=54
file[0].timestamp=1234567890
file[1].name=document.pdf
file[1].size=9876
file[1].timestamp=1234567900
...
```

---

## Security Considerations

### Authentication
- Passwords are hashed with SHA256
- Password hashes stored in `metadata.txt`
- No session tokens (Phase 1 - connection-based sessions)

### Authorization
- Users can only access files in their own directory
- Path traversal attacks prevented (no `/` in filenames)
- Filenames sanitized on server side

### Quota Enforcement
- 100 MB default quota per user
- Checked before accepting upload data
- Updated on UPLOAD (add) and DELETE (subtract)

---

## Client Implementation Guidelines

### Connection Management
1. Connect to server (default port 10985)
2. Read welcome message
3. Send SIGNUP or LOGIN
4. Read response - retry if failed
5. Proceed to file operations
6. Send QUIT before closing

### File Upload Best Practices
1. Open file locally
2. Get file size with `fseek(fp, 0, SEEK_END); ftell(fp)`
3. Send UPLOAD command with filename (basename only) and size
4. Send file data in chunks (e.g., 4096 bytes)
5. Wait for server response

### File Download Best Practices
1. Send DOWNLOAD command
2. Receive data in chunks
3. Buffer until finding end marker `\nDOWNLOAD OK`
4. Write data before marker to file
5. Check for error messages

### Error Handling
- Always check server responses for error messages
- Handle quota exceeded gracefully
- Retry on network errors
- Close socket on fatal errors

---

## Testing

### Manual Testing with netcat

```bash
# Start server
./server

# Connect with netcat
nc localhost 10985

# Signup
SIGNUP testuser password123

# Upload (requires binary data)
# Use client program instead

# List files
LIST

# Quit
QUIT
```

### Automated Testing
See `test_client.sh` for comprehensive test suite.

---

## Protocol Version

**Version:** 1.0 (Phase 1)
**Date:** October 2024
**Status:** Stable

---

## Future Enhancements (Phase 2+)

- Session tokens for authentication
- TLS/SSL encryption
- Compression support
- Resumable uploads/downloads
- Delta sync
- File versioning
- Sharing and permissions

---

## References

- Server source: `src/threads/client_thread.c` (authentication and command parsing)
- Worker source: `src/threads/worker_thread.c` (file operations)
- Client source: `client/client.c` (reference implementation)
- Test script: `test_client.sh` (automated tests)
