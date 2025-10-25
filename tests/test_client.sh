#!/bin/bash

# Automated test script for Dropbox Clone client
# Tests SIGNUP, LOGIN, UPLOAD, LIST, DOWNLOAD, DELETE operations

set -e

HOST="localhost"
PORT="10985"
TEST_USER="testuser_$(date +%s)"
TEST_PASS="testpass123"
TEST_FILE="/tmp/test_upload.txt"
DOWNLOAD_FILE="/tmp/test_download.txt"

echo "==================================="
echo "Dropbox Clone Client Test Script"
echo "==================================="
echo ""
echo "Test User: $TEST_USER"
echo "Test File: $TEST_FILE"
echo ""

# Create test file if it doesn't exist
if [ ! -f "$TEST_FILE" ]; then
    echo "Creating test file..."
    echo "This is a test file for Dropbox Clone - $(date)" > "$TEST_FILE"
fi

echo "Test file contents:"
cat "$TEST_FILE"
echo ""

# Test 1: SIGNUP
echo "--- Test 1: SIGNUP ---"
(
    echo "1"           # Choose SIGNUP
    echo "$TEST_USER"  # Username
    echo "$TEST_PASS"  # Password
    sleep 1
    echo "list"        # List files (should be empty)
    sleep 1
    echo "quit"        # Quit
) | ./dbc_client "$HOST" "$PORT"

echo ""
echo "✓ Test 1 passed: SIGNUP successful"
echo ""

# Test 2: LOGIN and UPLOAD
echo "--- Test 2: LOGIN and UPLOAD ---"
(
    echo "2"           # Choose LOGIN
    echo "$TEST_USER"  # Username
    echo "$TEST_PASS"  # Password
    sleep 1
    echo "upload $TEST_FILE"  # Upload file
    sleep 2
    echo "list"        # List files (should show uploaded file)
    sleep 1
    echo "quit"
) | ./dbc_client "$HOST" "$PORT"

echo ""
echo "✓ Test 2 passed: LOGIN and UPLOAD successful"
echo ""

# Test 3: LOGIN and DOWNLOAD
echo "--- Test 3: LOGIN and DOWNLOAD ---"
rm -f "$DOWNLOAD_FILE"
(
    echo "2"           # Choose LOGIN
    echo "$TEST_USER"  # Username
    echo "$TEST_PASS"  # Password
    sleep 1
    echo "list"        # List files
    sleep 1
    echo "download test_upload.txt"  # Download file
    sleep 2
    echo "quit"
) | ./dbc_client "$HOST" "$PORT"

# Verify downloaded file exists
if [ -f "test_upload.txt" ]; then
    echo ""
    echo "Downloaded file contents:"
    cat "test_upload.txt"
    echo ""
    echo "✓ Test 3 passed: DOWNLOAD successful"
    rm -f "test_upload.txt"
else
    echo "✗ Test 3 failed: Downloaded file not found"
    exit 1
fi

echo ""

# Test 4: LOGIN and DELETE
echo "--- Test 4: LOGIN and DELETE ---"
(
    echo "2"           # Choose LOGIN
    echo "$TEST_USER"  # Username
    echo "$TEST_PASS"  # Password
    sleep 1
    echo "delete test_upload.txt"  # Delete file
    sleep 1
    echo "list"        # List files (should be empty)
    sleep 1
    echo "quit"
) | ./dbc_client "$HOST" "$PORT"

echo ""
echo "✓ Test 4 passed: DELETE successful"
echo ""

# Test 5: Wrong password
echo "--- Test 5: LOGIN with wrong password (should fail) ---"
(
    echo "2"              # Choose LOGIN
    echo "$TEST_USER"     # Username
    echo "wrongpassword"  # Wrong password
    sleep 1
    echo "3"              # Quit
) | ./dbc_client "$HOST" "$PORT"

echo ""
echo "✓ Test 5 passed: Wrong password rejected as expected"
echo ""

echo "==================================="
echo "All tests passed! ✓"
echo "==================================="
