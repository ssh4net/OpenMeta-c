#ifndef OMC_SOURCE_H
#define OMC_SOURCE_H
#include "omc/omc_base.h"
#include "omc/omc_span.h"
#include "omc/omc_types.h"
OMC_EXTERN_C_BEGIN

typedef enum omc_source_io_code {
    OMC_SOURCE_IO_OK = 0,
    OMC_SOURCE_IO_ERROR,
    OMC_SOURCE_IO_CHANGED,
    OMC_SOURCE_IO_CANCELLED
} omc_source_io_code;
typedef struct omc_source_io_res {
    omc_source_io_code code;
    omc_u64 bytes_read;
} omc_source_io_res;
/* Synchronous exact-read callback. Report bytes actually written, even on error.
 * No retries or ownership transfer. The host keeps size and content stable. */
typedef omc_source_io_res (*omc_source_read_at)(void *context, omc_u64 offset,
                                                omc_u8 *destination, omc_size size);
typedef struct omc_source {
    omc_u64 size;
    const omc_u8 *contiguous_data;
    void *context;
    omc_source_read_at read_at;
    int concurrent_reads; /* Descriptive host promise; no locks are installed. */
} omc_source;
typedef struct omc_source_range {
    omc_source source;
    omc_u64 source_offset;
    omc_u64 size;
} omc_source_range;
typedef enum omc_source_code {
    OMC_SOURCE_OK = 0,
    OMC_SOURCE_INVALID_ARGUMENT,
    OMC_SOURCE_OUT_OF_RANGE,
    OMC_SOURCE_REQUEST_TOO_LARGE,
    OMC_SOURCE_REQUEST_LIMIT,
    OMC_SOURCE_BYTE_LIMIT,
    OMC_SOURCE_SHORT_READ,
    OMC_SOURCE_IO_FAILURE,
    OMC_SOURCE_CHANGED,
    OMC_SOURCE_CANCELLED,
    OMC_SOURCE_CONTRACT_VIOLATION,
    OMC_SOURCE_SCRATCH_TOO_SMALL
} omc_source_code;
typedef struct omc_source_limits {
    omc_u32 max_requests;
    omc_u64 max_total_bytes;
    omc_u64 max_single_read_bytes;
} omc_source_limits;
typedef struct omc_source_state {
    omc_source_code code;
    omc_source_io_code io_code;
    omc_u32 requests_issued;
    omc_u64 bytes_requested;
    omc_u64 bytes_completed;
    omc_u64 failure_offset;
    omc_u64 failure_request_bytes;
    omc_u64 failure_bytes_read;
} omc_source_state;
/* Borrowed cache. Reset when changing source/range or starting an operation.
 * Views expire on refill. Memory-source views have the source's lifetime. */
typedef struct omc_source_window {
    omc_u8 *storage;
    omc_size capacity;
    omc_u64 range_offset;
    omc_u64 valid_bytes;
    int valid;
} omc_source_window;
typedef struct omc_source_view {
    omc_source_code code;
    omc_const_bytes bytes;
    int cache_hit;
} omc_source_view;

OMC_API omc_source omc_source_memory(const omc_u8 *bytes, omc_size size);
OMC_API omc_source omc_source_callback(omc_u64 size, void *context,
                                       omc_source_read_at callback,
                                       int concurrent_reads);
OMC_API int omc_source_valid(const omc_source *source);
OMC_API int omc_source_range_valid(const omc_source_range *range);
OMC_API void omc_source_limits_init(omc_source_limits *limits);
OMC_API void omc_source_state_init(omc_source_state *state);
OMC_API void omc_source_window_reset(omc_source_window *window);
OMC_API omc_source_code omc_source_read(const omc_source_range *range, omc_u64 offset,
                                        omc_u8 *destination, omc_size size,
                                        omc_source_state *state,
                                        const omc_source_limits *limits);
/* minimum_read_bytes controls optional read-ahead; zero requests exactly size.
 * Zero-copy memory views and cache hits consume no I/O budget. */
OMC_API omc_source_view omc_source_read_view(const omc_source_range *range,
                                             omc_u64 offset, omc_u64 size,
                                             omc_source_window *window,
                                             omc_source_state *state,
                                             const omc_source_limits *limits,
                                             omc_u64 minimum_read_bytes);
OMC_EXTERN_C_END
#endif
