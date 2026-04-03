#include "omc/omc_xmp_apply.h"

#include <string.h>

static void
omc_xmp_apply_res_init(omc_xmp_apply_res* res)
{
    if (res == (omc_xmp_apply_res*)0) {
        return;
    }
    memset(res, 0, sizeof(*res));
    res->writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    res->embedded.status = OMC_XMP_WRITE_UNSUPPORTED;
    res->sidecar.status = OMC_XMP_DUMP_LIMIT;
}

void
omc_xmp_apply_opts_init(omc_xmp_apply_opts* opts)
{
    if (opts == (omc_xmp_apply_opts*)0) {
        return;
    }

    opts->format = OMC_SCAN_FMT_UNKNOWN;
    opts->writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    opts->destination_embedded_mode = OMC_XMP_DEST_EMBEDDED_PRESERVE_EXISTING;
    omc_xmp_embed_opts_init(&opts->embedded);
    omc_xmp_sidecar_req_init(&opts->sidecar);
    opts->sidecar.format = OMC_XMP_SIDECAR_PORTABLE;
    opts->sidecar.include_existing_xmp = 1;
}

omc_status
omc_xmp_apply(const omc_u8* file_bytes, omc_size file_size,
              const omc_store* store, omc_arena* edited_out,
              omc_arena* sidecar_out, const omc_xmp_apply_opts* opts,
              omc_xmp_apply_res* out_res)
{
    omc_xmp_apply_opts local_opts;
    omc_xmp_write_opts write_opts;
    omc_status status;

    if (file_bytes == (const omc_u8*)0 || store == (const omc_store*)0
        || edited_out == (omc_arena*)0 || sidecar_out == (omc_arena*)0
        || out_res == (omc_xmp_apply_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_apply_opts*)0) {
        omc_xmp_apply_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_apply_res_init(out_res);
    out_res->writeback_mode = opts->writeback_mode;
    omc_arena_reset(edited_out);
    omc_arena_reset(sidecar_out);

    omc_xmp_write_opts_init(&write_opts);
    write_opts.format = opts->format;
    write_opts.embed = opts->embedded;

    if (opts->writeback_mode == OMC_XMP_WRITEBACK_SIDECAR_ONLY) {
        write_opts.write_embedded_xmp = 0;
        write_opts.strip_existing_xmp =
            opts->destination_embedded_mode
            == OMC_XMP_DEST_EMBEDDED_STRIP_EXISTING;
    } else {
        write_opts.write_embedded_xmp = 1;
        write_opts.strip_existing_xmp = 1;
    }

    status = omc_xmp_write_embedded(file_bytes, file_size, store, edited_out,
                                    &write_opts, &out_res->embedded);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    if (opts->writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY) {
        out_res->sidecar_requested = 1;
        status = omc_xmp_dump_sidecar_req(store, sidecar_out, &opts->sidecar,
                                          &out_res->sidecar);
        if (status != OMC_STATUS_OK) {
            return status;
        }
    }

    return OMC_STATUS_OK;
}
