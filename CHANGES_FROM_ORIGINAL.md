# Changes from Original PetPS Repository

This document summarizes all modifications made to the original PetPS repository from https://github.com/thustorage/PetPS

## Summary of Changes

These changes improve portability, fix broken dependencies, and make the build system more flexible for different user environments.

---

## 1. Root CMakeLists.txt

### Changes:
- **Fixed compiler setting order**: Moved compiler definitions before `project()` command to avoid CMake reconfiguration loops
- **Made paths portable**: Changed hardcoded user paths to use `$ENV{HOME}` environment variable
  - `folly_DIR`: `/home/xieminhui/folly-install/...` → `$ENV{HOME}/folly-install/...`
  - `FOLLY_ROOT_DIR`: `/home/xieminhui/folly-install` → `$ENV{HOME}/folly-install`
  - `TBB_DIR`: `/home/xieminhui/intel/...` → `$ENV{HOME}/intel/...`
- **Made TBB optional**: Changed from `find_package(TBB REQUIRED)` to `find_package(TBB)` with warning message

### Impact:
- Works on any user's system without manual path modifications
- Continues build even if TBB is not found

---

## 2. src/base/CMakeLists.txt

### Changes:
- Added dependency check for Cityhash target to ensure proper build order

### Impact:
- Ensures Cityhash is built before base library when using external project

---

## 3. Source File Include Paths

### Files Modified:
- `src/kv_engine/PiBenchWrapperDash.cc`
- `src/pet_kv/PiBenchWrapper.cc`

### Changes:
- Fixed hardcoded include paths:
  - **Before**: `#include "/home/xieminhui/HashEvaluation/hash/common/hash_api.h"`
  - **After**: `#include "third_party/HashEvaluation-for-petps/hash/common/hash_api.h"`

### Impact:
- Uses project-local third_party directory instead of user-specific absolute paths
- Code is portable across different systems

---

## 4. src/pet_kv/CMakeLists.txt

### Changes:
- Commented out hardcoded copy commands that copied built libraries to user-specific paths
- Added optional alternative with environment variable support

### Impact:
- Build succeeds without requiring `/home/xieminhui/HashEvaluation/bin/` directory
- Users can uncomment and customize if needed

---

## 5. third_party/HashEvaluation-for-petps/CMakeLists.txt

### Major Changes:
- **Static linking for PMDK**: Changed from dynamic linking to static linking
  - Added `PMDK_LIB_DIR` variable pointing to built PMDK libraries
  - Changed `vmem pmemobj` to explicit static library paths
- **Added missing dependencies**: Added `ndctl` and `daxctl` libraries
- **Fixed include paths**: Added PMDK include directory for Clevel

### Libraries Affected:
- PMHashPCLHT
- PMHashLevel
- PMHashClevel
- PMHashCCEHVM

### Impact:
- Resolves linking errors with PMDK libraries
- Ensures all required dependencies are included

---

## 6. third_party/Mayfly-main/include/Rdma.h

### Changes:
- Enabled `OFED_VERSION_5` define (was commented out)

### Impact:
- Enables compatibility with OpenFabrics Enterprise Distribution version 5

---

## 7. third_party/cmake/FindCityhash.cmake

### Changes:
- **Fixed broken URL**: Changed from unmaintained fork to official Google repository
  - **Before**: `https://github.com/formath/cityhash/archive/1.1.1.tar.gz` (broken)
  - **After**: `https://github.com/google/cityhash.git` (official repo)
- Changed from tarball download to git clone

### Impact:
- Cityhash downloads successfully from maintained repository
- More reliable build process

---

## 8. tools/gen_dash_pmdk.sh

### Changes:
- Modified to skip documentation build (which requires `pandoc`)
- Temporarily modifies Makefile to remove `doc` target, then restores it
- Added error handling with `|| true` to continue on partial build

### Impact:
- Build succeeds without requiring pandoc package
- Reduces dependencies for basic functionality

---

## 9. tools/install-dependences.sh

### Changes:
- **Added version pin for fmt**: Added `git checkout 9.1.0` to ensure compatible version
- **Commented out glog removal**: Commented out `sudo apt remove -y glog`

### Impact:
- Ensures compatible fmt version (folly can be sensitive to fmt versions)
- More conservative about removing system packages

---

## 10. Deleted Files

### Files Removed:
- `restartMemcache.sh`

### Reason:
- Wrapper script pointing to another script location
- Not essential for core functionality

---

## Missing Packages and Additional Dependencies

### Required System Packages (from install-dependences.sh):
```bash
# Core build tools
gcc-9, g++, cmake, make

# Boost and event libraries
libboost-all-dev, libevent-dev

# Compression libraries
liblz4-dev, liblzma-dev, libsnappy-dev, zlib1g-dev

# Utility libraries
libdouble-conversion-dev, libgoogle-glog-dev, libgflags-dev
libiberty-dev, binutils-dev, libjemalloc-dev, libssl-dev
pkg-config, libunwind-dev, libtool

# PMDK dependencies (may need to be installed separately)
libndctl-dev, libdaxctl-dev
```

### Built from Source:
1. **fmt** (v9.1.0)
2. **glog** (v0.5.0)
3. **folly** (v2022.01.17.00)
4. **protobuf** (v3.10.0)
5. **gperftools** (v2.7)

### Python Packages:
```bash
pip3 install numpy pandas zmq paramiko
```

### Optional Dependencies:
- **TBB**: Intel Threading Building Blocks (made optional in CMakeLists.txt)
- **pandoc**: Only needed if building PMDK documentation (skipped in gen_dash_pmdk.sh)

---

## Build Instructions Summary

1. Install system dependencies:
   ```bash
   bash tools/install-dependences.sh
   ```

2. Build PMDK libraries:
   ```bash
   bash tools/gen_dash_pmdk.sh
   ```

3. Build the project:
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

---

## Portability Improvements

All user-specific hardcoded paths have been replaced with:
- Environment variables (`$ENV{HOME}`)
- Project-relative paths (`third_party/...`)
- Optional/configurable sections (commented out)

This fork should build on any Linux system with the required dependencies installed.
