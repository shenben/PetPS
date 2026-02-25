#!/usr/bin/env bash
set -euo pipefail

SERVER=10.10.2.248
CLIENT=10.10.2.249
USER=pxg
PASS=ppppxg
SUDO_PASS=${SUDO_PASS:-$PASS}
ROOT=/home/pxg/PetPS
LOGROOT="$ROOT/logs"
RUN_ID=$(date +%Y%m%d_%H%M%S)
RUN_DIR="$LOGROOT/run_$RUN_ID"
SERVER_DIR="$RUN_DIR/server"
CLIENT_DIR="$RUN_DIR/client"
KEY_SPACE_M=${KEY_SPACE_M:-20}
VALUE_SIZE=${VALUE_SIZE:-512}
DRAM_MMAP_GB=${DRAM_MMAP_GB:-64}

mkdir -p "$SERVER_DIR" "$CLIENT_DIR"

hard_kill_local() {
  pkill -9 -u "$USER" -f benchmark_client 2>/dev/null || true
  pkill -9 -u "$USER" -f "timeout .*benchmark_client" 2>/dev/null || true
}

ensure_memcached_conf() {
  printf "%s\n21111\n" "$CLIENT" > "$ROOT/third_party/Mayfly-main/memcached.conf"
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "printf '%s\n21111\n' '$CLIENT' > '$ROOT/third_party/Mayfly-main/memcached.conf'"
}

backup_client_logs() {
  local dst="$CLIENT_DIR/prev"
  mkdir -p "$dst"
  hard_kill_local
  for f in /tmp/server.log /tmp/client.log \
           /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR \
           /tmp/benchmark_client.INFO /tmp/benchmark_client.WARNING /tmp/benchmark_client.ERROR /tmp/benchmark_client.FATAL; do
    if [ -e "$f" ]; then
      cp -a "$f" "$dst/$(basename "$f").$RUN_ID" || true
    fi
  done
  for link in /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR \
              /tmp/benchmark_client.INFO /tmp/benchmark_client.WARNING /tmp/benchmark_client.ERROR; do
    if [ -L "$link" ]; then
      tgt=$(readlink -f "$link" || true)
      if [ -n "$tgt" ] && [ -e "$tgt" ]; then
        cp -a "$tgt" "$dst/$(basename "$tgt")" || true
      fi
    fi
  done
  rm -f /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR \
        /tmp/benchmark_client.INFO /tmp/benchmark_client.WARNING /tmp/benchmark_client.ERROR \
        /tmp/server.log /tmp/client.log || true
}

reset_memcached() {
  pkill -9 -f "memcached .* -p 21111" 2>/dev/null || true
  if [ -e /tmp/memcached.pid ]; then
    pkill -F /tmp/memcached.pid 2>/dev/null || true
    rm -f /tmp/memcached.pid
  fi
  if pgrep -af "memcached .* -p 21111" >/dev/null 2>&1; then
    printf '%s\n' "$SUDO_PASS" | sudo -S pkill -9 -f "memcached .* -p 21111" \
      >/dev/null 2>&1 || true
  fi
  memcached -u "$USER" -l "$CLIENT" -p 21111 -c 10000 -d -P /tmp/memcached.pid -I 8m || true
  sleep 2
  if command -v nc >/dev/null 2>&1; then
    printf "flush_all\r\n" | nc -q 1 "$CLIENT" 21111 || true
  fi
}

backup_server_logs() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" bash -s <<EOF
set -e
dst="$SERVER_DIR/prev"
mkdir -p "$SERVER_DIR" "$SERVER_DIR/prev"
for f in /tmp/server.log /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR; do
  if [ -e "\$f" ]; then
    cp -a "\$f" "\$dst/\$(basename "\$f").$RUN_ID" || true
  fi
done
for link in /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR; do
  if [ -L "\$link" ]; then
    tgt=\$(readlink -f "\$link" || true)
    if [ -n "\$tgt" ] && [ -e "\$tgt" ]; then
      cp -a "\$tgt" "\$dst/\$(basename "\$tgt")" || true
    fi
  fi
done
rm -f /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR /tmp/server.log || true
EOF
}

stop_server() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "pkill -9 petps_server 2>/dev/null || true"
}

start_server_test() {
  local t="$1"
  local b="$2"
  local sdir="$SERVER_DIR/t${t}_b${b}"
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" bash -s <<EOF
pkill -9 petps_server 2>/dev/null || true
cd "$ROOT"
mkdir -p "$sdir/glog"
export GLOG_log_dir="$sdir/glog"
export PETPS_DRAM_MMAP_GB=$DRAM_MMAP_GB
export PETPS_DRAM_SKIP_MLOCK=1
export PETPS_DRAM_SKIP_MEMSET=1
export PETPS_DRAM_SKIP_POPULATE=1
./build/bin/petps_server \\
  --numa_id=0 --rnic_id=1 --gid_index=3 --global_id=0 \\
  --num_server_processes=1 --num_client_processes=1 \\
  --key_space_m=$KEY_SPACE_M --value_size=$VALUE_SIZE --thread_num=18 \\
  --use_dram=true --use_sglist=false --max_kv_num_per_request=500 \\
  --db=KVEnginePetKV --preload=false --check_after_preload=false \\
  > "$sdir/server.log" 2>&1 &
EOF
  echo "$sdir"
}

run_client_one() {
  local t="$1"
  local b="$2"
  local tdir="$CLIENT_DIR/t${t}_b${b}"
  mkdir -p "$tdir/glog"
  export GLOG_log_dir="$tdir/glog"
  local rc=0
  set +e
  "$ROOT/build/bin/benchmark_client" \
    --numa_id=0 --rnic_id=1 --gid_index=3 --global_id=1 \
    --num_server_processes=1 --num_client_processes=1 \
    --thread_num="$t" --batch_read_count="$b" --async_req_num=1 \
    --key_space_m=$KEY_SPACE_M --value_size=$VALUE_SIZE --dataset=zipfian \
    --zipf_theta=0.99 --read_ratio=100 --benchmark_seconds=10 \
    --client_ready_timeout_s=240 \
    > "$tdir/client.log" 2>&1
  rc=$?
  set -e

  local thr_line
  thr_line=$(grep -E "^throughput" "$tdir/client.log" | tail -n 1 || true)
  local mreq="NA"
  local mkv="NA"
  local status="PASS"
  if [ $rc -ne 0 ]; then
    status="FAIL($rc)"
  fi
  if [ -n "$thr_line" ]; then
    mreq=$(echo "$thr_line" | awk '{print $2}')
    mkv=$(echo "$thr_line" | awk '{print $4}')
  fi
  echo -e "$t\t$b\t$mreq\t$mkv\t$status" >> "$RUN_DIR/results.tsv"
}

fetch_server_logs() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "tar -C '$SERVER_DIR' -czf /tmp/petps_server_logs_${RUN_ID}.tgz ." >/dev/null 2>&1 || true
  sshpass -p "$PASS" scp -o StrictHostKeyChecking=no \
    "$USER@$SERVER:/tmp/petps_server_logs_${RUN_ID}.tgz" \
    "$SERVER_DIR/server_logs_${RUN_ID}.tgz" >/dev/null 2>&1 || true
  if [ -f "$SERVER_DIR/server_logs_${RUN_ID}.tgz" ]; then
    tar -C "$SERVER_DIR" -xzf "$SERVER_DIR/server_logs_${RUN_ID}.tgz" >/dev/null 2>&1 || true
  fi
}

ensure_memcached_conf
backup_client_logs
backup_server_logs

echo -e "thread_num\tbatch_read_count\tMreq_s\tMkv_s\tstatus" > "$RUN_DIR/results.tsv"

THREADS=(1 2 4 8)
BATCHES=(1 10 50 100)

for t in "${THREADS[@]}"; do
  for b in "${BATCHES[@]}"; do
    reset_memcached
    sdir=$(start_server_test "$t" "$b")
    sleep 5
    run_client_one "$t" "$b"
    stop_server
    sleep 2
  done
 done

fetch_server_logs

echo "Run complete: $RUN_DIR"
