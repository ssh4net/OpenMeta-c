#ifndef OMC_READ_INTERNAL_H
#define OMC_READ_INTERNAL_H
#include "omc/omc_read_source.h"
omc_read_res omc_read_tiff_source(const omc_source_range* range,
                                  omc_store* store,
                                  omc_read_source_workspace* workspace,
                                  omc_source_state* state,
                                  const omc_read_source_opts* opts,
                                  omc_exif_source_res* source_result);
omc_scan_res omc_scan_tiff_header(const omc_u8* bytes, omc_size available,
                                  omc_u64 file_size, omc_blk_ref* blocks,
                                  omc_u32 capacity);
#endif
