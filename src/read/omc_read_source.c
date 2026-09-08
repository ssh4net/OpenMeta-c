#include "read/omc_read_internal.h"
#include "omc/omc_edit.h"
#include <string.h>

void
omc_read_source_opts_init(omc_read_source_opts *opts)
{
    if (opts != NULL) {
        omc_read_opts_init(&opts->decode);
        omc_source_limits_init(&opts->io);
    }
}
omc_read_source_res
omc_read_source(const omc_source_range* range, omc_store* store,
                  omc_read_source_workspace* w, omc_source_state* state,
                  const omc_read_source_opts* opts)
{
    omc_read_source_res res;
    omc_read_source_opts defaults;
    omc_store candidate;
    omc_exif_source_res exif_source;
    omc_u8 signature[16];
    omc_scan_fmt format;
    memset(&res, 0, sizeof(res));
    if (!omc_source_range_valid(range) || store == NULL || w == NULL || state == NULL ||
        (w->block_capacity && w->blocks == NULL) || (w->ifd_capacity && w->ifds == NULL) ||
        (w->payload_capacity && w->payload == NULL) ||
        (w->payload_index_capacity && w->payload_indices == NULL) ||
        (w->metadata_capacity && w->metadata == NULL)) {
        res.status = OMC_READ_SOURCE_INVALID_ARGUMENT; return res;
    }
    if (state->code != OMC_SOURCE_OK) { res.status = OMC_READ_SOURCE_IO; return res; }
    if (opts == NULL) { omc_read_source_opts_init(&defaults); opts = &defaults; }
    if (range->source.contiguous_data != NULL) {
        res.decoded = omc_read_simple(range->source.contiguous_data + (omc_size)range->source_offset,
                          (omc_size)range->size, store, w->blocks, w->block_capacity,
                          w->ifds, w->ifd_capacity, w->payload, w->payload_capacity,
                          w->payload_indices, w->payload_index_capacity, &opts->decode);
        return res;
    }
    if (range->size < 2U) { res.status = OMC_READ_SOURCE_MALFORMED; return res; }
    if (omc_source_read(range, 0U, signature, 2U, state, &opts->io) != OMC_SOURCE_OK) {
        res.status = OMC_READ_SOURCE_IO; return res;
    }
    format = OMC_SCAN_FMT_UNKNOWN;
    if (signature[0] == 0xFFU && signature[1] == 0xD8U) format = OMC_SCAN_FMT_JPEG;
    else if (signature[0] == 0x89U && signature[1] == 'P') format = OMC_SCAN_FMT_PNG;
    else if (signature[0] == 'R' && signature[1] == 'I') format = OMC_SCAN_FMT_WEBP;
    else if (signature[0] == 'G' && signature[1] == 'I') format = OMC_SCAN_FMT_GIF;
    else if (signature[0] == 'F' && signature[1] == 'U') format = OMC_SCAN_FMT_RAF;
    else if (signature[0] == 'F' && signature[1] == 'O') format = OMC_SCAN_FMT_X3F;
    else if (signature[0] == 0x76U && signature[1] == 0x2fU) format = OMC_SCAN_FMT_EXR;
    else if ((signature[0] == 'I' && signature[1] == 'I') ||
             (signature[0] == 'M' && signature[1] == 'M')) format = OMC_SCAN_FMT_TIFF;
    else if (range->size >= 8U) {
        if (omc_source_read(range, 2U, signature + 2U, 6U, state, &opts->io) != OMC_SOURCE_OK) {
            res.status = OMC_READ_SOURCE_IO; return res;
        }
        if (memcmp(signature + 4U, "jP  ", 4U) == 0) format = OMC_SCAN_FMT_JP2;
        else if (memcmp(signature + 4U, "JXL ", 4U) == 0) format = OMC_SCAN_FMT_JXL;
        else if (signature[0] != 'G' && signature[0] != 'F' && signature[0] != 0x76U)
            format = OMC_SCAN_FMT_HEIF;
    }
    if (format == OMC_SCAN_FMT_TIFF && range->size >= 14U) {
        if (omc_source_read(range, 6U, signature + 6U, 2U, state, &opts->io) != OMC_SOURCE_OK) {
            res.status = OMC_READ_SOURCE_IO; return res;
        }
        if (memcmp(signature + 6U, "HE", 2U) == 0) {
            if (omc_source_read(range, 8U, signature + 8U, 6U, state, &opts->io) != OMC_SOURCE_OK) {
                res.status = OMC_READ_SOURCE_IO; return res;
            }
            if (memcmp(signature + 8U, "APCCDR", 6U) == 0) format = OMC_SCAN_FMT_CRW;
        }
    }
    if (format == OMC_SCAN_FMT_UNKNOWN) { res.status = OMC_READ_SOURCE_UNSUPPORTED; return res; }
    omc_store_init(&candidate);
    if (omc_edit_commit(store, NULL, 0U, &candidate) != OMC_STATUS_OK) {
        res.status = OMC_READ_SOURCE_LIMIT; return res;
    }
    if (format == OMC_SCAN_FMT_TIFF) {
        memset(&exif_source, 0, sizeof(exif_source));
        res.decoded = omc_read_tiff_source(range, &candidate, w, state, opts, &exif_source);
        res.value_scratch_needed = exif_source.value_scratch_needed;
        res.scratch_used = exif_source.value_scratch_used;
        res.nested_payloads_skipped = exif_source.nested_payloads_skipped;
    } else {
        res.decoded = omc_read_container_source(range, format, &candidate, w, state, opts, &res);
    }
    if (state->code != OMC_SOURCE_OK) res.status = OMC_READ_SOURCE_IO;
    else if (res.status == OMC_READ_SOURCE_LIMIT || res.value_scratch_needed != 0U || res.decoded.exif.status == OMC_EXIF_LIMIT ||
             res.decoded.exif.status == OMC_EXIF_NOMEM || res.decoded.exr.status == OMC_EXR_LIMIT ||
             res.decoded.exr.status == OMC_EXR_NOMEM || res.decoded.pay.status == OMC_PAY_LIMIT ||
             res.decoded.pay.status == OMC_PAY_NOMEM || res.decoded.pay.status == OMC_PAY_TRUNCATED)
        res.status = OMC_READ_SOURCE_LIMIT;
    else if (res.decoded.scan.status == OMC_SCAN_UNSUPPORTED) res.status = OMC_READ_SOURCE_UNSUPPORTED;
    else if (res.decoded.scan.status == OMC_SCAN_MALFORMED) res.status = OMC_READ_SOURCE_MALFORMED;
    if (res.status == OMC_READ_SOURCE_OK) { omc_store_fini(store); *store = candidate; }
    else omc_store_fini(&candidate);
    return res;
}
