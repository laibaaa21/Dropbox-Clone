#!/bin/bash

# Phase 2 Demo Script - Runs all concurrency tests

echo "================================================================"
echo "          StashCLI - Phase 2 Demo Script"
echo "================================================================"
echo ""
echo "This script demonstrates the Phase 2 implementation by running:"
echo "  1. Building the server and client"
echo "  2. Running Phase 2 concurrency tests"
echo "  3. Displaying test results"
echo ""
echo "================================================================"
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Track results
TESTS_PASSED=0
TESTS_FAILED=0

# Function to print colored output
print_status() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓ PASSED${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}✗ FAILED${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

echo -e "${BLUE}[1/5] Cleaning previous builds...${NC}"
make clean > /dev/null 2>&1
rm -rf storage/* test_results
echo "      Done"
echo ""

echo -e "${BLUE}[2/5] Building server and client...${NC}"
if make all > /dev/null 2>&1; then
    echo -e "      ${GREEN}Build successful${NC}"
else
    echo -e "      ${RED}Build failed${NC}"
    exit 1
fi
echo ""

echo -e "${BLUE}[3/5] Starting server...${NC}"
./server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "      ${RED}Server failed to start${NC}"
    exit 1
fi
echo "      Server started (PID: $SERVER_PID)"
echo ""

echo -e "${BLUE}[4/5] Running Phase 2 concurrency tests...${NC}"
echo ""

./tests/test_phase2_concurrency.sh > /tmp/demo_phase2_output.log 2>&1
TEST_RESULT=$?

# Display test output
cat /tmp/demo_phase2_output.log

echo ""
echo -e "${BLUE}[5/5] Shutting down server...${NC}"
kill -SIGINT $SERVER_PID 2>/dev/null
sleep 2

if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID 2>/dev/null
fi

echo "      Server stopped"
echo ""

# Print summary
echo "================================================================"
echo "                      Test Summary"
echo "================================================================"
echo ""

if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ All Phase 2 tests PASSED${NC}"
    echo ""
    echo "The implementation successfully handles:"
    echo "  • Multiple concurrent clients"
    echo "  • Multiple sessions per user"
    echo "  • Concurrent file operations"
    echo "  • Safe session management"
    echo "  • Graceful error handling"
    echo ""
    echo "================================================================"
    exit 0
else
    echo -e "${RED}✗ Some Phase 2 tests FAILED${NC}"
    echo ""
    echo "Check the output above for details."
    echo "Full log saved to: /tmp/demo_phase2_output.log"
    echo ""
    echo "================================================================"
    exit 1
fi

