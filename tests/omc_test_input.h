#ifndef OMC_TEST_INPUT_H
#define OMC_TEST_INPUT_H
#include "omc/omc_source.h"
#include "omc_test_assert.h"
#include <string.h>
typedef struct omc_test_input { const omc_u8* bytes; omc_size size; } omc_test_input;
static omc_source_io_res
omc_test_input_read(void* context, omc_u64 offset, omc_u8* out, omc_size size)
{
    omc_test_input* input = (omc_test_input*)context;
    omc_source_io_res r;
    assert(offset >= 37U && offset - 37U <= input->size);
    offset -= 37U;
    assert(size <= input->size - offset);
    if (size) memcpy(out, input->bytes + (omc_size)offset, size);
    r.code = OMC_SOURCE_IO_OK;
    r.bytes_read = size;
    return r;
}
#endif
