#include "omc/omc_exr.h"
#include "omc/omc_read.h"

#include "omc_test_assert.h"
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
append_i32le(omc_u8* out, omc_size* io_size, omc_s32 value)
{
    append_u32le(out, io_size, (omc_u32)value);
}

static void
append_u64le(omc_u8* out, omc_size* io_size, omc_u64 value)
{
    append_u8(out, io_size, (omc_u8)(value & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 32) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 40) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 48) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 56) & 0xFFU));
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

static omc_size
build_exr_known_types(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);

    append_cstr(out, &size, "displayWindow");
    append_cstr(out, &size, "box2i");
    append_u32le(out, &size, 16U);
    append_i32le(out, &size, 1);
    append_i32le(out, &size, 2);
    append_i32le(out, &size, 3);
    append_i32le(out, &size, 4);

    append_cstr(out, &size, "whiteLuminance");
    append_cstr(out, &size, "rational");
    append_u32le(out, &size, 8U);
    append_i32le(out, &size, -3);
    append_u32le(out, &size, 2U);

    append_cstr(out, &size, "timeCode");
    append_cstr(out, &size, "timecode");
    append_u32le(out, &size, 8U);
    append_u32le(out, &size, 0x11223344U);
    append_u32le(out, &size, 0x55667788U);

    append_cstr(out, &size, "adoptedNeutral");
    append_cstr(out, &size, "v2d");
    append_u32le(out, &size, 16U);
    append_u64le(out, &size, ((omc_u64)0x3FF80000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40040000UL << 32) | 0x00000000UL);

    append_cstr(out, &size, "cameraForward");
    append_cstr(out, &size, "v3i");
    append_u32le(out, &size, 12U);
    append_i32le(out, &size, -1);
    append_i32le(out, &size, 0);
    append_i32le(out, &size, 9);

    append_cstr(out, &size, "lookModTransform");
    append_cstr(out, &size, "floatvector");
    append_u32le(out, &size, 12U);
    append_u32le(out, &size, 0x3E800000U);
    append_u32le(out, &size, 0x3F000000U);
    append_u32le(out, &size, 0x3F800000U);

    append_cstr(out, &size, "filmKeyCode");
    append_cstr(out, &size, "keycode");
    append_u32le(out, &size, 28U);
    append_i32le(out, &size, 1);
    append_i32le(out, &size, 2);
    append_i32le(out, &size, 3);
    append_i32le(out, &size, 4);
    append_i32le(out, &size, 5);
    append_i32le(out, &size, 6);
    append_i32le(out, &size, 7);

    append_u8(out, &size, 0U);
    return size;
}

static omc_size
build_exr_float_matrix_types(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);

    append_cstr(out, &size, "dataWindowF");
    append_cstr(out, &size, "box2f");
    append_u32le(out, &size, 16U);
    append_u32le(out, &size, 0x3F800000U);
    append_u32le(out, &size, 0x40000000U);
    append_u32le(out, &size, 0x40400000U);
    append_u32le(out, &size, 0x40800000U);

    append_cstr(out, &size, "primaries");
    append_cstr(out, &size, "chromaticities");
    append_u32le(out, &size, 32U);
    append_u32le(out, &size, 0x3E99999AU);
    append_u32le(out, &size, 0x3F19999AU);
    append_u32le(out, &size, 0x3DCCCCCDU);
    append_u32le(out, &size, 0x3E4CCCCDU);
    append_u32le(out, &size, 0x3D4CCCCDU);
    append_u32le(out, &size, 0x3D99999AU);
    append_u32le(out, &size, 0x3F2AAAABU);
    append_u32le(out, &size, 0x3F2AAAABU);

    append_cstr(out, &size, "cameraTransform");
    append_cstr(out, &size, "m44d");
    append_u32le(out, &size, 128U);
    append_u64le(out, &size, ((omc_u64)0x3FF00000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40000000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40080000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40100000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40140000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40180000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x401C0000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40200000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40220000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40240000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40260000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40280000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x402A0000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x402C0000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x402E0000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40300000UL << 32) | 0x00000000UL);

    append_u8(out, &size, 0U);
    return size;
}

static omc_size
build_exr_complex_raw_types(omc_u8* out)
{
    static const omc_u8 chlist_payload[] = {
        'R', 0U,
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        0U
    };
    static const omc_u8 preview_payload[] = {
        0x02U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x00U,
        0x10U, 0x20U, 0x30U, 0x40U,
        0x50U, 0x60U, 0x70U, 0x80U
    };
    static const omc_u8 stringvector_payload[] = {
        0x03U, 0x00U, 0x00U, 0x00U, 'o', 'n', 'e',
        0x03U, 0x00U, 0x00U, 0x00U, 't', 'w', 'o'
    };
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);

    append_attr_raw(out, &size, "channels", "chlist", chlist_payload,
                    sizeof(chlist_payload));
    append_attr_raw(out, &size, "thumbnail", "preview", preview_payload,
                    sizeof(preview_payload));
    append_attr_raw(out, &size, "aliases", "stringvector",
                    stringvector_payload, sizeof(stringvector_payload));
    append_u8(out, &size, 0U);
    return size;
}

static omc_size
build_exr_enum_vector_matrix_types(omc_u8* out)
{
    omc_size size;

    size = 0U;
    append_u32le(out, &size, 20000630U);
    append_u32le(out, &size, 2U);

    append_cstr(out, &size, "compressionMode");
    append_cstr(out, &size, "compression");
    append_u32le(out, &size, 1U);
    append_u8(out, &size, 3U);

    append_cstr(out, &size, "environment");
    append_cstr(out, &size, "envmap");
    append_u32le(out, &size, 1U);
    append_u8(out, &size, 1U);

    append_cstr(out, &size, "scanOrder");
    append_cstr(out, &size, "lineOrder");
    append_u32le(out, &size, 1U);
    append_u8(out, &size, 2U);

    append_cstr(out, &size, "deepState");
    append_cstr(out, &size, "deepImageState");
    append_u32le(out, &size, 1U);
    append_u8(out, &size, 2U);

    append_cstr(out, &size, "screenWindowCenter");
    append_cstr(out, &size, "v2f");
    append_u32le(out, &size, 8U);
    append_u32le(out, &size, 0x3F000000U);
    append_u32le(out, &size, 0x3FC00000U);

    append_cstr(out, &size, "worldDir");
    append_cstr(out, &size, "v3f");
    append_u32le(out, &size, 12U);
    append_u32le(out, &size, 0x40200000U);
    append_u32le(out, &size, 0x40600000U);
    append_u32le(out, &size, 0x40900000U);

    append_cstr(out, &size, "matrix3f");
    append_cstr(out, &size, "m33f");
    append_u32le(out, &size, 36U);
    append_u32le(out, &size, 0x3F800000U);
    append_u32le(out, &size, 0x40000000U);
    append_u32le(out, &size, 0x40400000U);
    append_u32le(out, &size, 0x40800000U);
    append_u32le(out, &size, 0x40A00000U);
    append_u32le(out, &size, 0x40C00000U);
    append_u32le(out, &size, 0x40E00000U);
    append_u32le(out, &size, 0x41000000U);
    append_u32le(out, &size, 0x41100000U);

    append_cstr(out, &size, "cameraUp");
    append_cstr(out, &size, "v3d");
    append_u32le(out, &size, 24U);
    append_u64le(out, &size, ((omc_u64)0x40080000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40140000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x401E0000UL << 32) | 0x00000000UL);

    append_cstr(out, &size, "matrix3d");
    append_cstr(out, &size, "m33d");
    append_u32le(out, &size, 72U);
    append_u64le(out, &size, ((omc_u64)0x3FF00000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40000000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40080000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40100000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40140000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40180000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x401C0000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40200000UL << 32) | 0x00000000UL);
    append_u64le(out, &size, ((omc_u64)0x40220000UL << 32) | 0x00000000UL);

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
assert_i32_array_value(const omc_store* store, const omc_entry* entry,
                       const omc_s32* expect, omc_u32 count)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == OMC_ELEM_I32);
    assert(entry->value.count == count);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(view.size == (omc_size)count * sizeof(omc_s32));
    assert(memcmp(view.data, expect, view.size) == 0);
}

static void
assert_u32_array_value(const omc_store* store, const omc_entry* entry,
                       const omc_u32* expect, omc_u32 count,
                       omc_elem_type elem_type)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == elem_type);
    assert(entry->value.count == count);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(view.size == (omc_size)count * sizeof(omc_u32));
    assert(memcmp(view.data, expect, view.size) == 0);
}

static void
assert_u64_array_value(const omc_store* store, const omc_entry* entry,
                       const omc_u64* expect, omc_u32 count,
                       omc_elem_type elem_type)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_ARRAY);
    assert(entry->value.elem_type == elem_type);
    assert(entry->value.count == count);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(view.size == (omc_size)count * sizeof(omc_u64));
    assert(memcmp(view.data, expect, view.size) == 0);
}

static void
assert_bytes_value(const omc_store* store, const omc_entry* entry,
                   const omc_u8* expect, omc_size expect_size)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(view.size == expect_size);
    assert(memcmp(view.data, expect, expect_size) == 0);
}

static void
assert_u8_scalar_value(const omc_entry* entry, omc_u8 expect)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.count == 1U);
    assert(entry->value.u.u64 == expect);
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
test_exr_decodes_known_numeric_type_family(void)
{
    omc_u8 exr[512];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* entry;
    const omc_s32 box2i_expect[4] = { 1, 2, 3, 4 };
    const omc_s32 v3i_expect[3] = { -1, 0, 9 };
    const omc_s32 keycode_expect[7] = { 1, 2, 3, 4, 5, 6, 7 };
    const omc_u32 timecode_expect[2] = { 0x11223344U, 0x55667788U };
    const omc_u32 floatvector_expect[3] = {
        0x3E800000U, 0x3F000000U, 0x3F800000U
    };
    const omc_u64 v2d_expect[2] = {
        ((omc_u64)0x3FF80000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40040000UL << 32) | 0x00000000UL
    };

    exr_size = build_exr_known_types(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.entries_decoded == 7U);

    entry = find_exr_entry(&store, 0U, "displayWindow");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 1U);
    assert_i32_array_value(&store, entry, box2i_expect, 4U);

    entry = find_exr_entry(&store, 0U, "whiteLuminance");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 19U);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_SRATIONAL);
    assert(entry->value.u.sr.numer == -3);
    assert(entry->value.u.sr.denom == 2);

    entry = find_exr_entry(&store, 0U, "timeCode");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 23U);
    assert_u32_array_value(&store, entry, timecode_expect, 2U,
                           OMC_ELEM_U32);

    entry = find_exr_entry(&store, 0U, "adoptedNeutral");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 26U);
    assert_u64_array_value(&store, entry, v2d_expect, 2U,
                           OMC_ELEM_F64_BITS);

    entry = find_exr_entry(&store, 0U, "cameraForward");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 27U);
    assert_i32_array_value(&store, entry, v3i_expect, 3U);

    entry = find_exr_entry(&store, 0U, "lookModTransform");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 10U);
    assert_u32_array_value(&store, entry, floatvector_expect, 3U,
                           OMC_ELEM_F32_BITS);

    entry = find_exr_entry(&store, 0U, "filmKeyCode");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 12U);
    assert_i32_array_value(&store, entry, keycode_expect, 7U);

    omc_store_fini(&store);
}

static void
test_exr_decodes_float_matrix_type_family(void)
{
    omc_u8 exr[512];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* entry;
    const omc_u32 box2f_expect[4] = {
        0x3F800000U, 0x40000000U, 0x40400000U, 0x40800000U
    };
    const omc_u32 chroma_expect[8] = {
        0x3E99999AU, 0x3F19999AU, 0x3DCCCCCDU, 0x3E4CCCCDU,
        0x3D4CCCCDU, 0x3D99999AU, 0x3F2AAAABU, 0x3F2AAAABU
    };
    const omc_u64 m44d_expect[16] = {
        ((omc_u64)0x3FF00000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40000000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40080000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40100000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40140000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40180000UL << 32) | 0x00000000UL,
        ((omc_u64)0x401C0000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40200000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40220000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40240000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40260000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40280000UL << 32) | 0x00000000UL,
        ((omc_u64)0x402A0000UL << 32) | 0x00000000UL,
        ((omc_u64)0x402C0000UL << 32) | 0x00000000UL,
        ((omc_u64)0x402E0000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40300000UL << 32) | 0x00000000UL
    };

    exr_size = build_exr_float_matrix_types(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.entries_decoded == 3U);

    entry = find_exr_entry(&store, 0U, "dataWindowF");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 2U);
    assert_u32_array_value(&store, entry, box2f_expect, 4U,
                           OMC_ELEM_F32_BITS);

    entry = find_exr_entry(&store, 0U, "primaries");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 5U);
    assert_u32_array_value(&store, entry, chroma_expect, 8U,
                           OMC_ELEM_F32_BITS);

    entry = find_exr_entry(&store, 0U, "cameraTransform");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 17U);
    assert_u64_array_value(&store, entry, m44d_expect, 16U,
                           OMC_ELEM_F64_BITS);

    omc_store_fini(&store);
}

static void
test_exr_preserves_complex_types_as_raw_bytes(void)
{
    omc_u8 exr[512];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* entry;
    static const omc_u8 chlist_expect[] = {
        'R', 0U,
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        0U
    };
    static const omc_u8 preview_expect[] = {
        0x02U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x00U, 0x00U,
        0x10U, 0x20U, 0x30U, 0x40U,
        0x50U, 0x60U, 0x70U, 0x80U
    };
    static const omc_u8 stringvector_expect[] = {
        0x03U, 0x00U, 0x00U, 0x00U, 'o', 'n', 'e',
        0x03U, 0x00U, 0x00U, 0x00U, 't', 'w', 'o'
    };

    exr_size = build_exr_complex_raw_types(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.entries_decoded == 3U);

    entry = find_exr_entry(&store, 0U, "channels");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 4U);
    assert(entry->origin.wire_type_name.size == 0U);
    assert_bytes_value(&store, entry, chlist_expect, sizeof(chlist_expect));

    entry = find_exr_entry(&store, 0U, "thumbnail");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 18U);
    assert(entry->origin.wire_type_name.size == 0U);
    assert_bytes_value(&store, entry, preview_expect, sizeof(preview_expect));

    entry = find_exr_entry(&store, 0U, "aliases");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 21U);
    assert(entry->origin.wire_type_name.size == 0U);
    assert_bytes_value(&store, entry, stringvector_expect,
                       sizeof(stringvector_expect));

    omc_store_fini(&store);
}

static void
test_exr_decodes_enum_vector_matrix_types(void)
{
    omc_u8 exr[1024];
    omc_size exr_size;
    omc_store store;
    omc_exr_res res;
    const omc_entry* entry;
    const omc_u32 v2f_expect[2] = {
        0x3F000000U, 0x3FC00000U
    };
    const omc_u32 v3f_expect[3] = {
        0x40200000U, 0x40600000U, 0x40900000U
    };
    const omc_u32 m33f_expect[9] = {
        0x3F800000U, 0x40000000U, 0x40400000U,
        0x40800000U, 0x40A00000U, 0x40C00000U,
        0x40E00000U, 0x41000000U, 0x41100000U
    };
    const omc_u64 v3d_expect[3] = {
        ((omc_u64)0x40080000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40140000UL << 32) | 0x00000000UL,
        ((omc_u64)0x401E0000UL << 32) | 0x00000000UL
    };
    const omc_u64 m33d_expect[9] = {
        ((omc_u64)0x3FF00000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40000000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40080000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40100000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40140000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40180000UL << 32) | 0x00000000UL,
        ((omc_u64)0x401C0000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40200000UL << 32) | 0x00000000UL,
        ((omc_u64)0x40220000UL << 32) | 0x00000000UL
    };

    exr_size = build_exr_enum_vector_matrix_types(exr);
    omc_store_init(&store);
    res = omc_exr_dec(exr, exr_size, &store, OMC_ENTRY_FLAG_NONE,
                      (const omc_exr_opts*)0);
    assert(res.status == OMC_EXR_OK);
    assert(res.entries_decoded == 9U);

    entry = find_exr_entry(&store, 0U, "compressionMode");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 6U);
    assert_u8_scalar_value(entry, 3U);

    entry = find_exr_entry(&store, 0U, "environment");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 8U);
    assert_u8_scalar_value(entry, 1U);

    entry = find_exr_entry(&store, 0U, "scanOrder");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 13U);
    assert_u8_scalar_value(entry, 2U);

    entry = find_exr_entry(&store, 0U, "deepState");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 30U);
    assert_u8_scalar_value(entry, 2U);

    entry = find_exr_entry(&store, 0U, "screenWindowCenter");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 25U);
    assert_u32_array_value(&store, entry, v2f_expect, 2U, OMC_ELEM_F32_BITS);

    entry = find_exr_entry(&store, 0U, "worldDir");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 28U);
    assert_u32_array_value(&store, entry, v3f_expect, 3U, OMC_ELEM_F32_BITS);

    entry = find_exr_entry(&store, 0U, "matrix3f");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 14U);
    assert_u32_array_value(&store, entry, m33f_expect, 9U,
                           OMC_ELEM_F32_BITS);

    entry = find_exr_entry(&store, 0U, "cameraUp");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 29U);
    assert_u64_array_value(&store, entry, v3d_expect, 3U, OMC_ELEM_F64_BITS);

    entry = find_exr_entry(&store, 0U, "matrix3d");
    assert(entry != (const omc_entry*)0);
    assert(entry->origin.wire_type.code == 15U);
    assert_u64_array_value(&store, entry, m33d_expect, 9U, OMC_ELEM_F64_BITS);

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
    test_exr_decodes_known_numeric_type_family();
    test_exr_decodes_float_matrix_type_family();
    test_exr_preserves_complex_types_as_raw_bytes();
    test_exr_decodes_enum_vector_matrix_types();
    test_read_simple_decodes_exr_header_fallback();
    return 0;
}
