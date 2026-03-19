#include "omc/omc_irb.h"

#include <assert.h>
#include <string.h>

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
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
append_u64be(omc_u8* out, omc_size* io_size, omc_u64 value)
{
    append_u8(out, io_size, (omc_u8)((value >> 56) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 48) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 40) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 32) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 24) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 16) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 8) & 0xFFU));
    append_u8(out, io_size, (omc_u8)((value >> 0) & 0xFFU));
}

static omc_u32
f32_bits(float value)
{
    omc_u32 bits;

    bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double
f64_from_bits(omc_u64 bits)
{
    double value;

    value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void
append_text(omc_u8* out, omc_size* io_size, const char* text)
{
    omc_size n;

    n = strlen(text);
    memcpy(out + *io_size, text, n);
    *io_size += n;
}

static void
append_bytes(omc_u8* out, omc_size* io_size, const omc_u8* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_utf16be_string32(omc_u8* out, omc_size* io_size, const char* text)
{
    omc_size n;
    omc_size i;

    n = strlen(text);
    append_u32be(out, io_size, (omc_u32)n);
    for (i = 0U; i < n; ++i) {
        append_u8(out, io_size, 0U);
        append_u8(out, io_size, (omc_u8)text[i]);
    }
}

static void
append_pascal_string(omc_u8* out, omc_size* io_size, const omc_u8* text,
                     omc_size text_size)
{
    append_u8(out, io_size, (omc_u8)(text_size & 0xFFU));
    append_bytes(out, io_size, text, text_size);
}

static void
append_irb_resource(omc_u8* out, omc_size* io_size, omc_u16 resource_id,
                    const omc_u8* payload, omc_size payload_size)
{
    append_text(out, io_size, "8BIM");
    append_u16be(out, io_size, resource_id);
    append_u8(out, io_size, 0U);
    append_u8(out, io_size, 0U);
    append_u32be(out, io_size, (omc_u32)payload_size);
    append_bytes(out, io_size, payload, payload_size);
    if ((payload_size & 1U) != 0U) {
        append_u8(out, io_size, 0U);
    }
}

static omc_const_bytes
arena_view(const omc_store* store, omc_byte_ref ref)
{
    return omc_arena_view(&store->arena, ref);
}

static const omc_entry*
find_irb_entry(const omc_store* store, omc_u16 resource_id)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_PHOTOSHOP_IRB
            && entry->key.u.photoshop_irb.resource_id == resource_id) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_irb_field(const omc_store* store, omc_u16 resource_id, const char* field,
               omc_u32 ordinal)
{
    omc_size i;
    omc_u32 seen;
    omc_size field_len;

    seen = 0U;
    field_len = strlen(field);
    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_PHOTOSHOP_IRB_FIELD
            || entry->key.u.photoshop_irb_field.resource_id != resource_id) {
            continue;
        }
        field_view = arena_view(store, entry->key.u.photoshop_irb_field.field);
        if (field_view.size != field_len
            || memcmp(field_view.data, field, field_len) != 0) {
            continue;
        }
        if (seen == ordinal) {
            return entry;
        }
        seen += 1U;
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_iptc_entry(const omc_store* store, omc_u16 record, omc_u16 dataset)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if (entry->key.kind == OMC_KEY_IPTC_DATASET
            && entry->key.u.iptc_dataset.record == record
            && entry->key.u.iptc_dataset.dataset == dataset) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static void
assert_text_entry(const omc_store* store, const omc_entry* entry,
                  omc_text_encoding enc, const char* text)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    assert(entry->value.text_encoding == enc);
    view = arena_view(store, entry->value.u.ref);
    assert(view.size == strlen(text));
    assert(memcmp(view.data, text, view.size) == 0);
}

static void
test_decode_irb_with_iptc(void)
{
    omc_u8 irb[64];
    static const omc_u8 iptc[] = { 0x1CU, 0x02U, 0x19U, 0x00U, 0x04U,
                                   (omc_u8)'t', (omc_u8)'e', (omc_u8)'s',
                                   (omc_u8)'t' };
    static const omc_u8 other[] = { 0x01U, 0x02U, 0x03U };
    omc_size irb_size;
    omc_store store;
    omc_irb_res meas;
    omc_irb_res dec;
    const omc_entry* entry;

    irb_size = 0U;
    append_irb_resource(irb, &irb_size, 0x0404U, iptc, sizeof(iptc));
    append_irb_resource(irb, &irb_size, 0x1234U, other, sizeof(other));

    omc_store_init(&store);
    meas = omc_irb_meas(irb, irb_size, (const omc_irb_opts*)0);
    dec = omc_irb_dec(irb, irb_size, &store, OMC_INVALID_BLOCK_ID,
                      (const omc_irb_opts*)0);

    assert(meas.status == OMC_IRB_OK);
    assert(dec.status == OMC_IRB_OK);
    assert(dec.resources_decoded == 2U);
    assert(dec.entries_decoded == meas.entries_decoded);
    assert(dec.iptc_entries_decoded == 1U);

    entry = find_irb_entry(&store, 0x0404U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);

    entry = find_irb_entry(&store, 0x1234U);
    assert(entry != (const omc_entry*)0);

    entry = find_iptc_entry(&store, 2U, 25U);
    assert(entry != (const omc_entry*)0);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);

    omc_store_fini(&store);
}

static void
test_decode_irb_derived_fields(void)
{
    static const omc_u8 clipping_text[] = {
        (omc_u8)'C', (omc_u8)'a', (omc_u8)'f', (omc_u8)0xE9, (omc_u8)'!'
    };
    static const omc_u8 digest[16] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU
    };
    omc_u8 irb[2048];
    omc_u8 payload[256];
    omc_size irb_size;
    omc_size payload_size;
    omc_store store;
    omc_irb_res dec;
    const omc_entry* entry;

    irb_size = 0U;

    payload_size = 0U;
    append_u32be(payload, &payload_size, 0x00488000U);
    append_u16be(payload, &payload_size, 1U);
    append_u16be(payload, &payload_size, 1U);
    append_u32be(payload, &payload_size, 0x00904000U);
    append_u16be(payload, &payload_size, 2U);
    append_u16be(payload, &payload_size, 1U);
    append_irb_resource(irb, &irb_size, 0x03EDU, payload, payload_size);

    payload_size = 0U;
    append_u32be(payload, &payload_size, 1U);
    append_u8(payload, &payload_size, 1U);
    append_utf16be_string32(payload, &payload_size, "Writer");
    append_utf16be_string32(payload, &payload_size, "Reader");
    append_u32be(payload, &payload_size, 1U);
    append_irb_resource(irb, &irb_size, 0x0421U, payload, payload_size);

    payload_size = 0U;
    append_u16be(payload, &payload_size, 2U);
    append_u16be(payload, &payload_size, 0x0101U);
    append_u16be(payload, &payload_size, 3U);
    append_irb_resource(irb, &irb_size, 0x0406U, payload, payload_size);

    payload_size = 0U;
    append_u16be(payload, &payload_size, 2U);
    append_u32be(payload, &payload_size, f32_bits(10.0f));
    append_u32be(payload, &payload_size, f32_bits(20.0f));
    append_u32be(payload, &payload_size, f32_bits(1.5f));
    append_irb_resource(irb, &irb_size, 0x0426U, payload, payload_size);

    payload_size = 0U;
    append_u32be(payload, &payload_size, 2U);
    append_u32be(payload, &payload_size, 0U);
    append_u32be(payload, &payload_size, 1U);
    append_utf16be_string32(payload, &payload_size, "https://one.example");
    append_u32be(payload, &payload_size, 0U);
    append_u32be(payload, &payload_size, 2U);
    append_utf16be_string32(payload, &payload_size, "https://two.example");
    append_irb_resource(irb, &irb_size, 0x041EU, payload, payload_size);

    payload_size = 0U;
    append_u16be(payload, &payload_size, 7U);
    append_u16be(payload, &payload_size, 8U);
    append_u16be(payload, &payload_size, 9U);
    append_irb_resource(irb, &irb_size, 0x0402U, payload, payload_size);

    append_irb_resource(irb, &irb_size, 0x0425U, digest, sizeof(digest));

    payload_size = 0U;
    append_pascal_string(payload, &payload_size, clipping_text,
                         sizeof(clipping_text));
    append_u8(payload, &payload_size, 0U);
    append_u8(payload, &payload_size, 0U);
    append_irb_resource(irb, &irb_size, 0x0BB7U, payload, payload_size);

    payload_size = 0U;
    append_u16be(payload, &payload_size, 5U);
    append_u16be(payload, &payload_size, 1U);
    append_u16be(payload, &payload_size, 2U);
    append_u16be(payload, &payload_size, 3U);
    append_u16be(payload, &payload_size, 4U);
    append_u8(payload, &payload_size, 0U);
    append_u8(payload, &payload_size, 250U);
    append_u8(payload, &payload_size, 7U);
    append_irb_resource(irb, &irb_size, 0x0435U, payload, payload_size);

    payload_size = 0U;
    append_u16be(payload, &payload_size, 1U);
    append_u16be(payload, &payload_size, 1U);
    append_u32be(payload, &payload_size, 18U);
    append_u16be(payload, &payload_size, 2U);
    append_irb_resource(irb, &irb_size, 0x2710U, payload, payload_size);

    omc_store_init(&store);
    dec = omc_irb_dec(irb, irb_size, &store, 7U, (const omc_irb_opts*)0);
    assert(dec.status == OMC_IRB_OK);
    assert(dec.resources_decoded == 10U);
    assert(dec.entries_decoded > dec.resources_decoded);

    entry = find_irb_field(&store, 0x03EDU, "XResolution", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_F64_BITS);
    assert(f64_from_bits(entry->value.u.f64_bits) == 72.5);
    assert((entry->flags & OMC_ENTRY_FLAG_DERIVED) != 0U);

    entry = find_irb_field(&store, 0x03EDU, "DisplayedUnitsY", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 2U);

    entry = find_irb_field(&store, 0x0421U, "WriterName", 0U);
    assert_text_entry(&store, entry, OMC_TEXT_UTF8, "Writer");
    entry = find_irb_field(&store, 0x0421U, "ReaderName", 0U);
    assert_text_entry(&store, entry, OMC_TEXT_UTF8, "Reader");

    entry = find_irb_field(&store, 0x0406U, "PhotoshopQuality", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == 2);
    entry = find_irb_field(&store, 0x0406U, "ProgressiveScans", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_I16);
    assert(entry->value.u.i64 == 3);

    entry = find_irb_field(&store, 0x0426U, "PrintPositionX", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_F32_BITS);
    assert(entry->value.u.f32_bits == f32_bits(10.0f));
    entry = find_irb_field(&store, 0x0426U, "PrintScale", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_F32_BITS);
    assert(entry->value.u.f32_bits == f32_bits(1.5f));

    entry = find_irb_field(&store, 0x041EU, "URLListCount", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 2U);
    entry = find_irb_field(&store, 0x041EU, "URL", 0U);
    assert_text_entry(&store, entry, OMC_TEXT_UTF8, "https://one.example");
    entry = find_irb_field(&store, 0x041EU, "URL", 1U);
    assert_text_entry(&store, entry, OMC_TEXT_UTF8, "https://two.example");

    entry = find_irb_field(&store, 0x0402U, "LayersGroupInfoCount", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.u.u64 == 3U);
    entry = find_irb_field(&store, 0x0402U, "LayersGroupInfo", 2U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 9U);

    entry = find_irb_field(&store, 0x0425U, "IPTCDigest", 0U);
    assert_text_entry(&store, entry, OMC_TEXT_ASCII,
                      "000102030405060708090a0b0c0d0e0f");

    entry = find_irb_field(&store, 0x0BB7U, "ClippingPathName", 0U);
    assert_text_entry(&store, entry, OMC_TEXT_UTF8, "Caf\xC3\xA9!");

    entry = find_irb_field(&store, 0x0435U, "ChannelOptionsCount", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 1U);
    entry = find_irb_field(&store, 0x0435U, "ChannelColorData", 3U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 4U);
    entry = find_irb_field(&store, 0x0435U, "ChannelOpacity", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == 250U);

    entry = find_irb_field(&store, 0x2710U, "PrintFlagsInfoVersion", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U16);
    assert(entry->value.u.u64 == 1U);
    entry = find_irb_field(&store, 0x2710U, "BleedWidthValue", 0U);
    assert(entry != (const omc_entry*)0);
    assert(entry->value.elem_type == OMC_ELEM_U32);
    assert(entry->value.u.u64 == 18U);

    omc_store_fini(&store);
}

static void
test_irb_clipping_path_ascii_policy(void)
{
    static const omc_u8 clipping_text[] = {
        (omc_u8)'C', (omc_u8)'a', (omc_u8)'f', (omc_u8)0xE9, (omc_u8)'!'
    };
    omc_u8 irb[64];
    omc_u8 payload[32];
    omc_size irb_size;
    omc_size payload_size;
    omc_store store;
    omc_irb_opts opts;
    omc_irb_res dec;

    irb_size = 0U;
    payload_size = 0U;
    append_pascal_string(payload, &payload_size, clipping_text,
                         sizeof(clipping_text));
    append_irb_resource(irb, &irb_size, 0x0BB7U, payload, payload_size);

    omc_irb_opts_init(&opts);
    opts.string_charset = OMC_IRB_STR_ASCII;

    omc_store_init(&store);
    dec = omc_irb_dec(irb, irb_size, &store, OMC_INVALID_BLOCK_ID, &opts);
    assert(dec.status == OMC_IRB_OK);
    assert(dec.resources_decoded == 1U);
    assert(dec.entries_decoded == 1U);
    assert(find_irb_field(&store, 0x0BB7U, "ClippingPathName", 0U)
           == (const omc_entry*)0);
    omc_store_fini(&store);
}

static void
test_irb_padding(void)
{
    omc_u8 irb[32];
    static const omc_u8 payload[] = { 0x01U, 0x02U, 0x03U };
    omc_size irb_size;
    omc_store store;
    omc_irb_res dec;

    irb_size = 0U;
    append_irb_resource(irb, &irb_size, 0x1234U, payload, sizeof(payload));
    append_u8(irb, &irb_size, 0U);
    append_u8(irb, &irb_size, 0U);

    omc_store_init(&store);
    dec = omc_irb_dec(irb, irb_size, &store, OMC_INVALID_BLOCK_ID,
                      (const omc_irb_opts*)0);
    assert(dec.status == OMC_IRB_OK);
    assert(dec.resources_decoded == 1U);
    omc_store_fini(&store);
}

int
main(void)
{
    test_decode_irb_with_iptc();
    test_decode_irb_derived_fields();
    test_irb_clipping_path_ascii_policy();
    test_irb_padding();
    return 0;
}
