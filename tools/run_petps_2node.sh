#!/bin/bash
# PetPS 2-Node Quick Start Guide
#
# Prerequisites on both nodes:
#   1. Memcached running on node 246 at 10.113.164.246:11211
#   2. PetPS binaries built and available
#   3. RDMA/RoCE connectivity between nodes
#
# Node IPs:
#   - Node 246: 10.113.164.246 (ethernet), 10.10.2.246 (RoCE)
#   - Node 247: 10.113.164.247 (ethernet), 10.10.2.247 (RoCE)

# === ON NODE 246 (Memcached Coordinator + Server 0) ===

# 1. Start memcached (if not already running)
pkill memcached 2>/dev/null
memcached -p 11211 -d
sleep 1

# 2. Update memcached.conf to point to coordinator
echo "10.113.164.246" > third_party/Mayfly-main/memcached.conf

# 3. Clean up previous state
rm -rf /dev/shm/petps_server0

# 4. Start Server 0 (global_id=0, first server)
./bin/petps_server \
    --num_server_processes=2 \
    --global_id=0 \
    --numa_id=0 \
    --thread_num=8 \
    --key_space_m=100 \
    --value_size=64 \
    --db=KVEnginePetKV \
    --use_sglist=true \
    --preload=false

# === ON NODE 247 (Server 1) ===

# 1. Update memcached.conf to point to coordinator
echo "10.113.164.246" > third_party/Mayfly-main/memcached.conf

# 2. Clean up previous state
rm -rf /dev/shm/petps_server1

# 3. Start Server 1 (global_id=1, second server)
./bin/petps_server \
    --num_server_processes=2 \
    --global_id=1 \
    --numa_id=0 \
    --thread_num=8 \
    --key_space_m=100 \
    --value_size=64 \
    --db=KVEnginePetKV \
    --use_sglist=true \
    --preload=false

# === ON NODE 247 (Client) ===

# Run benchmark client
./bin/benchmark_client \
    --num_server_processes=2 \
    --num_client_processes=1 \
    --global_id=2 \
    --thread_num=8 \
    --batch_read_count=300 \
    --async_req_num=1 \
    --key_space_m=100 \
    --value_size=64 \
    --zipf_theta=0.99 \
    --read_ratio=100 \
    --server_ip=10.10.2.246 \
    --benchmark_seconds=30

# === ALTERNATIVE: Run client on node 246 ===
./bin/benchmark_client \
    --num_server_processes=2 \
    --num_client_processes=1 \
    --global_id=2 \
    --thread_num=8 \
    --batch_read_count=300 \
    --async_req_num=1 \
    --key_space_m=100 \
    --value_size=64 \
    --zipf_theta=0.99 \
    --read_ratio=100 \
    --server_ip=10.10.2.247 \
    --benchmark_seconds=30
