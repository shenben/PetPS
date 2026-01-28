# FROM nvcr.io/nvidia/merlin/merlin-hugectr:22.07 as devel

# FROM ubuntu:18.04 as devel
FROM ubuntu:18.04 as devel

# ENV http_proxy=http://10.74.176.8:11080
# ENV https_proxy=http://10.74.176.8:11080
ENV no_proxy=localhost,127.0.0.1,kwaidc.com

ENV CC=/usr/bin/gcc
ENV CXX=/usr/bin/g++

RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt install -y ca-certificates

# COPY ubuntu22.04.apt /etc/apt/sources.list
COPY ubuntu18.04.apt /etc/apt/sources.list

RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
vim gdb git wget tar unzip curl clang-format libboost-all-dev

RUN apt-get update -y && \
apt-get install -y --no-install-recommends \
    libboost-all-dev \
    libevent-dev \
    libdouble-conversion-dev \
    libgoogle-glog-dev \
    libgflags-dev \
    libiberty-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    zlib1g-dev \
    binutils-dev \
    libjemalloc-dev \
    libssl-dev \
    pkg-config \
    libunwind-dev \
    libunwind8-dev \
    libelf-dev \
    libdwarf-dev \
    cloc \
    check \
    sudo \
    libtbb-dev

RUN apt-get update -y && apt remove -y libgoogle-glog-dev

RUN apt-get update -y && apt-get install -y zsh \
    google-perftools \
    openssh-server software-properties-common \
    build-essential cmake

# glog
RUN cd /tmp && git clone https://github.com/google/glog && \
cd glog && \
git checkout v0.5.0 && \
mkdir build && \
cd build && \
cmake .. && \
make -j$(nproc) && \
make install

# fmt
RUN cd /tmp && git clone https://github.com/fmtlib/fmt.git && cd fmt && git checkout a34a97cc1d7d50ed6 && \
mkdir _build && cd _build && cmake .. && make -j$(nproc) && \
make install

# PB
RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt-get install -y libtool autoconf

RUN cd /tmp && wget https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.10.0.tar.gz && \
tar zxvf v3.10.0.tar.gz && \
cd protobuf-3.10.0 && \
./autogen.sh && \
./configure && \
make -j$(nproc) && \
make install

# memcached
RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt-get install -y libmemcached-dev

# gperftools
RUN cd /tmp && wget https://github.com/gperftools/gperftools/releases/download/gperftools-2.7/gperftools-2.7.tar.gz && \
tar -zxf gperftools-2.7.tar.gz && \
cd gperftools-2.7/ && \
./configure && \
make -j$(nproc) && \
make install

# NDCTL
RUN apt remove -y ndctl daxctl

RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt-get install -y git gcc g++ autoconf automake asciidoc asciidoctor bash-completion xmlto libtool pkg-config libglib2.0-0 libglib2.0-dev libfabric1 libfabric-dev doxygen graphviz pandoc libncurses5 libkmod2 libkmod-dev libudev-dev uuid-dev libjson-c-dev libkeyutils-dev libiniparser-dev
RUN cp /usr/include/iniparser/* /usr/include/

## install gcc-5
RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt install -y gcc-5 g++-5

RUN sed -i '$d' /etc/apt/sources.list
RUN sed -i '$d' /etc/apt/sources.list
# install gcc-5 end

RUN cd /tmp && git clone https://github.com/pmem/ndctl && \
cd ndctl && \
git checkout v64 && \
sed -i '176 i LDFLAGS=-lkeyutils ${LDFLAGS}' configure.ac && \
CC=$(which gcc-5) CXX=$(which g++-5) ./autogen.sh && \
CC=$(which gcc-5) CXX=$(which g++-5) ./configure CFLAGS='-g -O2' --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib64 && \
make -j$(nproc) && \
make check && \
make install

ENV LD_LIBRARY_PATH=/usr/lib64:$LD_LIBRARY_PATH

# ndctl done

RUN apt-get update -y && \
DEBIAN_FRONTEND=noninteractive apt install -y sshpass netcat

# IB driver
RUN cd /tmp && wget https://content.mellanox.com/ofed/MLNX_OFED-4.7-3.2.9.0/MLNX_OFED_LINUX-4.7-3.2.9.0-ubuntu18.04-x86_64.tgz && \
tar zxf MLNX_OFED_LINUX-4.7-3.2.9.0-ubuntu18.04-x86_64.tgz && \
cd MLNX_OFED_LINUX-4.7-3.2.9.0-ubuntu18.04-x86_64 && \
./mlnxofedinstall --user-space-only --without-fw-update --force && \
cd .. && rm MLNX_OFED_LINUX-4.7-3.2.9.0-ubuntu18.04-x86_64.tgz
# IB driver done

RUN git config --global user.email "you@example.com" && \
git config --global user.name "Your Name"

# Dependency: Build and install Redis native client.
ARG HIREDIS_VER=1.0.2
ARG REDIS_PP_VER=1.3.3
ARG ROCKSDB_VER=6.29.3
ARG RDKAFKA_VER=1.8.2

RUN git clone --branch v${HIREDIS_VER} --depth 1 https://github.com/redis/hiredis.git hiredis && \
    mkdir hiredis/build && \
    cd hiredis/build && \
    cmake .. && \
    make -j$(nproc) && \
    make install && \
    cd ../.. && \
    rm -rf hiredis

RUN git clone --branch ${REDIS_PP_VER} --depth 1 https://github.com/sewenew/redis-plus-plus.git redis_pp && \
    mkdir redis_pp/build && \
    cd redis_pp/build && \
    cmake -DREDIS_PLUS_PLUS_CXX_STANDARD=17 .. && \
    make -j$(nproc) && \
    make install && \
    cd ../.. && \
    rm -rf redis_pp

# Dependency: Build and install RocksDB.
RUN apt-get update -y && apt-get install -y libgflags-dev
RUN git clone --branch v${ROCKSDB_VER} --depth 1 https://github.com/facebook/rocksdb.git rocksdb && \
    cd rocksdb && \
    PORTABLE=1 make -j$(nproc) shared_lib && \
    make install-shared && \
    cd .. && \
    rm -rf rocksdb

# Dependency: Build and install RdKafka.
RUN git clone --branch v"${RDKAFKA_VER}" --depth 1 https://github.com/edenhill/librdkafka.git rdkafka && \
    cd rdkafka && \
    ./configure --enable-static && \
    make -j$(nproc) && \
    make install && \
    cd .. && \
    rm -rf rdkafka

RUN apt-get update && apt install -y gcc-11 g++-11

# tbb
RUN cd /tmp && \
wget https://registrationcenter-download.intel.com/akdlm/irc_nas/18728/l_tbb_oneapi_p_2021.6.0.835_offline.sh && \
sh l_tbb_oneapi_p_2021.6.0.835_offline.sh -a -s --eula accept

RUN mv /usr/bin/gcc /usr/bin/gcc.back
RUN mv /usr/bin/g++ /usr/bin/g++.back
RUN cp /usr/bin/gcc-11 /usr/bin/gcc
RUN cp /usr/bin/g++-11 /usr/bin/g++

# RUN source /opt/intel/oneapi/setvars.sh
