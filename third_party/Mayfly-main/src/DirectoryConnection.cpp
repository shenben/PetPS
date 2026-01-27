#include "DirectoryConnection.h"

#include "Connection.h"

DirectoryConnection::DirectoryConnection(uint16_t dirID, void *dsmPool,
                                         uint64_t dsmSize, uint32_t machineNR,
                                         RemoteConnection *remoteInfo,
                                         void *cachePool, uint64_t cacheSize,
                                         bool is_server)
    : dirID(dirID), remoteInfo(remoteInfo) {
  char dbg_msg[128];
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] creating context\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  createContext(&ctx, 1, 0);  // port=1, gidIndex=0 (use valid GID)
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] context created\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] about to create CQ, ctx.ctx=%p\n", dirID, ctx.ctx);
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
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] CQ created, cq=%p\n", dirID, cq);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  if (cq == nullptr) {
    printf("error create cq 2\n");
  }

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] creating RawMessageConnection\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  message = new RawMessageConnection(ctx, cq, DIR_MESSAGE_NR);

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] RawMessageConnection created\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] calling initRecv\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  message->initRecv();

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] initRecv done\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] calling initSend\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  message->initSend();

  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] initSend done\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));

  // dsm memory
  this->dsmPool = dsmPool;
  this->dsmSize = dsmSize;
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] creating dsmMR, dsmPool=%p, dsmSize=%lu\n", dirID, dsmPool, dsmSize);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
  this->dsmMR = createMemoryRegion((uint64_t)dsmPool, dsmSize, &ctx, is_server);
  snprintf(dbg_msg, sizeof(dbg_msg), "DEBUG: DirectoryConnection[%d] dsmMR created\n", dirID);
  ::write(STDERR_FILENO, dbg_msg, strlen(dbg_msg));
  this->dsmLKey = dsmMR->lkey;

  // cache memory
  this->cachePool = cachePool;
  this->cacheSize = cacheSize;
  this->cacheMR = createMemoryRegion((uint64_t)cachePool, cacheSize, &ctx);
  this->cacheLKey = cacheMR->lkey;

  // on-chip lock memory
  this->lockPool = (void *)define::kLockStartAddr;
  this->lockSize = define::kLockChipMemSize;
  // this->lockMR =
  //     createMemoryRegionOnChip((uint64_t)this->lockPool, this->lockSize,
  //     &ctx);
  // this->lockLKey = lockMR->lkey;

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    data2app[i] = new ibv_qp *[machineNR];
    for (size_t k = 0; k < machineNR; ++k) {
      createQueuePair(&data2app[i][k], IBV_QPT_RC, cq, &ctx);
    }
  }
}

void DirectoryConnection::sendMessage2App(RawMessage *m, uint16_t node_id,
                                          uint16_t th_id) {
  message->sendRawMessage(m, remoteInfo[node_id].appMessageQPN[th_id],
                          remoteInfo[node_id].appAh[th_id]);
  ;
}
