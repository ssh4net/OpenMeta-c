#ifndef OMC_CRC32_H
#define OMC_CRC32_H
#include "omc/omc_types.h"
/* IEEE CRC-32 running state: initialize to all ones, complement at the end. */
omc_u32 omc_crc32_update(omc_u32 crc, const omc_u8 *bytes, omc_size size);
#endif
