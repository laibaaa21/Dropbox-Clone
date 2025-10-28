CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -O2
INCLUDES = -Isrc
LDFLAGS = -lcrypto -lsqlite3

# Server source files
SERVER_SRCS = src/main.c \
              src/threads/client_thread.c \
              src/threads/worker_thread.c \
              src/queue/client_queue.c \
              src/queue/task_queue.c \
              src/session/response_queue.c \
              src/session/session_manager.c \
              src/auth/auth.c \
              src/auth/user_metadata.c \
              src/auth/database.c \
              src/sync/file_locks.c \
              src/utils/network_utils.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_TARGET = server

# TSAN-enabled build
TSAN_OBJS = $(SERVER_SRCS:.c=.tsan.o)
TSAN_TARGET = server-tsan
TSAN_CFLAGS = -Wall -Wextra -pthread -g -O1 -fsanitize=thread

# Client source files
CLIENT_SRCS = client/client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_TARGET = dbc_client

# Targets
.PHONY: all clean run run-client test help server-tsan

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -pthread $(LDFLAGS)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# TSAN build target
$(TSAN_TARGET): $(TSAN_OBJS)
	$(CC) $(TSAN_CFLAGS) -o $@ $^ -pthread $(LDFLAGS)

%.tsan.o: %.c
	$(CC) $(TSAN_CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Run server
run: $(SERVER_TARGET)
	./$(SERVER_TARGET)

# Run client (example)
run-client: $(CLIENT_TARGET)
	./$(CLIENT_TARGET) localhost 12345 list

# Clean build artifacts
clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_TARGET) $(CLIENT_TARGET)
	rm -f $(TSAN_OBJS) $(TSAN_TARGET)
	rm -f src/*.o src/**/*.o client/*.o src/*.tsan.o src/**/*.tsan.o

# Clean storage directory
clean-storage:
	rm -rf storage/*

# Full clean
distclean: clean clean-storage

# Help
help:
	@echo "Dropbox Clone - Makefile targets:"
	@echo "  make all          - Build server and client"
	@echo "  make server       - Build server only"
	@echo "  make client       - Build client only"
	@echo "  make server-tsan  - Build TSAN-enabled server for race detection"
	@echo "  make run          - Build and run server"
	@echo "  make run-client   - Build and run client (example)"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make clean-storage - Remove all user files"
	@echo "  make distclean    - Full clean (build + storage)"
	@echo "  make help         - Show this help message"
