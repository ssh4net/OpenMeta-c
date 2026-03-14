#ifndef OMC_TYPES_H
#define OMC_TYPES_H

#include <stddef.h>

#include "omc/omc_base.h"
#include "omc/omc_cfg.h"

OMC_EXTERN_C_BEGIN

typedef signed char omc_s8;
typedef unsigned char omc_u8;
typedef signed short omc_s16;
typedef unsigned short omc_u16;
typedef signed int omc_s32;
typedef unsigned int omc_u32;

#if OMC_HAVE_U64_ULONG
typedef long omc_s64;
typedef unsigned long omc_u64;
#elif OMC_HAVE_U64_ULL
#if defined(__GNUC__) || defined(__clang__)
__extension__ typedef long long omc_s64;
__extension__ typedef unsigned long long omc_u64;
#else
typedef long long omc_s64;
typedef unsigned long long omc_u64;
#endif
#elif OMC_HAVE_U64_MSVC_INT64
typedef __int64 omc_s64;
typedef unsigned __int64 omc_u64;
#else
#error No usable 64-bit integer type is available.
#endif

typedef size_t omc_size;

typedef char omc_check_u8_size[(sizeof(omc_u8) == 1U) ? 1 : -1];
typedef char omc_check_u16_size[(sizeof(omc_u16) == 2U) ? 1 : -1];
typedef char omc_check_u32_size[(sizeof(omc_u32) == 4U) ? 1 : -1];
typedef char omc_check_u64_size[(sizeof(omc_u64) == 8U) ? 1 : -1];

OMC_EXTERN_C_END

#endif
