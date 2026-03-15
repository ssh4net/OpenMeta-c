#include "omc/omc_scan.h"

#include <assert.h>
#include <string.h>

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_bytes(omc_u8* out, omc_size* io_size, const void* src, omc_size n)
{
    memcpy(out + *io_size, src, n);
    *io_size += n;
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    append_bytes(out, io_size, text, strlen(text));
}

static void
append_u16be(omc_u8* out, omc_size* io_size, omc_u16 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
}

static void
append_u32be(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
}

static void
append_u32le(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
}

static void
write_u32le_at(omc_u8* out, omc_u32 off, omc_u32 value)
{
    out[off + 0U] = (omc_u8)((value >> 0) & 0xFFU);
    out[off + 1U] = (omc_u8)((value >> 8) & 0xFFU);
    out[off + 2U] = (omc_u8)((value >> 16) & 0xFFU);
    out[off + 3U] = (omc_u8)((value >> 24) & 0xFFU);
}

static omc_u32
fourcc(char a, char b, char c, char d)
{
    return OMC_FOURCC(a, b, c, d);
}

static omc_size
make_test_tiff_le(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_text(out, &size, "II");
    append_u16be(out, &size, 0x2A00U);
    append_bytes(out, &size, "\x08\x00\x00\x00\x00\x00\x00\x00", 8U);
    return size;
}

static void
append_png_chunk(omc_u8* out, omc_size* io_size, const char* type,
                 const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)payload_size);
    append_bytes(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    append_u32be(out, io_size, 0U);
}

static omc_size
make_test_png_scan(omc_u8* out)
{
    static const omc_u8 png_sig[8] = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU
    };
    static const omc_u8 icc_payload[] = {
        'i', 'c', 'c', 0x00U, 0x00U, 'A', 'B', 'C'
    };
    omc_u8 tiff[16];
    omc_u8 xmp_payload[64];
    omc_size tiff_size;
    omc_size xmp_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    xmp_size = 0U;
    append_text(xmp_payload, &xmp_size, "XML:com.adobe.xmp");
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_u8(xmp_payload, &xmp_size, 0U);
    append_text(xmp_payload, &xmp_size, "<x/>");

    size = 0U;
    append_bytes(out, &size, png_sig, sizeof(png_sig));
    append_png_chunk(out, &size, "eXIf", tiff, tiff_size);
    append_png_chunk(out, &size, "iTXt", xmp_payload, xmp_size);
    append_png_chunk(out, &size, "iCCP", icc_payload, sizeof(icc_payload));
    append_png_chunk(out, &size, "IEND", (const omc_u8*)0, 0U);
    return size;
}

static void
append_webp_chunk(omc_u8* out, omc_size* io_size, const char* type,
                  const omc_u8* payload, omc_size payload_size)
{
    append_bytes(out, io_size, type, 4U);
    append_u32le(out, io_size, (omc_u32)payload_size);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static omc_size
make_test_webp_scan(omc_u8* out)
{
    omc_u8 tiff[16];
    omc_u8 exif_payload[32];
    static const omc_u8 xmp_payload[] = { '<', 'x', '/', '>' };
    static const omc_u8 icc_payload[] = { 'A', 'B', 'C' };
    omc_size tiff_size;
    omc_size exif_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    exif_size = 0U;
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    size = 0U;
    append_text(out, &size, "RIFF");
    append_u32le(out, &size, 0U);
    append_text(out, &size, "WEBP");
    append_webp_chunk(out, &size, "EXIF", exif_payload, exif_size);
    append_webp_chunk(out, &size, "XMP ", xmp_payload, sizeof(xmp_payload));
    append_webp_chunk(out, &size, "ICCP", icc_payload, sizeof(icc_payload));
    write_u32le_at(out, 4U, (omc_u32)(size - 8U));
    return size;
}

static void
append_fullbox_header(omc_u8* out, omc_size* io_size, omc_u8 version)
{
    append_u8(out, io_size, version);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
}

static void
append_bmff_box(omc_u8* out, omc_size* io_size, omc_u32 type,
                const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)(8U + payload_size));
    append_u32be(out, io_size, type);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static omc_size
make_test_bmff_meta_scan(omc_u8* out, omc_u32 major_brand)
{
    static const char xmp_payload[] = "<x/>";
    static const omc_u8 jumb_payload[] = {
        0x00U, 0x00U, 0x00U, 0x09U,
        'j', 'u', 'm', 'b', 0xA0U
    };
    static const omc_u8 canon_uuid[16] = {
        0x85U, 0xC0U, 0xB6U, 0x87U, 0x82U, 0x0FU, 0x11U, 0xE0U,
        0x81U, 0x11U, 0xF4U, 0xCEU, 0x46U, 0x2BU, 0x6AU, 0x48U
    };
    omc_u8 tiff[16];
    omc_u8 exif_payload[32];
    omc_u8 idat_payload[128];
    omc_u8 infe_exif[64];
    omc_u8 infe_xmp[96];
    omc_u8 infe_jumb[96];
    omc_u8 iinf_payload[320];
    omc_u8 iloc_payload[160];
    omc_u8 idat_box[160];
    omc_u8 colr_payload[16];
    omc_u8 colr_box[32];
    omc_u8 ipco_payload[48];
    omc_u8 ipco_box[64];
    omc_u8 iprp_payload[80];
    omc_u8 iprp_box[96];
    omc_u8 meta_payload[768];
    omc_u8 moov_box[16];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size idat_size;
    omc_size exif_off;
    omc_size xmp_off;
    omc_size jumb_off;
    omc_size infe_exif_size;
    omc_size infe_xmp_size;
    omc_size infe_jumb_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size idat_box_size;
    omc_size colr_payload_size;
    omc_size colr_box_size;
    omc_size ipco_size;
    omc_size ipco_box_size;
    omc_size iprp_size;
    omc_size iprp_box_size;
    omc_size meta_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    (void)canon_uuid;

    tiff_size = make_test_tiff_le(tiff);
    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 6U);
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    idat_size = 0U;
    exif_off = idat_size;
    append_bytes(idat_payload, &idat_size, exif_payload, exif_size);
    xmp_off = idat_size;
    append_text(idat_payload, &idat_size, xmp_payload);
    jumb_off = idat_size;
    append_bytes(idat_payload, &idat_size, jumb_payload, sizeof(jumb_payload));

    infe_exif_size = 0U;
    append_fullbox_header(infe_exif, &infe_exif_size, 2U);
    append_u16be(infe_exif, &infe_exif_size, 1U);
    append_u16be(infe_exif, &infe_exif_size, 0U);
    append_u32be(infe_exif, &infe_exif_size, fourcc('E', 'x', 'i', 'f'));
    append_text(infe_exif, &infe_exif_size, "Exif");
    append_u8(infe_exif, &infe_exif_size, 0U);

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/rdf+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    infe_jumb_size = 0U;
    append_fullbox_header(infe_jumb, &infe_jumb_size, 2U);
    append_u16be(infe_jumb, &infe_jumb_size, 3U);
    append_u16be(infe_jumb, &infe_jumb_size, 0U);
    append_u32be(infe_jumb, &infe_jumb_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_jumb, &infe_jumb_size, "C2PA");
    append_u8(infe_jumb, &infe_jumb_size, 0U);
    append_text(infe_jumb, &infe_jumb_size, "application/jumbf");
    append_u8(infe_jumb, &infe_jumb_size, 0U);
    append_u8(infe_jumb, &infe_jumb_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 0U);
    append_u16be(iinf_payload, &iinf_size, 3U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_exif, infe_exif_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_jumb, infe_jumb_size);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 1U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x40U);
    append_u16be(iloc_payload, &iloc_size, 3U);

    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)exif_size);

    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)xmp_off);
    append_u32be(iloc_payload, &iloc_size, 4U);

    append_u16be(iloc_payload, &iloc_size, 3U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)jumb_off);
    append_u32be(iloc_payload, &iloc_size, (omc_u32)sizeof(jumb_payload));

    idat_box_size = 0U;
    append_bmff_box(idat_box, &idat_box_size, fourcc('i', 'd', 'a', 't'),
                    idat_payload, idat_size);

    colr_payload_size = 0U;
    append_u32be(colr_payload, &colr_payload_size, fourcc('p', 'r', 'o', 'f'));
    append_text(colr_payload, &colr_payload_size, "ICC");
    colr_box_size = 0U;
    append_bmff_box(colr_box, &colr_box_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_payload_size);

    ipco_size = 0U;
    append_bmff_box(ipco_payload, &ipco_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_payload_size);
    ipco_box_size = 0U;
    append_bmff_box(ipco_box, &ipco_box_size, fourcc('i', 'p', 'c', 'o'),
                    ipco_payload, ipco_size);

    iprp_size = 0U;
    append_bytes(iprp_payload, &iprp_size, ipco_box, ipco_box_size);
    iprp_box_size = 0U;
    append_bmff_box(iprp_box, &iprp_box_size, fourcc('i', 'p', 'r', 'p'),
                    iprp_payload, iprp_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bytes(meta_payload, &meta_size, idat_box, idat_box_size);
    append_bytes(meta_payload, &meta_size, iprp_box, iprp_box_size);

    moov_size = 0U;
    append_bmff_box(moov_box, &moov_size, fourcc('m', 'o', 'o', 'v'),
                    (const omc_u8*)0, 0U);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bytes(out, &size, moov_box, moov_size);
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static omc_size
make_test_cr3_scan(omc_u8* out)
{
    static const omc_u8 canon_uuid[16] = {
        0x85U, 0xC0U, 0xB6U, 0x87U, 0x82U, 0x0FU, 0x11U, 0xE0U,
        0x81U, 0x11U, 0xF4U, 0xCEU, 0x46U, 0x2BU, 0x6AU, 0x48U
    };
    omc_u8 tiff[16];
    omc_u8 exif_payload[32];
    omc_u8 cmt_box[64];
    omc_u8 uuid_payload[96];
    omc_u8 uuid_box[128];
    omc_u8 moov_payload[160];
    omc_u8 ftyp_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size cmt_size;
    omc_size uuid_payload_size;
    omc_size uuid_box_size;
    omc_size moov_size;
    omc_size ftyp_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    exif_size = 0U;
    append_text(exif_payload, &exif_size, "Exif");
    append_u8(exif_payload, &exif_size, 0U);
    append_u8(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    cmt_size = 0U;
    append_bmff_box(cmt_box, &cmt_size, fourcc('C', 'M', 'T', '1'),
                    exif_payload, exif_size);

    uuid_payload_size = 0U;
    append_bytes(uuid_payload, &uuid_payload_size, canon_uuid, 16U);
    append_bytes(uuid_payload, &uuid_payload_size, cmt_box, cmt_size);
    uuid_box_size = 0U;
    append_bmff_box(uuid_box, &uuid_box_size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_payload_size);

    moov_size = 0U;
    append_bmff_box(moov_payload, &moov_size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_payload_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, fourcc('c', 'r', 'x', ' '));
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('C', 'R', '3', ' '));

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'o', 'o', 'v'),
                    uuid_box, uuid_box_size);
    return size;
}

static omc_size
make_test_jp2_scan(omc_u8* out)
{
    static const omc_u8 xmp_uuid[16] = {
        0xBEU, 0x7AU, 0xCFU, 0xCBU, 0x97U, 0xA9U, 0x42U, 0xE8U,
        0x9CU, 0x71U, 0x99U, 0x94U, 0x91U, 0xE3U, 0xAFU, 0xACU
    };
    static const omc_u8 geo_uuid[16] = {
        0xB1U, 0x4BU, 0xF8U, 0xBDU, 0x08U, 0x3DU, 0x4BU, 0x43U,
        0xA5U, 0xAEU, 0x8CU, 0xD7U, 0xD5U, 0xA6U, 0xCEU, 0x03U
    };
    omc_u8 colr_payload[16];
    omc_u8 colr_box[24];
    omc_u8 uuid_payload[64];
    omc_u8 tiff[32];
    omc_size colr_payload_size;
    omc_size colr_box_size;
    omc_size uuid_size;
    omc_size tiff_size;
    omc_size size;

    colr_payload_size = 0U;
    append_u8(colr_payload, &colr_payload_size, 2U);
    append_u8(colr_payload, &colr_payload_size, 0U);
    append_u8(colr_payload, &colr_payload_size, 0U);
    append_text(colr_payload, &colr_payload_size, "ICC");

    colr_box_size = 0U;
    append_bmff_box(colr_box, &colr_box_size, fourcc('c', 'o', 'l', 'r'),
                    colr_payload, colr_payload_size);

    tiff_size = make_test_tiff_le(tiff);

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('j', 'P', ' ', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('j', 'p', '2', 'h'),
                    colr_box, colr_box_size);

    uuid_size = 0U;
    append_bytes(uuid_payload, &uuid_size, xmp_uuid, 16U);
    append_text(uuid_payload, &uuid_size, "<xmp/>");
    append_bmff_box(out, &size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_size);

    uuid_size = 0U;
    append_bytes(uuid_payload, &uuid_size, geo_uuid, 16U);
    append_bytes(uuid_payload, &uuid_size, tiff, tiff_size);
    append_bmff_box(out, &size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_size);
    return size;
}

static omc_size
make_test_jxl_scan(omc_u8* out)
{
    omc_u8 tiff[16];
    omc_u8 exif_payload[32];
    omc_u8 brob_payload[16];
    omc_size tiff_size;
    omc_size exif_size;
    omc_size brob_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    exif_size = 0U;
    append_u32be(exif_payload, &exif_size, 0U);
    append_bytes(exif_payload, &exif_size, tiff, tiff_size);

    brob_size = 0U;
    append_u32be(brob_payload, &brob_size, fourcc('x', 'm', 'l', ' '));
    append_text(brob_payload, &brob_size, "zzz");

    size = 0U;
    append_u32be(out, &size, 12U);
    append_u32be(out, &size, fourcc('J', 'X', 'L', ' '));
    append_u32be(out, &size, 0x0D0A870AU);
    append_bmff_box(out, &size, fourcc('E', 'x', 'i', 'f'),
                    exif_payload, exif_size);
    append_bmff_box(out, &size, fourcc('x', 'm', 'l', ' '),
                    (const omc_u8*)"<xmp/>", 6U);
    append_bmff_box(out, &size, fourcc('b', 'r', 'o', 'b'),
                    brob_payload, brob_size);
    return size;
}

static omc_size
make_test_bmff_jp2_brand_scan(omc_u8* out)
{
    static const omc_u8 geo_uuid[16] = {
        0xB1U, 0x4BU, 0xF8U, 0xBDU, 0x08U, 0x3DU, 0x4BU, 0x43U,
        0xA5U, 0xAEU, 0x8CU, 0xD7U, 0xD5U, 0xA6U, 0xCEU, 0x03U
    };
    omc_u8 ftyp_payload[16];
    omc_u8 uuid_payload[64];
    omc_u8 tiff[32];
    omc_size ftyp_size;
    omc_size uuid_size;
    omc_size tiff_size;
    omc_size size;

    tiff_size = make_test_tiff_le(tiff);
    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, fourcc('j', 'p', '2', ' '));
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('j', 'p', '2', ' '));

    uuid_size = 0U;
    append_bytes(uuid_payload, &uuid_size, geo_uuid, 16U);
    append_bytes(uuid_payload, &uuid_size, tiff, tiff_size);

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('u', 'u', 'i', 'd'),
                    uuid_payload, uuid_size);
    return size;
}

static omc_size
make_test_bmff_iref_scan(omc_u8* out, omc_u32 major_brand,
                         omc_u8 iref_version, int split_parts)
{
    static const char xmp_payload[] = "<xmp/>";
    omc_u8 infe_xmp[96];
    omc_u8 iinf_payload[160];
    omc_u8 iloc_payload[256];
    omc_u8 iref_iloc_payload[96];
    omc_u8 iref_payload[128];
    omc_u8 idat_box[64];
    omc_u8 meta_payload[768];
    omc_u8 ftyp_payload[16];
    omc_size infe_xmp_size;
    omc_size iinf_size;
    omc_size iloc_size;
    omc_size iref_iloc_size;
    omc_size iref_size;
    omc_size idat_box_size;
    omc_size meta_size;
    omc_size ftyp_size;
    omc_size size;

    infe_xmp_size = 0U;
    append_fullbox_header(infe_xmp, &infe_xmp_size, 2U);
    append_u16be(infe_xmp, &infe_xmp_size, 1U);
    append_u16be(infe_xmp, &infe_xmp_size, 0U);
    append_u32be(infe_xmp, &infe_xmp_size, fourcc('m', 'i', 'm', 'e'));
    append_text(infe_xmp, &infe_xmp_size, "XMP");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_text(infe_xmp, &infe_xmp_size, "application/xmp+xml");
    append_u8(infe_xmp, &infe_xmp_size, 0U);
    append_u8(infe_xmp, &infe_xmp_size, 0U);

    iinf_size = 0U;
    append_fullbox_header(iinf_payload, &iinf_size, 2U);
    append_u32be(iinf_payload, &iinf_size, 1U);
    append_bmff_box(iinf_payload, &iinf_size, fourcc('i', 'n', 'f', 'e'),
                    infe_xmp, infe_xmp_size);

    idat_box_size = 0U;
    append_bmff_box(idat_box, &idat_box_size, fourcc('i', 'd', 'a', 't'),
                    (const omc_u8*)xmp_payload, sizeof(xmp_payload) - 1U);

    iloc_size = 0U;
    append_fullbox_header(iloc_payload, &iloc_size, 2U);
    append_u8(iloc_payload, &iloc_size, 0x44U);
    append_u8(iloc_payload, &iloc_size, 0x00U);
    append_u32be(iloc_payload, &iloc_size, split_parts ? 3U : 2U);

    append_u32be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, 1U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size,
                 split_parts ? 3U : (omc_u32)(sizeof(xmp_payload) - 1U));

    if (split_parts) {
        append_u32be(iloc_payload, &iloc_size, 3U);
        append_u16be(iloc_payload, &iloc_size, 1U);
        append_u16be(iloc_payload, &iloc_size, 0U);
        append_u16be(iloc_payload, &iloc_size, 1U);
        append_u32be(iloc_payload, &iloc_size, 3U);
        append_u32be(iloc_payload, &iloc_size, 3U);
    }

    append_u32be(iloc_payload, &iloc_size, 1U);
    append_u16be(iloc_payload, &iloc_size, 2U);
    append_u16be(iloc_payload, &iloc_size, 0U);
    append_u16be(iloc_payload, &iloc_size, split_parts ? 2U : 1U);
    append_u32be(iloc_payload, &iloc_size, 0U);
    append_u32be(iloc_payload, &iloc_size,
                 split_parts ? 3U : (omc_u32)(sizeof(xmp_payload) - 1U));
    if (split_parts) {
        append_u32be(iloc_payload, &iloc_size, 0U);
        append_u32be(iloc_payload, &iloc_size, 3U);
    }

    iref_iloc_size = 0U;
    if (iref_version == 0U) {
        append_u16be(iref_iloc_payload, &iref_iloc_size, 1U);
        append_u16be(iref_iloc_payload, &iref_iloc_size,
                     split_parts ? 2U : 1U);
        append_u16be(iref_iloc_payload, &iref_iloc_size, 2U);
        if (split_parts) {
            append_u16be(iref_iloc_payload, &iref_iloc_size, 3U);
        }
    } else {
        append_u32be(iref_iloc_payload, &iref_iloc_size, 1U);
        append_u16be(iref_iloc_payload, &iref_iloc_size,
                     split_parts ? 2U : 1U);
        append_u32be(iref_iloc_payload, &iref_iloc_size, 2U);
        if (split_parts) {
            append_u32be(iref_iloc_payload, &iref_iloc_size, 3U);
        }
    }

    iref_size = 0U;
    append_fullbox_header(iref_payload, &iref_size, iref_version);
    append_bmff_box(iref_payload, &iref_size, fourcc('i', 'l', 'o', 'c'),
                    iref_iloc_payload, iref_iloc_size);

    meta_size = 0U;
    append_fullbox_header(meta_payload, &meta_size, 0U);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'i', 'n', 'f'),
                    iinf_payload, iinf_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'l', 'o', 'c'),
                    iloc_payload, iloc_size);
    append_bmff_box(meta_payload, &meta_size, fourcc('i', 'r', 'e', 'f'),
                    iref_payload, iref_size);
    append_bytes(meta_payload, &meta_size, idat_box, idat_box_size);

    ftyp_size = 0U;
    append_u32be(ftyp_payload, &ftyp_size, major_brand);
    append_u32be(ftyp_payload, &ftyp_size, 0U);
    append_u32be(ftyp_payload, &ftyp_size, fourcc('m', 'i', 'f', '1'));

    size = 0U;
    append_bmff_box(out, &size, fourcc('f', 't', 'y', 'p'),
                    ftyp_payload, ftyp_size);
    append_bmff_box(out, &size, fourcc('m', 'e', 't', 'a'),
                    meta_payload, meta_size);
    return size;
}

static void
test_scan_jpeg(void)
{
    static const omc_u8 jpeg_bytes[] = {
        0xFFU, 0xD8U,
        0xFFU, 0xE1U, 0x00U, 0x10U,
        'E', 'x', 'i', 'f', 0x00U, 0x00U, 'I', 'I', 0x2AU, 0x00U,
        0x08U, 0x00U, 0x00U, 0x00U,
        0xFFU, 0xE1U, 0x00U, 0x23U,
        'h', 't', 't', 'p', ':', '/', '/', 'n', 's', '.', 'a', 'd', 'o', 'b',
        'e', '.', 'c', 'o', 'm', '/', 'x', 'a', 'p', '/', '1', '.', '0', '/',
        0x00U,
        '<', 'x', '/', '>',
        0xFFU, 0xE2U, 0x00U, 0x13U,
        'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0x00U,
        0x01U, 0x02U,
        'A', 'B', 'C',
        0xFFU, 0xEDU, 0x00U, 0x14U,
        'P', 'h', 'o', 't', 'o', 's', 'h', 'o', 'p', ' ', '3', '.', '0', 0x00U,
        '8', 'B', 'I', 'M',
        0xFFU, 0xFEU, 0x00U, 0x04U, 'O', 'K',
        0xFFU, 0xD9U
    };
    omc_blk_ref blocks[5];
    omc_scan_res res;

    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(jpeg_bytes, sizeof(jpeg_bytes), blocks, 5U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 5U);
    assert(res.needed == 5U);

    assert(blocks[0].format == OMC_SCAN_FMT_JPEG);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[0].data_offset == 12U);
    assert(blocks[0].data_size == 8U);
    assert(blocks[0].id == 0xFFE1U);

    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[1].data_size == 4U);
    assert(memcmp(jpeg_bytes + (omc_size)blocks[1].data_offset, "<x/>", 4U)
           == 0);

    assert(blocks[2].kind == OMC_BLK_ICC);
    assert(blocks[2].chunking == OMC_BLK_CHUNK_JPEG_APP2_SEQ);
    assert(blocks[2].part_index == 0U);
    assert(blocks[2].part_count == 2U);
    assert(blocks[2].data_size == 3U);

    assert(blocks[3].kind == OMC_BLK_PS_IRB);
    assert(blocks[3].chunking == OMC_BLK_CHUNK_PS_IRB_8BIM);
    assert(blocks[3].data_size == 4U);

    assert(blocks[4].kind == OMC_BLK_COMMENT);
    assert(blocks[4].data_size == 2U);
}

static void
test_scan_png(void)
{
    omc_u8 png_bytes[256];
    omc_size png_size;
    omc_blk_ref blocks[4];
    omc_scan_res res;

    png_size = make_test_png_scan(png_bytes);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(png_bytes, png_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 3U);
    assert(res.needed == 3U);

    assert(blocks[0].format == OMC_SCAN_FMT_PNG);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[0].data_size == 12U);

    assert(blocks[1].format == OMC_SCAN_FMT_PNG);
    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[1].compression == OMC_BLK_COMP_NONE);
    assert(blocks[1].data_size == 4U);
    assert(memcmp(png_bytes + (omc_size)blocks[1].data_offset, "<x/>", 4U)
           == 0);

    assert(blocks[2].format == OMC_SCAN_FMT_PNG);
    assert(blocks[2].kind == OMC_BLK_ICC);
    assert(blocks[2].compression == OMC_BLK_COMP_DEFLATE);
    assert(blocks[2].data_size == 3U);
}

static void
test_scan_webp(void)
{
    omc_u8 webp_bytes[256];
    omc_size webp_size;
    omc_blk_ref blocks[4];
    omc_scan_res res;

    webp_size = make_test_webp_scan(webp_bytes);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(webp_bytes, webp_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 3U);
    assert(res.needed == 3U);

    assert(blocks[0].format == OMC_SCAN_FMT_WEBP);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[0].data_size == 12U);

    assert(blocks[1].format == OMC_SCAN_FMT_WEBP);
    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[1].data_size == 4U);
    assert(memcmp(webp_bytes + (omc_size)blocks[1].data_offset, "<x/>", 4U)
           == 0);

    assert(blocks[2].format == OMC_SCAN_FMT_WEBP);
    assert(blocks[2].kind == OMC_BLK_ICC);
    assert(blocks[2].data_size == 3U);
}

static void
test_scan_tiff(void)
{
    static const omc_u8 tiff_bytes[] = {
        'I', 'I', 0x2AU, 0x00U,
        0x08U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    omc_blk_ref block;
    omc_scan_res res;

    memset(&block, 0, sizeof(block));
    res = omc_scan_tiff(tiff_bytes, sizeof(tiff_bytes), &block, 1U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 1U);
    assert(res.needed == 1U);
    assert(block.format == OMC_SCAN_FMT_TIFF);
    assert(block.kind == OMC_BLK_EXIF);
    assert(block.outer_size == sizeof(tiff_bytes));
    assert(block.data_size == sizeof(tiff_bytes));
}

static void
test_scan_bmff_heif_and_avif(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_blk_ref blocks[8];
    omc_scan_res res;

    file_size = make_test_bmff_meta_scan(file_bytes, fourcc('h', 'e', 'i', 'c'));
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 8U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 4U);
    assert(blocks[0].format == OMC_SCAN_FMT_HEIF);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[2].kind == OMC_BLK_JUMBF);
    assert(blocks[3].kind == OMC_BLK_ICC);

    file_size = make_test_bmff_meta_scan(file_bytes, fourcc('a', 'v', 'i', 'f'));
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 8U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 4U);
    assert(blocks[0].format == OMC_SCAN_FMT_AVIF);
    assert(blocks[0].kind == OMC_BLK_EXIF);
}

static void
test_scan_bmff_cr3(void)
{
    omc_u8 file_bytes[512];
    omc_size file_size;
    omc_blk_ref blocks[4];
    omc_scan_res res;

    file_size = make_test_cr3_scan(file_bytes);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 1U);
    assert(blocks[0].format == OMC_SCAN_FMT_CR3);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[0].id == fourcc('C', 'M', 'T', '1'));
}

static void
test_scan_bmff_iref_v1_and_v0(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_blk_ref blocks[4];
    omc_scan_res res;

    file_size = make_test_bmff_iref_scan(file_bytes, fourcc('h', 'e', 'i', 'c'),
                                         1U, 0);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 1U);
    assert(blocks[0].format == OMC_SCAN_FMT_HEIF);
    assert(blocks[0].kind == OMC_BLK_XMP);
    assert(blocks[0].group == 1U);
    assert(blocks[0].data_size == 6U);
    assert(memcmp(file_bytes + (omc_size)blocks[0].data_offset, "<xmp/>", 6U)
           == 0);

    file_size = make_test_bmff_iref_scan(file_bytes, fourcc('a', 'v', 'i', 'f'),
                                         0U, 0);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 1U);
    assert(blocks[0].format == OMC_SCAN_FMT_AVIF);
    assert(blocks[0].kind == OMC_BLK_XMP);
    assert(blocks[0].group == 1U);
    assert(blocks[0].data_size == 6U);
}

static void
test_scan_bmff_iref_reference_order(void)
{
    omc_u8 file_bytes[1024];
    omc_size file_size;
    omc_blk_ref blocks[4];
    omc_scan_res res;

    file_size = make_test_bmff_iref_scan(file_bytes, fourcc('h', 'e', 'i', 'c'),
                                         1U, 1);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 2U);
    assert(blocks[0].kind == OMC_BLK_XMP);
    assert(blocks[0].group == 1U);
    assert(blocks[0].part_index == 0U);
    assert(blocks[0].part_count == 2U);
    assert(blocks[0].logical_offset == 0U);
    assert(blocks[0].data_size == 3U);
    assert(memcmp(file_bytes + (omc_size)blocks[0].data_offset, "<xm", 3U)
           == 0);

    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[1].group == 1U);
    assert(blocks[1].part_index == 1U);
    assert(blocks[1].part_count == 2U);
    assert(blocks[1].logical_offset == 3U);
    assert(blocks[1].data_size == 3U);
    assert(memcmp(file_bytes + (omc_size)blocks[1].data_offset, "p/>", 3U)
           == 0);
}

static void
test_scan_jp2_and_jxl(void)
{
    omc_u8 file_bytes[512];
    omc_size file_size;
    omc_blk_ref blocks[8];
    omc_scan_res res;

    file_size = make_test_jp2_scan(file_bytes);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 8U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 3U);
    assert(blocks[0].format == OMC_SCAN_FMT_JP2);
    assert(blocks[0].kind == OMC_BLK_ICC);
    assert(blocks[1].format == OMC_SCAN_FMT_JP2);
    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[1].chunking == OMC_BLK_CHUNK_JP2_UUID);
    assert(blocks[2].format == OMC_SCAN_FMT_JP2);
    assert(blocks[2].kind == OMC_BLK_EXIF);
    assert(blocks[2].chunking == OMC_BLK_CHUNK_JP2_UUID);

    file_size = make_test_jxl_scan(file_bytes);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 8U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 3U);
    assert(blocks[0].format == OMC_SCAN_FMT_JXL);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[1].format == OMC_SCAN_FMT_JXL);
    assert(blocks[1].kind == OMC_BLK_XMP);
    assert(blocks[2].format == OMC_SCAN_FMT_JXL);
    assert(blocks[2].kind == OMC_BLK_COMP_METADATA);
    assert(blocks[2].compression == OMC_BLK_COMP_BROTLI);
    assert(blocks[2].chunking == OMC_BLK_CHUNK_BROB_REALTYPE);
    assert(blocks[2].aux_u32 == fourcc('x', 'm', 'l', ' '));
}

static void
test_scan_bmff_jp2_brand(void)
{
    omc_u8 file_bytes[256];
    omc_size file_size;
    omc_blk_ref blocks[4];
    omc_scan_res res;

    file_size = make_test_bmff_jp2_brand_scan(file_bytes);
    memset(blocks, 0, sizeof(blocks));
    res = omc_scan_auto(file_bytes, file_size, blocks, 4U);

    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 1U);
    assert(blocks[0].format == OMC_SCAN_FMT_JP2);
    assert(blocks[0].kind == OMC_BLK_EXIF);
    assert(blocks[0].chunking == OMC_BLK_CHUNK_JP2_UUID);
}

static void
test_scan_measure_and_truncation(void)
{
    static const omc_u8 tiff_bytes[] = {
        'M', 'M', 0x00U, 0x2AU,
        0x00U, 0x00U, 0x00U, 0x08U,
        0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    omc_scan_res res;

    res = omc_scan_tiff(tiff_bytes, sizeof(tiff_bytes), (omc_blk_ref*)0, 0U);
    assert(res.status == OMC_SCAN_TRUNCATED);
    assert(res.written == 0U);
    assert(res.needed == 1U);

    res = omc_scan_meas_tiff(tiff_bytes, sizeof(tiff_bytes));
    assert(res.status == OMC_SCAN_OK);
    assert(res.written == 0U);
    assert(res.needed == 1U);
}

int
main(void)
{
    test_scan_jpeg();
    test_scan_png();
    test_scan_webp();
    test_scan_tiff();
    test_scan_bmff_heif_and_avif();
    test_scan_bmff_cr3();
    test_scan_bmff_iref_v1_and_v0();
    test_scan_bmff_iref_reference_order();
    test_scan_jp2_and_jxl();
    test_scan_bmff_jp2_brand();
    test_scan_measure_and_truncation();
    return 0;
}
