#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "Common.h"

class CacheConfig {
public:
  uint32_t cacheSize;

  CacheConfig(uint32_t cacheSize = 128) : cacheSize(cacheSize) {} // MB
};

struct ClusterInfo {
  uint32_t serverNR;
  uint32_t clientNR;

  ClusterInfo(uint32_t n) : serverNR(n), clientNR(0) {}
  ClusterInfo() : ClusterInfo(2) {}
};

class DSMConfig {
public:
  CacheConfig cacheConfig;

  ClusterInfo cluster_info;
  uint32_t machineNR; //

  uint64_t baseAddr; // in bytes
  uint64_t dsmSize;  // in bytes
  bool is_client;

  char NIC_name;
  bool skip_barrier;  // skip barrier in constructor, call manually later

  DSMConfig(const CacheConfig &cacheConfig = CacheConfig(),
            ClusterInfo cluster_info = ClusterInfo(),
            uint64_t dsmSize = 64, bool is_client = false, bool skip_barrier = false)
      : cacheConfig(cacheConfig), cluster_info(cluster_info), dsmSize(dsmSize),
        is_client(is_client), skip_barrier(skip_barrier) {
    machineNR = cluster_info.clientNR + cluster_info.serverNR;
    baseAddr = 0;
  }
};

#endif /* __CONFIG_H__ */
