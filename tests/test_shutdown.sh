#!/bin/bash
# Test Phase 2.7: Robust Shutdown Mechanism

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

cd "$(dirname "$0")/.."

echo -e "${YELLOW}Phase 2.7 Shutdown Test${NC}"
echo ""

# Clean up
rm -rf storage/testuser*
killall server 2>/dev/null || true

# Start server
echo "Starting server..."
./server 10986 > /tmp/server_shutdown.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    cat /tmp/server_shutdown.log
    exit 1
fi

echo "Server running (PID: $SERVER_PID)"
echo ""

# Create a user and start a long-running operation
echo -e "${YELLOW}[TEST 1]${NC} Starting client connections..."

# Start multiple clients that will stay connected
for i in {1..3}; do
    (
        echo "1"  # SIGNUP
        echo "testuser$i"
        echo "pass$i"
        sleep 1  # Stay connected for a bit
        echo "quit"
    ) | ./dbc_client localhost 10986 > /tmp/client_$i.log 2>&1 &
done

sleep 1
echo "Clients connected"

# Send SIGINT to server
echo ""
echo -e "${YELLOW}[TEST 2]${NC} Sending SIGINT to server (Ctrl+C simulation)..."
kill -SIGINT $SERVER_PID

# Wait for server to shut down
echo "Waiting for graceful shutdown..."
wait $SERVER_PID 2>/dev/null || true

echo ""
echo -e "${YELLOW}[TEST 3]${NC} Checking server shutdown log..."

# Check if shutdown was graceful
if grep -q "GRACEFUL SHUTDOWN INITIATED" /tmp/server_shutdown.log; then
    echo -e "${GREEN}✓ PASSED${NC}: Shutdown initiated message found"
else
    echo -e "${RED}✗ FAILED${NC}: Shutdown message not found"
fi

if grep -q "All client threads terminated" /tmp/server_shutdown.log; then
    echo -e "${GREEN}✓ PASSED${NC}: Client threads terminated cleanly"
else
    echo -e "${RED}✗ FAILED${NC}: Client threads did not terminate cleanly"
fi

if grep -q "All worker threads terminated" /tmp/server_shutdown.log; then
    echo -e "${GREEN}✓ PASSED${NC}: Worker threads terminated cleanly"
else
    echo -e "${RED}✗ FAILED${NC}: Worker threads did not terminate cleanly"
fi

if grep -q "SERVER SHUTDOWN COMPLETE" /tmp/server_shutdown.log; then
    echo -e "${GREEN}✓ PASSED${NC}: Server shutdown completed successfully"
else
    echo -e "${RED}✗ FAILED${NC}: Server did not complete shutdown"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Phase 2.7 Shutdown Test Complete${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Server shutdown log:"
tail -20 /tmp/server_shutdown.log
