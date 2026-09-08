#include "read/omc_input.h"
#include <string.h>
void
omc_input_memory(omc_input* input, const omc_u8* bytes, omc_size size)
{
    memset(input, 0, sizeof(*input));
    input->range.source = omc_source_memory(bytes, size);
    input->range.size = size;
}
int
omc_input_read(omc_input* input, omc_u64 offset, void* out, omc_size size)
{
    if (input == NULL || offset > input->range.size ||
        size > input->range.size - offset) return 0;
    if (input->range.source.contiguous_data != NULL) {
        if (size != 0U)
            memcpy(out, input->range.source.contiguous_data +
                   (omc_size)(input->range.source_offset + offset), size);
        return 1;
    }
    if (!omc_input_ok(input)) return 0;
    if (size <= input->cache_size && offset >= input->cache_offset &&
        offset - input->cache_offset <= input->cache_size - size) {
        if (size) memcpy(out, input->cache + (omc_size)(offset - input->cache_offset), size);
        return 1;
    }
    if (omc_source_read(&input->range, offset, (omc_u8*)out, size,
                          input->state, input->limits) != OMC_SOURCE_OK) return 0;
    if (size != 0U && size <= sizeof(input->cache)) {
        memcpy(input->cache, out, size);
        input->cache_offset = offset;
        input->cache_size = size;
    }
    return 1;
}
omc_u8
omc_input_byte(omc_input* input, omc_u64 offset)
{
    omc_u8 value = 0U;
    (void)omc_input_read(input, offset, &value, 1U);
    return value;
}
int
omc_input_match(omc_input* input, omc_u64 offset, const void* value, omc_size size)
{
    omc_u8 buffer[32];
    const omc_u8* expected = (const omc_u8*)value;
    omc_size n;
    if (input == NULL || offset > input->range.size ||
        size > input->range.size - offset) return 0;
    while (size != 0U) {
        n = size < sizeof(buffer) ? size : sizeof(buffer);
        if (!omc_input_read(input, offset, buffer, n) || memcmp(buffer, expected, n)) return 0;
        expected += n;
        offset += n;
        size -= n;
    }
    return 1;
}
int
omc_input_number(omc_input* input, omc_u64 offset, unsigned width, int little,
                 omc_u64* value)
{
    omc_u8 buffer[8];
    omc_u64 n = 0U;
    unsigned i;
    if (width > sizeof(buffer) || !omc_input_read(input, offset, buffer, width)) return 0;
    for (i = 0U; i < width; ++i) n = (n << 8U) | buffer[little ? width - 1U - i : i];
    *value = n;
    return 1;
}
omc_input
omc_input_slice(const omc_input* input, omc_u64 offset, omc_u64 size)
{
    omc_input child = *input;
    child.cache_size = 0U;
    if (offset > input->range.size || size > input->range.size - offset) {
        child.range.size = 0U;
    } else {
        child.range.source_offset += offset;
        child.range.size = size;
    }
    return child;
}
int
omc_input_ok(const omc_input* input)
{
    return input != NULL && (input->state == NULL || input->state->code == OMC_SOURCE_OK);
}
