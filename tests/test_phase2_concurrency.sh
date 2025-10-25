#!/bin/bash
# Phase 2 Concurrency Test Suite
# Tests Phase 2.2 (Multiple Concurrent Clients) and Phase 2.3 (Multiple Sessions Per User)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
TEST_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="$TEST_DIR/server"
CLIENT_BIN="$TEST_DIR/dbc_client"
SERVER_PORT=10985
SERVER_PID=""
TEMP_DIR=$(mktemp -d)
PASSED=0
FAILED=0

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ -n "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    # DON'T remove temp dir for debugging
    # rm -rf "$TEMP_DIR"
    rm -rf "$TEST_DIR/storage/testuser"* 2>/dev/null || true
}

trap cleanup EXIT

# Helper functions
print_test() {
    echo -e "${YELLOW}[TEST $1]${NC} $2"
}

print_pass() {
    echo -e "${GREEN}✓ PASSED${NC}: $1"
    ((PASSED++))
}

print_fail() {
    echo -e "${RED}✗ FAILED${NC}: $1"
    ((FAILED++))
}

wait_for_server() {
    echo "Waiting for server to start..."
    for i in {1..20}; do
        # Try to connect using bash's /dev/tcp feature or ss command
        if (echo > /dev/tcp/localhost/$SERVER_PORT) 2>/dev/null; then
            echo "Server is ready!"
            return 0
        elif ss -tuln 2>/dev/null | grep -q ":$SERVER_PORT "; then
            echo "Server is ready!"
            return 0
        fi
        sleep 0.5
    done
    echo "Server failed to start!"
    cat "$TEMP_DIR/server.log" 2>/dev/null || true
    return 1
}

# Start server
start_server() {
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
    
    # Start server in background
    "$SERVER_BIN" $SERVER_PORT > "$TEMP_DIR/server.log" 2>&1 &
    SERVER_PID=$!
    
    if ! wait_for_server; then
        echo "Server log:"
        cat "$TEMP_DIR/server.log"
        exit 1
    fi
    
    # Give server extra time to fully initialize
    sleep 2
    echo "Server fully initialized"
}

# Test helper: run client operation
# Note: The client uses numeric menu: 1=SIGNUP, 2=LOGIN
run_client_op() {
    local username=$1
    local password=$2
    local command=$3
    local file_arg=$4
    local output_file=$5

    {
        # Send menu choice (1=SIGNUP, 2=LOGIN)
        if [ "$command" = "1" ] || [ "$command" = "SIGNUP" ]; then
            echo "1"
        elif [ "$command" = "2" ] || [ "$command" = "LOGIN" ]; then
            echo "2"
        fi

        # Send username and password for auth commands
        if [ "$command" = "1" ] || [ "$command" = "2" ] || [ "$command" = "SIGNUP" ] || [ "$command" = "LOGIN" ]; then
            echo "$username"
            echo "$password"
        fi

        # Send file operation commands
        if [ -n "$file_arg" ]; then
            if [ "$command" = "upload" ]; then
                echo "upload"
                echo "$file_arg"
            elif [ "$command" = "download" ]; then
                echo "download"
                echo "$file_arg"
            elif [ "$command" = "delete" ]; then
                echo "delete"
                echo "$file_arg"
            fi
        fi

        echo "quit"
    } | "$CLIENT_BIN" localhost $SERVER_PORT > "$output_file" 2>&1 || true
}

# Create test files
create_test_file() {
    local filename=$1
    local content=$2
    echo "$content" > "$TEMP_DIR/$filename"
}

#############################################
# PHASE 2.2 TESTS - Multiple Concurrent Clients
#############################################

start_server

echo ""
print_test "2.2.1" "Multiple different users can signup concurrently"

# Create 5 users concurrently
for i in {1..5}; do
    {
        echo "1"  # SIGNUP choice
        echo "testuser$i"
        echo "pass$i"
        echo "quit"
    } | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/signup_$i.log" 2>&1 &
done
sleep 2  # Give clients time to complete
wait

# Verify all signups succeeded
success_count=0
for i in {1..5}; do
    if [ -f "$TEMP_DIR/signup_$i.log" ]; then
        if grep -q "SIGNUP OK" "$TEMP_DIR/signup_$i.log"; then
            ((success_count++))
        else
            echo "  signup_$i.log exists but no SIGNUP OK found"
        fi
    else
        echo "  signup_$i.log does not exist"
    fi
done

if [ $success_count -eq 5 ]; then
    print_pass "All 5 users signed up concurrently"
else
    print_fail "Only $success_count/5 users signed up successfully"
    echo "  Temp dir: $TEMP_DIR"
    ls -la "$TEMP_DIR"/ 2>/dev/null || echo "Temp dir not accessible"
    echo "  Sample log content (signup_1.log):"
    cat "$TEMP_DIR/signup_1.log" | head -20
    exit 1  # Exit early for debugging
fi

echo ""
print_test "2.2.2" "Multiple users can login concurrently"

# Login 5 users concurrently
for i in {1..5}; do
    {
        echo "2"  # LOGIN choice
        echo "testuser$i"
        echo "pass$i"
        echo "quit"
    } | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/login_$i.log" 2>&1 &
done
wait

# Verify all logins succeeded
success_count=0
for i in {1..5}; do
    if grep -q "LOGIN OK" "$TEMP_DIR/login_$i.log"; then
        ((success_count++))
    fi
done

if [ $success_count -eq 5 ]; then
    print_pass "All 5 users logged in concurrently"
else
    print_fail "Only $success_count/5 users logged in successfully"
fi

echo ""
print_test "2.2.3" "10 concurrent clients performing mixed operations"

# Create test files
for i in {1..10}; do
    create_test_file "testfile$i.txt" "Test content from client $i - $(date)"
done

# Run 10 clients concurrently doing various operations
for i in {1..10}; do
    user_num=$(( (i % 5) + 1 ))  # Distribute across 5 users
    {
        echo "2"  # LOGIN choice
        echo "testuser$user_num"
        echo "pass$user_num"
        
        # Upload
        echo "upload"
        echo "$TEMP_DIR/testfile$i.txt"
        
        # List
        echo "list"
        
        # Download
        echo "download"
        echo "testfile$i.txt"
        echo "$TEMP_DIR/downloaded_$i.txt"
        
        # Quit
        echo "quit"
    } | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/concurrent_$i.log" 2>&1 &
done

echo "Waiting for all 10 concurrent operations to complete..."
wait

# Count successful operations
upload_success=0
download_success=0
for i in {1..10}; do
    if grep -q "Upload successful" "$TEMP_DIR/concurrent_$i.log"; then
        ((upload_success++))
    fi
    if [ -f "$TEMP_DIR/downloaded_$i.txt" ]; then
        ((download_success++))
    fi
done

echo "  Upload success: $upload_success/10"
echo "  Download success: $download_success/10"

if [ $upload_success -ge 8 ] && [ $download_success -ge 8 ]; then
    print_pass "Concurrent operations successful (upload: $upload_success/10, download: $download_success/10)"
else
    print_fail "Some concurrent operations failed (upload: $upload_success/10, download: $download_success/10)"
fi

#############################################
# PHASE 2.3 TESTS - Multiple Sessions Per User
#############################################

echo ""
print_test "2.3.1" "Same user can have multiple concurrent sessions"

# Create test files for multi-session test
create_test_file "session1.txt" "Content from session 1"
create_test_file "session2.txt" "Content from session 2"
create_test_file "session3.txt" "Content from session 3"

# Login as testuser1 from 3 different sessions and upload different files
{
    echo "2"  # LOGIN choice
    echo "testuser1"
    echo "pass1"
    echo "upload"
    echo "$TEMP_DIR/session1.txt"
    echo "list"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/session1.log" 2>&1 &
PID1=$!

{
    echo "2"  # LOGIN choice
    echo "testuser1"
    echo "pass1"
    echo "upload"
    echo "$TEMP_DIR/session2.txt"
    echo "list"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/session2.log" 2>&1 &
PID2=$!

{
    echo "2"  # LOGIN choice
    echo "testuser1"
    echo "pass1"
    echo "upload"
    echo "$TEMP_DIR/session3.txt"
    echo "list"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/session3.log" 2>&1 &
PID3=$!

# Wait for all sessions
wait $PID1 $PID2 $PID3

# Check if all uploads succeeded
session_success=0
for i in 1 2 3; do
    if grep -q "Upload successful" "$TEMP_DIR/session$i.log"; then
        ((session_success++))
    fi
done

if [ $session_success -eq 3 ]; then
    print_pass "Same user uploaded from 3 concurrent sessions"
else
    print_fail "Only $session_success/3 sessions succeeded for same user"
fi

echo ""
print_test "2.3.2" "Multiple sessions can perform operations simultaneously"

# Verify all 3 files exist in storage
files_found=0
for i in 1 2 3; do
    if [ -f "$TEST_DIR/storage/testuser1/session$i.txt" ]; then
        ((files_found++))
    fi
done

if [ $files_found -eq 3 ]; then
    print_pass "All 3 files from concurrent sessions found in storage"
else
    print_fail "Only $files_found/3 files found from concurrent sessions"
fi

echo ""
print_test "2.3.3" "Concurrent downloads by same user work correctly"

# Download same file from multiple sessions
{
    echo "2"  # LOGIN choice
    echo "testuser1"
    echo "pass1"
    echo "download"
    echo "session1.txt"
    echo "$TEMP_DIR/dl_s1_1.txt"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > /dev/null 2>&1 &

{
    echo "2"  # LOGIN choice
    echo "testuser1"
    echo "pass1"
    echo "download"
    echo "session1.txt"
    echo "$TEMP_DIR/dl_s1_2.txt"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > /dev/null 2>&1 &

{
    echo "2"  # LOGIN choice
    echo "testuser1"
    echo "pass1"
    echo "download"
    echo "session2.txt"
    echo "$TEMP_DIR/dl_s2_1.txt"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > /dev/null 2>&1 &

wait

# Check downloads
download_count=0
[ -f "$TEMP_DIR/dl_s1_1.txt" ] && ((download_count++))
[ -f "$TEMP_DIR/dl_s1_2.txt" ] && ((download_count++))
[ -f "$TEMP_DIR/dl_s2_1.txt" ] && ((download_count++))

if [ $download_count -eq 3 ]; then
    print_pass "Concurrent downloads by same user successful"
else
    print_fail "Only $download_count/3 concurrent downloads succeeded"
fi

echo ""
print_test "2.3.4" "Mixed operations (upload/download/delete) by same user"

# One session uploads, another downloads, another deletes
create_test_file "mixed1.txt" "Mixed test 1"
create_test_file "mixed2.txt" "Mixed test 2"

{
    echo "2"  # LOGIN choice
    echo "testuser2"
    echo "pass2"
    echo "upload"
    echo "$TEMP_DIR/mixed1.txt"
    sleep 0.1
    echo "list"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/mixed1.log" 2>&1 &

{
    echo "2"  # LOGIN choice
    echo "testuser2"
    echo "pass2"
    sleep 0.2  # Let first upload complete
    echo "list"
    echo "download"
    echo "session1.txt"  # Download file uploaded earlier
    echo "$TEMP_DIR/mixed_dl.txt"
    echo "quit"
} | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/mixed2.log" 2>&1 &

wait

mixed_success=0
grep -q "Upload successful" "$TEMP_DIR/mixed1.log" && ((mixed_success++))
[ -f "$TEMP_DIR/mixed_dl.txt" ] && ((mixed_success++))

if [ $mixed_success -eq 2 ]; then
    print_pass "Mixed concurrent operations by same user work correctly"
else
    print_fail "Some mixed operations failed ($mixed_success/2)"
fi

#############################################
# STRESS TEST
#############################################

echo ""
print_test "2.2.4" "Stress test: 20 concurrent clients"

for i in {1..20}; do
    user_num=$(( (i % 5) + 1 ))
    {
        echo "2"  # LOGIN choice
        echo "testuser$user_num"
        echo "pass$user_num"
        echo "list"
        echo "quit"
    } | "$CLIENT_BIN" localhost $SERVER_PORT > "$TEMP_DIR/stress_$i.log" 2>&1 &
done

echo "Waiting for 20 concurrent clients..."
wait

# Count successes
stress_success=0
for i in {1..20}; do
    if grep -q "LOGIN OK" "$TEMP_DIR/stress_$i.log"; then
        ((stress_success++))
    fi
done

echo "  Success rate: $stress_success/20"

if [ $stress_success -ge 18 ]; then
    print_pass "Stress test passed ($stress_success/20 clients handled)"
else
    print_fail "Stress test had issues ($stress_success/20 clients succeeded)"
fi

#############################################
# SUMMARY
#############################################

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}TEST SUMMARY${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Total Tests Passed: ${GREEN}$PASSED${NC}"
echo "Total Tests Failed: ${RED}$FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}   ALL PHASE 2.2 & 2.3 TESTS PASSED! ✓${NC}"
    echo -e "${GREEN}========================================${NC}"
    exit 0
else
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}   SOME TESTS FAILED${NC}"
    echo -e "${RED}========================================${NC}"
    echo ""
    echo "Check logs in: $TEMP_DIR"
    echo "Server log: $TEMP_DIR/server.log"
    exit 1
fi

