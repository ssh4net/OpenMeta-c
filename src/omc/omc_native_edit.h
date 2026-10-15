#ifndef OMC_NATIVE_EDIT_H
#define OMC_NATIVE_EDIT_H

#include "omc/omc_store.h"

OMC_EXTERN_C_BEGIN

enum {
    OMC_NATIVE_EDIT_CONTRACT_VERSION = 1,
    OMC_NATIVE_IFD0_REMOVAL_CAPACITY = 7,
    OMC_NATIVE_EXIF_REMOVAL_CAPACITY = 85,
    OMC_NATIVE_GPS_REMOVAL_CAPACITY = 31
};

/* Owned bounded edit intent. Removal lists are strictly increasing, supported
 * tag IDs; GPSVersionID and ColorSpace are not removal selectors. Omitted
 * destination values survive a merge. Whole-directory clear is explicit and
 * conflicts with that directory's merge or selected removals. */
typedef struct omc_native_edit_intent {
    omc_u32 ifd0_removal_count;
    omc_u16 ifd0_removals[OMC_NATIVE_IFD0_REMOVAL_CAPACITY];
    omc_u32 exif_removal_count;
    omc_u16 exif_removals[OMC_NATIVE_EXIF_REMOVAL_CAPACITY];
    omc_u32 gps_removal_count;
    omc_u16 gps_removals[OMC_NATIVE_GPS_REMOVAL_CAPACITY];
    int merge_exif;
    int merge_gps;
    int clear_exif;
    int clear_gps;
} omc_native_edit_intent;

OMC_API void
omc_native_edit_intent_init(omc_native_edit_intent* intent);

/* Collect Dirty|Deleted selectors with live-over-tombstone precedence. Clean
 * tombstones have no destination authority. Dirty native capture/GPS values
 * select their bounded merge mode. Source and output must not overlap. */
OMC_API omc_status
omc_native_edit_intent_collect(const omc_store* source,
                               omc_native_edit_intent* intent);

OMC_API omc_status
omc_native_edit_intent_validate(const omc_native_edit_intent* intent);

OMC_API int
omc_native_edit_intent_present(const omc_native_edit_intent* intent);

OMC_EXTERN_C_END

#endif
