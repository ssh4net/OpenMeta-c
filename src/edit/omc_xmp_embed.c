#include "omc/omc_xmp_embed.h"

#include <string.h>

static const omc_u8 k_omc_xmp_embed_jpeg_prefix[]
    = "http://ns.adobe.com/xap/1.0/\0";

static const omc_u8 k_omc_xmp_embed_png_prefix[] = {
    'X', 'M', 'L', ':', 'c', 'o', 'm', '.', 'a', 'd', 'o',
    'b', 'e', '.', 'x', 'm', 'p', 0U, 0U, 0U, 0U, 0U
};

static void
omc_xmp_embed_res_init(omc_xmp_dump_res* res)
{
    if (res == (omc_xmp_dump_res*)0) {
        return;
    }

    res->status = OMC_XMP_DUMP_OK;
    res->written = 0U;
    res->needed = 0U;
    res->entries = 0U;
}

static int
omc_xmp_embed_get_prefix(omc_scan_fmt format, const omc_u8** out_prefix,
                         omc_size* out_prefix_size)
{
    if (out_prefix == (const omc_u8**)0
        || out_prefix_size == (omc_size*)0) {
        return 0;
    }

    switch (format) {
    case OMC_SCAN_FMT_JPEG:
        *out_prefix = k_omc_xmp_embed_jpeg_prefix;
        *out_prefix_size = sizeof(k_omc_xmp_embed_jpeg_prefix) - 1U;
        return 1;
    case OMC_SCAN_FMT_PNG:
        *out_prefix = k_omc_xmp_embed_png_prefix;
        *out_prefix_size = sizeof(k_omc_xmp_embed_png_prefix);
        return 1;
    case OMC_SCAN_FMT_TIFF:
    case OMC_SCAN_FMT_DNG:
    case OMC_SCAN_FMT_WEBP:
    case OMC_SCAN_FMT_JP2:
    case OMC_SCAN_FMT_JXL:
    case OMC_SCAN_FMT_HEIF:
    case OMC_SCAN_FMT_AVIF:
    case OMC_SCAN_FMT_CR3:
        *out_prefix = (const omc_u8*)0;
        *out_prefix_size = 0U;
        return 1;
    default:
        *out_prefix = (const omc_u8*)0;
        *out_prefix_size = 0U;
        return 0;
    }
}

static omc_status
omc_xmp_embed_dump_packet_once(const omc_store* store, omc_u8* out,
                               omc_size out_cap,
                               const omc_xmp_sidecar_req* req,
                               omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_cfg cfg;

    if (store == (const omc_store*)0 || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_xmp_sidecar_cfg_from_req(&cfg, req);
    if (cfg.format == OMC_XMP_SIDECAR_PORTABLE) {
        return omc_xmp_dump_portable(store, out, out_cap, &cfg.portable,
                                     out_res);
    }
    return omc_xmp_dump_lossless(store, out, out_cap, &cfg.lossless, out_res);
}

void
omc_xmp_embed_opts_init(omc_xmp_embed_opts* opts)
{
    if (opts == (omc_xmp_embed_opts*)0) {
        return;
    }

    opts->format = OMC_SCAN_FMT_JPEG;
    omc_xmp_sidecar_req_init(&opts->packet);
    opts->packet.format = OMC_XMP_SIDECAR_PORTABLE;
}

omc_status
omc_xmp_embed_payload(const omc_store* store, omc_u8* out, omc_size out_cap,
                      const omc_xmp_embed_opts* opts,
                      omc_xmp_dump_res* out_res)
{
    omc_xmp_embed_opts local_opts;
    const omc_u8* prefix;
    omc_size prefix_size;
    omc_size prefix_written;
    omc_u8* packet_out;
    omc_size packet_cap;
    omc_xmp_dump_res packet_res;
    omc_status status;

    if (store == (const omc_store*)0 || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == (omc_u8*)0 && out_cap != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_embed_opts*)0) {
        omc_xmp_embed_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_embed_res_init(out_res);
    if (!omc_xmp_embed_get_prefix(opts->format, &prefix, &prefix_size)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    prefix_written = 0U;
    if (prefix_size != 0U && out_cap != 0U) {
        prefix_written = prefix_size;
        if (prefix_written > out_cap) {
            prefix_written = out_cap;
        }
        if (prefix_written != 0U) {
            memcpy(out, prefix, prefix_written);
        }
    }

    packet_out = (omc_u8*)0;
    packet_cap = 0U;
    if (out_cap > prefix_size) {
        packet_out = out + prefix_size;
        packet_cap = out_cap - prefix_size;
    }

    status = omc_xmp_embed_dump_packet_once(store, packet_out, packet_cap,
                                            &opts->packet, &packet_res);
    if (status != OMC_STATUS_OK) {
        return status;
    }

    out_res->entries = packet_res.entries;
    out_res->needed = (omc_u64)prefix_size + packet_res.needed;
    out_res->written = (omc_u64)prefix_written + packet_res.written;

    if (packet_res.status == OMC_XMP_DUMP_LIMIT) {
        out_res->status = OMC_XMP_DUMP_LIMIT;
    } else if (out_res->needed > (omc_u64)out_cap) {
        out_res->status = OMC_XMP_DUMP_TRUNCATED;
    } else {
        out_res->status = OMC_XMP_DUMP_OK;
    }

    return OMC_STATUS_OK;
}

omc_status
omc_xmp_embed_payload_arena(const omc_store* store, omc_arena* out,
                            const omc_xmp_embed_opts* opts,
                            omc_xmp_dump_res* out_res)
{
    omc_xmp_embed_opts local_opts;
    const omc_u8* prefix;
    omc_size prefix_size;
    omc_arena packet;
    omc_xmp_dump_res packet_res;
    omc_byte_ref ignored_ref;
    omc_status status;
    omc_u64 final_needed;

    if (store == (const omc_store*)0 || out == (omc_arena*)0
        || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_embed_opts*)0) {
        omc_xmp_embed_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_embed_res_init(out_res);
    if (!omc_xmp_embed_get_prefix(opts->format, &prefix, &prefix_size)) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    omc_arena_init(&packet);
    status = omc_xmp_dump_sidecar_req(store, &packet, &opts->packet,
                                      &packet_res);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&packet);
        return status;
    }

    if (packet.size > ((omc_size)(~(omc_size)0) - prefix_size)) {
        omc_arena_fini(&packet);
        return OMC_STATUS_OVERFLOW;
    }

    omc_arena_reset(out);
    if (prefix_size != 0U) {
        status = omc_arena_append(out, prefix, prefix_size, &ignored_ref);
        if (status != OMC_STATUS_OK) {
            omc_arena_fini(&packet);
            return status;
        }
    }
    status = omc_arena_append(out, packet.data, packet.size, &ignored_ref);
    if (status != OMC_STATUS_OK) {
        omc_arena_fini(&packet);
        return status;
    }

    final_needed = (omc_u64)prefix_size + packet_res.needed;
    out_res->entries = packet_res.entries;
    out_res->needed = final_needed;
    out_res->written = (omc_u64)out->size;
    out_res->status = packet_res.status;

    omc_arena_fini(&packet);
    return OMC_STATUS_OK;
}
