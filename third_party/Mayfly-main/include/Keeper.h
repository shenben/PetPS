#ifndef __KEEPER__H__
#define __KEEPER__H__

#include <assert.h>
#include <infiniband/verbs.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <thread>

#include <libmemcached/memcached.h>

#include "Config.h"
#include "Debug.h"
#include "Rdma.h"

class Keeper {

protected:
  static const char *SERVER_NUM_KEY;
  static const char *CLIENT_NUM_KEY;

  uint32_t maxServer;
  uint16_t curServer;
  uint16_t myNodeID;
  std::string myIP;
  uint16_t myPort;

  memcached_st *memc;

public:
  uint16_t expectedClientNR;

protected:
  bool connectMemcached();
  bool disconnectMemcached();
  void serverConnect();
  void serverEnter(int globalID);
  virtual bool connectNode(uint16_t remoteID) = 0;

public:
  Keeper(uint32_t maxServer = 12);
  ~Keeper();

  uint16_t getMyNodeID() const { return this->myNodeID; }
  uint16_t getServerNR() const { return this->maxServer - this->expectedClientNR; }
  uint16_t getMyPort() const { return this->myPort; }
  uint16_t getExpectedClientNR() const { return this->expectedClientNR; }

  std::string getMyIP() const { return this->myIP; }
  memcached_st *getMemc() const { return this->memc; }

  // Get actual registered server count from memcached
  uint32_t getActualServerNum() const {
    size_t l;
    uint32_t flags;
    memcached_return rc;
    char *serverNumStr = memcached_get(memc, SERVER_NUM_KEY,
                                       strlen(SERVER_NUM_KEY), &l, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS && serverNumStr) {
      uint32_t serverNum = atoi(serverNumStr);
      free(serverNumStr);
      return serverNum;
    }
    return 0;
  }

  void memSet(const char *key, uint32_t klen, const char *val, uint32_t vlen);
  char *memGet(const char *key, uint32_t klen, size_t *v_size = nullptr);
  uint64_t memFetchAndAdd(const char *key, uint32_t klen);
};

#endif
