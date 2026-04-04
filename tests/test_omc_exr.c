#include "omc/omc_exr.h"
#include "omc/omc_read.h"

#include <assert.h>
#include <string.h>

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
}

static void
append_u32le(omc_u8* out, omc_size* io_size, omc_u32 value)
{
    append_u8(out, io_size, (omc_u8)(value & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
}

static void
append_raw(omc_u8* out, omc_size* io_size, const void* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    append_raw(out, io_size, text, strlen(text));
}

static void
append_cstr(omc_u8* out, omc_size* io_size, const char* text)
{
    append_text(out, io_size, text);
    append_u8(out, io_size, 0U);
}

static void
append_attr_raw(omc_u8* out, omc_size* io_size, const char* name,
                const char* type, const omc_u8* value, omc_size value_size)
{
    append_cstr(out, io_size, name);
    append_cstr(out, io_size, type);
    append_u32le(out, io_size, (omc_u32)value_size);
    append_raw(out, io_size, value, value_size);
}

static void
append_attr_text(omc_u8* out, omc_size* io_size, const char* name,
                 const char* type, const char* value)
{
    append_attr_raw(out, io_size, name, type, (const omc_u8*)value,
                    (omc_size)strlen(value));
}

static omc_size
build_exr_single_part(omc_u8* out)
{
    omc_u8 float_payload[4];
    omc_u32 one_bits;
    omc_size size;

    one_bits = 0x3F800000U;
    float_payload[0] = 0x00U;
    float_payload[1] = 0x00U;
    float_payload[2] = 0x80U;
    float_payload[3] = 0x3FU;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);
    append_attr_text(out, &size, "owner", "string", "Vlad");
    append_attr_raw(out, &size, "pixelAspectRatio", "float", float_payload,
                    sizeof(float_payload));
    append_u8(out, &size, 0U);
    (void)one_bits;
    return size;
}

static omc_size
build_exr_multipart_two_names(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U | 0x00001000U);
    append_attr_text(out, &size, "name", "string", "left");
    append_u8(out, &size, 0U);
    append_attr_text(out, &size, "name", "string", "right");
    append_u8(out, &size, 0U);
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
build_exr_unknown_type(omc_u8* out)
{
    static const omc_u8 payload[] = { 1U, 2U, 3U, 4U, 5U };
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);
    append_attr_raw(out, &size, "customA", "myVendorFoo", payload,
                    sizeof(payload));
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
build_exr_tiledesc(omc_u8* out)
{
    static const omc_u8 payload[] = {
        0x40U, 0x00U, 0x00U, 0x00U,
        0x40U, 0x00U, 0x00U, 0x00U,
        0x01U
    };
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);
    append_attr_raw(out, &size, "tiles", "tiledesc", payload,
                    sizeof(payload));
    append_u8(out, &size, 0U);
    return size;
}

static const omc_entry*
find_exr_entry(const omc_store* store, omc_u32 part_index, const char* name)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes name_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_EXR_ATTR
            || entry->key.u.exr_attr.part_index != part_index) {
            continue;
        }
        name_view = omc_arena_view(&store->arena, entry->key.u.exr_attr.name);
        if (name_view.size == strlen(name)
            && memcmp(name_view.data, name, name_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static void
assert_text_value(const omc_store* store, const omc_entry* entry,
                  const char* expect)
{
    omc_const_bytes value;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    value = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(value.size == strlen(expect));
    assert(memcmp(value.data, expect, value.size) == 0);
}

static void
test_exr_decodes_single_part_header_attributes(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* owner;

    exr_size = build_exr_single_part(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.parts_decoded == 1U);
    assert(res.entries_decoded == 2U);
    assert(store.block_count == 1U);
    assert(store.entry_count == 2U);

    owner = find_exr_entry(&store, 0U, "owner");
    assert_text_value(&store, owner, "Vlad");
    assert(owner->origin.wire_type.family == OMC_WIRE_OTHER);
    assert(owner->origin.wire_type.code == 20U);
    assert(owner->origin.wire_count == 4U);

    omc_store_fini(&store);
}

static void
test_exr_decodes_multipart_headers(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;

    exr_size = build_exr_multipart_two_names(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.parts_decoded == 2U);
    assert(res.entries_decoded == 2U);
    assert(store.block_count == 2U);
    assert(find_exr_entry(&store, 0U, "name") != (const omc_entry*)0);
    assert(find_exr_entry(&store, 1U, "name") != (const omc_entry*)0);
    omc_store_fini(&store);
}

static void
test_exr_reports_limit_exceeded_for_max_attributes(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_opts opts;
    omc_exr_res res;

    exr_size = build_exr_single_part(exr);
    omc_store_init(&store);
    omc_exr_opts_init(&opts);
    opts.limits.max_attributes = 1U;
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE, &opts);
    assert(res.status == OMC_EXR_LIMIT);
    assert(res.entries_decoded == 1U);
    omc_store_fini(&store);
}

static void
test_exr_estimate_matches_decode_counters(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_res meas;
    omc_exr_res dec;

    exr_size = build_exr_single_part(exr);
    omc_store_init(&store);
    meas = omc_exr_meas(exr, exr_size, (const omc_exr_opts*)0);
    dec = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(meas.status == dec.status);
    assert(meas.parts_decoded == dec.parts_decoded);
    assert(meas.entries_decoded == dec.entries_decoded);
    omc_store_fini(&store);
}

static void
test_exr_preserves_unknown_type_name_by_default(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* entry;
    omc_const_bytes type_name;

    exr_size = build_exr_unknown_type(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.entries_decoded == 1U);
    entry = find_exr_entry(&store, 0U, "customA");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 31U);
    type_name = omc_arena_view(&store.arena, entry->origin.wire_type_name);
    assert(type_name.size == strlen("myVendorFoo"));
    assert(memcmp(type_name.data, "myVendorFoo", type_name.size) == 0);
    omc_store_fini(&store);
}

static void
test_exr_can_disable_unknown_type_name_preservation(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_opts opts;
    omc_exr_res res;
    const omc_entry* entry;

    exr_size = build_exr_unknown_type(exr);
    omc_store_init(&store);
    omc_exr_opts_init(&opts);
    opts.preserve_unknown_type_name = 0;
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE, &opts);
    assert(res.status == OMC_EXR_OK);
    entry = find_exr_entry(&store, 0U, "customA");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type_name.size == 0U);
    omc_store_fini(&store);
}

static void
test_exr_decodes_tiledesc_as_u32_array(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* entry;
    omc_const_bytes view;
    omc_u32 values[3];

    exr_size = build_exr_tiledesc(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    entry = find_exr_entry(&store, 0U, "tiles");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 22U);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.count == 3U);
    view = omc_arena_view(&store.arena, entry->value.u.ref);
    assert(view.size == sizeof(values));
    memcpy(values, view.data, sizeof(values));
    assert(values[0] == 64U);
    assert(values[1] == 64U);
    assert(values[2] == 1U);
    omc_store_fini(&store);
}

static void
test_read_simple_decodes_exr_header_fallback(void)
{
    omc_u8 exr[256];
    omc_size exr_size;
    omc_store store;
    omc_blk_ref blocks[16];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[2048];
    omc_u32 scratch[64];
    omc_read_res read_res;

    exr_size = build_exr_single_part(exr);
    omc_store_init(&store);
    read_res = omc_read_simple(exr, exr_size, &store, blocks, 16U, ifds, 16U,
                               payload, sizeof(payload), scratch, 64U,
                               (const omc_read_opts*)0);
    assert(read_res.exr.status == OMC_EXR_OK);
    assert(read_res.exr.parts_decoded == 1U);
    assert(read_res.exr.entries_decoded == 2U);
    assert(read_res.exif.status == OMC_EXIF_OK);
    assert(read_res.xmp.status == OMC_XMP_OK);
    assert(store.entry_count == 2U);
    omc_store_fini(&store);
}

int
main(void)
{
    test_exr_decodes_single_part_header_attributes();
    test_exr_decodes_multipart_headers();
    test_exr_reports_limit_exceeded_for_max_attributes();
    test_exr_estimate_matches_decode_counters();
    test_exr_preserves_unknown_type_name_by_default();
    test_exr_can_disable_unknown_type_name_preservation();
    test_exr_decodes_tiledesc_as_u32_array();
    test_read_simple_decodes_exr_header_fallback();
    return 0;
}
