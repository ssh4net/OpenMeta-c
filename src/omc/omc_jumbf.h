#ifndef OMC_JUMBF_H
#define OMC_JUMBF_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_jumbf_status {
    OMC_JUMBF_OK = 0,
    OMC_JUMBF_UNSUPPORTED = 1,
    OMC_JUMBF_MALFORMED = 2,
    OMC_JUMBF_LIMIT = 3,
    OMC_JUMBF_NOMEM = 4
} omc_jumbf_status;

typedef struct omc_jumbf_limits {
    omc_u64 max_input_bytes;
    omc_u32 max_box_depth;
    omc_u32 max_boxes;
    omc_u32 max_entries;
    omc_u32 max_cbor_depth;
    omc_u32 max_cbor_items;
    omc_u32 max_cbor_key_bytes;
    omc_u32 max_cbor_text_bytes;
    omc_u32 max_cbor_bytes_bytes;
} omc_jumbf_limits;

typedef struct omc_jumbf_opts {
    int decode_cbor;
    int detect_c2pa;
    omc_jumbf_limits limits;
} omc_jumbf_opts;

typedef struct omc_jumbf_res {
    omc_jumbf_status status;
    omc_u32 boxes_decoded;
    omc_u32 cbor_items;
    omc_u32 entries_decoded;
} omc_jumbf_res;

OMC_API void
omc_jumbf_opts_init(omc_jumbf_opts* opts);

OMC_API omc_jumbf_res
omc_jumbf_dec(const omc_u8* bytes, omc_size size, omc_store* store,
              omc_block_id source_block, omc_entry_flags flags,
              const omc_jumbf_opts* opts);

OMC_API omc_jumbf_res
omc_jumbf_meas(const omc_u8* bytes, omc_size size,
               const omc_jumbf_opts* opts);

OMC_EXTERN_C_END

#endif
