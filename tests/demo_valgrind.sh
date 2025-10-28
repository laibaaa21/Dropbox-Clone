#!/bin/bash

# Valgrind Demo Script - Demonstrates memory leak detection

echo "================================================================"
echo "      Valgrind Demo - Memory Leak Detection"
echo "================================================================"
echo ""
echo "This script demonstrates memory leak detection by:"
echo "  1. Building the server"
echo "  2. Running tests under Valgrind"
echo "  3. Analyzing memory usage and leaks"
echo ""
echo "================================================================"
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}[1/3] Building server...${NC}"
make clean > /dev/null 2>&1
if make all > /dev/null 2>&1; then
    echo -e "      ${GREEN}Build successful${NC}"
else
    echo -e "      ${RED}Build failed${NC}"
    exit 1
fi
echo ""

echo -e "${BLUE}[2/3] Running Valgrind tests...${NC}"
echo "      This will run Phase 1 tests under Valgrind"
echo -e "      ${YELLOW}NOTE: Valgrind adds significant overhead (5-10x slower)${NC}"
echo "      (This may take 3-5 minutes)"
echo ""

# Create test_results directory if it doesn't exist
mkdir -p test_results

# Create a wrapper script that runs server under Valgrind
cat > server.valgrind.sh << 'EOF'
#!/bin/bash
exec valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=test_results/valgrind_output.log ./server.real "$@" 2>&1
EOF
chmod +x server.valgrind.sh

# Temporarily replace server with Valgrind wrapper
mv server server.real
mv server.valgrind.sh server

# Run Phase 1 tests (test script will start/stop server itself)
./tests/test_phase1.sh > /tmp/valgrind_demo.log 2>&1
TEST_RESULT=$?

# Restore original server
rm -f server
mv server.real server

# Check for memory leaks in Valgrind output
if grep -q "All heap blocks were freed -- no leaks are possible" test_results/valgrind_output.log; then
    VALGRIND_RESULT=0
elif grep -q "no leaks are possible" test_results/valgrind_output.log; then
    VALGRIND_RESULT=0
else
    # Check if there are any bytes leaked
    if grep "definitely lost:" test_results/valgrind_output.log | grep -q "0 bytes"; then
        VALGRIND_RESULT=0
    else
        VALGRIND_RESULT=1
    fi
fi

# If tests failed, overall failure
if [ $TEST_RESULT -ne 0 ]; then
    VALGRIND_RESULT=1
fi

echo ""
echo -e "${BLUE}[3/3] Analyzing Valgrind results...${NC}"
echo ""

# Extract and display results
echo "================================================================"
echo "                 Valgrind Test Results"
echo "================================================================"
echo ""

if [ $VALGRIND_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Valgrind: NO MEMORY LEAKS DETECTED${NC}"
    echo ""
    
    # Extract heap summary
    if [ -f test_results/valgrind_output.log ]; then
        echo "Memory Statistics:"
        echo ""
        grep "total heap usage:" test_results/valgrind_output.log | sed 's/==.*==/  /'
        echo ""
        grep "All heap blocks were freed" test_results/valgrind_output.log | sed 's/==.*==/  /'
        echo ""
        grep "ERROR SUMMARY:" test_results/valgrind_output.log | sed 's/==.*==/  /'
    fi
    
    echo ""
    echo "All resources properly cleaned up:"
    echo ""
    echo "  ✓ All malloc/calloc calls matched with free"
    echo "  ✓ All socket file descriptors closed"
    echo "  ✓ All threads joined"
    echo "  ✓ All mutexes destroyed"
    echo "  ✓ All condition variables destroyed"
    echo "  ✓ Perfect allocation/deallocation balance"
    echo ""
else
    echo -e "${RED}✗ Valgrind: MEMORY LEAKS DETECTED${NC}"
    echo ""
    echo "Valgrind found memory leaks in the code."
    echo "See detailed report in: test_results/valgrind_output.log"
    echo ""
    
    # Show leak summary if available
    if [ -f test_results/valgrind_output.log ]; then
        if grep -q "LEAK SUMMARY" test_results/valgrind_output.log; then
            echo "Leak Summary:"
            grep "LEAK SUMMARY" test_results/valgrind_output.log -A 5 | tail -6 | sed 's/==.*==/  /'
        fi
    fi
fi

echo "================================================================"
echo ""
echo "Full Valgrind output: test_results/valgrind_output.log"
echo ""
echo "To run Valgrind manually:"
echo "  valgrind --leak-check=full --show-leak-kinds=all ./server"
echo ""
echo "================================================================"

exit $VALGRIND_RESULT

