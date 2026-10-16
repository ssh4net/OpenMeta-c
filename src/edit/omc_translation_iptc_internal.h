#ifndef OMC_TRANSLATION_IPTC_INTERNAL_H
#define OMC_TRANSLATION_IPTC_INTERNAL_H

#include "omc/omc_translation.h"

#define OMC_IPTC_MAPPING_COUNT 20U

typedef struct omc_iptc_mapping {
    const char *ns;
    const char *path;
    omc_u32 bit;
    omc_u16 dataset;
    omc_u16 max_bytes;
    int repeated;
} omc_iptc_mapping;

/* Shared mapping identities and descriptor order for both IPTC directions. */
extern const omc_iptc_mapping
    omc_iptc_mappings[OMC_IPTC_MAPPING_COUNT];

#endif
