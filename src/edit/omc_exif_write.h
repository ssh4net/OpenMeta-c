#ifndef OMC_EXIF_WRITE_H
#define OMC_EXIF_WRITE_H

#include "omc/omc_arena.h"
#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_status.h"
#include "omc/omc_store.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_exif_write_status {
    OMC_EXIF_WRITE_OK = 0,
    OMC_EXIF_WRITE_UNSUPPORTED = 1,
    OMC_EXIF_WRITE_MALFORMED = 2,
    OMC_EXIF_WRITE_LIMIT = 3
} omc_exif_write_status;

typedef struct omc_exif_write_res {
    omc_exif_write_status status;
    omc_scan_fmt format;
    omc_u32 removed_exif_blocks;
    omc_u32 inserted_exif_blocks;
    omc_size needed;
    omc_size written;
} omc_exif_write_res;

void
omc_exif_write_res_init(omc_exif_write_res* res);

int
omc_exif_write_store_has_supported_tags(const omc_store* store);

omc_status
omc_exif_write_embedded(const omc_u8* file_bytes, omc_size file_size,
                        const omc_store* source_store, omc_arena* out,
                        omc_scan_fmt format, omc_exif_write_res* out_res);

/*
 * Builds one target-shaped EXIF carrier payload from source_store without
 * rewriting a file. Empty output with OMC_EXIF_WRITE_OK means the store had no
 * serializable supported EXIF for this helper.
 */
omc_status
omc_exif_write_build_transfer_payload(const omc_store* source_store,
                                      omc_arena* out, omc_scan_fmt format,
                                      omc_exif_write_res* out_res);

OMC_EXTERN_C_END

#endif
