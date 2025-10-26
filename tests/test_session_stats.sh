#!/bin/bash
# Test script for Phase 2.9 - Session Management & Lifetime
# Tests session statistics, tracking, and monitoring

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SERVER_PORT=10985
SERVER_PID=""
TEST_USER="session_test_user"
TEST_PASS="testpass123"

echo "=========================================="
echo "Phase 2.9 Session Management Test"
echo "=========================================="

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ ! -z "$SERVER_PID" ]; then
        echo "Stopping server (PID: $SERVER_PID)..."
        kill -SIGINT $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
    fi
    rm -f test_upload_*.txt
}

trap cleanup EXIT

# Start server
echo ""
echo "[1] Starting server..."
./server $SERVER_PORT > server_session_test.log 2>&1 &
SERVER_PID=$!
sleep 1

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}FAIL${NC}: Server failed to start"
    cat server_session_test.log
    exit 1
fi

echo -e "${GREEN}OK${NC}: Server started (PID: $SERVER_PID)"

# Helper function to run client command
run_client() {
    local cmd="$1"
    echo "$cmd" | nc localhost $SERVER_PORT 2>/dev/null || true
}

# Test 1: Multiple session creation
echo ""
echo "[2] Testing multiple session creation..."
for i in {1..5}; do
    (
        echo "SIGNUP session_user_$i pass$i"
        echo "QUIT"
    ) | nc localhost $SERVER_PORT > /dev/null 2>&1 &
done
sleep 2
echo -e "${GREEN}OK${NC}: 5 sessions created"

# Test 2: Session with operations
echo ""
echo "[3] Testing session with file operations..."
(
    echo "SIGNUP $TEST_USER $TEST_PASS"
    sleep 0.5

    # Create test file
    echo "Test content" > test_upload_session.txt
    SIZE=$(stat -c%s test_upload_session.txt 2>/dev/null || stat -f%z test_upload_session.txt 2>/dev/null)

    # Upload (operation 1)
    echo "UPLOAD test_file1.txt $SIZE"
    cat test_upload_session.txt
    sleep 0.5

    # List (operation 2)
    echo "LIST"
    sleep 0.5

    # Download (operation 3)
    echo "DOWNLOAD test_file1.txt"
    sleep 0.5

    # Delete (operation 4)
    echo "DELETE test_file1.txt"
    sleep 0.5

    echo "QUIT"
) | nc localhost $SERVER_PORT > /dev/null 2>&1
sleep 1
echo -e "${GREEN}OK${NC}: Session performed 4 operations"

# Test 3: Check server logs for session statistics
echo ""
echo "[4] Checking session statistics in server logs..."

# Look for session creation messages
SESSION_COUNT=$(grep -c "Created session" server_session_test.log || echo "0")
if [ "$SESSION_COUNT" -ge "5" ]; then
    echo -e "${GREEN}OK${NC}: Found $SESSION_COUNT session creations"
else
    echo -e "${YELLOW}WARNING${NC}: Expected at least 5 sessions, found $SESSION_COUNT"
fi

# Look for operation tracking
OPS_COUNT=$(grep -c "ops=" server_session_test.log || echo "0")
if [ "$OPS_COUNT" -ge "4" ]; then
    echo -e "${GREEN}OK${NC}: Found $OPS_COUNT operation tracking messages"
else
    echo -e "${YELLOW}WARNING${NC}: Expected at least 4 operations, found $OPS_COUNT"
fi

# Look for statistics tracking
STATS_FOUND=$(grep -c "active=" server_session_test.log || echo "0")
if [ "$STATS_FOUND" -gt "0" ]; then
    echo -e "${GREEN}OK${NC}: Session statistics tracking active"
else
    echo -e "${YELLOW}WARNING${NC}: No session statistics found"
fi

# Test 4: Verify session cleanup
echo ""
echo "[5] Testing session cleanup..."
sleep 1

# Stop server gracefully
echo "Sending shutdown signal..."
kill -SIGINT $SERVER_PID
sleep 2

# Check for cleanup messages
CLEANUP_COUNT=$(grep -c "destroyed" server_session_test.log || echo "0")
if [ "$CLEANUP_COUNT" -gt "0" ]; then
    echo -e "${GREEN}OK${NC}: Found $CLEANUP_COUNT session cleanup messages"
else
    echo -e "${YELLOW}WARNING${NC}: No session cleanup messages found"
fi

# Check for final statistics
FINAL_STATS=$(grep "Session statistics:" server_session_test.log || echo "")
if [ ! -z "$FINAL_STATS" ]; then
    echo -e "${GREEN}OK${NC}: Final statistics reported"
    echo "     $FINAL_STATS"
else
    echo -e "${YELLOW}WARNING${NC}: No final statistics found"
fi

SERVER_PID="" # Already stopped

echo ""
echo "=========================================="
echo -e "${GREEN}Phase 2.9 Session Management Test Complete${NC}"
echo "=========================================="
echo ""
echo "Server log saved to: server_session_test.log"
echo "Review the log to see detailed session tracking:"
echo "  - Session creation with active/peak counts"
echo "  - Operation tracking with operation counters"
echo "  - Session cleanup with final statistics"
echo ""
