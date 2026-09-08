#ifndef OMC_NATIVE_H
#define OMC_NATIVE_H
#include "read/omc_input.h"
#include "omc/omc_exif.h"
#include "omc/omc_scan.h"
omc_exif_source_res omc_native_dec(omc_input* input, omc_scan_fmt format,
                                    omc_store* store, const omc_exif_limits* limits);
#endif
