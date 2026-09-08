#ifndef OMC_INPUT_H
#define OMC_INPUT_H
#include "omc/omc_source.h"
/* Private borrowed input. Structural reads never prefetch an unknown body. */
typedef struct omc_input {
    omc_source_range range;
    omc_source_state* state;
    const omc_source_limits* limits;
    omc_u8* stream;
    omc_size stream_capacity;
    omc_u8 cache[32];
    omc_u64 cache_offset;
    omc_size cache_size;
} omc_input;
void omc_input_memory(omc_input* input, const omc_u8* bytes, omc_size size);
int omc_input_read(omc_input* input, omc_u64 offset, void* out, omc_size size);
omc_u8 omc_input_byte(omc_input* input, omc_u64 offset);
int omc_input_match(omc_input* input, omc_u64 offset, const void* value, omc_size size);
int omc_input_number(omc_input* input, omc_u64 offset, unsigned width, int little, omc_u64* value);
omc_input omc_input_slice(const omc_input* input, omc_u64 offset, omc_u64 size);
int omc_input_ok(const omc_input* input);
#endif
