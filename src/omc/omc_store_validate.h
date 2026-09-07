#ifndef OMC_STORE_VALIDATE_H
#define OMC_STORE_VALIDATE_H

#include "omc/omc_store.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_metadata_issue_code {
    OMC_METADATA_INVALID_KEY = 1,
    OMC_METADATA_INVALID_ORIGIN,
    OMC_METADATA_INVALID_VALUE,
    OMC_METADATA_SCALAR_RANGE,
    OMC_METADATA_ZERO_DENOMINATOR,
    OMC_METADATA_INVALID_TEXT,
    OMC_METADATA_INVALID_WIRE_TYPE,
    OMC_METADATA_INVALID_WIRE_COUNT,
    OMC_METADATA_WRONG_IFD,
    OMC_METADATA_WRONG_TYPE,
    OMC_METADATA_WRONG_COUNT,
    OMC_METADATA_DUPLICATE_SINGLETON,
    OMC_METADATA_INVALID_NAMESPACE,
    OMC_METADATA_INVALID_PATH,
    OMC_METADATA_UNKNOWN_TAG,
    OMC_METADATA_IMAGE_MISMATCH,
    OMC_METADATA_LIMIT,
    OMC_METADATA_RELATED_ENTRIES
} omc_metadata_issue_code;

typedef enum omc_unknown_tag_policy {
    OMC_UNKNOWN_TAG_ALLOW = 0,
    OMC_UNKNOWN_TAG_WARNING = 1,
    OMC_UNKNOWN_TAG_ERROR = 2
} omc_unknown_tag_policy;

typedef struct omc_metadata_validate_opts {
    int validate_schema;
    int validate_wire_hints;
    int warnings_as_errors;
    omc_unknown_tag_policy unknown_tags;
    omc_u32 max_issues;
    omc_u32 max_key_bytes;
    omc_u32 max_entries;
    omc_u64 max_arena_bytes;
    omc_u64 max_value_bytes;
    int has_dimensions;
    omc_u32 width;
    omc_u32 height;
    int has_samples_per_pixel;
    omc_u16 samples_per_pixel;
    int has_color_planes;
    omc_u16 color_planes;
} omc_metadata_validate_opts;

typedef struct omc_metadata_issue {
    omc_metadata_issue_code code;
    omc_entry_id entry;
    omc_entry_id related_entry;
    int warning;
} omc_metadata_issue;

typedef struct omc_metadata_validate_res {
    omc_status status;
    omc_u32 entries_checked;
    omc_u32 error_count;
    omc_u32 warning_count;
    omc_u32 issues_written;
    omc_u32 issues_needed;
} omc_metadata_validate_res;

OMC_API void omc_metadata_validate_opts_init(omc_metadata_validate_opts *opts);

/* Detached, read-only validation. No file or finalized-store state is needed.
 * A NULL issue buffer measures diagnostics. Capacity limits stored diagnostics,
 * not checks; max_issues bounds work after errors. */
OMC_API omc_metadata_validate_res
omc_validate_store(const omc_store *store, omc_metadata_issue *issues, omc_u32 capacity,
                   const omc_metadata_validate_opts *opts);

OMC_API omc_metadata_validate_res omc_validate_entry(
    const omc_store *store, omc_entry_id entry, omc_metadata_issue *issues,
    omc_u32 capacity, const omc_metadata_validate_opts *opts);

OMC_EXTERN_C_END
#endif
