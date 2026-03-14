#ifndef OMC_ARENA_H
#define OMC_ARENA_H

#include "omc/omc_base.h"
#include "omc/omc_span.h"
#include "omc/omc_status.h"

OMC_EXTERN_C_BEGIN

typedef struct omc_byte_ref {
    omc_u32 offset;
    omc_u32 size;
} omc_byte_ref;

typedef struct omc_arena {
    omc_u8* data;
    omc_size size;
    omc_size capacity;
} omc_arena;

OMC_API void
omc_arena_init(omc_arena* arena);

OMC_API void
omc_arena_reset(omc_arena* arena);

OMC_API void
omc_arena_fini(omc_arena* arena);

OMC_API omc_status
omc_arena_reserve(omc_arena* arena, omc_size capacity);

OMC_API omc_status
omc_arena_append(omc_arena* arena, const void* src, omc_size size,
                 omc_byte_ref* out_ref);

OMC_API omc_const_bytes
omc_arena_view(const omc_arena* arena, omc_byte_ref ref);

OMC_API omc_mut_bytes
omc_arena_view_mut(omc_arena* arena, omc_byte_ref ref);

OMC_EXTERN_C_END

#endif
