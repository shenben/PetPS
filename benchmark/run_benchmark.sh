#!/bin/bash

# PetPS Benchmark Script
# Usage: PETPS_SSH_PASS='your_password' ./run_benchmark.sh

set -e

# Get SSH password from environment variable
if [ -z "$PETPS_SSH_PASS" ]; then
    echo "ERROR: PETPS_SSH_PASS environment variable not set!"
    echo "Please run: export PETPS_SSH_PASS='your_password'"
    exit 1
fi

SERVER_IP="10.10.2.246"
CLIENT_IP="10.10.2.247"
SERVER_USER="pxg"
CLIENT_USER="pxg"
SERVER_BIN="/home/pxg/PetPS/build/bin/perf_sgl"
CLIENT_BIN="/home/pxg/PetPS/build/bin/perf_sgl"
VALUE_SIZE=64
BATCH_READ_COUNT=300
KEY_SPACE_M=100
ZIPF_THETA=0.99
THREAD_NUM=1
ASYNC_REQ_NUM=8
MAX_KV_NUM_PER_REQUEST=1
DATASET="zipfian"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Kill existing processes
log_info "Killing existing perf_sgl processes on server..."
sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} "pkill -9 -f perf_sgl" 2>/dev/null || true

log_info "Killing existing perf_sgl processes on client..."
pkill -9 -f perf_sgl 2>/dev/null || true

sleep 2

# Set up memcached barrier on server (for DSM coordination)
log_info "Setting up xmh-consistent-dsm barrier on server..."
sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} \
    "printf 'set xmh-consistent-dsm 0 0 1\r\n0\r\nquit\r\n' | nc localhost 21211" 2>/dev/null || true

# Start server with longer timeout for RDMA initialization
log_info "Starting server on ${SERVER_IP}..."
sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} \
    "cd /home/pxg/PetPS/build && nohup ./bin/perf_sgl \
    --actor=server \
    --value_size=${VALUE_SIZE} \
    --key_space_m=${KEY_SPACE_M} \
    --batch_read_count=${BATCH_READ_COUNT} \
    --thread_num=${THREAD_NUM} \
    --async_req_num=${ASYNC_REQ_NUM} \
    --max_kv_num_per_request=${MAX_KV_NUM_PER_REQUEST} \
    --use_sglist=true \
    --use_dram=true \
    > /tmp/perf_sgl_server.log 2>&1 &" &

# Wait longer for server RDMA initialization (60 seconds)
log_info "Waiting for server RDMA initialization (60s)..."
sleep 60

# Verify server is running
if sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} "pgrep -f 'perf_sgl.*actor=server'" 2>/dev/null | grep -q .; then
    log_info "Server is running"
else
    log_error "Server failed to start!"
    sshpass -p "$PETPS_SSH_PASS" ssh -o StrictHostKeyChecking=no ${SERVER_USER}@${SERVER_IP} "cat /tmp/perf_sgl_server.log" 2>/dev/null
    exit 1
fi

# Start client
log_info "Starting client on ${CLIENT_IP}..."
cd /home/pxg/PetPS/build
nohup ./bin/perf_sgl \
    --actor=client \
    --value_size=${VALUE_SIZE} \
    --key_space_m=${KEY_SPACE_M} \
    --batch_read_count=${BATCH_READ_COUNT} \
    --thread_num=${THREAD_NUM} \
    --async_req_num=${ASYNC_REQ_NUM} \
    --max_kv_num_per_request=${MAX_KV_NUM_PER_REQUEST} \
    --use_sglist=true \
    --use_dram=true \
    --zipf_theta=${ZIPF_THETA} \
    --dataset=${DATASET} \
    > /tmp/perf_sgl_client.log 2>&1 &

log_info "Client started in background. Check /tmp/perf_sgl_client.log for results"
log_info "To monitor: tail -f /tmp/perf_sgl_client.log"
