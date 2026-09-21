#ifndef OMC_METADATA_PATCH_H
#define OMC_METADATA_PATCH_H

#include "omc/omc_exif_tiff_serialize.h"
#include "omc/omc_xmp_dump.h"

OMC_EXTERN_C_BEGIN

#define OMC_METADATA_PATCH_CONTRACT_VERSION 1U
#define OMC_METADATA_PATCH_MAX_HANDLES 65534U
#define OMC_METADATA_PATCH_MAX_PLAN_ID ((omc_u64)0x0000FFFFFFFFFFFFUL)

typedef enum omc_metadata_patch_payload {
    OMC_METADATA_PATCH_EXIF_TIFF = 0,
    OMC_METADATA_PATCH_XMP = 1
} omc_metadata_patch_payload;

typedef enum omc_metadata_patch_code {
    OMC_METADATA_PATCH_NONE = 0,
    OMC_METADATA_PATCH_NULL_OUTPUT,
    OMC_METADATA_PATCH_INVALID_OPTIONS,
    OMC_METADATA_PATCH_INVALID_METADATA,
    OMC_METADATA_PATCH_NO_EXIF_DATA,
    OMC_METADATA_PATCH_LIMIT,
    OMC_METADATA_PATCH_SERIALIZATION_FAILED,
    OMC_METADATA_PATCH_EMPTY_REQUESTS,
    OMC_METADATA_PATCH_HANDLE_BUFFER_SIZE_MISMATCH,
    OMC_METADATA_PATCH_INVALID_REQUEST,
    OMC_METADATA_PATCH_KEY_NOT_FOUND,
    OMC_METADATA_PATCH_OCCURRENCE_OUT_OF_RANGE,
    OMC_METADATA_PATCH_VALUE_TYPE_MISMATCH,
    OMC_METADATA_PATCH_DUPLICATE_REQUEST,
    OMC_METADATA_PATCH_ALLOCATION_FAILED,
    OMC_METADATA_PATCH_INVALID_PLAN,
    OMC_METADATA_PATCH_INVALID_INSTANCE,
    OMC_METADATA_PATCH_EMPTY_UPDATES,
    OMC_METADATA_PATCH_INVALID_HANDLE,
    OMC_METADATA_PATCH_FOREIGN_HANDLE,
    OMC_METADATA_PATCH_DUPLICATE_HANDLE,
    OMC_METADATA_PATCH_WIDTH_MISMATCH,
    OMC_METADATA_PATCH_INVALID_VALUE,
    OMC_METADATA_PATCH_REPLAY_FAILED
} omc_metadata_patch_code;

typedef struct omc_metadata_patch_value_spec {
    omc_val_kind kind;
    omc_elem_type elem_type;
    omc_text_encoding text_encoding;
    omc_u32 count;
} omc_metadata_patch_value_spec;

typedef struct omc_metadata_patch_request {
    omc_key key;
    omc_u32 occurrence;
    omc_metadata_patch_value_spec expected;
    omc_u32 escaped_width;
} omc_metadata_patch_request;

typedef struct omc_metadata_patch_handle {
    omc_u64 token;
} omc_metadata_patch_handle;

typedef struct omc_metadata_patch_update {
    omc_metadata_patch_handle handle;
    const omc_val *value;
    const omc_arena *arena;
} omc_metadata_patch_update;

typedef struct omc_metadata_patch_exif_opts {
    int include_subifds;
    int inject_minimal_dng_version;
    int honor_wire_type_hints;
    int preserve_opaque_makernote;
    omc_u64 max_output_bytes;
} omc_metadata_patch_exif_opts;

typedef struct omc_metadata_patch_opts {
    omc_u64 plan_id;
    omc_metadata_patch_exif_opts exif;
    omc_xmp_portable_opts xmp;
    omc_u32 max_patch_requests;
    int validate;
} omc_metadata_patch_opts;

typedef struct omc_metadata_patch_result {
    omc_metadata_patch_code code;
    omc_exif_tiff_status exif_status;
    omc_xmp_dump_status xmp_status;
    omc_u64 payload_size;
    omc_u32 handle_count;
    omc_u32 patched_handles;
    omc_u32 failed_index;
} omc_metadata_patch_result;

typedef struct omc_metadata_patch_plan { void *state; } omc_metadata_patch_plan;
typedef struct omc_metadata_patch_instance { void *state; } omc_metadata_patch_instance;

typedef int (*omc_metadata_patch_replay_fn)(void *user,
                                             omc_metadata_patch_payload family,
                                             omc_const_bytes payload);

OMC_API void omc_metadata_patch_opts_init(omc_metadata_patch_opts *opts);
OMC_API void omc_metadata_patch_plan_init(omc_metadata_patch_plan *plan);
OMC_API void omc_metadata_patch_plan_reset(omc_metadata_patch_plan *plan);
OMC_API void omc_metadata_patch_instance_init(omc_metadata_patch_instance *instance);
OMC_API void omc_metadata_patch_instance_reset(omc_metadata_patch_instance *instance);
OMC_API const char *omc_metadata_patch_code_name(omc_metadata_patch_code code);
OMC_API omc_const_bytes omc_metadata_patch_plan_payload(
    const omc_metadata_patch_plan *plan, omc_metadata_patch_payload family);
OMC_API omc_const_bytes omc_metadata_patch_instance_payload(
    const omc_metadata_patch_instance *instance, omc_metadata_patch_payload family);

OMC_API omc_metadata_patch_result omc_metadata_patch_prepare(
    const omc_store *store, const omc_metadata_patch_request *requests,
    omc_u32 request_count, const omc_metadata_patch_opts *opts,
    omc_metadata_patch_handle *handles, omc_u32 handle_capacity,
    omc_metadata_patch_plan *out_plan);

OMC_API omc_metadata_patch_result omc_metadata_patch_instance_create(
    const omc_metadata_patch_plan *plan, omc_metadata_patch_instance *out_instance);

OMC_API omc_metadata_patch_result omc_metadata_patch_apply(
    omc_metadata_patch_instance *instance,
    const omc_metadata_patch_update *updates, omc_u32 update_count);

OMC_API omc_metadata_patch_result omc_metadata_patch_replay(
    const omc_metadata_patch_instance *instance,
    omc_metadata_patch_replay_fn callback, void *user);

OMC_EXTERN_C_END

#endif
