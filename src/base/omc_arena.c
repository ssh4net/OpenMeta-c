#include "omc/omc_arena.h"

#include <stdlib.h>
#include <string.h>

static int
omc_arena_ref_fits(omc_size value)
{
    return value <= (omc_size)(~(omc_u32)0);
}

static omc_size
omc_arena_next_capacity(omc_size current, omc_size needed)
{
    omc_size next;

    if (current == 0U) {
        next = 64U;
    } else {
        next = current;
    }

    while (next < needed) {
        if (next > ((omc_size)(~(omc_size)0) / 2U)) {
            next = needed;
            break;
        }
        next *= 2U;
    }

    return next;
}

void
omc_arena_init(omc_arena* arena)
{
    if (arena == NULL) {
        return;
    }

    arena->data = NULL;
    arena->size = 0U;
    arena->capacity = 0U;
}

void
omc_arena_reset(omc_arena* arena)
{
    if (arena == NULL) {
        return;
    }

    arena->size = 0U;
}

void
omc_arena_fini(omc_arena* arena)
{
    if (arena == NULL) {
        return;
    }

    free(arena->data);
    arena->data = NULL;
    arena->size = 0U;
    arena->capacity = 0U;
}

omc_status
omc_arena_reserve(omc_arena* arena, omc_size capacity)
{
    void* new_mem;

    if (arena == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (capacity <= arena->capacity) {
        return OMC_STATUS_OK;
    }

    new_mem = realloc(arena->data, capacity);
    if (new_mem == NULL) {
        return OMC_STATUS_NO_MEMORY;
    }

    arena->data = (omc_u8*)new_mem;
    arena->capacity = capacity;
    return OMC_STATUS_OK;
}

omc_status
omc_arena_append(omc_arena* arena, const void* src, omc_size size,
                 omc_byte_ref* out_ref)
{
    omc_size offset;
    omc_size needed;
    omc_size capacity;
    omc_status status;

    if (arena == NULL || out_ref == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (src == NULL && size != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (!omc_arena_ref_fits(arena->size) || !omc_arena_ref_fits(size)) {
        return OMC_STATUS_OVERFLOW;
    }
    if (size > ((omc_size)(~(omc_size)0) - arena->size)) {
        return OMC_STATUS_OVERFLOW;
    }

    offset = arena->size;
    needed = offset + size;
    if (!omc_arena_ref_fits(needed)) {
        return OMC_STATUS_OVERFLOW;
    }

    if (needed > arena->capacity) {
        capacity = omc_arena_next_capacity(arena->capacity, needed);
        status = omc_arena_reserve(arena, capacity);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    if (size != 0U) {
        memcpy(arena->data + offset, src, size);
    }
    arena->size = needed;

    out_ref->offset = (omc_u32)offset;
    out_ref->size = (omc_u32)size;
    return OMC_STATUS_OK;
}

omc_const_bytes
omc_arena_view(const omc_arena* arena, omc_byte_ref ref)
{
    omc_const_bytes view;

    view.data = NULL;
    view.size = 0U;

    if (arena == NULL) {
        return view;
    }
    if ((omc_size)ref.offset > arena->size) {
        return view;
    }
    if ((omc_size)ref.size > (arena->size - (omc_size)ref.offset)) {
        return view;
    }

    view.data = arena->data + ref.offset;
    view.size = ref.size;
    return view;
}

omc_mut_bytes
omc_arena_view_mut(omc_arena* arena, omc_byte_ref ref)
{
    omc_mut_bytes view;

    view.data = NULL;
    view.size = 0U;

    if (arena == NULL) {
        return view;
    }
    if ((omc_size)ref.offset > arena->size) {
        return view;
    }
    if ((omc_size)ref.size > (arena->size - (omc_size)ref.offset)) {
        return view;
    }

    view.data = arena->data + ref.offset;
    view.size = ref.size;
    return view;
}
