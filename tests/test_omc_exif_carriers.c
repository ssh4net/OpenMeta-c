#include "edit/omc_exif_write.h"
#include "omc/omc_read.h"
#include "omc_test_assert.h"
#include <string.h>

static void
put(omc_u8 *p, omc_u64 value, unsigned width)
{
    unsigned i;
    for (i = 0U; i < width; ++i)
        p[i] = (omc_u8)(value >> (8U * i));
}
static void
put_order(omc_u8 *p, omc_u64 value, unsigned width, int little)
{
    unsigned i;
    for (i = 0U; i < width; ++i)
        p[little ? i : width - 1U - i] = (omc_u8)(value >> (8U * i));
}

static void
test_tiff_noop(int big, int little)
{
    omc_u8 file[64];
    omc_size root, cs, ns;
    omc_store empty;
    omc_arena out;
    omc_exif_write_res res;
    unsigned pass;
    root = big ? 16U : 8U;
    cs = big ? 8U : 2U;
    ns = big ? 8U : 4U;
    memset(file, 0, sizeof(file));
    file[0] = file[1] = (omc_u8)(little ? 'I' : 'M');
    put_order(file + 2U, big ? 43U : 42U, 2U, little);
    if (big) put_order(file + 4U, 8U, 2U, little);
    put_order(file + (big ? 8U : 4U), root, big ? 8U : 4U, little);
    memcpy(file + 40U, "PIXELS!!", 8U);
    omc_store_init(&empty);
    omc_arena_init(&out);
    for (pass = 0U; pass < 2U; ++pass) {
        assert(omc_exif_write_embedded(file, sizeof(file), &empty, &out,
                  pass ? OMC_SCAN_FMT_UNKNOWN : OMC_SCAN_FMT_TIFF, &res) == OMC_STATUS_OK);
        assert(res.status == OMC_EXIF_WRITE_OK);
        assert(out.size == sizeof(file) && memcmp(out.data, file, sizeof(file)) == 0);
    }
    assert(omc_exif_write_embedded(file, root + cs + ns - 1U, &empty, &out,
               OMC_SCAN_FMT_TIFF, &res) == OMC_STATUS_OK);
    assert(res.status == OMC_EXIF_WRITE_MALFORMED && out.size == 0U);
    put_order(file + root, big ? ~(omc_u64)0 : 65535U, (unsigned)cs, little);
    assert(omc_exif_write_embedded(file, sizeof(file), &empty, &out,
               OMC_SCAN_FMT_TIFF, &res) == OMC_STATUS_OK);
    assert(res.status == OMC_EXIF_WRITE_MALFORMED && out.size == 0U);
    if (big) {
        put_order(file + root, (~(omc_u64)0 / 20U) + 1U, 8U, little);
        assert(omc_exif_write_embedded(file, sizeof(file), &empty, &out,
                   OMC_SCAN_FMT_TIFF, &res) == OMC_STATUS_OK);
        assert(res.status == OMC_EXIF_WRITE_MALFORMED && out.size == 0U);
    }
    assert(res.format == OMC_SCAN_FMT_TIFF);
    omc_arena_fini(&out);
    omc_store_fini(&empty);
}

static void
test_tiff_count_bounds(int big)
{
    omc_u8 file[128];
    omc_size root, cs, es, ns, child;
    omc_store source;
    omc_arena out;
    omc_arena canonical;
    omc_exif_write_res res;
    unsigned dir, pass;
    root = big ? 16U : 8U;
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    ns = big ? 8U : 4U;
    child = 64U;
    omc_store_init(&source);
    omc_arena_init(&out);
    omc_arena_init(&canonical);
    for (dir = 0U; dir < 3U; ++dir) {
        for (pass = 0U; pass < (big ? 3U : 2U); ++pass) {
            omc_size at, size;
            omc_u64 count;
            memset(file, 0, sizeof(file));
            file[0] = file[1] = 'I';
            put(file + 2U, big ? 43U : 42U, 2U);
            if (big) put(file + 4U, 8U, 2U);
            put(file + (big ? 8U : 4U), root, big ? 8U : 4U);
            if (dir != 0U) {
                put(file + root, 1U, (unsigned)cs);
                put(file + root + cs, dir == 1U ? 0x8769U : 0x8825U, 2U);
                put(file + root + cs + 2U, 4U, 2U);
                put(file + root + cs + 4U, 1U, big ? 8U : 4U);
                put(file + root + cs + (big ? 12U : 8U), child, 4U);
            }
            at = dir == 0U ? root : child;
            count = pass == 0U ? (big ? ~(omc_u64)0 : 65535U)
                     : pass == 1U ? 1U : (~(omc_u64)0 / 20U) + 1U;
            put(file + at, count, (unsigned)cs);
            size = pass == 1U ? at + cs + es + ns - 1U : sizeof(file);
            /* Direct overlay exposes the count guard before any output append;
             * an empty canonical store selects no replacement directories. */
            assert(omc_exif_overlay_tiff(file, size, &source, &canonical,
                                         &out, &res) == OMC_STATUS_OK);
            assert(res.status == OMC_EXIF_WRITE_MALFORMED && out.size == 0U);
        }
    }
    file[0] = file[1] = 'M';
    assert(omc_exif_overlay_tiff(file, sizeof(file), &source, &canonical,
                                 &out, &res) == OMC_STATUS_OK);
    assert(res.status == OMC_EXIF_WRITE_UNSUPPORTED && out.size == 0U);
    omc_arena_fini(&canonical);
    omc_arena_fini(&out);
    omc_store_fini(&source);
}
static void
raw_entry(omc_u8 *p, int big, omc_u16 tag, omc_u16 type, omc_u32 count, omc_u64 value)
{
    put(p, tag, 2U);
    put(p + 2U, type, 2U);
    put(p + 4U, count, big ? 8U : 4U);
    put(p + (big ? 12U : 8U), value, big ? 8U : 4U);
}
static void
add_text(omc_store *s, const char *ifd, omc_u16 tag, const char *value)
{
    omc_entry e;
    omc_byte_ref r;
    memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&s->arena, ifd, strlen(ifd), &r) == OMC_STATUS_OK);
    omc_key_make_exif_tag(&e.key, r, tag);
    assert(omc_arena_append(&s->arena, value, strlen(value), &r) == OMC_STATUS_OK);
    omc_val_make_text(&e.value, r, OMC_TEXT_ASCII);
    e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}
static const omc_entry *
find(const omc_store *s, omc_u16 tag)
{
    omc_size i;
    for (i = 0U; i < s->entry_count; ++i)
        if (s->entries[i].key.kind == OMC_KEY_EXIF_TAG &&
            s->entries[i].key.u.exif_tag.tag == tag)
            return &s->entries[i];
    return NULL;
}
static void
decode(const omc_arena *file, omc_store *store)
{
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[32];
    omc_u8 payload[4096];
    omc_u32 indices[32];
    omc_read_res r;
    r = omc_read_simple(file->data, file->size, store, blocks, 32U, ifds, 32U, payload,
                        sizeof(payload), indices, 32U, NULL);
    assert(r.scan.status == OMC_SCAN_OK);
    assert(r.exif.status == OMC_EXIF_OK);
}
static void
test_tiff(int big)
{
    omc_u8 file[160];
    omc_size cs;
    omc_size es;
    omc_size root;
    omc_size child;
    omc_store source;
    omc_store decoded;
    omc_arena out;
    omc_exif_write_res result;
    const omc_entry *e;
    memset(file, 0, sizeof(file));
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    root = big ? 16U : 8U;
    child = big ? 96U : 50U;
    file[0] = 'I';
    file[1] = 'I';
    put(file + 2U, big ? 43U : 42U, 2U);
    if (big)
        put(file + 4U, 8U, 2U);
    put(file + (big ? 8U : 4U), root, big ? 8U : 4U);
    put(file + root, 3U, (unsigned)cs);
    raw_entry(file + root + cs, big, 0x111U, 4U, 1U, 144U);
    raw_entry(file + root + cs + es, big, 0x8769U, 4U, 1U, child);
    raw_entry(file + root + cs + 2U * es, big, 0xF001U, 7U, 4U, 0xDEADBEEFU);
    put(file + child, 1U, (unsigned)cs);
    raw_entry(file + child + cs, big, 0xF002U, 7U, 4U, 0x7065656BU);
    memset(file + 144U, 0xA5, 16U);
    omc_store_init(&source);
    omc_store_init(&decoded);
    omc_arena_init(&out);
    add_text(&source, "ifd0", 0x131U, "Canonical C");
    add_text(&source, "exififd", 0x9011U, "-00:00");
    add_text(&source, "exififd", 0x9291U, "123456789");
    assert(omc_exif_write_embedded(file, sizeof(file), &source, &out, OMC_SCAN_FMT_TIFF,
                                   &result) == OMC_STATUS_OK);
    assert(result.status == OMC_EXIF_WRITE_OK);
    assert(memcmp(out.data + root, file + root, sizeof(file) - root) == 0);
    decode(&out, &decoded);
    assert(find(&decoded, 0x131U) != NULL && find(&decoded, 0x9011U) != NULL &&
           find(&decoded, 0x9291U) != NULL);
    assert(find(&decoded, 0xF001U) != NULL && find(&decoded, 0xF002U) != NULL);
    e = find(&decoded, 0x111U);
    assert(e != NULL && e->value.u.u64 == 144U);
    omc_store_fini(&decoded);
    omc_store_fini(&source);
    omc_arena_fini(&out);
}
static void
test_jpeg(void)
{
    static const omc_u8 empty[] = {0xFFU, 0xD8U, 0xFFU, 0xD9U};
    omc_store first;
    omc_store second;
    omc_store decoded;
    omc_arena old;
    omc_arena out;
    omc_exif_write_res result;
    omc_store_init(&first);
    omc_store_init(&second);
    omc_store_init(&decoded);
    omc_arena_init(&old);
    omc_arena_init(&out);
    add_text(&first, "ifd0", 0x10FU, "Retained Make");
    assert(omc_exif_write_embedded(empty, sizeof(empty), &first, &old,
                                   OMC_SCAN_FMT_JPEG, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_EXIF_WRITE_OK);
    add_text(&second, "ifd0", 0x131U, "New Software");
    add_text(&second, "exififd", 0x9011U, "+09:00");
    assert(omc_exif_write_embedded(old.data, old.size, &second, &out, OMC_SCAN_FMT_JPEG,
                                   &result) == OMC_STATUS_OK);
    assert(result.status == OMC_EXIF_WRITE_OK);
    decode(&out, &decoded);
    assert(find(&decoded, 0x10FU) != NULL && find(&decoded, 0x131U) != NULL &&
           find(&decoded, 0x9011U) != NULL);
    omc_store_fini(&decoded);
    omc_store_fini(&second);
    omc_store_fini(&first);
    omc_arena_fini(&out);
    omc_arena_fini(&old);
}
static void
test_png_crc(void)
{
    static const omc_u8 png[] = {
        137U, 80U, 78U, 71U, 13U, 10U,   26U,   10U,   0U,    0U, 0U, 13U,
        'I',  'H', 'D', 'R', 0U,  0U,    0U,    1U,    0U,    0U, 0U, 1U,
        8U,   2U,  0U,  0U,  0U,  0x90U, 0x77U, 0x53U, 0xDEU, 0U, 0U, 0U,
        0U,   'I', 'E', 'N', 'D', 0xAEU, 0x42U, 0x60U, 0x82U};
    omc_store store;
    omc_arena out;
    omc_exif_write_res result;
    omc_size offset;
    omc_size size;
    omc_size i;
    unsigned bit;
    omc_u32 crc;
    omc_u32 wire;
    int found;
    omc_store_init(&store);
    omc_arena_init(&out);
    add_text(&store, "ifd0", 0x131U, "CRC check");
    assert(omc_exif_write_embedded(png, sizeof(png), &store, &out, OMC_SCAN_FMT_PNG,
                                   &result) == OMC_STATUS_OK);
    assert(result.status == OMC_EXIF_WRITE_OK);
    found = 0;
    for (offset = 8U; offset + 12U <= out.size; offset += size + 12U) {
        size = ((omc_size)out.data[offset] << 24U) |
               ((omc_size)out.data[offset + 1U] << 16U) |
               ((omc_size)out.data[offset + 2U] << 8U) | out.data[offset + 3U];
        assert(size <= out.size - offset - 12U);
        if (memcmp(out.data + offset + 4U, "eXIf", 4U) != 0)
            continue;
        found = 1;
        crc = 0xFFFFFFFFU;
        for (i = offset + 4U; i < offset + 8U + size; ++i) {
            crc ^= out.data[i];
            for (bit = 0U; bit < 8U; ++bit)
                crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
        i = offset + 8U + size;
        wire = ((omc_u32)out.data[i] << 24U) | ((omc_u32)out.data[i + 1U] << 16U) |
               ((omc_u32)out.data[i + 2U] << 8U) | out.data[i + 3U];
        assert(wire == ~crc);
    }
    assert(found);
    omc_arena_fini(&out);
    omc_store_fini(&store);
}

int
main(void)
{
    test_tiff(0);
    test_tiff(1);
    test_tiff_noop(0, 0);
    test_tiff_noop(0, 1);
    test_tiff_noop(1, 0);
    test_tiff_noop(1, 1);
    test_tiff_count_bounds(0);
    test_tiff_count_bounds(1);
    test_jpeg();
    test_png_crc();
    return 0;
}
