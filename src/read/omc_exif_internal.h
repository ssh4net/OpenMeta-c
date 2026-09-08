/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OMC_EXIF_INTERNAL_H
#define OMC_EXIF_INTERNAL_H
#include "omc/omc_exif.h"
omc_exif_res omc_exif_dec_cmt3(const omc_u8 *bytes, omc_size size, omc_store *store,
                               omc_block_id block, const omc_exif_opts *opts);
omc_exif_source_res omc_exif_dec_cmt3_source(const omc_source_range *range,
                                             omc_store *store, omc_block_id block,
                                             const omc_exif_source_workspace *workspace,
                                             omc_source_state *state,
                                             const omc_source_limits *io_limits,
                                             const omc_exif_opts *opts);
#endif
