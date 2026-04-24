#include "omc/omc_arena.h"

#include <stdlib.h>
#include <string.h>

typedef struct omc_arena_retired {
    omc_u8* data;
    struct omc_arena_retired* next;
} omc_arena_retired;

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

static void
omc_arena_free_retired(omc_arena* arena)
{
    omc_arena_retired* node;
    omc_arena_retired* next;

    if (arena == NULL) {
        return;
    }

    node = (omc_arena_retired*)arena->retired_blocks;
    while (node != NULL) {
        next = node->next;
        free(node->data);
        free(node);
        node = next;
    }
    arena->retired_blocks = NULL;
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
    arena->retired_blocks = NULL;
}

void
omc_arena_reset(omc_arena* arena)
{
    if (arena == NULL) {
        return;
    }

    arena->size = 0U;
    omc_arena_free_retired(arena);
}

void
omc_arena_fini(omc_arena* arena)
{
    if (arena == NULL) {
        return;
    }

    omc_arena_free_retired(arena);
    free(arena->data);
    arena->data = NULL;
    arena->size = 0U;
    arena->capacity = 0U;
    arena->retired_blocks = NULL;
}

omc_status
omc_arena_reserve(omc_arena* arena, omc_size capacity)
{
    omc_arena_retired* retired;
    void* new_mem;

    if (arena == NULL) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (capacity <= arena->capacity) {
        return OMC_STATUS_OK;
    }

    new_mem = malloc(capacity);
    if (new_mem == NULL) {
        return OMC_STATUS_NO_MEMORY;
    }

    if (arena->size != 0U) {
        memcpy(new_mem, arena->data, arena->size);
    }
    if (arena->data != NULL) {
        retired = (omc_arena_retired*)malloc(sizeof(*retired));
        if (retired == NULL) {
            free(new_mem);
            return OMC_STATUS_NO_MEMORY;
        }
        retired->data = arena->data;
        retired->next = (omc_arena_retired*)arena->retired_blocks;
        arena->retired_blocks = retired;
    }

    arena->data = (omc_u8*)new_mem;
    arena->capacity = capacity;
    return OMC_STATUS_OK;
}

omc_status
omc_arena_append(omc_arena* arena, const void* src, omc_size size,
                 omc_byte_ref* out_ref)
{
    const omc_u8* src_bytes;
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

    src_bytes = (const omc_u8*)src;
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
        memcpy(arena->data + offset, src_bytes, size);
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
