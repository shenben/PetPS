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
RUN_DIR="$LOGROOT/multiclient_$RUN_ID"
SERVER_DIR="$RUN_DIR/server"
CLIENT_DIR="$RUN_DIR/client"

THREAD_NUM=${THREAD_NUM:-2}
BATCH_READ=${BATCH_READ:-10}
ASYNC_REQ=${ASYNC_REQ:-1}
KEY_SPACE_M=${KEY_SPACE_M:-20}
VALUE_SIZE=${VALUE_SIZE:-512}
BENCH_SECS=${BENCH_SECS:-10}
DRAM_MMAP_GB=${DRAM_MMAP_GB:-64}
RNIC_ID=${RNIC_ID:-1}
GID_INDEX=${GID_INDEX:-3}
TIMEOUT_PAD=${TIMEOUT_PAD:-180}
SERVER_READY_TIMEOUT_S=${SERVER_READY_TIMEOUT_S:-0}

mkdir -p "$SERVER_DIR" "$CLIENT_DIR"

hard_kill_local() {
  pkill -9 -u "$USER" -f benchmark_client 2>/dev/null || true
  pkill -9 -u "$USER" -f "timeout .*benchmark_client" 2>/dev/null || true
}

hard_kill_remote() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "pkill -9 -u '$USER' -f petps_server 2>/dev/null || true" \
    >/dev/null 2>&1 || true
}

cleanup() {
  hard_kill_local
  hard_kill_remote
}

trap cleanup EXIT

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

backup_server_logs() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "SERVER_DIR='$SERVER_DIR' RUN_ID='$RUN_ID' bash -s" <<'SSH_EOF'
set -e
mkdir -p "$SERVER_DIR/prev"
for f in /tmp/server.log /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR; do
  if [ -e "$f" ]; then
    cp -a "$f" "$SERVER_DIR/prev/$(basename "$f").$RUN_ID" || true
  fi
done
for link in /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR; do
  if [ -L "$link" ]; then
    tgt=$(readlink -f "$link" || true)
    if [ -n "$tgt" ] && [ -e "$tgt" ]; then
      cp -a "$tgt" "$SERVER_DIR/prev/$(basename "$tgt")" || true
    fi
  fi
done
rm -f /tmp/petps_server.INFO /tmp/petps_server.WARNING /tmp/petps_server.ERROR /tmp/server.log || true
SSH_EOF
}

init_server_dirs() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "mkdir -p '$SERVER_DIR' '$SERVER_DIR/prev'"
}

ensure_memcached_conf() {
  printf "%s\n21111\n" "$CLIENT" > "$ROOT/third_party/Mayfly-main/memcached.conf"
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "printf '%s\n21111\n' '$CLIENT' > '$ROOT/third_party/Mayfly-main/memcached.conf'"
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

start_server() {
  local client_count="$1"
  local sdir="$SERVER_DIR/c${client_count}"
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" bash -s <<SSH_EOF
pkill -9 petps_server 2>/dev/null || true
cd "$ROOT"
mkdir -p "$sdir/glog"
export GLOG_log_dir="$sdir/glog"
export PETPS_DRAM_MMAP_GB=$DRAM_MMAP_GB
export PETPS_DRAM_SKIP_MLOCK=1
export PETPS_DRAM_SKIP_MEMSET=1
export PETPS_DRAM_SKIP_POPULATE=1
./build/bin/petps_server \
  --numa_id=0 --rnic_id=$RNIC_ID --gid_index=$GID_INDEX --global_id=0 \
  --num_server_processes=1 --num_client_processes=$client_count \
  --key_space_m=$KEY_SPACE_M --value_size=$VALUE_SIZE --thread_num=18 \
  --use_dram=true --use_sglist=false --max_kv_num_per_request=500 \
  --db=KVEnginePetKV --preload=false --check_after_preload=false \
  > "$sdir/server.log" 2>&1 &
echo "started" > "$sdir/started"
SSH_EOF
}

stop_server() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "pkill -9 petps_server 2>/dev/null || true"
}

wait_for_server_ready() {
  local client_count="$1"
  if [ "$SERVER_READY_TIMEOUT_S" -le 0 ]; then
    return 0
  fi
  local sdir="$SERVER_DIR/c${client_count}"
  local deadline=$((SECONDS + SERVER_READY_TIMEOUT_S))
  while [ $SECONDS -lt $deadline ]; do
    if sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
      "grep -q 'All PS polling threads ready' '$sdir/glog/petps_server.INFO' 2>/dev/null || \
       grep -q 'dsm has been initialized' '$sdir/server.log' 2>/dev/null"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

check_server_running() {
  local client_count="$1"
  local sdir="$SERVER_DIR/c${client_count}"
  if ! sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "pgrep -fa petps_server >/dev/null 2>&1"; then
    sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
      "echo 'server_not_running' > '$sdir/server.failed'" >/dev/null 2>&1 || true
    return 1
  fi
  return 0
}

run_clients() {
  local client_count="$1"
  local cdir="$CLIENT_DIR/c${client_count}"
  mkdir -p "$cdir"
  hard_kill_local
  sleep 1
  local pids=()
  for gid in $(seq 1 "$client_count"); do
    local ldir="$cdir/client${gid}"
    mkdir -p "$ldir/glog"
    (
      export GLOG_log_dir="$ldir/glog"
      timeout --signal=TERM --kill-after=10 "$((BENCH_SECS + TIMEOUT_PAD))" \
      "$ROOT/build/bin/benchmark_client" \
        --numa_id=0 --rnic_id=$RNIC_ID --gid_index=$GID_INDEX --global_id=$gid \
        --num_server_processes=1 --num_client_processes=$client_count \
        --thread_num=$THREAD_NUM --batch_read_count=$BATCH_READ --async_req_num=$ASYNC_REQ \
        --key_space_m=$KEY_SPACE_M --value_size=$VALUE_SIZE --dataset=zipfian \
        --zipf_theta=0.99 --read_ratio=100 --benchmark_seconds=$BENCH_SECS \
        --client_ready_timeout_s=240 \
        --skip_get_server_threadids=true --fallback_server_thread_count=2 \
        > "$ldir/client.log" 2>&1
      echo $? > "$ldir/exit_code"
    ) &
    pids+=("$!")
    sleep 1
  done
  local rc=0
  for p in "${pids[@]}"; do
    wait "$p" || rc=1
  done
  return $rc
}

summarize_clients() {
  local client_count="$1"
  local cdir="$CLIENT_DIR/c${client_count}"
  local total_mreq=0
  local total_mkv=0
  local status="PASS"

  for gid in $(seq 1 "$client_count"); do
    local ldir="$cdir/client${gid}"
    local rc="0"
    if [ -f "$ldir/exit_code" ]; then
      rc=$(cat "$ldir/exit_code" || echo "0")
    fi
    local thr_line
    thr_line=$(grep -E "^throughput" "$ldir/client.log" | tail -n 1 || true)
    if [ -z "$thr_line" ]; then
      status="FAIL"
      continue
    fi
    if [ "$rc" != "0" ]; then
      status="FAIL($rc)"
    fi
    local mreq mkv
    mreq=$(echo "$thr_line" | awk '{print $2}')
    mkv=$(echo "$thr_line" | awk '{print $4}')
    total_mreq=$(awk -v a="$total_mreq" -v b="$mreq" 'BEGIN{printf "%.4f", a+b}')
    total_mkv=$(awk -v a="$total_mkv" -v b="$mkv" 'BEGIN{printf "%.4f", a+b}')
  done

  echo -e "$client_count\t$THREAD_NUM\t$BATCH_READ\t$total_mreq\t$total_mkv\t$status" >> "$RUN_DIR/results.tsv"
}

ensure_memcached_conf
backup_client_logs
backup_server_logs
init_server_dirs

echo -e "clients\tthread_num\tbatch_read_count\tMreq_s_sum\tMkv_s_sum\tstatus" > "$RUN_DIR/results.tsv"

CLIENT_COUNTS=(2 4 8)

for c in "${CLIENT_COUNTS[@]}"; do
  hard_kill_local
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$USER@$SERVER" \
    "pkill -9 petps_server 2>/dev/null || true"
  reset_memcached
  start_server "$c"
  sleep 3
  if ! check_server_running "$c"; then
    echo -e "$c\t$THREAD_NUM\t$BATCH_READ\t0\t0\tFAIL(server_not_running)" >> "$RUN_DIR/results.tsv"
    stop_server
    sleep 2
    continue
  fi
  run_clients "$c" || true
  if ! wait_for_server_ready "$c"; then
    echo "Server not ready within ${SERVER_READY_TIMEOUT_S}s for client_count=$c" \
      | tee -a "$RUN_DIR/results.tsv"
  fi
  summarize_clients "$c"
  stop_server
  sleep 2
 done

echo "Run complete: $RUN_DIR"
