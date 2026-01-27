#include "DSMKeeper.h"

#include "Connection.h"

const char *DSMKeeper::OK = "OK";
const char *DSMKeeper::ServerPrefix = "SPre";

void DSMKeeper::initLocalMeta() {
  ::write(STDERR_FILENO, "DEBUG: initLocalMeta() started\n", 31);
  localMeta.dsmBase = (uint64_t)dirCon[0]->dsmPool;
  localMeta.lockBase = (uint64_t)dirCon[0]->lockPool;
  localMeta.cacheBase = (uint64_t)thCon[0]->cachePool;

  // local node info
  localMeta.lid = thCon[0]->ctx.lid;
  memcpy((char *)localMeta.gid, (char *)(&thCon[0]->ctx.gid),
         16 * sizeof(uint8_t));
  char local_gid[64];
  snprintf(local_gid, sizeof(local_gid), "DEBUG: initLocalMeta() local lid=%u gid=%02x%02x...%02x%02x\n",
           localMeta.lid, localMeta.gid[0], localMeta.gid[1],
           localMeta.gid[14], localMeta.gid[15]);
  ::write(STDERR_FILENO, local_gid, strlen(local_gid));

  // per thread APP
  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    localMeta.appTh[i].rKey = thCon[i]->cacheMR->rkey;
    localMeta.appUdQpn[i] = thCon[i]->message->getQPN();
  }

  // per thread DIR
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    localMeta.dirTh[i].rKey = dirCon[i]->dsmMR->rkey;
    // localMeta.dirTh[i].lock_rkey = dirCon[i]->lockMR->rkey;
    localMeta.dirTh[i].cacheRKey = dirCon[i]->cacheMR->rkey;

    localMeta.dirUdQpn[i] = dirCon[i]->message->getQPN();
  }
  ::write(STDERR_FILENO, "DEBUG: initLocalMeta() done\n", 28);
}

bool DSMKeeper::connectNode(uint16_t remoteID) {
  char dbg[128];
  snprintf(dbg, sizeof(dbg), "DEBUG: connectNode(%d) started\n", remoteID);
  ::write(STDERR_FILENO, dbg, strlen(dbg));

  setDataToRemote(remoteID);

  std::string setK = setKey(remoteID);
  memSet(setK.c_str(), setK.size(), (char *)(&localMeta), sizeof(localMeta));
  snprintf(dbg, sizeof(dbg), "DEBUG: connectNode(%d) memSet localMeta\n", remoteID);
  ::write(STDERR_FILENO, dbg, strlen(dbg));

  std::string getK = getKey(remoteID);
  snprintf(dbg, sizeof(dbg), "DEBUG: connectNode(%d) getKey=%s (myNodeID=%d, remoteID=%d)\n",
           remoteID, getK.c_str(), getMyNodeID(), remoteID);
  ::write(STDERR_FILENO, dbg, strlen(dbg));

  ExchangeMeta *remoteMeta = (ExchangeMeta *)memGet(getK.c_str(), getK.size());
  if (!remoteMeta) {
    snprintf(dbg, sizeof(dbg), "ERROR: connectNode(%d) memGet failed for key=%s\n", remoteID, getK.c_str());
    ::write(STDERR_FILENO, dbg, strlen(dbg));
  } else {
    snprintf(dbg, sizeof(dbg), "DEBUG: connectNode(%d) memGet remoteMeta OK, lid=%u\n", remoteID, remoteMeta->lid);
    ::write(STDERR_FILENO, dbg, strlen(dbg));
  }

  setDataFromRemote(remoteID, remoteMeta);

  free(remoteMeta);
  snprintf(dbg, sizeof(dbg), "DEBUG: connectNode(%d) done\n", remoteID);
  ::write(STDERR_FILENO, dbg, strlen(dbg));
  return true;
}

void DSMKeeper::setDataToRemote(uint16_t remoteID) {
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    auto &c = dirCon[i];

    for (int k = 0; k < kv::kMaxNetThread; ++k) {
      localMeta.dirRcQpn2app[i][k] = c->data2app[k][remoteID]->qp_num;
    }
  }

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    auto &c = thCon[i];
    for (int k = 0; k < NR_DIRECTORY; ++k) {
      localMeta.appRcQpn2dir[i][k] = c->data[k][remoteID]->qp_num;
    }
  }
}

void DSMKeeper::setDataFromRemote(uint16_t remoteID, ExchangeMeta *remoteMeta) {
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    auto &c = dirCon[i];

    for (int k = 0; k < kv::kMaxNetThread; ++k) {
      auto &qp = c->data2app[k][remoteID];

      assert(qp->qp_type == IBV_QPT_RC);
      modifyQPtoInit(qp, &c->ctx);
      modifyQPtoRTR(qp, remoteMeta->appRcQpn2dir[k][i], remoteMeta->lid,
                    remoteMeta->gid, &c->ctx);
      modifyQPtoRTS(qp);
    }
  }

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    auto &c = thCon[i];
    for (int k = 0; k < NR_DIRECTORY; ++k) {
      auto &qp = c->data[k][remoteID];

      assert(qp->qp_type == IBV_QPT_RC);
      modifyQPtoInit(qp, &c->ctx);
      modifyQPtoRTR(qp, remoteMeta->dirRcQpn2app[k][i], remoteMeta->lid,
                    remoteMeta->gid, &c->ctx);
      modifyQPtoRTS(qp);
    }
  }

  auto &info = remoteCon[remoteID];
  info.dsmBase = remoteMeta->dsmBase;
  info.cacheBase = remoteMeta->cacheBase;
  info.lockBase = remoteMeta->lockBase;

  for (int i = 0; i < NR_DIRECTORY; ++i) {
    info.dsmRKey[i] = remoteMeta->dirTh[i].rKey;
    info.lockRKey[i] = remoteMeta->dirTh[i].lock_rkey;
    info.cacheRKey[i] = remoteMeta->dirTh[i].cacheRKey;
    info.dirMessageQPN[i] = remoteMeta->dirUdQpn[i];

    struct ibv_ah_attr ahAttr;
    // Debug: print remote GID/LID
    char gid_str[128];
    snprintf(gid_str, sizeof(gid_str), "DEBUG: setDataFromRemote[%d] remote lid=%u gid=%02x%02x...%02x%02x\n",
             remoteID, remoteMeta->lid, remoteMeta->gid[0], remoteMeta->gid[1],
             remoteMeta->gid[14], remoteMeta->gid[15]);
    ::write(STDERR_FILENO, gid_str, strlen(gid_str));

    fillAhAttr(&ahAttr, remoteMeta->lid, remoteMeta->gid, &dirCon[i]->ctx);
    info.dirAh[i] = ibv_create_ah(dirCon[i]->ctx.pd, &ahAttr);
    if (!info.dirAh[i]) {
      char err[128];
      snprintf(err, sizeof(err), "ERROR: ibv_create_ah failed for dirAh[%d], errno=%d\n", i, errno);
      ::write(STDERR_FILENO, err, strlen(err));
    }
    assert(info.dirAh[i]);
  }

  for (int k = 0; k < kv::kMaxNetThread; ++k) {

    info.appRKey[k] = remoteMeta->appTh[k].rKey;
    info.appMessageQPN[k] = remoteMeta->appUdQpn[k];

    struct ibv_ah_attr ahAttr;
    fillAhAttr(&ahAttr, remoteMeta->lid, remoteMeta->gid, &thCon[k]->ctx);
    info.appAh[k] = ibv_create_ah(thCon[k]->ctx.pd, &ahAttr);
    if (!info.appAh[k]) {
      char err[128];
      snprintf(err, sizeof(err), "ERROR: ibv_create_ah failed for appAh[%d], errno=%d\n", k, errno);
      ::write(STDERR_FILENO, err, strlen(err));
    }
    assert(info.appAh[k]);
  }
}

void DSMKeeper::connectMySelf() {
  ::write(STDERR_FILENO, "DEBUG: connectMySelf() started\n", 30);
  setDataToRemote(getMyNodeID());
  ::write(STDERR_FILENO, "DEBUG: connectMySelf() setDataToRemote done\n", 44);
  setDataFromRemote(getMyNodeID(), &localMeta);
  ::write(STDERR_FILENO, "DEBUG: connectMySelf() done\n", 27);
}


void DSMKeeper::barrier(const std::string &barrierKey, uint64_t k) {
  char dbg[128];
  snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s, %lu) started, myNodeID=%d, thread=%lu\n", barrierKey.c_str(), k, getMyNodeID(), pthread_self());
  ::write(STDERR_FILENO, dbg, strlen(dbg));

  std::string key = std::string("barrier-") + barrierKey;

  // Calculate the expected number of participants (servers + clients)
  uint16_t server_count = this->getServerNR();
  uint16_t client_count = this->getExpectedClientNR();
  uint16_t total_participants = server_count + client_count;

  // Determine if I should initialize the barrier
  // For k=1, only node 0 initializes and increments (first node does both)
  // For k>1, only the last node increments (does NOT initialize, just final increment)
  bool should_initialize = (k == 1 && this->getMyNodeID() == 0);
  bool should_final_increment = (k > 1 && this->getMyNodeID() == total_participants - 1);

  if (should_initialize) {
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) initializing to 0 and incrementing (myNodeID=%d, k=%lu)\n",
             barrierKey.c_str(), getMyNodeID(), k);
    ::write(STDERR_FILENO, dbg, strlen(dbg));
    memSet(key.c_str(), key.size(), "0", 1);
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) initialized by node %d\n", barrierKey.c_str(), getMyNodeID());
    ::write(STDERR_FILENO, dbg, strlen(dbg));

    // Node 0 increments after initialization for k=1
    uint64_t new_val = memFetchAndAdd(key.c_str(), key.size());
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) after memFetchAndAdd, myNodeID=%d, value=%lu\n", barrierKey.c_str(), getMyNodeID(), new_val);
    ::write(STDERR_FILENO, dbg, strlen(dbg));

    // Wait for all other nodes to reach k
    while (true) {
      uint64_t v = std::stoull(memGet(key.c_str(), key.size()));
      snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) checking: v=%lu, k=%lu\n", barrierKey.c_str(), v, k);
      ::write(STDERR_FILENO, dbg, strlen(dbg));
      if (v == k) {
        ::write(STDERR_FILENO, "DEBUG: barrier passed!\n", 22);
        return;
      }
      usleep(10000);
    }
  } else if (should_final_increment) {
    // For k>1, last node waits for initialization, then does final increment
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) waiting for initialization (last node, will increment)\n",
             barrierKey.c_str());
    ::write(STDERR_FILENO, dbg, strlen(dbg));
    while (true) {
      auto val = memGet(key.c_str(), key.size(), nullptr);
      if (val != nullptr) {
        free(val);
        break;
      }
      usleep(1000);
    }
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) initialization detected, doing final increment\n", barrierKey.c_str());
    ::write(STDERR_FILENO, dbg, strlen(dbg));

    // Wait until initializer has incremented (value >= 1)
    while (true) {
      uint64_t v = std::stoull(memGet(key.c_str(), key.size()));
      if (v >= 1) {
        break;
      }
      usleep(1000);
    }

    uint64_t new_val = memFetchAndAdd(key.c_str(), key.size());
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) after memFetchAndAdd, myNodeID=%d, value=%lu\n", barrierKey.c_str(), getMyNodeID(), new_val);
    ::write(STDERR_FILENO, dbg, strlen(dbg));

    // Wait for the barrier to be fully reached
    while (true) {
      uint64_t v = std::stoull(memGet(key.c_str(), key.size()));
      snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) checking: v=%lu, k=%lu\n", barrierKey.c_str(), v, k);
      ::write(STDERR_FILENO, dbg, strlen(dbg));
      if (v == k) {
        ::write(STDERR_FILENO, "DEBUG: barrier passed!\n", 22);
        return;
      }
      usleep(10000);
    }
  } else {
    // For other nodes (not initializer and not last node): wait and pass
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) waiting for initialization by node 0\n",
             barrierKey.c_str());
    ::write(STDERR_FILENO, dbg, strlen(dbg));
    while (true) {
      auto val = memGet(key.c_str(), key.size(), nullptr);
      if (val != nullptr) {
        free(val);
        break;
      }
      usleep(1000);
    }
    snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) initialization detected\n", barrierKey.c_str());
    ::write(STDERR_FILENO, dbg, strlen(dbg));

    // Wait until barrier is reached
    while (true) {
      uint64_t v = std::stoull(memGet(key.c_str(), key.size()));
      snprintf(dbg, sizeof(dbg), "DEBUG: barrier(%s) checking: v=%lu, k=%lu\n", barrierKey.c_str(), v, k);
      ::write(STDERR_FILENO, dbg, strlen(dbg));
      if (v == k) {
        ::write(STDERR_FILENO, "DEBUG: barrier passed!\n", 22);
        return;
      }
      usleep(10000);
    }
  }
}

uint64_t DSMKeeper::sum(const std::string &sum_key, uint64_t value) {
  std::string key_prefix = std::string("sum-") + sum_key;

  std::string key = key_prefix + std::to_string(this->getMyNodeID());
  memSet(key.c_str(), key.size(), (char *)&value, sizeof(value));

  uint64_t ret = 0;
  for (int i = 0; i < this->getServerNR(); ++i) {
    key = key_prefix + std::to_string(i);
    ret += *(uint64_t *)memGet(key.c_str(), key.size());
  }

  return ret;
}
