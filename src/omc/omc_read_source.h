#ifndef OMC_READ_SOURCE_H
#define OMC_READ_SOURCE_H
#include "omc/omc_source.h"
#include "omc/omc_read.h"
OMC_EXTERN_C_BEGIN

typedef struct omc_read_source_workspace {
    omc_u8 *metadata;
    omc_size metadata_capacity;
    omc_blk_ref *blocks;
    omc_u32 block_capacity;
    omc_exif_ifd_ref *ifds;
    omc_u32 ifd_capacity;
    omc_u8 *payload;
    omc_size payload_capacity;
    omc_u32 *payload_indices;
    omc_u32 payload_index_capacity;
} omc_read_source_workspace;
typedef struct omc_read_source_opts {
    omc_read_opts decode;
    omc_source_limits io;
} omc_read_source_opts;
typedef enum omc_read_source_status {
    OMC_READ_SOURCE_OK = 0,
    OMC_READ_SOURCE_INVALID_ARGUMENT,
    OMC_READ_SOURCE_UNSUPPORTED,
    OMC_READ_SOURCE_MALFORMED,
    OMC_READ_SOURCE_LIMIT,
    OMC_READ_SOURCE_IO
} omc_read_source_status;
typedef struct omc_read_source_res {
    omc_read_source_status status;
    omc_read_res decoded;
    omc_size scratch_used;
} omc_read_source_res;

OMC_API void omc_read_source_opts_init(omc_read_source_opts *opts);
/* Borrowed fixed-size source and caller-owned workspace. Callback conversion
 * currently supports JPEG and TIFF/BigTIFF/DNG. No whole-file callback fallback.
 * JPEG stops at SOS/EOI. TIFF gathers directory/value ranges into a bounded
 * TIFF-relative snapshot; metadata offsets must fit metadata_capacity.
 * Callback TIFF MakerNote enrichment is explicitly unsupported when requested
 * and present. Raw MakerNote values are retained with enrichment disabled.
 * Memory sources retain the existing contiguous reader and all its formats.
 * Collection/I/O failure leaves the store unchanged. Decode follows the
 * existing partial-result contract; inspect decoded statuses as well.
 * Block offsets are relative to range, matching the contiguous reader and C++
 * source scanners. I/O failure offsets are absolute in the backing source.
 * Workspace/source/store storage must not overlap. State failure is sticky. */
OMC_API omc_read_source_res omc_read_source(const omc_source_range *range,
                                            omc_store *store,
                                            omc_read_source_workspace *workspace,
                                            omc_source_state *state,
                                            const omc_read_source_opts *opts);
OMC_EXTERN_C_END
#endif
