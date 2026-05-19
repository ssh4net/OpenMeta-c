#ifndef OMC_CAPABILITIES_H
#define OMC_CAPABILITIES_H

#include "omc/omc_base.h"
#include "omc/omc_scan.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

enum { OMC_METADATA_CAPABILITIES_CONTRACT_VERSION = 1U };

typedef enum omc_metadata_capability_family {
    OMC_METADATA_CAPABILITY_EXIF          = 0,
    OMC_METADATA_CAPABILITY_XMP           = 1,
    OMC_METADATA_CAPABILITY_ICC           = 2,
    OMC_METADATA_CAPABILITY_IPTC          = 3,
    OMC_METADATA_CAPABILITY_MAKERNOTE     = 4,
    OMC_METADATA_CAPABILITY_PHOTOSHOP_IRB = 5,
    OMC_METADATA_CAPABILITY_JUMBF         = 6,
    OMC_METADATA_CAPABILITY_C2PA          = 7,
    OMC_METADATA_CAPABILITY_BMFF_FIELDS   = 8,
    OMC_METADATA_CAPABILITY_GEOTIFF       = 9,
    OMC_METADATA_CAPABILITY_EXR_ATTRIBUTE = 10
} omc_metadata_capability_family;

typedef enum omc_metadata_capability_support {
    OMC_METADATA_CAPABILITY_UNSUPPORTED = 0,
    OMC_METADATA_CAPABILITY_SUPPORTED   = 1,
    OMC_METADATA_CAPABILITY_BOUNDED     = 2,
    OMC_METADATA_CAPABILITY_DISABLED    = 3
} omc_metadata_capability_support;

typedef struct omc_metadata_capability {
    omc_scan_fmt format;
    omc_metadata_capability_family family;
    omc_metadata_capability_support read;
    omc_metadata_capability_support structured_decode;
    omc_metadata_capability_support transfer_prepare;
    omc_metadata_capability_support target_edit;
    omc_metadata_capability_support raw_preservation;
} omc_metadata_capability;

OMC_API const char*
omc_metadata_capability_family_name(omc_metadata_capability_family family);

OMC_API const char*
omc_metadata_capability_support_name(omc_metadata_capability_support support);

OMC_API int
omc_metadata_capability_available(omc_metadata_capability_support support);

OMC_API omc_metadata_capability
omc_metadata_capability_query(omc_scan_fmt format,
                              omc_metadata_capability_family family);

OMC_EXTERN_C_END

#endif
