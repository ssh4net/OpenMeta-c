#ifndef OMC_CIFF_H
#define OMC_CIFF_H

#include "omc/omc_exif.h"
#include "omc/omc_store.h"

OMC_EXTERN_C_BEGIN

omc_exif_res
omc_ciff_dec(const omc_u8* file_bytes, omc_size file_size,
             omc_store* store, omc_block_id source_block,
             const omc_exif_opts* opts);

OMC_EXTERN_C_END

#endif
