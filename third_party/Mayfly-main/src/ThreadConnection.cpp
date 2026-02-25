#include "ThreadConnection.h"

#include "Connection.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>

RdmaContext r_ctx;
ThreadConnection::ThreadConnection(uint16_t threadID, void *cachePool,
                                   uint64_t cacheSize, uint32_t machineNR,
                                   RemoteConnection *remoteInfo, bool is_dpu)
    : threadID(threadID), remoteInfo(remoteInfo) {

  if (is_dpu) { // DPU only can create limted RDMA context
    if (threadID >= kv::kMaxDPUThread) {
      ctx = r_ctx;
    } else {
      createContext(&ctx);
      r_ctx = ctx;
    }
  } else {
    createContext(&ctx);
  }

  cq = ibv_create_cq(ctx.ctx, RPC_QUEUE_SIZE, NULL, NULL, 0);
  if (cq == nullptr) {
    printf("error create cq 3\n");
  }

  message = new RawMessageConnection(ctx, cq, APP_MESSAGE_NR);

  this->cachePool = cachePool;
  if (cachePool == nullptr || cachePool == MAP_FAILED) {
    fprintf(stderr, "cachePool mmap failed: %p size=%lu\n", cachePool,
            static_cast<unsigned long>(cacheSize));
    abort();
  }
  cacheMR = createMemoryRegion((uint64_t)cachePool, cacheSize, &ctx);
  if (!cacheMR) {
    fprintf(stderr, "Failed to register cache MR, size=%lu\n",
            static_cast<unsigned long>(cacheSize));
    abort();
  }
  cacheLKey = cacheMR->lkey;

  // dir, RC
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    data[i] = new ibv_qp *[machineNR];
    for (size_t k = 0; k < machineNR; ++k) {
      if (!createQueuePair(&data[i][k], IBV_QPT_RC, cq, &ctx) ||
          data[i][k] == nullptr) {
        fprintf(stderr,
                "Failed to create QP (thread=%u, dir=%d, remote=%zu)\n",
                threadID, i, k);
        abort();
      }
    }
  }
}

void ThreadConnection::set_global_memory_lkey(void *dsmPool, uint64_t dsmSize) {
  auto mr = createMemoryRegion((uint64_t)dsmPool, dsmSize, &ctx, true);
  if (!mr) {
    fprintf(stderr,
            "Failed to register global memory MR, size=%lu\n",
            static_cast<unsigned long>(dsmSize));
    abort();
  }
  globalMemLkey = mr->lkey;
}

void ThreadConnection::sendMessage2Dir(RawMessage *m, uint16_t node_id,
                                       uint16_t dir_id) {

  message->sendRawMessage(m, remoteInfo[node_id].dirMessageQPN[dir_id],
                          remoteInfo[node_id].dirAh[dir_id]);
}

void ThreadConnection::sendMessage(RawMessage *m, uint16_t node_id,
                                   uint16_t t_id) {

  message->sendRawMessage(m, remoteInfo[node_id].appMessageQPN[t_id],
                          remoteInfo[node_id].appAh[t_id]);
}

void ThreadConnection::sendMessageWithData(RawMessage *m, Slice data,
                                           uint16_t node_id, uint16_t t_id) {

  message->sendRawMessageWithData(m, data, globalMemLkey,
                                  remoteInfo[node_id].appMessageQPN[t_id],
                                  remoteInfo[node_id].appAh[t_id]);
}
