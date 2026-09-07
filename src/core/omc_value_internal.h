#ifndef OMC_VALUE_INTERNAL_H
#define OMC_VALUE_INTERNAL_H
#include "omc/omc_store.h"

int omc_store_shape_valid(const omc_store *store);
int omc_value_shape_valid(const omc_val *value, const omc_arena *arena);
int omc_ref_valid(const omc_arena *arena, omc_byte_ref ref);
omc_status omc_clone_entry(const omc_entry *entry, const omc_arena *src, omc_arena *dst,
                           omc_entry *out_entry);
omc_u16 omc_value_tiff_type(const omc_val *value);
/* Precondition: valid array shape and index < count. */
void omc_value_array_scalar(const omc_val *value, const omc_arena *arena, omc_u32 index,
                            omc_val *scalar);
#endif
