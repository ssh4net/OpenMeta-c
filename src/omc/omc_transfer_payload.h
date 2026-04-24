#ifndef OMC_TRANSFER_PAYLOAD_H
#define OMC_TRANSFER_PAYLOAD_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_span.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"
#include "omc/omc_transfer.h"
#include "omc/omc_xmp_dump.h"

OMC_EXTERN_C_BEGIN

enum {
    OMC_TRANSFER_PAYLOAD_BATCH_VERSION = 1U
};

typedef enum omc_transfer_semantic_kind {
    OMC_TRANSFER_SEMANTIC_UNKNOWN = 0,
    OMC_TRANSFER_SEMANTIC_EXIF = 1,
    OMC_TRANSFER_SEMANTIC_XMP = 2,
    OMC_TRANSFER_SEMANTIC_ICC = 3,
    OMC_TRANSFER_SEMANTIC_IPTC = 4,
    OMC_TRANSFER_SEMANTIC_JUMBF = 5,
    OMC_TRANSFER_SEMANTIC_C2PA = 6
} omc_transfer_semantic_kind;

typedef enum omc_transfer_payload_op_kind {
    OMC_TRANSFER_PAYLOAD_OP_JPEG_MARKER = 0,
    OMC_TRANSFER_PAYLOAD_OP_TIFF_TAG_BYTES = 1,
    OMC_TRANSFER_PAYLOAD_OP_JXL_BOX = 2,
    OMC_TRANSFER_PAYLOAD_OP_JXL_ICC_PROFILE = 3,
    OMC_TRANSFER_PAYLOAD_OP_WEBP_CHUNK = 4,
    OMC_TRANSFER_PAYLOAD_OP_PNG_CHUNK = 5,
    OMC_TRANSFER_PAYLOAD_OP_JP2_BOX = 6,
    OMC_TRANSFER_PAYLOAD_OP_EXR_ATTRIBUTE = 7,
    OMC_TRANSFER_PAYLOAD_OP_BMFF_ITEM = 8,
    OMC_TRANSFER_PAYLOAD_OP_BMFF_PROPERTY = 9
} omc_transfer_payload_op_kind;

typedef struct omc_transfer_payload_op {
    omc_transfer_payload_op_kind kind;
    omc_u32 block_index;
    omc_u64 payload_size;
    omc_u64 serialized_size;
    omc_u8 jpeg_marker_code;
    omc_u16 tiff_tag;
    omc_u8 box_type[4];
    omc_u8 chunk_type[4];
    omc_u32 bmff_item_type;
    omc_u32 bmff_property_type;
    omc_u32 bmff_property_subtype;
    int bmff_mime_xmp;
    int compress;
} omc_transfer_payload_op;

typedef struct omc_transfer_payload {
    omc_transfer_semantic_kind semantic_kind;
    omc_const_bytes route;
    omc_transfer_payload_op op;
    omc_const_bytes payload;
} omc_transfer_payload;

typedef struct omc_transfer_payload_batch {
    omc_u32 contract_version;
    omc_scan_fmt target_format;
    int skip_empty_payloads;
    int stop_on_error;
    omc_u32 payload_count;
    const omc_transfer_payload* payloads;
} omc_transfer_payload_batch;

typedef struct omc_transfer_payload_build_opts {
    omc_scan_fmt format;
    int include_exif;
    int include_xmp;
    int include_icc;
    int include_iptc;
    int include_jumbf;
    int skip_empty_payloads;
    int stop_on_error;
    omc_xmp_sidecar_req xmp_packet;
} omc_transfer_payload_build_opts;

typedef struct omc_transfer_payload_io_res {
    omc_transfer_status status;
    omc_u64 bytes;
    omc_u32 payload_count;
} omc_transfer_payload_io_res;

typedef omc_transfer_status (*omc_transfer_payload_begin_batch_fn)(
    void* user, omc_scan_fmt target_format, omc_u32 payload_count);

typedef omc_transfer_status (*omc_transfer_payload_emit_fn)(
    void* user, const omc_transfer_payload* payload);

typedef omc_transfer_status (*omc_transfer_payload_end_batch_fn)(
    void* user, omc_scan_fmt target_format);

typedef struct omc_transfer_payload_replay_callbacks {
    omc_transfer_payload_begin_batch_fn begin_batch;
    omc_transfer_payload_emit_fn emit_payload;
    omc_transfer_payload_end_batch_fn end_batch;
    void* user;
} omc_transfer_payload_replay_callbacks;

typedef struct omc_transfer_payload_replay_res {
    omc_transfer_status status;
    omc_u32 replayed;
    omc_u32 failed_payload_index;
} omc_transfer_payload_replay_res;

OMC_API void
omc_transfer_payload_build_opts_init(omc_transfer_payload_build_opts* opts);

OMC_API void
omc_transfer_payload_batch_init(omc_transfer_payload_batch* batch);

OMC_API void
omc_transfer_payload_io_res_init(omc_transfer_payload_io_res* res);

OMC_API void
omc_transfer_payload_replay_res_init(omc_transfer_payload_replay_res* res);

OMC_API omc_transfer_semantic_kind
omc_transfer_classify_route_semantic_kind(const omc_u8* route,
                                          omc_size route_size);

OMC_API const char*
omc_transfer_semantic_kind_name(omc_transfer_semantic_kind kind);

/*
 * Built and deserialized batches borrow payload bytes and entry storage from
 * out_storage. Route names may alias static literals or out_storage bytes.
 */
OMC_API omc_status
omc_transfer_payload_batch_build(const omc_store* store,
                                 const omc_transfer_payload_build_opts* opts,
                                 omc_arena* out_storage,
                                 omc_transfer_payload_batch* out_batch,
                                 omc_transfer_payload_io_res* out_res);

OMC_API omc_status
omc_transfer_payload_batch_serialize(
    const omc_transfer_payload_batch* batch, omc_arena* out_bytes,
    omc_transfer_payload_io_res* out_res);

OMC_API omc_status
omc_transfer_payload_batch_deserialize(
    const omc_u8* bytes, omc_size size, omc_arena* out_storage,
    omc_transfer_payload_batch* out_batch,
    omc_transfer_payload_io_res* out_res);

OMC_API omc_status
omc_transfer_payload_batch_replay(
    const omc_transfer_payload_batch* batch,
    const omc_transfer_payload_replay_callbacks* callbacks,
    omc_transfer_payload_replay_res* out_res);

OMC_EXTERN_C_END

#endif
