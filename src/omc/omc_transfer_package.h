#ifndef OMC_TRANSFER_PACKAGE_H
#define OMC_TRANSFER_PACKAGE_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer.h"
#include "omc/omc_transfer_payload.h"
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

typedef struct omc_transfer_package_view {
    omc_transfer_semantic_kind semantic_kind;
    omc_const_bytes route;
    omc_transfer_package_chunk_kind package_kind;
    omc_u64 output_offset;
    omc_u8 jpeg_marker_code;
    omc_const_bytes bytes;
} omc_transfer_package_view;

typedef struct omc_transfer_package_build_opts {
    omc_scan_fmt format;
    omc_transfer_target_image_spec target_image_spec;
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
    void* user, const omc_transfer_package_view* view);

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
omc_transfer_package_batch_build_executed_output(
    const omc_u8* input_bytes, omc_size input_size,
    const omc_u8* output_bytes, omc_size output_size,
    const omc_transfer_res* execute, omc_arena* out_storage,
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

/*
 * Concatenates validated package chunk bytes into a caller-owned buffer.
 * Passing out_bytes == NULL with out_cap == 0 is valid for measurement.
 * On OMC_TRANSFER_LIMIT, out_res->bytes is the required output byte count and
 * the function does not write a partial output.
 */
OMC_API omc_status
omc_transfer_package_batch_materialize_to_buffer(
    const omc_transfer_package_batch* batch, omc_u8* out_bytes,
    omc_size out_cap, omc_transfer_package_io_res* out_res);

/*
 * Deserializes one persisted OMTPKG01 batch into temp_storage and then
 * materializes it into a caller-owned buffer. Passing out_bytes == NULL with
 * out_cap == 0 is valid for measurement.
 */
OMC_API omc_status
omc_transfer_package_bytes_materialize_to_buffer(
    const omc_u8* bytes, omc_size size, omc_arena* temp_storage,
    omc_u8* out_bytes, omc_size out_cap,
    omc_transfer_package_io_res* out_res);

/*
 * Collects zero-copy semantic views over validated package chunks into a
 * caller-owned array. Passing out_views == NULL with out_cap == 0 is valid for
 * measurement; on OMC_TRANSFER_LIMIT, out_res->chunk_count is the required
 * view count and no partial views are written.
 */
OMC_API omc_status
omc_transfer_package_batch_collect_views(
    const omc_transfer_package_batch* batch,
    omc_transfer_package_view* out_views, omc_u32 out_cap,
    omc_transfer_package_io_res* out_res);

/*
 * Concatenates validated package chunk bytes into out_bytes. The output arena
 * is reset before writing and must not be the storage backing batch chunks.
 */
OMC_API omc_status
omc_transfer_package_batch_materialize(
    const omc_transfer_package_batch* batch, omc_arena* out_bytes,
    omc_transfer_package_io_res* out_res);

OMC_API omc_status
omc_transfer_package_batch_replay(
    const omc_transfer_package_batch* batch,
    const omc_transfer_package_replay_callbacks* callbacks,
    omc_transfer_package_replay_res* out_res);

OMC_EXTERN_C_END

#endif
