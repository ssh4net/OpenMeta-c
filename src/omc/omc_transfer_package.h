#ifndef OMC_TRANSFER_PACKAGE_H
#define OMC_TRANSFER_PACKAGE_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer.h"
#include "omc/omc_xmp_dump.h"

OMC_EXTERN_C_BEGIN

enum {
    OMC_TRANSFER_PACKAGE_BATCH_VERSION = 2U
};

typedef enum omc_transfer_package_chunk_kind {
    OMC_TRANSFER_PACKAGE_CHUNK_SOURCE_RANGE = 0,
    OMC_TRANSFER_PACKAGE_CHUNK_TRANSFER_BLOCK = 1,
    OMC_TRANSFER_PACKAGE_CHUNK_JPEG_SEGMENT = 2,
    OMC_TRANSFER_PACKAGE_CHUNK_INLINE_BYTES = 3
} omc_transfer_package_chunk_kind;

typedef struct omc_transfer_package_chunk {
    omc_transfer_package_chunk_kind kind;
    omc_const_bytes route;
    omc_u64 output_offset;
    omc_u64 source_offset;
    omc_u32 block_index;
    omc_u8 jpeg_marker_code;
    omc_const_bytes bytes;
} omc_transfer_package_chunk;

typedef struct omc_transfer_package_batch {
    omc_u32 contract_version;
    omc_scan_fmt target_format;
    omc_u64 input_size;
    omc_u64 output_size;
    omc_u32 chunk_count;
    const omc_transfer_package_chunk* chunks;
} omc_transfer_package_batch;

typedef struct omc_transfer_package_build_opts {
    omc_scan_fmt format;
    int include_exif;
    int include_xmp;
    int include_icc;
    int include_iptc;
    int include_jumbf;
    int skip_empty_chunks;
    int stop_on_error;
    omc_xmp_sidecar_req xmp_packet;
} omc_transfer_package_build_opts;

typedef struct omc_transfer_package_io_res {
    omc_transfer_status status;
    omc_u64 bytes;
    omc_u32 chunk_count;
} omc_transfer_package_io_res;

typedef omc_transfer_status (*omc_transfer_package_begin_batch_fn)(
    void* user, omc_scan_fmt target_format, omc_u32 chunk_count);

typedef omc_transfer_status (*omc_transfer_package_emit_chunk_fn)(
    void* user, const omc_transfer_package_chunk* chunk);

typedef omc_transfer_status (*omc_transfer_package_end_batch_fn)(
    void* user, omc_scan_fmt target_format);

typedef struct omc_transfer_package_replay_callbacks {
    omc_transfer_package_begin_batch_fn begin_batch;
    omc_transfer_package_emit_chunk_fn emit_chunk;
    omc_transfer_package_end_batch_fn end_batch;
    void* user;
} omc_transfer_package_replay_callbacks;

typedef struct omc_transfer_package_replay_res {
    omc_transfer_status status;
    omc_u32 replayed;
    omc_u32 failed_chunk_index;
} omc_transfer_package_replay_res;

OMC_API void
omc_transfer_package_build_opts_init(omc_transfer_package_build_opts* opts);

OMC_API void
omc_transfer_package_batch_init(omc_transfer_package_batch* batch);

OMC_API void
omc_transfer_package_io_res_init(omc_transfer_package_io_res* res);

OMC_API void
omc_transfer_package_replay_res_init(omc_transfer_package_replay_res* res);

/*
 * Built and deserialized batches borrow chunk bytes and entry storage from
 * out_storage. Route names may alias static literals or out_storage bytes.
 */
OMC_API omc_status
omc_transfer_package_batch_build(const omc_store* store,
                                 const omc_transfer_package_build_opts* opts,
                                 omc_arena* out_storage,
                                 omc_transfer_package_batch* out_batch,
                                 omc_transfer_package_io_res* out_res);

OMC_API omc_status
omc_transfer_package_batch_serialize(
    const omc_transfer_package_batch* batch, omc_arena* out_bytes,
    omc_transfer_package_io_res* out_res);

OMC_API omc_status
omc_transfer_package_batch_deserialize(
    const omc_u8* bytes, omc_size size, omc_arena* out_storage,
    omc_transfer_package_batch* out_batch,
    omc_transfer_package_io_res* out_res);

OMC_API omc_status
omc_transfer_package_batch_replay(
    const omc_transfer_package_batch* batch,
    const omc_transfer_package_replay_callbacks* callbacks,
    omc_transfer_package_replay_res* out_res);

OMC_EXTERN_C_END

#endif
