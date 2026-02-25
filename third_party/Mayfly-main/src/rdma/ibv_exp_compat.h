// Compatibility header for deprecated ibv_exp_* API
// Maps old experimental API names to current rdma-core API

#ifndef IBV_EXP_COMPAT_H
#define IBV_EXP_COMPAT_H

#include <infiniband/verbs.h>

// Enable the modern OFED 5+ API path in Mayfly
#define OFED_VERSION_5 1

// The old experimental API structs are gone - just use the modern ibv_qp_init_attr_ex
// These defines prevent the old code from being used
#define ibv_exp_qp_init_attr ibv_qp_init_attr_ex
#define ibv_exp_create_qp ibv_create_qp_ex

// Map deprecated flags to modern equivalents
#define IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS IBV_QP_INIT_ATTR_CREATE_FLAGS
#define IBV_EXP_QP_INIT_ATTR_PD IBV_QP_INIT_ATTR_PD
#define IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG IBV_QP_INIT_ATTR_MAX_DEST_RD_ATOMIC

#endif // IBV_EXP_COMPAT_H
