#include "omc/omc_transfer_persist.h"

#include <stdio.h>
#include <string.h>

static void
omc_transfer_persist_res_init(omc_transfer_persist_res* res)
{
    if (res == (omc_transfer_persist_res*)0) {
        return;
    }
    memset(res, 0, sizeof(*res));
    res->status = OMC_TRANSFER_UNSUPPORTED;
    res->output_status = OMC_TRANSFER_UNSUPPORTED;
    res->xmp_sidecar_status = OMC_TRANSFER_UNSUPPORTED;
    res->xmp_sidecar_cleanup_status = OMC_TRANSFER_UNSUPPORTED;
}

static omc_status
omc_transfer_append_cstr(omc_arena* arena, const char* text,
                         omc_byte_ref* out_ref)
{
    if (out_ref == (omc_byte_ref*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    out_ref->offset = 0U;
    out_ref->size = 0U;
    if (arena == (omc_arena*)0 || text == (const char*)0) {
        return OMC_STATUS_OK;
    }
    return omc_arena_append(arena, text, strlen(text), out_ref);
}

static omc_status
omc_transfer_set_message(omc_arena* arena, omc_byte_ref* out_ref,
                         const char* text)
{
    return omc_transfer_append_cstr(arena, text, out_ref);
}

static int
omc_transfer_path_exists(const char* path)
{
    FILE* fp;

    if (path == (const char*)0 || path[0] == '\0') {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == (FILE*)0) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int
omc_transfer_write_file(const char* path, const omc_u8* bytes, omc_size size)
{
    FILE* fp;
    size_t wrote;

    if (path == (const char*)0 || path[0] == '\0') {
        return 0;
    }
    if (bytes == (const omc_u8*)0 && size != 0U) {
        return 0;
    }

    fp = fopen(path, "wb");
    if (fp == (FILE*)0) {
        return 0;
    }

    wrote = 0U;
    if (size != 0U) {
        wrote = fwrite(bytes, 1U, (size_t)size, fp);
    }
    if (fclose(fp) != 0) {
        return 0;
    }
    return wrote == (size_t)size;
}

static omc_status
omc_transfer_build_sidecar_path(const char* base_path, omc_arena* arena,
                                omc_byte_ref* out_ref)
{
    const char* slash;
    const char* dot;
    omc_size base_len;
    omc_size stem_len;
    omc_status status;

    if (out_ref == (omc_byte_ref*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    out_ref->offset = 0U;
    out_ref->size = 0U;
    if (base_path == (const char*)0 || base_path[0] == '\0') {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (arena == (omc_arena*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    base_len = (omc_size)strlen(base_path);
    slash = strrchr(base_path, '/');
    dot = strrchr(base_path, '.');
    if (dot != (const char*)0
        && (slash == (const char*)0 || dot > slash + 0)) {
        stem_len = (omc_size)(dot - base_path);
    } else {
        stem_len = base_len;
    }

    status = omc_arena_reserve(arena, arena->size + stem_len + 5U);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    out_ref->offset = (omc_u32)arena->size;
    out_ref->size = (omc_u32)(stem_len + 4U);
    memcpy(arena->data + arena->size, base_path, stem_len);
    memcpy(arena->data + arena->size + stem_len, ".xmp", 4U);
    arena->data[arena->size + stem_len + 4U] = 0U;
    arena->size += stem_len + 5U;
    return OMC_STATUS_OK;
}

static int
omc_transfer_persist_allows_missing_output_path(
    const omc_transfer_res* transfer, const omc_transfer_persist_opts* opts)
{
    if (transfer == (const omc_transfer_res*)0
        || opts == (const omc_transfer_persist_opts*)0) {
        return 0;
    }
    if (transfer->format != OMC_SCAN_FMT_DNG
        || transfer->dng_target_mode
               != OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD) {
        return 0;
    }
    if (transfer->writeback_mode != OMC_XMP_WRITEBACK_SIDECAR_ONLY) {
        return 0;
    }
    if (opts->write_output) {
        return 0;
    }
    return opts->xmp_sidecar_base_path != (const char*)0
           && opts->xmp_sidecar_base_path[0] != '\0';
}

static omc_status
omc_transfer_maybe_set_cleanup_path(
    const omc_transfer_res* transfer, const omc_transfer_persist_opts* opts,
    omc_arena* meta_out, omc_transfer_persist_res* out_res)
{
    const char* sidecar_base;
    omc_status status;

    if (transfer->writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY
        || opts->destination_sidecar_mode
               != OMC_TRANSFER_DEST_SIDECAR_STRIP_EXISTING) {
        return OMC_STATUS_OK;
    }

    sidecar_base = opts->xmp_sidecar_base_path;
    if (sidecar_base == (const char*)0 || sidecar_base[0] == '\0') {
        sidecar_base = opts->output_path;
    }
    if (sidecar_base == (const char*)0 || sidecar_base[0] == '\0') {
        out_res->xmp_sidecar_cleanup_status = OMC_TRANSFER_UNSUPPORTED;
        return omc_transfer_set_message(
            meta_out, &out_res->xmp_sidecar_cleanup_message,
            "destination xmp sidecar strip mode requires output_path or "
            "xmp_sidecar_base_path");
    }

    status = omc_transfer_build_sidecar_path(
        sidecar_base, meta_out, &out_res->xmp_sidecar_cleanup_path);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    out_res->xmp_sidecar_cleanup_requested = 1;
    out_res->xmp_sidecar_cleanup_status = OMC_TRANSFER_OK;
    if (omc_transfer_path_exists((const char*)omc_arena_view(
                                     meta_out,
                                     out_res->xmp_sidecar_cleanup_path)
                                     .data)) {
        return omc_transfer_set_message(
            meta_out, &out_res->xmp_sidecar_cleanup_message,
            "existing destination xmp sidecar should be removed");
    }
    return omc_transfer_set_message(
        meta_out, &out_res->xmp_sidecar_cleanup_message,
        "destination xmp sidecar cleanup path planned");
}

void
omc_transfer_persist_opts_init(omc_transfer_persist_opts* opts)
{
    if (opts == (omc_transfer_persist_opts*)0) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->write_output = 1;
    opts->remove_destination_xmp_sidecar = 1;
    opts->destination_sidecar_mode =
        OMC_TRANSFER_DEST_SIDECAR_PRESERVE_EXISTING;
}

omc_status
omc_transfer_persist(const omc_u8* edited_bytes, omc_size edited_size,
                     const omc_u8* sidecar_bytes, omc_size sidecar_size,
                     const omc_transfer_res* transfer,
                     const omc_transfer_persist_opts* opts,
                     omc_arena* meta_out,
                     omc_transfer_persist_res* out_res)
{
    omc_transfer_persist_opts local_opts;
    omc_status status;
    const char* sidecar_base;
    omc_const_bytes cleanup_path;
    int allow_missing_output_path;

    if (transfer == (const omc_transfer_res*)0 || meta_out == (omc_arena*)0
        || out_res == (omc_transfer_persist_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if ((edited_bytes == (const omc_u8*)0 && edited_size != 0U)
        || (sidecar_bytes == (const omc_u8*)0 && sidecar_size != 0U)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_transfer_persist_opts*)0) {
        omc_transfer_persist_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_arena_reset(meta_out);
    omc_transfer_persist_res_init(out_res);
    out_res->xmp_sidecar_requested =
        transfer->writeback_mode != OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    allow_missing_output_path =
        omc_transfer_persist_allows_missing_output_path(transfer, opts);

    status = omc_transfer_append_cstr(meta_out, opts->output_path,
                                      &out_res->output_path);
    if (status != OMC_STATUS_OK) {
        return status;
    }
    if ((opts->output_path == (const char*)0 || opts->output_path[0] == '\0')
        && !allow_missing_output_path) {
        out_res->status = OMC_TRANSFER_UNSUPPORTED;
        out_res->output_status = OMC_TRANSFER_UNSUPPORTED;
        status = omc_transfer_set_message(meta_out, &out_res->output_message,
                                          "persist output path is required");
        if (status != OMC_STATUS_OK) {
            return status;
        }
        out_res->message = out_res->output_message;
        return OMC_STATUS_OK;
    }

    if (!opts->write_output) {
        out_res->output_status = OMC_TRANSFER_OK;
        out_res->output_bytes = opts->prewritten_output_bytes;
        if (allow_missing_output_path) {
            status = omc_transfer_set_message(
                meta_out, &out_res->output_message,
                "dng minimal scaffold sidecar persist does not require "
                "output_path");
        } else {
            status = omc_transfer_set_message(
                meta_out, &out_res->output_message,
                "output bytes were already written by caller");
        }
        if (status != OMC_STATUS_OK) {
            return status;
        }
    } else {
        if (!transfer->edited_present || edited_size == 0U) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            out_res->output_status = OMC_TRANSFER_UNSUPPORTED;
            status = omc_transfer_set_message(
                meta_out, &out_res->output_message,
                "no edited output bytes available to persist");
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->message = out_res->output_message;
            return OMC_STATUS_OK;
        }
        if (!opts->overwrite_output
            && omc_transfer_path_exists(opts->output_path)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            out_res->output_status = OMC_TRANSFER_UNSUPPORTED;
            status = omc_transfer_set_message(meta_out,
                                              &out_res->output_message,
                                              "output path exists");
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->message = out_res->output_message;
            return OMC_STATUS_OK;
        }
        if (!omc_transfer_write_file(opts->output_path, edited_bytes,
                                     edited_size)) {
            out_res->status = OMC_TRANSFER_UNSUPPORTED;
            out_res->output_status = OMC_TRANSFER_UNSUPPORTED;
            status = omc_transfer_set_message(meta_out,
                                              &out_res->output_message,
                                              "failed to write output bytes");
            if (status != OMC_STATUS_OK) {
                return status;
            }
            out_res->message = out_res->output_message;
            return OMC_STATUS_OK;
        }
        out_res->output_status = OMC_TRANSFER_OK;
        out_res->output_bytes = (omc_u64)edited_size;
    }

    if (out_res->xmp_sidecar_requested) {
        sidecar_base = opts->xmp_sidecar_base_path;
        if (sidecar_base == (const char*)0 || sidecar_base[0] == '\0') {
            sidecar_base = opts->output_path;
        }
        if (sidecar_base == (const char*)0 || sidecar_base[0] == '\0') {
            out_res->xmp_sidecar_status = OMC_TRANSFER_UNSUPPORTED;
            status = omc_transfer_set_message(
                meta_out, &out_res->xmp_sidecar_message,
                "xmp sidecar persist requires output_path or "
                "xmp_sidecar_base_path");
            if (status != OMC_STATUS_OK) {
                return status;
            }
        } else {
            status = omc_transfer_build_sidecar_path(
                sidecar_base, meta_out, &out_res->xmp_sidecar_path);
            if (status != OMC_STATUS_OK) {
                return status;
            }
            if (transfer->sidecar.status != OMC_XMP_DUMP_OK) {
                out_res->xmp_sidecar_status = transfer->status;
                status = omc_transfer_set_message(
                    meta_out, &out_res->xmp_sidecar_message,
                    "xmp sidecar output is not available");
                if (status != OMC_STATUS_OK) {
                    return status;
                }
            } else if (sidecar_size == 0U) {
                out_res->xmp_sidecar_status = OMC_TRANSFER_OK;
                status = omc_transfer_set_message(
                    meta_out, &out_res->xmp_sidecar_message,
                    "no xmp sidecar output bytes");
                if (status != OMC_STATUS_OK) {
                    return status;
                }
            } else {
                omc_const_bytes sidecar_path_view;

                sidecar_path_view =
                    omc_arena_view(meta_out, out_res->xmp_sidecar_path);
                if (!opts->overwrite_xmp_sidecar
                    && omc_transfer_path_exists(
                           (const char*)sidecar_path_view.data)) {
                    out_res->xmp_sidecar_status = OMC_TRANSFER_UNSUPPORTED;
                    status = omc_transfer_set_message(
                        meta_out, &out_res->xmp_sidecar_message,
                        "xmp sidecar path exists");
                    if (status != OMC_STATUS_OK) {
                        return status;
                    }
                } else if (!omc_transfer_write_file(
                               (const char*)sidecar_path_view.data,
                               sidecar_bytes, sidecar_size)) {
                    out_res->xmp_sidecar_status = OMC_TRANSFER_UNSUPPORTED;
                    status = omc_transfer_set_message(
                        meta_out, &out_res->xmp_sidecar_message,
                        "failed to write xmp sidecar bytes");
                    if (status != OMC_STATUS_OK) {
                        return status;
                    }
                } else {
                    out_res->xmp_sidecar_status = OMC_TRANSFER_OK;
                    out_res->xmp_sidecar_bytes = (omc_u64)sidecar_size;
                }
            }
        }
    }

    status = omc_transfer_maybe_set_cleanup_path(transfer, opts, meta_out,
                                                 out_res);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    if (out_res->xmp_sidecar_cleanup_requested) {
        cleanup_path = omc_arena_view(meta_out,
                                      out_res->xmp_sidecar_cleanup_path);
        if (!opts->remove_destination_xmp_sidecar) {
            out_res->xmp_sidecar_cleanup_status = OMC_TRANSFER_OK;
            status = omc_transfer_set_message(
                meta_out, &out_res->xmp_sidecar_cleanup_message,
                "destination xmp sidecar cleanup skipped by options");
            if (status != OMC_STATUS_OK) {
                return status;
            }
        } else if (!omc_transfer_path_exists((const char*)cleanup_path.data)) {
            out_res->xmp_sidecar_cleanup_status = OMC_TRANSFER_OK;
            status = omc_transfer_set_message(
                meta_out, &out_res->xmp_sidecar_cleanup_message,
                "no existing destination xmp sidecar detected");
            if (status != OMC_STATUS_OK) {
                return status;
            }
        } else if (remove((const char*)cleanup_path.data) != 0) {
            out_res->xmp_sidecar_cleanup_status = OMC_TRANSFER_UNSUPPORTED;
            status = omc_transfer_set_message(
                meta_out, &out_res->xmp_sidecar_cleanup_message,
                "failed to remove destination xmp sidecar");
            if (status != OMC_STATUS_OK) {
                return status;
            }
        } else {
            out_res->xmp_sidecar_cleanup_status = OMC_TRANSFER_OK;
            out_res->xmp_sidecar_cleanup_removed = 1;
            status = omc_transfer_set_message(
                meta_out, &out_res->xmp_sidecar_cleanup_message,
                "removed existing destination xmp sidecar");
            if (status != OMC_STATUS_OK) {
                return status;
            }
        }
    }

    out_res->status = OMC_TRANSFER_OK;
    if (out_res->xmp_sidecar_requested
        && out_res->xmp_sidecar_status != OMC_TRANSFER_OK) {
        out_res->status = out_res->xmp_sidecar_status;
        out_res->message = out_res->xmp_sidecar_message;
        return OMC_STATUS_OK;
    }
    if (out_res->xmp_sidecar_cleanup_requested
        && out_res->xmp_sidecar_cleanup_status != OMC_TRANSFER_OK) {
        out_res->status = out_res->xmp_sidecar_cleanup_status;
        out_res->message = out_res->xmp_sidecar_cleanup_message;
        return OMC_STATUS_OK;
    }
    status = omc_transfer_set_message(meta_out, &out_res->message,
                                      "persisted transfer outputs");
    if (status != OMC_STATUS_OK) {
        return status;
    }
    return OMC_STATUS_OK;
}
