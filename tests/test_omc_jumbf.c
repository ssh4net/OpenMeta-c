#include "omc/omc_jumbf.h"

#include <assert.h>
#include <string.h>

static const omc_u8 k_sample_jumbf_box[] = {
    0x00U, 0x00U, 0x00U, 0x21U, 'j', 'u', 'm', 'b',
    0x00U, 0x00U, 0x00U, 0x0DU, 'j', 'u', 'm', 'd',
    'c', '2', 'p', 'a', 0x00U,
    0x00U, 0x00U, 0x00U, 0x0CU, 'c', 'b', 'o', 'r',
    0xA1U, 0x61U, 0x61U, 0x01U
};

static void
append_u8(omc_u8* out, omc_size* io_size, omc_u8 value)
{
    out[*io_size] = value;
    *io_size += 1U;
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
append_bytes(omc_u8* out, omc_size* io_size, const void* src, omc_size size)
{
    memcpy(out + *io_size, src, size);
    *io_size += size;
}

static void
append_box(omc_u8* out, omc_size* io_size, const char* type,
           const omc_u8* payload, omc_size payload_size)
{
    append_u32be(out, io_size, (omc_u32)(8U + payload_size));
    append_bytes(out, io_size, type, 4U);
    if (payload_size != 0U) {
        append_bytes(out, io_size, payload, payload_size);
    }
}

static omc_size
make_jumbf_with_cbor(omc_u8* out, const omc_u8* cbor_payload,
                     omc_size cbor_payload_size)
{
    omc_u8 jumd_box[64];
    omc_u8 cbor_box[512];
    omc_u8 jumb_payload[640];
    omc_size jumd_size;
    omc_size cbor_size;
    omc_size jumb_payload_size;
    omc_size out_size;

    jumd_size = 0U;
    append_box(jumd_box, &jumd_size, "jumd", (const omc_u8*)"c2pa\0", 5U);

    cbor_size = 0U;
    append_box(cbor_box, &cbor_size, "cbor", cbor_payload, cbor_payload_size);

    jumb_payload_size = 0U;
    append_bytes(jumb_payload, &jumb_payload_size, jumd_box, jumd_size);
    append_bytes(jumb_payload, &jumb_payload_size, cbor_box, cbor_size);

    out_size = 0U;
    append_box(out, &out_size, "jumb", jumb_payload, jumb_payload_size);
    return out_size;
}

static const omc_entry*
find_jumbf_field(const omc_store* store, const char* field)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes field_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_JUMBF_FIELD) {
            continue;
        }
        field_view = omc_arena_view(&store->arena, entry->key.u.jumbf_field.field);
        if (field_view.size == strlen(field)
            && memcmp(field_view.data, field, field_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static const omc_entry*
find_jumbf_cbor_key(const omc_store* store, const char* key_text)
{
    omc_size i;

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_const_bytes key_view;

        entry = &store->entries[i];
        if (entry->key.kind != OMC_KEY_JUMBF_CBOR_KEY) {
            continue;
        }
        key_view = omc_arena_view(&store->arena, entry->key.u.jumbf_cbor_key.key);
        if (key_view.size == strlen(key_text)
            && memcmp(key_view.data, key_text, key_view.size) == 0) {
            return entry;
        }
    }
    return (const omc_entry*)0;
}

static void
assert_text_entry_equals(const omc_store* store, const omc_entry* entry,
                         const char* expected)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(view.size == strlen(expected));
    assert(memcmp(view.data, expected, view.size) == 0);
}

static void
assert_bytes_entry_equals(const omc_store* store, const omc_entry* entry,
                          const omc_u8* expected, omc_size expected_size)
{
    omc_const_bytes view;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_BYTES);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    assert(view.size == expected_size);
    assert(memcmp(view.data, expected, expected_size) == 0);
}

static void
assert_scalar_u64_equals(const omc_entry* entry, omc_u64 expected)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.u.u64 == expected);
}

static void
assert_scalar_i64_equals(const omc_entry* entry, omc_s64 expected)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    if (entry->value.elem_type == OMC_ELEM_I64) {
        assert(entry->value.u.i64 == expected);
    } else {
        assert(entry->value.elem_type == OMC_ELEM_U64);
        assert((omc_s64)entry->value.u.u64 == expected);
    }
}

static void
test_jumbf_decode_sample(void)
{
    omc_store store;
    omc_jumbf_res res;
    omc_jumbf_res meas;
    const omc_entry* marker;
    const omc_entry* box_type;
    const omc_entry* cbor_value;
    omc_const_bytes value_view;

    omc_store_init(&store);

    res = omc_jumbf_dec(k_sample_jumbf_box, sizeof(k_sample_jumbf_box), &store,
                        0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);

    assert(res.status == OMC_JUMBF_OK);
    assert(res.boxes_decoded == 3U);
    assert(res.cbor_items == 3U);
    assert(res.entries_decoded >= 10U);

    marker = find_jumbf_field(&store, "c2pa.detected");
    assert(marker != (const omc_entry*)0);
    assert(marker->value.kind == OMC_VAL_SCALAR);
    assert(marker->value.u.u64 == 1U);

    box_type = find_jumbf_field(&store, "box.0.type");
    assert(box_type != (const omc_entry*)0);
    value_view = omc_arena_view(&store.arena, box_type->value.u.ref);
    assert(value_view.size == 4U);
    assert(memcmp(value_view.data, "jumb", 4U) == 0);

    cbor_value = find_jumbf_cbor_key(&store, "box.0.1.cbor.a");
    assert(cbor_value != (const omc_entry*)0);
    assert(cbor_value->value.kind == OMC_VAL_SCALAR);
    assert(cbor_value->value.u.u64 == 1U);

    meas = omc_jumbf_meas(k_sample_jumbf_box, sizeof(k_sample_jumbf_box),
                          (const omc_jumbf_opts*)0);
    assert(meas.status == OMC_JUMBF_OK);
    assert(meas.boxes_decoded == res.boxes_decoded);
    assert(meas.cbor_items == res.cbor_items);
    assert(meas.entries_decoded == res.entries_decoded);
    assert(meas.entries_decoded == (omc_u32)store.entry_count);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_semantic_summary(void)
{
    static const omc_u8 k_cbor_payload[] = {
        0xA1U,
        0x69U, 'm', 'a', 'n', 'i', 'f', 'e', 's', 't', 's',
        0xA1U,
        0x6FU, 'a', 'c', 't', 'i', 'v', 'e', '_', 'm',
        'a', 'n', 'i', 'f', 'e', 's', 't',
        0xA4U,
        0x6FU, 'c', 'l', 'a', 'i', 'm', '_', 'g', 'e',
        'n', 'e', 'r', 'a', 't', 'o', 'r',
        0x68U, 'O', 'p', 'e', 'n', 'M', 'e', 't', 'a',
        0x6AU, 'a', 's', 's', 'e', 'r', 't', 'i', 'o', 'n', 's',
        0x82U, 0x01U, 0x02U,
        0x69U, 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e',
        0x62U, 'o', 'k',
        0x65U, 'c', 'l', 'a', 'i', 'm',
        0x64U, 't', 'e', 's', 't'
    };
    omc_u8 jumbf[256];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    jumbf_size = make_jumbf_with_cbor(jumbf, k_cbor_payload,
                                      sizeof(k_cbor_payload));
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(&store, "c2pa.detected"), 1U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.manifest_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.claim_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.assertion_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.signature_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.manifest_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.claim_count"),
                             1U);
    assert(find_jumbf_field(&store, "c2pa.semantic.cbor_key_count")
           != (const omc_entry*)0);
    assert(find_jumbf_field(&store, "c2pa.semantic.assertion_key_hits")
           != (const omc_entry*)0);
    assert_text_entry_equals(&store,
                             find_jumbf_field(
                                 &store, "c2pa.semantic.claim_generator"),
                             "OpenMeta");

    omc_store_fini(&store);
}

static void
test_jumbf_extracts_cose_signature_fields(void)
{
    static const omc_u8 k_protected[] = { 0xA1U, 0x01U, 0x26U };
    static const omc_u8 k_public_key_der[] = { 0x01U, 0x02U, 0x03U };
    static const omc_u8 k_certificate_der[] = { 0x04U, 0x05U, 0x06U };
    static const omc_u8 k_signature[] = { 0xAAU, 0x55U };
    static const omc_u8 k_cbor_payload[] = {
        0xA1U,
        0x69U, 'm', 'a', 'n', 'i', 'f', 'e', 's', 't', 's',
        0xA1U,
        0x6FU, 'a', 'c', 't', 'i', 'v', 'e', '_', 'm',
        'a', 'n', 'i', 'f', 'e', 's', 't',
        0xA1U,
        0x6AU, 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', 's',
        0x81U,
        0x84U,
        0x43U, 0xA1U, 0x01U, 0x26U,
        0xA3U,
        0x01U, 0x26U,
        0x6EU, 'p', 'u', 'b', 'l', 'i', 'c', '_', 'k', 'e',
        'y', '_', 'd', 'e', 'r',
        0x43U, 0x01U, 0x02U, 0x03U,
        0x6FU, 'c', 'e', 'r', 't', 'i', 'f', 'i', 'c', 'a',
        't', 'e', '_', 'd', 'e', 'r',
        0x43U, 0x04U, 0x05U, 0x06U,
        0xF6U,
        0x42U, 0xAAU, 0x55U
    };
    omc_u8 jumbf[384];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;
    omc_jumbf_res meas;

    jumbf_size = make_jumbf_with_cbor(jumbf, k_cbor_payload,
                                      sizeof(k_cbor_payload));
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.signature_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.signature_count"),
                             1U);
    assert_text_entry_equals(&store,
                             find_jumbf_field(
                                 &store, "c2pa.semantic.signature.0.algorithm"),
                             "es256");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.signature.0.algorithm"),
                             "es256");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             1U);
    assert_scalar_i64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.signature.0.cose_unprotected_alg"),
                             (omc_s64)-7);
    assert_bytes_entry_equals(&store,
                              find_jumbf_field(
                                  &store,
                                  "c2pa.signature.0.protected_bytes"),
                              k_protected, sizeof(k_protected));
    assert_bytes_entry_equals(&store,
                              find_jumbf_field(
                                  &store,
                                  "c2pa.signature.0.signature_bytes"),
                              k_signature, sizeof(k_signature));
    assert_bytes_entry_equals(&store,
                              find_jumbf_field(
                                  &store,
                                  "c2pa.signature.0.public_key_der"),
                              k_public_key_der, sizeof(k_public_key_der));
    assert_bytes_entry_equals(&store,
                              find_jumbf_field(
                                  &store,
                                  "c2pa.signature.0.certificate_der"),
                              k_certificate_der,
                              sizeof(k_certificate_der));

    meas = omc_jumbf_meas(jumbf, jumbf_size, (const omc_jumbf_opts*)0);
    assert(meas.status == OMC_JUMBF_OK);
    assert(meas.boxes_decoded == res.boxes_decoded);
    assert(meas.cbor_items == res.cbor_items);
    assert(meas.entries_decoded == res.entries_decoded);
    assert(meas.entries_decoded == (omc_u32)store.entry_count);

    omc_store_fini(&store);
}

static void
test_jumbf_limit_entries(void)
{
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    omc_jumbf_opts_init(&opts);
    opts.limits.max_entries = 4U;

    res = omc_jumbf_meas(k_sample_jumbf_box, sizeof(k_sample_jumbf_box), &opts);
    assert(res.status == OMC_JUMBF_LIMIT);
}

static void
test_jumbf_cbor_half_and_simple_scalars(void)
{
    static const omc_u8 k_cbor_payload[] = {
        0xA2U,
        0x61U, 0x68U,
        0xF9U, 0x3EU, 0x00U,
        0x61U, 0x73U,
        0xF0U
    };
    omc_u8 jumbf[128];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;
    const omc_entry* half_entry;
    const omc_entry* simple_entry;

    jumbf_size = make_jumbf_with_cbor(jumbf, k_cbor_payload,
                                      sizeof(k_cbor_payload));
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    half_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.h");
    assert(half_entry != (const omc_entry*)0);
    assert(half_entry->value.kind == OMC_VAL_SCALAR);
    assert(half_entry->value.elem_type == OMC_ELEM_F32_BITS);
    assert(half_entry->value.u.f32_bits == 0x3FC00000U);

    simple_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.s");
    assert(simple_entry != (const omc_entry*)0);
    assert(simple_entry->value.kind == OMC_VAL_SCALAR);
    assert(simple_entry->value.elem_type == OMC_ELEM_U8);
    assert(simple_entry->value.u.u64 == 16U);

    omc_store_fini(&store);
}

static void
test_jumbf_cbor_indefinite_and_paths(void)
{
    static const omc_u8 k_cbor_payload[] = {
        0xA4U,
        0x61U, 0x74U,
        0x7FU,
        0x62U, 0x68U, 0x69U,
        0x63U, 0x21U, 0x21U, 0x21U,
        0xFFU,
        0x61U, 0x62U,
        0x5FU,
        0x42U, 0x01U, 0x02U,
        0x41U, 0x03U,
        0xFFU,
        0x63U, 0x61U, 0x72U, 0x72U,
        0x9FU,
        0x01U, 0x02U, 0xFFU,
        0x63U, 0x6DU, 0x61U, 0x70U,
        0xBFU,
        0x01U,
        0x61U, 0x78U,
        0xFFU
    };
    static const omc_u8 k_expected_bytes[] = { 0x01U, 0x02U, 0x03U };
    omc_u8 jumbf[256];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;
    const omc_entry* text_entry;
    const omc_entry* bytes_entry;
    const omc_entry* arr0_entry;
    const omc_entry* arr1_entry;
    const omc_entry* map_entry;

    jumbf_size = make_jumbf_with_cbor(jumbf, k_cbor_payload,
                                      sizeof(k_cbor_payload));
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    text_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.t");
    assert_text_entry_equals(&store, text_entry, "hi!!!");

    bytes_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.b");
    assert_bytes_entry_equals(&store, bytes_entry, k_expected_bytes,
                              sizeof(k_expected_bytes));

    arr0_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.arr[0]");
    assert(arr0_entry != (const omc_entry*)0);
    assert(arr0_entry->value.kind == OMC_VAL_SCALAR);
    assert(arr0_entry->value.elem_type == OMC_ELEM_U64);
    assert(arr0_entry->value.u.u64 == 1U);

    arr1_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.arr[1]");
    assert(arr1_entry != (const omc_entry*)0);
    assert(arr1_entry->value.kind == OMC_VAL_SCALAR);
    assert(arr1_entry->value.elem_type == OMC_ELEM_U64);
    assert(arr1_entry->value.u.u64 == 2U);

    map_entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.map.1");
    assert_text_entry_equals(&store, map_entry, "x");

    omc_store_fini(&store);
}

static void
test_jumbf_cbor_composite_key_fallback(void)
{
    static const omc_u8 k_cbor_payload[] = {
        0xA1U,
        0x82U, 0x01U, 0x02U,
        0x03U
    };
    omc_u8 jumbf[128];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;
    const omc_entry* entry;

    jumbf_size = make_jumbf_with_cbor(jumbf, k_cbor_payload,
                                      sizeof(k_cbor_payload));
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    entry = find_jumbf_cbor_key(&store, "box.0.1.cbor.k0_arr");
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U64);
    assert(entry->value.u.u64 == 3U);

    omc_store_fini(&store);
}

int
main(void)
{
    test_jumbf_decode_sample();
    test_jumbf_emits_c2pa_semantic_summary();
    test_jumbf_extracts_cose_signature_fields();
    test_jumbf_limit_entries();
    test_jumbf_cbor_half_and_simple_scalars();
    test_jumbf_cbor_indefinite_and_paths();
    test_jumbf_cbor_composite_key_fallback();
    return 0;
}
