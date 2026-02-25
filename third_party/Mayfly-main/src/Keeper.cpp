#include "Keeper.h"
#include "mayfly_config.h"
#include <fstream>
#include <iostream>
#include <random>

char *getIP();

std::string trim(const std::string &s) {
  std::string res = s;
  if (!res.empty()) {
    res.erase(0, res.find_first_not_of(" "));
    res.erase(res.find_last_not_of(" ") + 1);
  }
  return res;
}

const char *Keeper::SERVER_NUM_KEY = "serverNum";

namespace {
void EnsureServerNumKey(memcached_st *memc) {
  if (!memc) return;
  const char *key = "serverNum";
  const char *val = "0";
  memcached_return rc =
      memcached_add(memc, key, strlen(key), val, 1, (time_t)0, (uint32_t)0);
  // If key already exists, NOTSTORED is expected and harmless.
  if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTSTORED &&
      rc != MEMCACHED_DATA_EXISTS) {
    fprintf(stderr, "ServerNum init failed: %s\n",
            memcached_strerror(memc, rc));
  }
}
}  // namespace

Keeper::Keeper(uint32_t maxServer)
    : maxServer(maxServer), curServer(0), memc(NULL) {}

Keeper::~Keeper() {
  //   listener.detach();

  disconnectMemcached();
}

bool Keeper::connectMemcached() {
  memcached_server_st *servers = NULL;
  memcached_return rc;

  std::ifstream conf(MAYFLY_PATH "/memcached.conf");

  if (!conf) {
    fprintf(stderr, "can't open memcached.conf\n");
    return false;
  }
  const char *env_p = std::getenv("LC_DEBUG_XMH");
  std::string addr, port;
  if (env_p != nullptr && strcmp(env_p, "LC_DEBUG_XMH") == 0) {
    std::cout << "use memcached in 130:21111" << std::endl;
    addr = "10.0.2.130";
    port = "21111";
  } else {
    std::getline(conf, addr);
    std::getline(conf, port);
    std::cout << "use memcached in " << trim(addr) << ":" << trim(port)
              << std::endl;
  }

  memc = memcached_create(NULL);
  servers = memcached_server_list_append(servers, trim(addr).c_str(),
                                         std::stoi(trim(port)), &rc);
  rc = memcached_server_push(memc, servers);

  if (rc != MEMCACHED_SUCCESS) {
    fprintf(stderr, "Counld't add server:%s\n", memcached_strerror(memc, rc));
    return false;
  }

  // Avoid indefinite blocking on memcached I/O in degraded networks.
  const uint64_t timeout_ms = 2000;
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT, timeout_ms);
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_RCV_TIMEOUT, timeout_ms);
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_SND_TIMEOUT, timeout_ms);
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_POLL_TIMEOUT, timeout_ms);
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_TCP_NODELAY, 1);
  // Use ASCII protocol to maximize compatibility with different memcached builds.
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 0);
  EnsureServerNumKey(memc);
  return true;
}

bool Keeper::disconnectMemcached() {
  if (memc) {
    memcached_quit(memc);
    memcached_free(memc);
    memc = NULL;
  }
  return true;
}

void Keeper::serverEnter(int globalID) {
  memcached_return rc;
  uint64_t serverNum;
  EnsureServerNumKey(memc);
  if (globalID == -1) {
    while (true) {
      rc = memcached_increment(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), 1,
                               &serverNum);
      if (rc == MEMCACHED_SUCCESS) {

        myNodeID = serverNum - 1;

        printf("I am servers %d\n", myNodeID);
        return;
      }
      if (rc == MEMCACHED_NOTFOUND) {
        EnsureServerNumKey(memc);
        usleep(10000);
        continue;
      }
      fprintf(stderr,
              "Server %d Counld't incr value and get ID: %s, retry...\n",
              myNodeID, memcached_strerror(memc, rc));
      usleep(10000);
    }
  } else {
    // Best-effort reset of stale coordination key to avoid deadlock across runs.
    uint64_t cur = 0;
    if (memTryGetUint("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), &cur)) {
      if (cur > static_cast<uint64_t>(globalID)) {
        auto s = std::to_string(globalID);
        memSet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), s.c_str(),
               s.size());
      }
    } else {
      const char *zero = "0";
      memSet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), zero, 1);
    }
    while (true) {
      rc = memcached_increment(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), 1,
                               &serverNum);
      if (rc == MEMCACHED_SUCCESS) {
        myNodeID = globalID;

        printf("I am servers %d\n", myNodeID);
        if (serverNum > maxServer) {
          auto v = std::to_string(maxServer);
          memSet(SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), v.c_str(), v.size());
        }
        auto v = std::to_string(globalID + 1);
        memSet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), v.c_str(),
               v.size());
        return;
      }
      if (rc == MEMCACHED_NOTFOUND) {
        EnsureServerNumKey(memc);
        usleep(10000);
        continue;
      }
      fprintf(stderr,
              "Server %d Counld't incr value and get ID: %s, retry...\n",
              myNodeID, memcached_strerror(memc, rc));
      usleep(10000);
    }
  }
}

void Keeper::serverConnect() {

  size_t l;
  uint32_t flags;
  memcached_return rc;

  while (curServer < maxServer) {
    // std::cout << "poll in server connect";
    char *serverNumStr = memcached_get(memc, SERVER_NUM_KEY,
                                       strlen(SERVER_NUM_KEY), &l, &flags, &rc);
    if (rc != MEMCACHED_SUCCESS) {
      if (rc == MEMCACHED_NOTFOUND) {
        EnsureServerNumKey(memc);
      }
      fprintf(stderr, "Server %d Counld't get serverNum: %s, retry\n", myNodeID,
              memcached_strerror(memc, rc));
      usleep(10000);
      continue;
    }
    uint32_t serverNum = atoi(serverNumStr);
    free(serverNumStr);
    // If only this server is present and serverNum already covers it, don't wait.
    if (maxServer == 1 && myNodeID == 0 && serverNum >= 1) {
      curServer = maxServer;
      return;
    }
    // If memcached isn't advancing, avoid deadlock in single-server runs.
    if (maxServer == 1 && myNodeID == 0 && serverNum == 0) {
      curServer = maxServer;
      return;
    }
    if (serverNum > maxServer) {
      fprintf(stderr,
              "serverNum (%u) > maxServer (%u). Capping to maxServer.\n",
              serverNum, maxServer);
      serverNum = maxServer;
    }

    // /connect server K
    for (size_t k = curServer; k < serverNum; ++k) {
      if (k != myNodeID) {
        connectNode(k);
        printf("I connect server %zu\n", k);
      }
    }
    curServer = serverNum;
  }
}

void Keeper::memSet(const char *key, uint32_t klen, const char *val,
                    uint32_t vlen) {

  memcached_return rc;
  while (true) {
    rc = memcached_set(memc, key, klen, val, vlen, (time_t)0, (uint32_t)0);
    if (rc == MEMCACHED_SUCCESS) {
      break;
    }
    usleep(400);
  }
}

char *Keeper::memGet(const char *key, uint32_t klen, size_t *v_size) {

  size_t l;
  char *res;
  uint32_t flags;
  memcached_return rc;

  while (true) {

    res = memcached_get(memc, key, klen, &l, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS) {
      break;
    }
    usleep(400 * myNodeID);
  }

  if (v_size != nullptr) {
    *v_size = l;
  }

  return res;
}

uint64_t Keeper::memFetchAndAdd(const char *key, uint32_t klen) {
  uint64_t res;
  while (true) {
    memcached_return rc = memcached_increment(memc, key, klen, 1, &res);
    if (rc == MEMCACHED_SUCCESS) {
      return res;
    }
    if (rc == MEMCACHED_NOTFOUND) {
      const char *val = "0";
      memcached_return add_rc =
          memcached_add(memc, key, klen, val, 1, (time_t)0, (uint32_t)0);
      // If key already exists, NOTSTORED is expected and harmless.
      if (add_rc != MEMCACHED_SUCCESS && add_rc != MEMCACHED_NOTSTORED &&
          add_rc != MEMCACHED_DATA_EXISTS) {
        fprintf(stderr, "memFetchAndAdd init failed: %s\n",
                memcached_strerror(memc, add_rc));
      }
    }
    usleep(1000);
  }
}

bool Keeper::memTryGetUint(const char *key, uint32_t klen, uint64_t *out) {
  size_t l = 0;
  uint32_t flags = 0;
  memcached_return rc;
  char *res = memcached_get(memc, key, klen, &l, &flags, &rc);
  if (rc != MEMCACHED_SUCCESS || res == nullptr) {
    if (res) {
      free(res);
    }
    return false;
  }
  std::string s(res, l);
  free(res);
  if (s.empty()) {
    return false;
  }
  try {
    *out = std::stoull(s);
  } catch (...) {
    return false;
  }
  return true;
}
