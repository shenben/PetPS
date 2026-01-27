#include "ThreadConnection.h"

#include "Connection.h"
#include <unistd.h>

RdmaContext r_ctx;
ThreadConnection::ThreadConnection(uint16_t threadID, void *cachePool,
                                   uint64_t cacheSize, uint32_t machineNR,
                                   RemoteConnection *remoteInfo, bool is_dpu)
    : threadID(threadID), remoteInfo(remoteInfo) {
  char dbg_msg[128];
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] ENTERED\n", threadID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  if (is_dpu) { // DPU only can create limted RDMA context
    if (threadID >= kv::kMaxDPUThread) {
      ctx = r_ctx;
    } else {
      snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] calling createContext\n", threadID);
      ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
      createContext(&ctx, 1, 0);  // port=1, gidIndex=0 (use valid GID)
      snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] createContext returned\n", threadID);
      ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
      r_ctx = ctx;
    }
  } else {
    snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] calling createContext (non-DPU)\n", threadID);
    ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
    createContext(&ctx, 1, 0);  // port=1, gidIndex=0 (use valid GID)
    snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] createContext returned (non-DPU)\n", threadID);
    ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
  }

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] AFTER createContext, ctx.ctx=%p, ctx.pd=%p\n", threadID, ctx.ctx, ctx.pd);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] about to create CQ with ctx.ctx=%p\n", threadID, ctx.ctx);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

#ifdef OFED_VERSION_5
  struct ibv_cq_init_attr_ex attr;
  memset(&attr, 0, sizeof(attr));
  attr.cqe = RPC_QUEUE_SIZE;
  attr.cq_context = NULL;
  attr.channel = NULL;
  attr.comp_vector = 0;
  attr.comp_mask = 0;
  auto cq_ex = ibv_create_cq_ex(ctx.ctx, &attr);
  cq = cq_ex ? ibv_cq_ex_to_cq(cq_ex) : nullptr;
#else
  cq = ibv_create_cq(ctx.ctx, RPC_QUEUE_SIZE, NULL, NULL, 0);
#endif
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] CQ created, cq=%p\n", threadID, cq);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  if (cq == nullptr) {
    printf("error create cq 3\n");
  }

  message = new RawMessageConnection(ctx, cq, APP_MESSAGE_NR);
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] RawMessageConnection created\n", threadID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  this->cachePool = cachePool;
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] creating cacheMR\n", threadID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
  cacheMR = createMemoryRegion((uint64_t)cachePool, cacheSize, &ctx);
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: ThreadConnection[%d] cacheMR created, lkey=%u\n", threadID, cacheMR->lkey);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
  cacheLKey = cacheMR->lkey;

  // dir, RC
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    data[i] = new ibv_qp *[machineNR];
    for (size_t k = 0; k < machineNR; ++k) {
      createQueuePair(&data[i][k], IBV_QPT_RC, cq, &ctx);
    }
  }
}

void ThreadConnection::set_global_memory_lkey(void *dsmPool, uint64_t dsmSize) {
  auto mr = createMemoryRegion((uint64_t)dsmPool, dsmSize, &ctx, true);
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
