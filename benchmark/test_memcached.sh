#!/bin/bash

# Test Memcached connectivity between client and server
# Usage: PETPS_SSH_PASS='your_password' ./test_memcached.sh

set -e

# Get SSH password from environment variable
if [ -z "$PETPS_SSH_PASS" ]; then
    echo "ERROR: PETPS_SSH_PASS environment variable not set!"
    echo "Please run: export PETPS_SSH_PASS='your_password'"
    exit 1
fi

SERVER_IP="10.10.2.246"
SERVER_USER="pxg"
MEMCACHED_PORT=21211

echo "Testing Memcached connectivity..."
echo "Server: ${SERVER_IP}:${MEMCACHED_PORT}"
echo ""

# Test 1: Check if memcached is running on server
echo "Test 1: Check memcached process on server..."
if sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} \
    "pgrep -f memcached" 2>/dev/null | grep -q .; then
    echo "  [PASS] Memcached is running on server"
else
    echo "  [FAIL] Memcached is NOT running on server"
    exit 1
fi

# Test 2: Check if memcached port is listening
echo "Test 2: Check memcached port on server..."
if sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} \
    "ss -tlnp | grep ${MEMCACHED_PORT}" 2>/dev/null | grep -q .; then
    echo "  [PASS] Memcached port ${MEMCACHED_PORT} is listening"
else
    echo "  [FAIL] Memcached port ${MEMCACHED_PORT} is NOT listening"
    exit 1
fi

# Test 3: Set a key via memcached protocol
echo "Test 3: Set/get test key via memcached protocol..."
EXPECTED_VALUE="test_value_$$"
if printf "set test_key_${$} 0 0 ${#EXPECTED_VALUE}\r\n${EXPECTED_VALUE}\r\nquit\r\n" | \
    nc -w 3 ${SERVER_IP} ${MEMCACHED_PORT} | grep -q "STORED"; then
    echo "  [PASS] Successfully set test key"

    # Try to get it back
    RESPONSE=$(printf "get test_key_${$}\r\nquit\r\n" | nc -w 3 ${SERVER_IP} ${MEMCACHED_PORT})
    if echo "$RESPONSE" | grep -q "${EXPECTED_VALUE}"; then
        echo "  [PASS] Successfully retrieved test key"
    else
        echo "  [WARN] Could not retrieve test key (but set worked)"
    fi
else
    echo "  [FAIL] Failed to set test key"
    exit 1
fi

# Test 4: Check serverNum counter (used by DSM)
echo "Test 4: Check xmh-consistent-dsm counter..."
RESPONSE=$(printf "get xmh-consistent-dsm\r\nquit\r\n" | nc -w 3 ${SERVER_IP} ${MEMCACHED_PORT})
echo "  xmh-consistent-dsm value: $RESPONSE"

echo ""
echo "All connectivity tests passed!"
