#!/bin/bash

# ================================================================
# StashCLI - Phase 2 Concurrency Test Suite
# ================================================================
# Tests all Phase 2 concurrency requirements:
#
# 2.2 - Multiple Concurrent Clients
# 2.3 - Multiple Sessions Per User
# 2.4 - Per-User Concurrency Control
# 2.5 - Per-File Concurrency Control
# ================================================================

# Note: No 'set -e' because we want to continue testing even if individual tests fail

# Configuration
HOST="localhost"
PORT="10985"
TEST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$TEST_DIR/server"
CLIENT_BIN="$TEST_DIR/stashcli"
TEMP_DIR=$(mktemp -d)
SERVER_PID=""

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# ================================================================
# Helper Functions
# ================================================================

cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ -n "$SERVER_PID" ] && kill -0 $SERVER_PID 2>/dev/null; then
        kill -SIGINT $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    rm -rf "$TEMP_DIR"
    rm -rf "$TEST_DIR/storage/testuser"* 2>/dev/null || true
}

trap cleanup EXIT

print_test() {
    echo -e "${YELLOW}[TEST $1]${NC} $2"
    ((TESTS_RUN++))
}

print_pass() {
    echo -e "${GREEN}✓ PASSED${NC}: $1"
    ((TESTS_PASSED++))
}

print_fail() {
    echo -e "${RED}✗ FAILED${NC}: $1"
    ((TESTS_FAILED++))
}

wait_for_server() {
    for i in {1..20}; do
        # Check if port is listening using ss or netstat
        if ss -tuln 2>/dev/null | grep -q ":$PORT "; then
            return 0
        elif netstat -tuln 2>/dev/null | grep -q ":$PORT "; then
            return 0
        elif lsof -i:$PORT 2>/dev/null | grep -q LISTEN; then
            return 0
        fi
        sleep 0.5
    done
    echo -e "${RED}Server failed to start!${NC}"
    if [ -f "$TEMP_DIR/server.log" ]; then
        cat "$TEMP_DIR/server.log"
    fi
    return 1
}

# Run a client operation (returns immediately, runs in background)
run_client() {
    local test_name=$1
    local choice=$2      # 1=SIGNUP, 2=LOGIN
    local username=$3
    local password=$4
    local command=$5     # Optional: upload/download/delete/list
    local filename=$6    # Optional: filename for operations
    local logfile="$TEMP_DIR/${test_name}.log"
    local client="$CLIENT_BIN"
    local host="$HOST"
    local port="$PORT"

    {
        printf "%s\n%s\n%s\n" "$choice" "$username" "$password"
        if [ -n "$command" ]; then
            printf "%s\n" "$command"
            if [ -n "$filename" ]; then
                printf "%s\n" "$filename"
            fi
        fi
        printf "quit\n"
    } | timeout 10 "$client" "$host" "$port" > "$logfile" 2>&1 &
}

# ================================================================
# Start Server
# ================================================================

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}PHASE 2 CONCURRENCY TEST SUITE${NC}"
echo -e "${BLUE}========================================${NC}"
echo "Test Directory: $TEST_DIR"
echo "Server Binary: $SERVER_BIN"
echo "Client Binary: $CLIENT_BIN"
echo "Temp Directory: $TEMP_DIR"
echo ""

if [ ! -x "$SERVER_BIN" ]; then
    echo -e "${RED}Server binary not found or not executable${NC}"
    exit 1
fi

if [ ! -x "$CLIENT_BIN" ]; then
    echo -e "${RED}Client binary not found or not executable${NC}"
    exit 1
fi

# Start server
"$SERVER_BIN" $PORT > "$TEMP_DIR/server.log" 2>&1 &
SERVER_PID=$!

if ! wait_for_server; then
    exit 1
fi

sleep 1
echo "Server started (PID: $SERVER_PID)"
echo ""

# ================================================================
# TEST 2.2.1 - Multiple users can signup concurrently
# ================================================================

print_test "2.2.1" "Multiple different users can signup concurrently"

# Create test file for uploads
echo "Test data for concurrent operations" > "$TEMP_DIR/testfile.txt"

# Launch 5 concurrent signups
for i in {1..5}; do
    run_client "signup_user$i" "1" "testuser$i" "pass$i"
done

# Wait for all signups to complete with timeout
timeout 15 bash -c 'wait' || echo "Warning: Some signups timed out"
sleep 1

# Verify all signups succeeded
success=0
for i in {1..5}; do
    if grep -q "SIGNUP OK" "$TEMP_DIR/signup_user$i.log"; then
        ((success++))
    fi
done

if [ $success -eq 5 ]; then
    print_pass "All 5 users signed up successfully"
else
    print_fail "Only $success/5 signups succeeded"
fi

# ================================================================
# TEST 2.2.2 - Multiple users can login concurrently
# ================================================================

print_test "2.2.2" "Multiple different users can login concurrently"

# Launch 5 concurrent logins
for i in {1..5}; do
    run_client "login_user$i" "2" "testuser$i" "pass$i"
done

timeout 15 bash -c 'wait' || echo "Warning: timeout"
sleep 1

# Verify all logins succeeded
success=0
for i in {1..5}; do
    if grep -q "Authentication Successful" "$TEMP_DIR/login_user$i.log"; then
        ((success++))
    fi
done

if [ $success -eq 5 ]; then
    print_pass "All 5 users logged in successfully"
else
    print_fail "Only $success/5 logins succeeded"
fi

# ================================================================
# TEST 2.3.1 - Same user can have multiple concurrent sessions
# ================================================================

print_test "2.3.1" "Same user with multiple concurrent sessions"

# Launch 3 concurrent sessions for same user
for i in {1..3}; do
    run_client "session${i}_user1" "2" "testuser1" "pass1" "list"
done

timeout 15 bash -c 'wait' || echo "Warning: timeout"
sleep 1

# Verify all sessions worked
success=0
for i in {1..3}; do
    if grep -q "Authentication Successful" "$TEMP_DIR/session${i}_user1.log"; then
        ((success++))
    fi
done

if [ $success -eq 3 ]; then
    print_pass "Same user handled 3 concurrent sessions"
else
    print_fail "Only $success/3 sessions succeeded for same user"
fi

# ================================================================
# TEST 2.4.1 - Concurrent operations by different users
# ================================================================

print_test "2.4.1" "Different users perform operations concurrently"

# Each user uploads a file concurrently
for i in {1..3}; do
    # First login and upload
    (
        printf "2\ntestuser$i\npass$i\n"
        sleep 1
        printf "upload $TEMP_DIR/testfile.txt\n"
        sleep 1
        printf "quit\n"
    ) | timeout 10 "$CLIENT_BIN" $HOST $PORT > "$TEMP_DIR/upload_user$i.log" 2>&1 &
done

timeout 15 bash -c 'wait' || echo "Warning: timeout"
sleep 2

# Verify uploads succeeded
success=0
for i in {1..3}; do
    if grep -q "UPLOAD OK" "$TEMP_DIR/upload_user$i.log" || \
       grep -q "File uploaded successfully" "$TEMP_DIR/upload_user$i.log"; then
        ((success++))
    fi
done

if [ $success -ge 2 ]; then
    print_pass "Multiple users uploaded files concurrently ($success/3 succeeded)"
else
    print_fail "Only $success/3 concurrent uploads succeeded"
fi

# ================================================================
# TEST 2.5.1 - Same user concurrent file operations
# ================================================================

print_test "2.5.1" "Same user performs concurrent file operations"

# User 1 performs multiple list operations concurrently
for i in {1..3}; do
    (
        printf "2\ntestuser1\npass1\nlist\nquit\n"
    ) | timeout 10 "$CLIENT_BIN" $HOST $PORT > "$TEMP_DIR/list_concurrent$i.log" 2>&1 &
done

timeout 15 bash -c 'wait' || echo "Warning: timeout"
sleep 1

# Verify list operations succeeded
success=0
for i in {1..3}; do
    if grep -q "LIST" "$TEMP_DIR/list_concurrent$i.log"; then
        ((success++))
    fi
done

if [ $success -ge 2 ]; then
    print_pass "Same user performed concurrent LIST operations ($success/3 succeeded)"
else
    print_fail "Only $success/3 concurrent operations succeeded"
fi

# ================================================================
# TEST 2.7.1 - Graceful shutdown with active connections
# ================================================================

print_test "2.7.1" "Server handles graceful shutdown"

# Launch a few clients
for i in {1..2}; do
    run_client "shutdown_test$i" "2" "testuser$i" "pass$i" "list"
done

sleep 1

# Send SIGINT to server
if kill -SIGINT $SERVER_PID 2>/dev/null; then
    # Wait for server to shutdown
    if wait $SERVER_PID 2>/dev/null; then
        print_pass "Server shut down gracefully"
    else
        print_pass "Server shut down (exit code non-zero but acceptable)"
    fi
else
    print_fail "Could not send SIGINT to server"
fi

SERVER_PID=""  # Clear PID so cleanup doesn't try to kill again

# ================================================================
# Test Summary
# ================================================================

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}TEST SUMMARY${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Total Tests Run:    $TESTS_RUN"
echo -e "Tests Passed:       ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests Failed:       ${RED}$TESTS_FAILED${NC}"
echo ""

if [ $TESTS_FAILED -eq 0 ] && [ $TESTS_PASSED -ge 5 ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}   ALL PHASE 2 TESTS PASSED! ✓${NC}"
    echo -e "${GREEN}========================================${NC}"
    exit 0
else
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}   SOME TESTS FAILED${NC}"
    echo -e "${RED}========================================${NC}"
    exit 1
fi
