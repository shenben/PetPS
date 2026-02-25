# Pure DRAM Build and Benchmark Guide

This branch focuses on DRAM-only deployment (no PMem) with stable multi-client benchmarking and reproducible scripts.

**Scope**
- DRAM backend only (`--use_dram=true`, `--use_sglist=false`).
- RDMA over RoCE.
- Memcached for QP exchange.

**Build (DRAM only)**
1. Configure:
   ```bash
   cd /home/pxg/PetPS
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_PMEM=OFF
   make -j$(nproc)
   ```
2. If you use a non-default Folly install, either edit `CMakeLists.txt` or pass overrides:
   `-Dfolly_DIR=... -DFOLLY_ROOT_DIR=... -DTBB_DIR=...`.

**Runtime Flags Added/Used**
- `--rnic_id`: RDMA device index override (default follows `--numa_id`).
- `--gid_index`: RDMA GID index override (RoCE; e.g., 3).
- `--client_ready_timeout_s`: client thread ready timeout.
- `--skip_get_server_threadids` and `--fallback_server_thread_count` for safe client init.
- `--check_after_preload` to optionally validate preload.

**DRAM Memory Controls (Env Vars)**
- `PETPS_DRAM_MMAP_GB`: DRAM mmap size (GB). Example: `64`.
- `PETPS_DRAM_SKIP_MLOCK`: skip `mlock` for DRAM mmap.
- `PETPS_DRAM_SKIP_MEMSET`: skip zeroing the mmap region.
- `PETPS_DRAM_SKIP_POPULATE`: skip `MAP_POPULATE`.

**Important Limits**
- `kMaxNetThread` is set to 32 in `third_party/Mayfly-main/include/Common.h`.
- Keep `--thread_num <= 32` to avoid QP over-allocation.

**Memcached Configuration**
- Edit `third_party/Mayfly-main/memcached.conf` to point at your memcached server IP and port.
- Example (RoCE cluster used in tests):
  ```
  10.10.2.249
  21111
  ```

**Scripts (Recommended)**
- Small single-client sweep: `tools/run_small_bench_matrix.sh`
- Multi-client sweep (2/4/8 clients): `tools/run_multiclient_test.sh`

**Example Usage**
- Small sweep:
  ```bash
  cd /home/pxg/PetPS
  KEY_SPACE_M=20 DRAM_MMAP_GB=64 ./tools/run_small_bench_matrix.sh
  ```
- Multi-client sweep:
  ```bash
  cd /home/pxg/PetPS
  KEY_SPACE_M=20 DRAM_MMAP_GB=64 THREAD_NUM=8 BATCH_READ=100 BENCH_SECS=120 \
    ./tools/run_multiclient_test.sh
  ```

**Benchmark Records (DRAM, RoCE, GID index 3)**
These are recorded from this branch using the scripts above. Logs are stored under `/home/pxg/PetPS/logs/...` in the test environment.

1. Single client matrix (KEY_SPACE_M=20, DRAM_MMAP_GB=64, value_size=512)

| thread_num | batch_read_count | Mreq/s | Mkv/s | status |
|---|---|---|---|---|
| 1 | 1 | 0.2373 | 0.2373 | PASS |
| 1 | 10 | 0.1123 | 1.1227 | PASS |
| 1 | 50 | 0.0549 | 2.7427 | PASS |
| 1 | 100 | 0.0277 | 2.7690 | PASS |
| 2 | 1 | 0.4770 | 0.4770 | PASS |
| 2 | 10 | 0.2204 | 2.2036 | PASS |
| 2 | 50 | 0.0412 | 2.0588 | PASS |
| 2 | 100 | 0.0550 | 5.4995 | PASS |
| 4 | 1 | 0.6850 | 0.6850 | PASS |
| 4 | 10 | 0.4769 | 4.7688 | PASS |
| 4 | 50 | 0.1418 | 7.0910 | PASS |
| 4 | 100 | 0.0945 | 9.4457 | PASS |
| 8 | 1 | 1.7446 | 1.7446 | PASS |
| 8 | 10 | 0.7363 | 7.3627 | PASS |
| 8 | 50 | 0.2672 | 13.3604 | PASS |
| 8 | 100 | 0.1222 | 12.2174 | PASS |

2. Multi-client (small scale, KEY_SPACE_M=5, DRAM_MMAP_GB=32, THREAD_NUM=1, BATCH_READ=10)

| clients | Mreq/s sum | Mkv/s sum | status |
|---|---|---|---|
| 2 | 0.0002 | 0.0012 | PASS |
| 4 | 0.0004 | 0.0024 | PASS |
| 8 | 0.0000 | 0.0024 | PASS |

3. Multi-client (medium scale, KEY_SPACE_M=20, DRAM_MMAP_GB=64, THREAD_NUM=8, BATCH_READ=100)

| clients | Mreq/s sum | Mkv/s sum | status |
|---|---|---|---|
| 2 | 0.0002 | 0.0160 | PASS |
| 4 | 0.0000 | 0.0085 | PASS |
| 8 | 0.0000 | 0.0066 | PASS |

**Notes**
- Throughput is intentionally small in multi-client tests with low thread count and short duration; increase `THREAD_NUM`, `ASYNC_REQ`, and `BATCH_READ` to push higher utilization.
- DRAM runs are non-persistent. Pools and metadata are reinitialized on every start.
