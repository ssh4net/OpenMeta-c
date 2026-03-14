#ifndef OMC_EXIF_H
#define OMC_EXIF_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_exif_status {
    OMC_EXIF_OK = 0,
    OMC_EXIF_TRUNCATED = 1,
    OMC_EXIF_UNSUPPORTED = 2,
    OMC_EXIF_MALFORMED = 3,
    OMC_EXIF_LIMIT = 4,
    OMC_EXIF_NOMEM = 5
} omc_exif_status;

typedef enum omc_exif_lim_rsn {
    OMC_EXIF_LIM_NONE = 0,
    OMC_EXIF_LIM_MAX_IFDS = 1,
    OMC_EXIF_LIM_MAX_ENTRIES_IFD = 2,
    OMC_EXIF_LIM_MAX_ENTRIES_TOTAL = 3,
    OMC_EXIF_LIM_VALUE_COUNT = 4
} omc_exif_lim_rsn;

typedef enum omc_exif_ifd_kind {
    OMC_EXIF_IFD = 0,
    OMC_EXIF_EXIF_IFD = 1,
    OMC_EXIF_GPS_IFD = 2,
    OMC_EXIF_INTEROP_IFD = 3,
    OMC_EXIF_SUB_IFD = 4
} omc_exif_ifd_kind;

typedef struct omc_exif_ifd_ref {
    omc_exif_ifd_kind kind;
    omc_u32 index;
    omc_u64 offset;
    omc_block_id block;
} omc_exif_ifd_ref;

typedef struct omc_exif_limits {
    omc_u32 max_ifds;
    omc_u32 max_entries_per_ifd;
    omc_u32 max_total_entries;
    omc_u64 max_value_bytes;
} omc_exif_limits;

typedef struct omc_exif_tok_pol {
    const char* ifd_prefix;
    const char* subifd_prefix;
    const char* exif_ifd_token;
    const char* gps_ifd_token;
    const char* interop_ifd_token;
} omc_exif_tok_pol;

typedef struct omc_exif_opts {
    int include_pointer_tags;
    int decode_printim;
    int decode_geotiff;
    int decode_makernote;
    int decode_embedded_containers;
    omc_exif_tok_pol tokens;
    omc_exif_limits limits;
} omc_exif_opts;

typedef struct omc_exif_res {
    omc_exif_status status;
    omc_u32 ifds_written;
    omc_u32 ifds_needed;
    omc_u32 entries_decoded;
    omc_exif_lim_rsn limit_reason;
    omc_u64 limit_ifd_offset;
    omc_u16 limit_tag;
} omc_exif_res;

OMC_API void
omc_exif_opts_init(omc_exif_opts* opts);

OMC_API omc_exif_res
omc_exif_dec(const omc_u8* tiff_bytes, omc_size tiff_size,
             omc_store* store, omc_exif_ifd_ref* out_ifds, omc_u32 ifd_cap,
             const omc_exif_opts* opts);

OMC_API omc_exif_res
omc_exif_meas(const omc_u8* tiff_bytes, omc_size tiff_size,
              const omc_exif_opts* opts);

OMC_EXTERN_C_END

#endif
