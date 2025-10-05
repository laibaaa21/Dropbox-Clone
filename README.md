# dropbox_clone

Multi-threaded client-server file storage system with upload, download, delete and list features

## 📘 Project Overview

This project implements a **Dropbox-like file storage server** using C, sockets and multithreading.  
It allows clients to connect to the server and perform operations such as:

- **SIGNUP / LOGIN** — Create or log into a user account
- **UPLOAD** — Send a file to the server
- **DOWNLOAD** — Retrieve a file from the server
- **DELETE** — Remove a file from the server
- **LIST** — View all uploaded files for a user

The design follows a producer-consumer model using synchronized queues for efficient concurrent execution.

---

## ⚙️ How to Build and Run

### 🧩 Step 1 — Prerequisites

Make sure your Ubuntu system has these installed:

```bash
sudo apt update
sudo apt install build-essential netcat
```
