#ifndef __LINEAR_KEEPER__H__
#define __LINEAR_KEEPER__H__

#include <vector>

#include "Keeper.h"

struct ThreadConnection;
struct DirectoryConnection;
struct CacheAgentConnection;
struct RemoteConnection;

struct ExPerThread {

  uint32_t rKey;
  uint32_t cacheRKey; // for write/read directory's cache memory
  uint32_t lock_rkey; // for directory on-chip memory
} __attribute__((packed));

struct ExchangeMeta {

  uint16_t lid;
  uint8_t gid[16];

  uint64_t dsmBase;
  uint64_t cacheBase;
  uint64_t lockBase;

  ExPerThread appTh[kv::kMaxNetThread];
  ExPerThread dirTh[NR_DIRECTORY];

  uint32_t appUdQpn[kv::kMaxNetThread];
  uint32_t dirUdQpn[NR_DIRECTORY];

  uint32_t appRcQpn2dir[kv::kMaxNetThread][NR_DIRECTORY];

  uint32_t dirRcQpn2app[NR_DIRECTORY][kv::kMaxNetThread];

} __attribute__((packed));

class DSMKeeper : public Keeper {

private:
  static const char *OK;
  static const char *ServerPrefix;

  ThreadConnection **thCon;
  DirectoryConnection **dirCon;
  RemoteConnection *remoteCon;

  ExchangeMeta localMeta;

  std::vector<std::string> serverList;

  // Key format: "myNodeID-remoteID" for both storing and retrieving
  // This way, when node A stores for node B, node B can retrieve using getKey(A)
  std::string setKey(uint16_t remoteID) {
    return std::to_string(getMyNodeID()) + "-" + std::to_string(remoteID);
  }

  std::string getKey(uint16_t remoteID) {
    // To retrieve metadata stored by remote node, use the same format as setKey
    // remote node stored at: remoteNodeID-thisNodeID
    // so we retrieve using: remoteNodeID-thisNodeID
    return std::to_string(remoteID) + "-" + std::to_string(getMyNodeID());
  }

  void initLocalMeta();

  void connectMySelf();
  void initRouteRule();

  void setDataToRemote(uint16_t remoteID);
  void setDataFromRemote(uint16_t remoteID, ExchangeMeta *remoteMeta);

protected:
  virtual bool connectNode(uint16_t remoteID) override;

public:
  DSMKeeper(ThreadConnection **thCon, DirectoryConnection **dirCon,
            RemoteConnection *remoteCon, int globalID, uint32_t maxServer = 12, uint32_t expectedClientNR = 1)
      : Keeper(maxServer), thCon(thCon), dirCon(dirCon), remoteCon(remoteCon) {
    this->expectedClientNR = expectedClientNR;
    fprintf(stderr, "DEBUG: DSMKeeper constructor started, globalID=%d, expectedClientNR=%u\n", globalID, expectedClientNR);
    if (!connectMemcached()) {
      fprintf(stderr, "DEBUG: connectMemcached() failed, returning early\n");
      return;
    }
    fprintf(stderr, "DEBUG: connectMemcached() succeeded, calling serverEnter()\n");
    serverEnter(globalID);
    fprintf(stderr, "DEBUG: serverEnter() returned\n");
  }

  ~DSMKeeper() { disconnectMemcached(); }

  void run() {
    ::write(STDERR_FILENO, "DEBUG: DSMKeeper::run() started\n", 31);
    initLocalMeta();
    ::write(STDERR_FILENO, "DEBUG: DSMKeeper::run() after initLocalMeta\n", 43);

    // For non-first nodes: store metadata BEFORE connecting to servers
    // This ensures servers can find our metadata when they try to connect
    // The first server (ID 0) doesn't need this as it waits for others to connect
    // All other nodes (servers with ID > 0 and clients) must store metadata first
    if (getMyNodeID() > 0) {
      ::write(STDERR_FILENO, "DEBUG: DSMKeeper::run() storing metadata before connect (non-first node)\n", 73);
      // Store metadata for each server (servers have IDs 0 to serverNum-1)
      uint32_t serverNum = this->getServerNR();
      for (uint16_t sid = 0; sid < serverNum; sid++) {
        std::string setK = setKey(sid);
        memSet(setK.c_str(), setK.size(), (char *)(&localMeta), sizeof(localMeta));
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "DEBUG: DSMKeeper::run() stored metadata at %s\n", setK.c_str());
        ::write(STDERR_FILENO, dbg, strlen(dbg));
      }
      ::write(STDERR_FILENO, "DEBUG: DSMKeeper::run() stored metadata, now connecting\n", 58);
    }

    serverConnect();
    ::write(STDERR_FILENO, "DEBUG: DSMKeeper::run() after serverConnect\n", 42);
    connectMySelf();
    ::write(STDERR_FILENO, "DEBUG: DSMKeeper::run() completed\n", 33);
  }

  void barrier(const std::string &barrierKey, uint64_t k);
  uint64_t sum(const std::string &sum_key, uint64_t value);
};

#endif
