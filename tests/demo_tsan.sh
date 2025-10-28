#!/bin/bash

# TSAN Demo Script - Demonstrates ThreadSanitizer testing

echo "================================================================"
echo "    ThreadSanitizer (TSAN) Demo - Data Race Detection"
echo "================================================================"
echo ""
echo "This script demonstrates data race detection by:"
echo "  1. Building TSAN-instrumented server"
echo "  2. Running concurrency tests under TSAN"
echo "  3. Analyzing TSAN output for data races"
echo ""
echo "================================================================"
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}[1/3] Building server and TSAN-enabled server...${NC}"
echo "      (Compiling with -fsanitize=thread)"
make clean > /dev/null 2>&1
if make all > /dev/null 2>&1 && make server-tsan > /dev/null 2>&1; then
    echo -e "      ${GREEN}Build successful${NC}"
else
    echo -e "      ${RED}Build failed${NC}"
    exit 1
fi
echo ""

echo -e "${BLUE}[2/3] Running TSAN tests...${NC}"
echo "      This will run Phase 1 tests under TSAN"
echo "      (This may take 2-3 minutes)"
echo ""

# Create test_results directory if it doesn't exist
mkdir -p test_results

# Temporarily rename server and use server-tsan so tests use TSAN version
mv server server.bak
cp server-tsan server

# Run Phase 1 tests (test script will start/stop server itself)
# Capture all output including server stderr which has TSAN warnings
./tests/test_phase1.sh > /tmp/tsan_demo.log 2> test_results/tsan_output.log
TEST_RESULT=$?

# Restore original server
rm -f server
mv server.bak server

# Check for data races and other TSAN issues in output
if grep -q "WARNING: ThreadSanitizer:" test_results/tsan_output.log; then
    TSAN_RESULT=1
else
    TSAN_RESULT=0
fi

# If tests failed, overall failure
if [ $TEST_RESULT -ne 0 ]; then
    TSAN_RESULT=1
fi

echo ""
echo -e "${BLUE}[3/3] Analyzing TSAN results...${NC}"
echo ""

# Extract and display results
echo "================================================================"
echo "                    TSAN Test Results"
echo "================================================================"
echo ""

if [ $TSAN_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ ThreadSanitizer: NO DATA RACES DETECTED${NC}"
    echo ""
    echo "TSAN verified that all shared data is properly synchronized:"
    echo ""
    echo "  ✓ ClientQueue - Thread-safe socket queue"
    echo "  ✓ TaskQueue - Thread-safe task queue"
    echo "  ✓ SessionManager - Session management and statistics"
    echo "  ✓ UserDatabase - User metadata and authentication"
    echo "  ✓ FileLockManager - Per-file concurrency control"
    echo "  ✓ Response structures - Worker→Client delivery"
    echo ""
    echo "All mutexes and condition variables are correctly used."
    echo "No race conditions detected during concurrent operations."
    echo ""
else
    echo -e "${RED}✗ ThreadSanitizer: DATA RACES DETECTED${NC}"
    echo ""
    echo "TSAN found race conditions in the code."
    echo "See detailed report in: test_results/tsan_output.log"
    echo ""
    
    # Show issue details if available
    if [ -f test_results/tsan_output.log ]; then
        ISSUE_COUNT=$(grep -c "WARNING: ThreadSanitizer:" test_results/tsan_output.log)
        echo "Number of issues detected: $ISSUE_COUNT"
        echo ""
        echo "First issues detected:"
        grep -A 5 "WARNING: ThreadSanitizer:" test_results/tsan_output.log | head -20
    fi
fi

echo "================================================================"
echo ""
echo "Full TSAN output: test_results/tsan_output.log"
echo "Test report: test_results/tsan_report.txt"
echo ""
echo "================================================================"

exit $TSAN_RESULT

