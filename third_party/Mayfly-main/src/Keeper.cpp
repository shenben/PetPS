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
const char *Keeper::CLIENT_NUM_KEY = "clientNum";

Keeper::Keeper(uint32_t maxServer)
    : maxServer(maxServer), curServer(0), memc(NULL) {}

Keeper::~Keeper() {
  //   listener.detach();

  disconnectMemcached();
}

bool Keeper::connectMemcached() {
  memcached_server_st *servers = NULL;
  memcached_return rc;

  fprintf(stderr, "DEBUG: connectMemcached() started\n");

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

  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
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

  if (globalID == -1) {
    // I am a server
    // Initialize serverNum key ONLY if it doesn't exist (server only)
    // Don't overwrite if another server already initialized it
    {
      uint32_t flags;
      size_t value_len;
      char* existing_value = memcached_get(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), &value_len, &flags, &rc);
      if (existing_value) {
        fprintf(stderr, "DEBUG: %s already exists with value length=%zu, not resetting\n", SERVER_NUM_KEY, value_len);
        free(existing_value);
      } else {
        memcached_return rc_init;
        rc_init = memcached_set(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY),
                                "0", 1, (time_t)0, (uint32_t)0);
        fprintf(stderr, "DEBUG: Initialized %s, rc=%s\n", SERVER_NUM_KEY, memcached_strerror(memc, rc_init));
      }
    }

    fprintf(stderr, "DEBUG: serverEnter() for server (globalID=-1)\n");
    while (true) {
      // First verify the key exists
      uint32_t flags;
      size_t value_len;
      char* value = memcached_get(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), &value_len, &flags, &rc);
      if (value) {
        fprintf(stderr, "DEBUG: Key %s exists with value length=%zu\n", SERVER_NUM_KEY, value_len);
        free(value);
      } else {
        fprintf(stderr, "DEBUG: Key %s does NOT exist, rc=%s\n", SERVER_NUM_KEY, memcached_strerror(memc, rc));
      }
      rc = memcached_increment(memc, SERVER_NUM_KEY, strlen(SERVER_NUM_KEY), 1,
                               &serverNum);
      if (rc == MEMCACHED_SUCCESS) {
        myNodeID = serverNum - 1;
        printf("I am server %d\n", myNodeID);
        // Set xmh-consistent-dsm to allow first client (globalID=0) to proceed
        memSet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), "1", 1);
        return;
      }
      fprintf(stderr,
              "Server %d Counld't incr value and get ID: %s, retry...\n",
              myNodeID, memcached_strerror(memc, rc));
      usleep(10000);
    }
  } else {
    // I am a client
    fprintf(stderr, "DEBUG: serverEnter() for client (globalID=%d)\n", globalID);

    // Get the server count to determine our node ID
    // Clients have node IDs >= serverNR
    size_t l;
    uint32_t flags;
    memcached_return rc;
    uint32_t serverNum = 0;
    fprintf(stderr, "DEBUG: client reading serverNum key\n");
    {
      char *serverNumStr = memcached_get(memc, SERVER_NUM_KEY,
                                         strlen(SERVER_NUM_KEY), &l, &flags, &rc);
      fprintf(stderr, "DEBUG: client got serverNum response, rc=%s\n", memcached_strerror(memc, rc));
      if (rc == MEMCACHED_SUCCESS && serverNumStr) {
        serverNum = atoi(serverNumStr);
        fprintf(stderr, "DEBUG: client parsed serverNum=%u\n", serverNum);
        free(serverNumStr);
      }
    }

    // Get the client count and increment to get our index
    uint32_t curClientNum = 0;
    fprintf(stderr, "DEBUG: client reading clientNum key\n");
    {
      char *clientNumStr = memcached_get(memc, CLIENT_NUM_KEY,
                                         strlen(CLIENT_NUM_KEY), &l, &flags, &rc);
      fprintf(stderr, "DEBUG: client got clientNum response, rc=%s\n", memcached_strerror(memc, rc));
      if (rc == MEMCACHED_SUCCESS && clientNumStr) {
        curClientNum = atoi(clientNumStr);
        fprintf(stderr, "DEBUG: client parsed curClientNum=%u\n", curClientNum);
        free(clientNumStr);
      }
    }

    // Increment to reserve our spot
    uint32_t newClientNum = curClientNum + 1;
    memcached_set(memc, CLIENT_NUM_KEY, strlen(CLIENT_NUM_KEY),
                  std::to_string(newClientNum).c_str(), std::to_string(newClientNum).length(),
                  (time_t)0, (uint32_t)0);

    fprintf(stderr, "DEBUG: client incremented clientNum from %u to %u\n", curClientNum, newClientNum);

    // Client node ID = serverNum + (newClientNum - 1) = serverNum + curClientNum
    // This ensures: first client (curClientNum=0) gets node ID = serverNum + 0
    //              second client (curClientNum=1) gets node ID = serverNum + 1
    // The server connects to clients using: serverNum + i where i is 0, 1, 2, ...
    myNodeID = serverNum + curClientNum;
    printf("I am client %d (globalID=%d, serverNR=%u, clientIndex=%u)\n", myNodeID, globalID, serverNum, curClientNum);

    // Signal that we're ready (wait for server to signal that it's ready)
    // The server sets this key to "1" when it completes initialization
    while (1) {
      auto str = memGet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), nullptr);
      int val = std::atoi(str);
      if (val >= 1) {
        fprintf(stderr, "DEBUG: client (globalID=%d) found xmh-consistent-dsm=%d, proceeding\n", globalID, val);
        break;
      }
      usleep(1000);
    }

    // Initialize clientNum key
    {
      memcached_return rc_init;
      rc_init = memcached_set(memc, CLIENT_NUM_KEY, strlen(CLIENT_NUM_KEY),
                              "0", 1, (time_t)0, (uint32_t)0);
      (void)rc_init;
    }

    // Increment client count (for informational purposes)
    uint64_t clientNum;
    rc = memcached_increment(memc, CLIENT_NUM_KEY, strlen(CLIENT_NUM_KEY), 1,
                             &clientNum);
    if (rc == MEMCACHED_SUCCESS) {
      fprintf(stderr, "DEBUG: client incremented clientNum to %lu\n", clientNum);
    }

    // Signal next client (if any)
    auto v = std::to_string(globalID + 1);
    memSet("xmh-consistent-dsm", strlen("xmh-consistent-dsm"), v.c_str(),
           v.size());
    return;
  }
}

void Keeper::serverConnect() {
  size_t l;
  uint32_t flags;
  memcached_return rc;

  // Determine if I'm a server or client
  // Servers have IDs < maxServer, clients have IDs >= maxServer
  bool is_server = (myNodeID < maxServer);

  // Get the server count
  uint32_t serverNum = 0;
  {
    char *serverNumStr = memcached_get(memc, SERVER_NUM_KEY,
                                       strlen(SERVER_NUM_KEY), &l, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS && serverNumStr) {
      serverNum = atoi(serverNumStr);
      free(serverNumStr);
    }
  }

  // Get the client count
  uint32_t clientNum = 0;
  {
    char *clientNumStr = memcached_get(memc, CLIENT_NUM_KEY,
                                       strlen(CLIENT_NUM_KEY), &l, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS && clientNumStr) {
      clientNum = atoi(clientNumStr);
      free(clientNumStr);
    }
  }

  fprintf(stderr, "DEBUG: serverConnect() is_server=%d, serverNum=%u, clientNum=%u, myNodeID=%d, maxServer=%u\n",
          is_server, serverNum, clientNum, myNodeID, maxServer);

  if (is_server) {
    // I am a server: connect to other servers only (not clients)
    // Clients will connect to servers, not the other way around
    // Use serverNum (actual server count) instead of maxServer to avoid connecting to client nodes
    for (size_t k = curServer; k < serverNum; ++k) {
      if (k != myNodeID) {
        connectNode(k);
        fprintf(stderr, "DEBUG: I connect to server %zu (serverNum=%u)\n", k, serverNum);
      }
    }
    curServer = maxServer;

    // Connect to clients (client IDs start from configured_server_count)
    // Use maxServer - expectedClientNR to get the configured server count
    // Client i has ID = configured_server_count + i
    uint16_t serverCount = maxServer - expectedClientNR;
    uint32_t totalClients = clientNum;  // Current number of clients
    for (uint32_t i = 0; i < totalClients; i++) {
      uint16_t clientID = serverCount + i;
      if (clientID != myNodeID) {  // Should always be true for servers
        connectNode(clientID);
        fprintf(stderr, "DEBUG: I connect to client %u (clientIndex=%u, serverCount=%u)\n", clientID, i, serverCount);
      }
    }

    // For servers, we also need to check if there are any late-registering clients
    // Keep checking until we see all expected clients
    uint32_t lastClientNum = clientNum;
    uint32_t expectedClients = this->getExpectedClientNR();  // Get from cluster config
    fprintf(stderr, "DEBUG: Starting late client polling, initial clientNum=%u, expectedClients=%u\n", clientNum, expectedClients);

    // Wait indefinitely for all expected clients
    int log_counter = 0;
    while (lastClientNum < expectedClients) {
      usleep(100000);  // 100ms

      // Re-read client count
      char *newClientNumStr = memcached_get(memc, CLIENT_NUM_KEY,
                                           strlen(CLIENT_NUM_KEY), &l, &flags, &rc);
      if (rc == MEMCACHED_SUCCESS && newClientNumStr) {
        uint32_t newClientNum = atoi(newClientNumStr);
        free(newClientNumStr);

        if (newClientNum > lastClientNum) {
          fprintf(stderr, "DEBUG: New clients detected! last=%u, new=%u\n", lastClientNum, newClientNum);
          // Connect to new clients
          // Use maxServer - expectedClientNR to get the configured server count
          // The client's global ID is: configured_server_count + clientIndex
          uint16_t serverCount = maxServer - expectedClientNR;
          for (uint32_t i = lastClientNum; i < newClientNum; i++) {
            uint16_t clientID = serverCount + i;
            if (clientID != myNodeID) {
              connectNode(clientID);
              fprintf(stderr, "DEBUG: Late connect to client %u (clientIndex=%u, serverCount=%u, maxServer=%u)\n", clientID, i, serverCount, maxServer);
            }
          }
          // Give time for RDMA connections to be fully established
          // This ensures address handles are ready before RPCs are sent
          fprintf(stderr, "DEBUG: Waiting for RDMA connections to stabilize...\n");
          usleep(500000);  // 500ms sleep
          fprintf(stderr, "DEBUG: RDMA connections should be ready\n");
          lastClientNum = newClientNum;
        }
      }
      // Log progress every 5 seconds (every 50 iterations)
      log_counter++;
      if (log_counter >= 50) {
        log_counter = 0;
        if (lastClientNum < expectedClients) {
          fprintf(stderr, "DEBUG: Waiting for clients... %u/%u connected\n", lastClientNum, expectedClients);
        }
      }
    }
    fprintf(stderr, "DEBUG: All %u clients connected\n", expectedClients);
  } else {
    // I am a client: connect to all servers
    // Servers have IDs 0 to serverNum-1
    for (uint16_t k = 0; k < serverNum; ++k) {
      if (k != myNodeID) {
        connectNode(k);
        fprintf(stderr, "DEBUG: I connect to server %d\n", k);
      }
    }
    curServer = serverNum;  // Mark that we've processed all servers
  }

  fprintf(stderr, "DEBUG: serverConnect() completed\n");
}

void Keeper::memSet(const char *key, uint32_t klen, const char *val,
                    uint32_t vlen) {

  memcached_return rc;
  int retry_count = 0;
  while (true) {
    rc = memcached_set(memc, key, klen, val, vlen, (time_t)0, (uint32_t)0);
    if (rc == MEMCACHED_SUCCESS) {
      if (retry_count > 0) {
        fprintf(stderr, "DEBUG: memSet succeeded after %d retries for key '%s'\n", retry_count, key);
      }
      break;
    }
    retry_count++;
    if (retry_count % 1000 == 0) {
      fprintf(stderr, "DEBUG: memSet retry %d for key '%s', error: %s\n", retry_count, key, memcached_strerror(memc, rc));
    }
    usleep(400);
  }
}

char *Keeper::memGet(const char *key, uint32_t klen, size_t *v_size) {

  size_t l;
  char *res;
  uint32_t flags;
  memcached_return rc;
  int retry_count = 0;

  while (true) {

    res = memcached_get(memc, key, klen, &l, &flags, &rc);
    if (rc == MEMCACHED_SUCCESS) {
      break;
    }
    retry_count++;
    // Log every 10000 retries (~4 seconds) instead of every 1000 to reduce log spam
    if (retry_count % 10000 == 0) {
      fprintf(stderr, "DEBUG: memGet retry %d for key '%s', error: %s\n", retry_count, key, memcached_strerror(memc, rc));
    }
    usleep(400 * myNodeID);
  }
  if (retry_count > 0) {
    fprintf(stderr, "DEBUG: memGet succeeded after %d retries for key '%s'\n", retry_count, key);
  }

  if (v_size != nullptr) {
    *v_size = l;
  }

  return res;
}

uint64_t Keeper::memFetchAndAdd(const char *key, uint32_t klen) {
  uint64_t res;
  int retry_count = 0;
  while (true) {
    memcached_return rc = memcached_increment(memc, key, klen, 1, &res);
    if (rc == MEMCACHED_SUCCESS) {
      fprintf(stderr, "DEBUG: memFetchAndAdd succeeded for key '%s', value=%lu\n", key, res);
      return res;
    }
    retry_count++;
    if (retry_count % 1000 == 0) {
      fprintf(stderr, "DEBUG: memFetchAndAdd retry %d for key '%s', error: %s\n", retry_count, key, memcached_strerror(memc, rc));
    }
    usleep(400);
  }
}
