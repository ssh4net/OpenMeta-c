#ifndef OMC_BMFF_REWRITE_H
#define OMC_BMFF_REWRITE_H

#include "omc/omc_transfer.h"

enum {
    OMC_BMFF_FAMILY_EXIF = 1,
    OMC_BMFF_FAMILY_XMP = 2,
    OMC_BMFF_FAMILY_JUMBF = 3,
    OMC_BMFF_FAMILY_C2PA = 4
};
typedef struct omc_bmff_write_item {
    unsigned family;
    omc_const_bytes payload;
} omc_bmff_write_item;

typedef struct omc_bmff_write_res {
    omc_transfer_status status;
    omc_u32 removed[5];
    omc_u32 inserted[5];
} omc_bmff_write_res;

/* Shared carrier/package writer. Family bits are 1 << family. Inserting a
 * family replaces its old items. strip_mask additionally removes families.
 * ICC action: 0 preserve, 1 replace, 2 strip. Output publishes only on success.
 * The bounded append layout retains original media addresses; old meta becomes
 * a free box and the replacement meta is appended. */
omc_status omc_bmff_rewrite(const omc_u8 *bytes, omc_size size,
                            const omc_bmff_write_item *items, omc_u32 count,
                            unsigned strip_mask, int icc_action,
                            omc_const_bytes profile, int require_primary,
                            omc_arena *out, omc_bmff_write_res *result);
#endif
