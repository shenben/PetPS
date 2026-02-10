# PetPS: Supporting Huge Embedding Models with Tiered Memory


Embedding models are effective for learning high-dimensional sparse data in the scenarios of ad/recommendation/searching. 
Traditionally, they are deployed in DRAM parameter servers (PS) for online inference access. However, the ever-increasing model capacity makes this practice suffer from both high storage costs and long recovery time. 
Rapidly developing Persistent Memory (PM) offers new opportunities to PSs owing to its large capacity at low costs, as well as its persistence, while the application of PM also faces performance challenges.

This project introduce PetPS (<u>P</u>ersistent <u>E</u>mbedding <u>T</u>able <u>P</u>arameter <u>S</u>erver), a low-cost and high-performance PS on PM. It contains two main techniques:
- PetHash, a PM hash index tailored for embedding model workloads.
- NIC Gathering, which retrofits the DMA engine on NICs for gathering parameters.

For more details, please refer to our paper.

[[VLDB'23] PetPS: Supporting Huge Embedding Models with Persistent Memory](https://www.vldb.org/pvldb/vol16/p1013-xie.pdf)

If you find this repository useful, we would appreciate citations to our paper.


## System Requirements
- Mellanox ConnectX-5 NICs and above
- Intel Optane Persistent Memory (Gen1 or Gen2)
- RDMA Driver: MLNX_OFED_LINUX-4.9-5.1.0.0 (If you use MLNX_OFED_LINUX-5**, you should modify codes to resolve interface incompatibility - see Ubuntu 22.04 setup notes)
- Memcached (to exchange QP information)
- Software dependencies:
    - Folly v2022.01.17.00
    - GLog v0.5.0
    - fmt
    - PB v3.10.0
    - Intel OneAPI (libtbb v2021.6.0)
    - Boost 1.74 (for Ubuntu 22.04)



## Directory structure
```
petps
|---- benchmark        # evaluation scripts
|---- src              
    |---- base         # basic utilities used for programming
    |---- benchmark    # perf PS clients
    |---- kv_engine    # different kv engines (Dash, PetHash, etc.)
    |---- memory       # simple memory allocators and epoch-based reclaimnation
    |---- pet_kv       # implementation of PetHash
    |---- ps           # parameter server (including the implementation of NIC gathering technique)
|---- test             # unit tests
|---- tools            # small scripts
```

## Setup

### Step 1: Setup Dependences

#### Option 1: Ubuntu 18.04
- We provide script for installing 3rd-party dependencies, but it is tested only on ubuntu 18.04.

    `bash tools/install-dependences.sh`

- For other distributions of linux, you may manually install the dependencies as shown in `install-dependences.sh`.

#### Option 2: Ubuntu 22.04 (with MLNX_OFED_LINUX-5.x)

**Prerequisites:**
```bash
# Remove conflicting old boost installation
sudo mv /usr/local/include/boost /usr/local/include/boost.old
```

**Install Dependencies:**
```bash
# Install system dependencies
sudo apt install -y build-essential cmake libssl-dev libcurl4-openssl-dev \
    libgflags-dev libgtest-dev libgoogle-glog-dev libibverbs-dev \
    libncurses-dev libdw-dev libdwarf-dev libunwind-dev libaio-dev \
    libsodium-dev libbz2-dev liblzma-dev liblz4-dev libzstd-dev \
    libsnappy-dev libdouble-conversion-dev
```

**Build Folly (v2022.01.17.00):**
```bash
cd ~/folly
git checkout v2022.01.17.00
rm -rf _build && mkdir _build && cd _build
cmake .. -DCMAKE_PREFIX_PATH=/usr -DCMAKE_CXX_FLAGS="-fPIC" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j$(nproc)
rm -rf ~/folly-install && make DESTDIR=~/folly-install install
```

**Build Cityhash:**
```bash
cd ~/PetPS/build/cityhash/cityhash-master
./configure --prefix=$PWD/../install
make -j$(nproc) && make install
```

**Compile PetPS:**
```bash
cd ~/PetPS/build
rm -rf CMakeCache.txt CMakeFiles
cmake .. -DCMAKE_BUILD_TYPE=Release -DCITYHASH_ROOT_DIR=$HOME/PetPS/build/cityhash/install
make -j$(nproc)
```

**Note:** If you encounter boost version mismatch errors, ensure `/usr/local/include/boost` (old boost 1.62) is removed and only system boost 1.74 is used.

### Step 2: Setup Persistent Memory

- Set Optane DCPMM to AppDirect mode.

    `$ sudo ipmctl create -f -goal persistentmemorytype=appdirect`

- Switch the mode of PM between `fsdax` and `devdax`.

    - RDMA scatter-gather DMA (SG-DMA) requires PM in the `devdax` mode, while the implementation of some existing PM index (e.g. Dash) requires PM in the `fsdax` mode. 
    - We provide python scripts to switch the mode of PM between fsdax and devdax. Please see the python function `InitPMToFsDax` and `InitPMToDevDax` in `benchmark/exp_config.py`.
    - Don't worry, our script will automatically call the two functions aforementioned for each experiment :).

### Step 3: Setup RDMA NIC

- If you use RoCE and the MTU of your NIC is not equal to 4200 (check with ifconfig), modify the value path_mtu in `third_party/Mayfly-main/src/rdma/StateTrans.cpp`.

## Getting Started

- Compile the project.

    ```bash
    mkdir build; cd build
    # For Ubuntu 18.04:
    cmake .. -DCMAKE_BUILD_TYPE=Release
    # For Ubuntu 22.04 (with CITYHASH_ROOT_DIR):
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCITYHASH_ROOT_DIR=$HOME/PetPS/build/cityhash/install
    make -j
    ```

- Set the IPs of servers for evaluation in `benchmark/exp_config.py`.

- Set the IP and port of memcached server in `third_party/Mayfly-main/memcached.conf`.

- Set the variable `exp_lists` in `benchmark/bench.py`, to the experiments you want to run (all experiments in the VLDB paper can be seen in `exp_config.py`).

- Run the one-click startup script to replicate our experiments.

    `cd benchmark; python3 bench.py`

- You can find the log output in `benchmark/log`.

