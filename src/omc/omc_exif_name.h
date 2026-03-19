#ifndef OMC_EXIF_NAME_H
#define OMC_EXIF_NAME_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_exif_name_policy {
    OMC_EXIF_NAME_CANONICAL = 0,
    OMC_EXIF_NAME_EXIFTOOL_COMPAT = 1
} omc_exif_name_policy;

typedef enum omc_exif_name_status {
    OMC_EXIF_NAME_OK = 0,
    OMC_EXIF_NAME_EMPTY = 1,
    OMC_EXIF_NAME_TRUNCATED = 2,
    OMC_EXIF_NAME_INVALID = 3
} omc_exif_name_status;

OMC_API omc_exif_name_status
omc_exif_tag_name(const char* ifd_name, omc_u16 tag,
                  char* out_name, omc_size out_cap, omc_size* out_len);

OMC_API omc_exif_name_status
omc_exif_entry_name(const omc_store* store, const omc_entry* entry,
                    omc_exif_name_policy policy,
                    char* out_name, omc_size out_cap, omc_size* out_len);

OMC_EXTERN_C_END

#endif
