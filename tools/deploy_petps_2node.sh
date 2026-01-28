#!/bin/bash
# PetPS 2-Node Deployment Script
# Node 246: 10.113.164.246 (memcached coordinator + server 0)
# Node 247: 10.113.164.247 (server 1)

set -e

# Configuration
PETPS_DIR="/home/pxg/PetPS"
NODE246="pxg@10.113.164.246"
NODE247="pxg@10.113.164.247"
PASSWORD="1234"

# Server IPs for RDMA communication (RoCE)
SERVER0_IP="10.10.2.246"
SERVER1_IP="10.10.2.247"

# Memcached
MEMCACHED_PORT="11211"

echo "=== PetPS 2-Node Deployment ==="

# Step 1: Sync binaries to both nodes
echo "[1/5] Syncing binaries to nodes..."
sshpass -p $PASSWORD scp -r $PETPS_DIR/bin $NODE246:/home/pxg/
sshpass -p $PASSWORD scp -r $PETPS_DIR/bin $NODE247:/home/pxg/
sshpass -p $PASSWORD scp -r $PETPS_DIR/lib $NODE246:/home/pxg/
sshpass -p $PASSWORD scp -r $PETPS_DIR/lib $NODE247:/home/pxg/

# Step 2: Start memcached on node 246 (coordinator)
echo "[2/5] Starting memcached on node 246..."
sshpass -p $PASSWORD ssh $NODE246 "pkill memcached 2>/dev/null; memcached -p $MEMCACHED_PORT -d && sleep 1 && pgrep memcached"
sshpass -p $PASSWORD ssh $NODE246 "echo 'Memcached started'"
# Update memcached.conf to point to node 246
sshpass -p $PASSWORD ssh $NODE246 "echo '10.113.164.246' > /home/pxg/PetPS/third_party/Mayfly-main/memcached.conf"
sshpass -p $PASSWORD ssh $NODE247 "echo '10.113.164.246' > /home/pxg/PetPS/third_party/Mayfly-main/memcached.conf"

# Step 3: Start server 0 on node 246
echo "[3/5] Starting server 0 on node 246..."
sshpass -p $PASSWORD ssh $NODE246 "cd /home/pxg/PetPS && rm -rf /dev/shm/petps_server0"
sshpass -p $PASSWORD ssh $NODE246 "cd /home/pxg/PetPS && ./bin/petps_server \
    --num_server_processes=2 \
    --global_id=0 \
    --numa_id=0 \
    --thread_num=8 \
    --key_space_m=100 \
    --value_size=64 \
    --db=KVEnginePetKV \
    --use_sglist=true \
    --preload=false \
    > /tmp/server0.log 2>&1 &"
echo "Server 0 started on node 246 (check /tmp/server0.log)"

# Step 4: Start server 1 on node 247
echo "[4/5] Starting server 1 on node 247..."
sshpass -p $PASSWORD ssh $NODE247 "cd /home/pxg/PetPS && rm -rf /dev/shm/petps_server1"
sshpass -p $PASSWORD ssh $NODE247 "cd /home/pxg/PetPS && ./bin/petps_server \
    --num_server_processes=2 \
    --global_id=1 \
    --numa_id=0 \
    --thread_num=8 \
    --key_space_m=100 \
    --value_size=64 \
    --db=KVEnginePetKV \
    --use_sglist=true \
    --preload=false \
    > /tmp/server1.log 2>&1 &"
echo "Server 1 started on node 247 (check /tmp/server1.log)"

# Wait for servers to initialize
echo "Waiting for servers to initialize..."
sleep 5

# Step 5: Run client on node 247
echo "[5/5] Running benchmark client on node 247..."
sshpass -p $PASSWORD ssh $NODE247 "cd /home/pxg/PetPS && ./bin/benchmark_client \
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
    --benchmark_seconds=30"

echo "=== Deployment Complete ==="
echo "Check logs:"
echo "  Server 0: $NODE246:/tmp/server0.log"
echo "  Server 1: $NODE247:/tmp/server1.log"
