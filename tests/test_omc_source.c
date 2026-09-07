#include "omc/omc_source.h"
#include "omc_test_assert.h"
#include <string.h>

typedef struct host {
    omc_u8 bytes[64];
    omc_u64 base;
    unsigned calls;
    int mode;
} host;
static omc_source_io_res
read_at(void *context, omc_u64 offset, omc_u8 *destination, omc_size size)
{
    host *h = (host *)context;
    omc_source_io_res r;
    h->calls++;
    assert(offset >= h->base && offset - h->base <= sizeof(h->bytes));
    assert(size <= sizeof(h->bytes) - (omc_size)(offset - h->base));
    r.code = OMC_SOURCE_IO_OK;
    r.bytes_read = size;
    if (h->mode == 1)
        r.bytes_read = size ? size - 1U : 0U;
    if (h->mode == 2)
        r.code = OMC_SOURCE_IO_CHANGED;
    if (h->mode == 3)
        r.code = OMC_SOURCE_IO_CANCELLED;
    if (h->mode == 4)
        r.code = OMC_SOURCE_IO_ERROR;
    if (h->mode == 5)
        r.code = (omc_source_io_code)19;
    if (h->mode == 6)
        r.bytes_read = size + 1U;
    if (h->mode < 5)
        memcpy(destination, h->bytes + (omc_size)(offset - h->base),
               (omc_size)r.bytes_read);
    return r;
}
int
main(void)
{
    host h;
    omc_source_range range;
    omc_source_state state;
    omc_source_limits limits;
    omc_source_window window;
    omc_source_view view;
    omc_u8 out[16], cache[16];
    unsigned i, calls;
    omc_source_code expected[] = {OMC_SOURCE_OK,
                                  OMC_SOURCE_SHORT_READ,
                                  OMC_SOURCE_CHANGED,
                                  OMC_SOURCE_CANCELLED,
                                  OMC_SOURCE_IO_FAILURE,
                                  OMC_SOURCE_CONTRACT_VIOLATION,
                                  OMC_SOURCE_CONTRACT_VIOLATION};
    memset(&h, 0, sizeof(h));
    for (i = 0U; i < 64U; ++i)
        h.bytes[i] = (omc_u8)i;
    h.base = ((omc_u64)1U << 32U) + 17U;
    range.source = omc_source_callback(h.base + 64U, &h, read_at, 1);
    range.source_offset = h.base;
    range.size = 64U;
    assert(omc_source_valid(&range.source) && omc_source_range_valid(&range));
    omc_source_limits_init(&limits);
    for (i = 0U; i < 7U; ++i) {
        h.mode = (int)i;
        h.calls = 0U;
        omc_source_state_init(&state);
        assert(omc_source_read(&range, 3U, out, 8U, &state, &limits) == expected[i]);
        assert(h.calls == 1U && state.requests_issued == 1U &&
               state.bytes_requested == 8U);
        assert(state.bytes_completed == (i == 1U ? 7U : i >= 5U ? 0U : 8U));
        if (i) {
            assert(state.failure_offset == h.base + 3U &&
                   state.failure_request_bytes == 8U);
            assert(omc_source_read(&range, 0U, out, 1U, &state, &limits) ==
                   expected[i]);
            assert(h.calls == 1U);
        } else
            assert(memcmp(out, h.bytes + 3U, 8U) == 0);
    }
    h.mode = 0;
    h.calls = 0;
    omc_source_state_init(&state);
    assert(omc_source_read(&range, 64U, NULL, 0U, &state, &limits) == OMC_SOURCE_OK);
    assert(h.calls == 0U);
    assert(omc_source_read(&range, 63U, out, 2U, &state, &limits) ==
           OMC_SOURCE_OUT_OF_RANGE);
    assert(h.calls == 0U && state.failure_offset == 63U);
    omc_source_state_init(&state);
    limits.max_single_read_bytes = 3U;
    memset(out, 0xA5, sizeof(out));
    assert(omc_source_read(&range, 0U, out, 4U, &state, &limits) ==
           OMC_SOURCE_REQUEST_TOO_LARGE);
    assert(out[0] == 0xA5U && h.calls == 0U);
    omc_source_state_init(&state);
    omc_source_limits_init(&limits);
    limits.max_requests = 1U;
    assert(omc_source_read(&range, 0U, out, 1U, &state, &limits) == OMC_SOURCE_OK);
    assert(omc_source_read(&range, 1U, out, 1U, &state, &limits) ==
           OMC_SOURCE_REQUEST_LIMIT);
    omc_source_state_init(&state);
    omc_source_limits_init(&limits);
    limits.max_total_bytes = 3U;
    assert(omc_source_read(&range, 0U, out, 4U, &state, &limits) ==
           OMC_SOURCE_BYTE_LIMIT);
    omc_source_state_init(&state);
    omc_source_limits_init(&limits);
    state.bytes_requested = ~(omc_u64)0;
    assert(omc_source_read(&range, 0U, out, 1U, &state, &limits) ==
           OMC_SOURCE_BYTE_LIMIT);
    omc_source_state_init(&state);
    state.requests_issued = 0xFFFFFFFFU;
    limits.max_requests = 0U;
    assert(omc_source_read(&range, 0U, out, 1U, &state, &limits) ==
           OMC_SOURCE_REQUEST_LIMIT);
    omc_source_state_init(&state);
    state.bytes_completed = 1U;
    assert(omc_source_read(&range, 0U, out, 1U, &state, &limits) ==
           OMC_SOURCE_INVALID_ARGUMENT);
    range.source.contiguous_data = h.bytes;
    assert(!omc_source_valid(&range.source));
    range.source.contiguous_data = NULL;
    range.source_offset = ~(omc_u64)0;
    assert(!omc_source_range_valid(&range));
    range.source_offset = h.base;

    memset(&window, 0, sizeof(window));
    window.storage = cache;
    window.capacity = sizeof(cache);
    omc_source_state_init(&state);
    omc_source_limits_init(&limits);
    limits.max_total_bytes = 12U;
    view = omc_source_read_view(&range, 2U, 3U, &window, &state, &limits, 32U);
    assert(view.code == OMC_SOURCE_OK && !view.cache_hit &&
           state.bytes_requested == 12U);
    assert(view.bytes.data == cache && memcmp(cache, h.bytes + 2U, 3U) == 0);
    calls = h.calls;
    view = omc_source_read_view(&range, 4U, 2U, &window, &state, &limits, 32U);
    assert(view.code == OMC_SOURCE_OK && view.cache_hit && h.calls == calls);
    view = omc_source_read_view(&range, 20U, 2U, &window, &state, &limits, 32U);
    assert(view.code == OMC_SOURCE_BYTE_LIMIT && !window.valid && h.calls == calls);
    omc_source_state_init(&state);
    view = omc_source_read_view(&range, 0U, 17U, &window, &state, NULL, 0U);
    assert(view.code == OMC_SOURCE_SCRATCH_TOO_SMALL);
    omc_source_state_init(&state);
    h.mode = 1;
    view = omc_source_read_view(&range, 0U, 3U, &window, &state, NULL, 0U);
    assert(view.code == OMC_SOURCE_SHORT_READ && !window.valid);

    range.source = omc_source_memory(h.bytes, sizeof(h.bytes));
    range.source_offset = 1U;
    range.size = 63U;
    omc_source_state_init(&state);
    limits.max_total_bytes = 1U;
    view = omc_source_read_view(&range, 2U, 20U, NULL, &state, &limits, 0U);
    assert(view.code == OMC_SOURCE_OK && view.bytes.data == h.bytes + 3U &&
           state.requests_issued == 0U);
    assert(omc_source_read(&range, 0U, out, 2U, &state, &limits) ==
           OMC_SOURCE_BYTE_LIMIT);
    omc_source_state_init(&state);
    omc_source_limits_init(&limits);
    assert(omc_source_read(&range, 0U, h.bytes + 2U, 8U, &state, &limits) ==
           OMC_SOURCE_OK);
    assert(h.bytes[2] == 1U && h.bytes[9] == 8U && state.bytes_completed == 8U);
    range.source = omc_source_memory(NULL, 0U);
    range.source_offset = 0U;
    range.size = 0U;
    omc_source_state_init(&state);
    assert(omc_source_read(&range, 0U, NULL, 0U, &state, NULL) == OMC_SOURCE_OK);
    return 0;
}
