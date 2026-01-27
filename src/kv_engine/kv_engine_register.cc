#include "base_kv.h"
#include "engine_pethash.h"
// Temporarily disable HashAPI engine due to missing external dependencies
// #include "engine_hashapi.h"
// Temporarily disable Map engine due to missing hash_api_valid_file_size
// #include "engine_map.h"
#ifdef PMEM
#include "engine_dash.h"
#include "engine_mappm.h"
#include "engine_multishard_map_pm.h"
#endif
// #include "engine_f14.h"
#include "engine_fake.h"

DEFINE_int32(prefetch_method, 1, "prefetch method");