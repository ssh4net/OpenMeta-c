#include "omc/omc_source.h"
#include <string.h>

static omc_source_code
fail(omc_source_state *state, omc_source_code code, omc_u64 offset, omc_u64 request,
     omc_source_io_code io, omc_u64 read)
{
    if (state != NULL && state->code == OMC_SOURCE_OK) {
        state->code = code;
        state->io_code = io;
        state->failure_offset = offset;
        state->failure_request_bytes = request;
        state->failure_bytes_read = read;
    }
    return code;
}
omc_source
omc_source_memory(const omc_u8 *bytes, omc_size size)
{
    omc_source s;
    memset(&s, 0, sizeof(s));
    s.size = size;
    s.contiguous_data = bytes;
    s.concurrent_reads = 1;
    return s;
}
omc_source
omc_source_callback(omc_u64 size, void *context, omc_source_read_at callback,
                    int concurrent_reads)
{
    omc_source s;
    memset(&s, 0, sizeof(s));
    s.size = size;
    s.context = context;
    s.read_at = callback;
    s.concurrent_reads = concurrent_reads;
    return s;
}
int
omc_source_valid(const omc_source *source)
{
    if (source == NULL || (source->contiguous_data != NULL && source->read_at != NULL))
        return 0;
    if (source->size && source->contiguous_data == NULL && source->read_at == NULL)
        return 0;
    return source->contiguous_data == NULL || source->size <= (omc_u64)(omc_size)-1;
}
int
omc_source_range_valid(const omc_source_range *range)
{
    return range != NULL && omc_source_valid(&range->source) &&
           range->source_offset <= range->source.size &&
           range->size <= range->source.size - range->source_offset;
}
void
omc_source_limits_init(omc_source_limits *limits)
{
    if (limits == NULL)
        return;
    limits->max_requests = 65536U;
    limits->max_total_bytes = 64U * 1024U * 1024U;
    limits->max_single_read_bytes = 16U * 1024U * 1024U;
}
void
omc_source_state_init(omc_source_state *state)
{
    if (state != NULL)
        memset(state, 0, sizeof(*state));
}
void
omc_source_window_reset(omc_source_window *window)
{
    if (window != NULL) {
        window->valid = 0;
        window->valid_bytes = 0U;
        window->range_offset = 0U;
    }
}
omc_source_code
omc_source_read(const omc_source_range *range, omc_u64 offset, omc_u8 *destination,
                omc_size size, omc_source_state *state, const omc_source_limits *limits)
{
    omc_source_limits defaults;
    omc_source_io_res io;
    omc_u64 absolute;
    omc_source_code code;
    if (state == NULL)
        return OMC_SOURCE_INVALID_ARGUMENT;
    if (state->code != OMC_SOURCE_OK)
        return state->code;
    if (limits == NULL) {
        omc_source_limits_init(&defaults);
        limits = &defaults;
    }
    if (!omc_source_range_valid(range) || (destination == NULL && size) ||
        state->bytes_completed > state->bytes_requested)
        return fail(state, OMC_SOURCE_INVALID_ARGUMENT, offset, size, OMC_SOURCE_IO_OK,
                    0U);
    if (offset > range->size || size > range->size - offset)
        return fail(state, OMC_SOURCE_OUT_OF_RANGE, offset, size, OMC_SOURCE_IO_OK, 0U);
    absolute = range->source_offset + offset;
    if (size == 0U)
        return OMC_SOURCE_OK;
    if (limits->max_single_read_bytes && size > limits->max_single_read_bytes)
        return fail(state, OMC_SOURCE_REQUEST_TOO_LARGE, absolute, size,
                    OMC_SOURCE_IO_OK, 0U);
    if ((limits->max_requests && state->requests_issued >= limits->max_requests) ||
        state->requests_issued == 0xFFFFFFFFU)
        return fail(state, OMC_SOURCE_REQUEST_LIMIT, absolute, size, OMC_SOURCE_IO_OK,
                    0U);
    if (size > ~(omc_u64)0 - state->bytes_requested ||
        (limits->max_total_bytes &&
         (state->bytes_requested > limits->max_total_bytes ||
          size > limits->max_total_bytes - state->bytes_requested)))
        return fail(state, OMC_SOURCE_BYTE_LIMIT, absolute, size, OMC_SOURCE_IO_OK, 0U);
    state->requests_issued++;
    state->bytes_requested += size;
    if (range->source.contiguous_data != NULL) {
        memmove(destination, range->source.contiguous_data + (omc_size)absolute, size);
        state->bytes_completed += size;
        return OMC_SOURCE_OK;
    }
    io = range->source.read_at(range->source.context, absolute, destination, size);
    if (io.code < OMC_SOURCE_IO_OK || io.code > OMC_SOURCE_IO_CANCELLED ||
        io.bytes_read > size)
        return fail(state, OMC_SOURCE_CONTRACT_VIOLATION, absolute, size, io.code,
                    io.bytes_read);
    state->bytes_completed += io.bytes_read;
    switch (io.code) {
    case OMC_SOURCE_IO_OK:
        code = io.bytes_read == size ? OMC_SOURCE_OK : OMC_SOURCE_SHORT_READ;
        break;
    case OMC_SOURCE_IO_ERROR:
        code = OMC_SOURCE_IO_FAILURE;
        break;
    case OMC_SOURCE_IO_CHANGED:
        code = OMC_SOURCE_CHANGED;
        break;
    case OMC_SOURCE_IO_CANCELLED:
        code = OMC_SOURCE_CANCELLED;
        break;
    default:
        code = OMC_SOURCE_CONTRACT_VIOLATION;
        break;
    }
    return code == OMC_SOURCE_OK
               ? code
               : fail(state, code, absolute, size, io.code, io.bytes_read);
}
omc_source_view
omc_source_read_view(const omc_source_range *range, omc_u64 offset, omc_u64 size,
                     omc_source_window *window, omc_source_state *state,
                     const omc_source_limits *limits, omc_u64 minimum_read_bytes)
{
    omc_source_view result;
    omc_source_limits defaults;
    omc_u64 delta, fetch, remaining;
    memset(&result, 0, sizeof(result));
    if (state == NULL) {
        result.code = OMC_SOURCE_INVALID_ARGUMENT;
        return result;
    }
    if (state->code != OMC_SOURCE_OK) {
        result.code = state->code;
        return result;
    }
    if (!omc_source_range_valid(range)) {
        result.code = fail(state, OMC_SOURCE_INVALID_ARGUMENT, offset, size,
                           OMC_SOURCE_IO_OK, 0U);
        return result;
    }
    if (offset > range->size || size > range->size - offset ||
        size > (omc_u64)(omc_size)-1) {
        result.code =
            fail(state, OMC_SOURCE_OUT_OF_RANGE, offset, size, OMC_SOURCE_IO_OK, 0U);
        return result;
    }
    if (range->source.contiguous_data != NULL) {
        result.bytes.data =
            range->source.contiguous_data + (omc_size)(range->source_offset + offset);
        result.bytes.size = (omc_size)size;
        return result;
    }
    if (!size)
        return result;
    if (window == NULL || window->storage == NULL || size > window->capacity) {
        result.code = fail(state, OMC_SOURCE_SCRATCH_TOO_SMALL, offset, size,
                           OMC_SOURCE_IO_OK, 0U);
        return result;
    }
    if (window->valid && window->valid_bytes <= window->capacity &&
        offset >= window->range_offset) {
        delta = offset - window->range_offset;
        if (delta <= window->valid_bytes && size <= window->valid_bytes - delta) {
            result.bytes.data = window->storage + (omc_size)delta;
            result.bytes.size = (omc_size)size;
            result.cache_hit = 1;
            return result;
        }
    }
    if (limits == NULL) {
        omc_source_limits_init(&defaults);
        limits = &defaults;
    }
    fetch = size > minimum_read_bytes ? size : minimum_read_bytes;
    if (fetch > window->capacity)
        fetch = window->capacity;
    if (fetch > range->size - offset)
        fetch = range->size - offset;
    if (limits->max_single_read_bytes && fetch > limits->max_single_read_bytes)
        fetch = limits->max_single_read_bytes;
    if (limits->max_total_bytes && state->bytes_requested < limits->max_total_bytes) {
        remaining = limits->max_total_bytes - state->bytes_requested;
        if (fetch > remaining)
            fetch = remaining;
    }
    if (fetch < size) {
        result.code =
            fail(state, OMC_SOURCE_BYTE_LIMIT, offset, size, OMC_SOURCE_IO_OK, 0U);
        return result;
    }
    omc_source_window_reset(window);
    result.code =
        omc_source_read(range, offset, window->storage, (omc_size)fetch, state, limits);
    if (result.code != OMC_SOURCE_OK)
        return result;
    window->range_offset = offset;
    window->valid_bytes = fetch;
    window->valid = 1;
    result.bytes.data = window->storage;
    result.bytes.size = (omc_size)size;
    return result;
}
