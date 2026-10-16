#ifndef OMC_IPTC_SERIALIZE_H
#define OMC_IPTC_SERIALIZE_H

#include "omc/omc_store.h"

OMC_EXTERN_C_BEGIN

#define OMC_IPTC_IIM_SERIALIZE_CONTRACT_VERSION 1U

typedef enum omc_iptc_iim_serialize_status {
    OMC_IPTC_IIM_SERIALIZE_OK = 0,
    OMC_IPTC_IIM_SERIALIZE_OUTPUT_TRUNCATED,
    OMC_IPTC_IIM_SERIALIZE_INVALID_OPTIONS,
    OMC_IPTC_IIM_SERIALIZE_INVALID_METADATA,
    OMC_IPTC_IIM_SERIALIZE_NO_IPTC_DATA,
    OMC_IPTC_IIM_SERIALIZE_LIMIT,
    OMC_IPTC_IIM_SERIALIZE_NO_MEMORY
} omc_iptc_iim_serialize_status;

typedef struct omc_iptc_iim_serialize_opts {
    omc_u32 max_datasets;
    omc_u32 max_dataset_bytes;
    omc_u64 max_output_bytes;
} omc_iptc_iim_serialize_opts;

typedef struct omc_iptc_iim_serialize_res {
    omc_iptc_iim_serialize_status status;
    omc_u64 written;
    omc_u64 needed;
    omc_u32 entries_serialized;
    omc_u32 entries_skipped;
    omc_entry_id failed_entry;
} omc_iptc_iim_serialize_res;

OMC_API void
omc_iptc_iim_serialize_opts_init(omc_iptc_iim_serialize_opts *opts);

/* Serialize active IPTC-IIM entries as an unwrapped canonical byte stream.
 * The caller must keep the shape-valid store unchanged for the call. The C
 * store has no finalized-state bit, so callers must establish that precondition
 * themselves. Deleted entries and other key families are ignored. Duplicate
 * datasets and empty payloads are preserved. Dataset 1:90 sorts first, then
 * source order, record, dataset, and entry ordinal.
 *
 * Bytes and text values emit their exact stored bytes. Arrays emit their exact
 * raw storage bytes without numeric conversion or byte-order changes. This
 * function does not transcode text or validate IPTC text semantics. Active
 * entries with truncated or unreadable flags, invalid keys, unsupported value
 * kinds, invalid counts, or invalid arena spans fail before output changes.
 * Output must not overlap the store object or its arena, entry, or block
 * storage. A short output receives the canonical prefix after complete
 * preflight; bytes after `written` stay
 * unchanged. Empty output measures the stream. Other failures leave output
 * unchanged and report zero `written` and `needed`.
 *
 * Defaults are 200,000 datasets, 8 MiB per dataset, and 64 MiB total output.
 * All limits must be nonzero. `max_output_bytes` must fit in uint32_t and
 * omc_size. */
OMC_API omc_iptc_iim_serialize_res
omc_serialize_iptc_iim(const omc_store *store, omc_mut_bytes output,
                       const omc_iptc_iim_serialize_opts *opts);

OMC_EXTERN_C_END

#endif
