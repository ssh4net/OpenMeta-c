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

static void
append_cbor_head(omc_u8* out, omc_size* io_size, omc_u8 major, omc_u8 arg)
{
    assert(arg < 24U);
    append_u8(out, io_size, (omc_u8)((major << 5) | arg));
}

static void
append_cbor_map(omc_u8* out, omc_size* io_size, omc_u8 count)
{
    append_cbor_head(out, io_size, 5U, count);
}

static void
append_cbor_array(omc_u8* out, omc_size* io_size, omc_u8 count)
{
    append_cbor_head(out, io_size, 4U, count);
}

static void
append_cbor_text(omc_u8* out, omc_size* io_size, const char* text)
{
    const omc_size len = strlen(text);

    assert(len < 256U);
    if (len < 24U) {
        append_cbor_head(out, io_size, 3U, (omc_u8)len);
    } else {
        append_u8(out, io_size, (omc_u8)((3U << 5) | 24U));
        append_u8(out, io_size, (omc_u8)len);
    }
    append_bytes(out, io_size, text, len);
}

static void
append_cbor_bytes(omc_u8* out, omc_size* io_size, const omc_u8* bytes,
                  omc_size size)
{
    assert(size < 256U);
    if (size < 24U) {
        append_cbor_head(out, io_size, 2U, (omc_u8)size);
    } else {
        append_u8(out, io_size, (omc_u8)((2U << 5) | 24U));
        append_u8(out, io_size, (omc_u8)size);
    }
    append_bytes(out, io_size, bytes, size);
}

static void
append_cbor_i64(omc_u8* out, omc_size* io_size, omc_s64 value)
{
    omc_u64 arg;
    omc_u8 major;

    if (value >= 0) {
        major = 0U;
        arg = (omc_u64)value;
    } else {
        major = 1U;
        arg = (omc_u64)(-1 - value);
    }
    if (arg < 24U) {
        append_cbor_head(out, io_size, major, (omc_u8)arg);
    } else if (arg <= 0xFFU) {
        append_u8(out, io_size, (omc_u8)((major << 5) | 24U));
        append_u8(out, io_size, (omc_u8)arg);
    } else if (arg <= 0xFFFFU) {
        append_u8(out, io_size, (omc_u8)((major << 5) | 25U));
        append_u8(out, io_size, (omc_u8)((arg >> 8) & 0xFFU));
        append_u8(out, io_size, (omc_u8)(arg & 0xFFU));
    } else if (arg <= 0xFFFFFFFFUL) {
        append_u8(out, io_size, (omc_u8)((major << 5) | 26U));
        append_u32be(out, io_size, (omc_u32)arg);
    } else {
        assert(arg <= 0xFFFFFFFFFFFFFFFFULL);
        append_u8(out, io_size, (omc_u8)((major << 5) | 27U));
        append_u32be(out, io_size, (omc_u32)(arg >> 32));
        append_u32be(out, io_size, (omc_u32)(arg & 0xFFFFFFFFUL));
    }
}

static omc_size
make_jumb_box_with_label(omc_u8* out, const char* label,
                         const omc_u8* payload_boxes,
                         omc_size payload_boxes_size)
{
    const omc_size label_len = strlen(label);
    const omc_size jumd_payload_size = label_len + 1U;
    const omc_size jumb_payload_size = 8U + jumd_payload_size
                                       + payload_boxes_size;
    omc_size out_size;

    out_size = 0U;
    append_u32be(out, &out_size, (omc_u32)(8U + jumb_payload_size));
    append_bytes(out, &out_size, "jumb", 4U);
    append_u32be(out, &out_size, (omc_u32)(8U + jumd_payload_size));
    append_bytes(out, &out_size, "jumd", 4U);
    append_bytes(out, &out_size, label, label_len);
    append_u8(out, &out_size, 0U);
    if (payload_boxes_size != 0U) {
        append_bytes(out, &out_size, payload_boxes, payload_boxes_size);
    }
    return out_size;
}

static omc_size
make_claim_jumb_box(omc_u8* out, const char* label, const omc_u8* claim_cbor,
                    omc_size claim_cbor_size)
{
    omc_u8 cbor_box[512];
    omc_size cbor_box_size;

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", claim_cbor, claim_cbor_size);
    return make_jumb_box_with_label(out, label, cbor_box, cbor_box_size);
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
assert_text_entry_contains(const omc_store* store, const omc_entry* entry,
                           const char* expected_substring)
{
    omc_const_bytes view;
    omc_size needle_size;
    omc_size i;

    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_TEXT);
    view = omc_arena_view(&store->arena, entry->value.u.ref);
    needle_size = strlen(expected_substring);
    assert(view.size >= needle_size);
    for (i = 0U; i + needle_size <= view.size; ++i) {
        if (memcmp(view.data + i, expected_substring, needle_size) == 0) {
            return;
        }
    }
    assert(0);
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
assert_scalar_u8_equals(const omc_entry* entry, omc_u8 expected)
{
    assert(entry != (const omc_entry*)0);
    assert(entry->value.kind == OMC_VAL_SCALAR);
    assert(entry->value.elem_type == OMC_ELEM_U8);
    assert(entry->value.u.u64 == (omc_u64)expected);
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

typedef struct test_u64_field_expect {
    const char* field;
    omc_u64 value;
} test_u64_field_expect;

static void
assert_jumbf_u64_fields(const omc_store* store,
                        const test_u64_field_expect* fields, omc_size count)
{
    omc_size i;

    for (i = 0U; i < count; ++i) {
        assert_scalar_u64_equals(find_jumbf_field(store, fields[i].field),
                                 fields[i].value);
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
                                 &store,
                                 "c2pa.semantic.active_manifest_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.active_manifest_count"),
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
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.assertion_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_linked_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_orphan_count"),
                             1U);
    assert(find_jumbf_field(&store, "c2pa.semantic.cbor_key_count")
           != (const omc_entry*)0);
    assert(find_jumbf_field(&store, "c2pa.semantic.assertion_key_hits")
           != (const omc_entry*)0);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.active_manifest.prefix"),
        "box.0.1.cbor.manifests.active_manifest");
    assert_text_entry_equals(&store,
                             find_jumbf_field(
                                 &store, "c2pa.semantic.claim_generator"),
                             "OpenMeta");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_manifest_claim_signature_projection(void)
{
    static const omc_u8 k_cbor_payload[] = {
        0xA1U,
        0x69U, 'm', 'a', 'n', 'i', 'f', 'e', 's', 't', 's',
        0xA1U,
        0x6FU, 'a', 'c', 't', 'i', 'v', 'e', '_', 'm',
        'a', 'n', 'i', 'f', 'e', 's', 't',
        0xA1U,
        0x66U, 'c', 'l', 'a', 'i', 'm', 's',
        0x82U,
        0xA4U,
        0x6FU, 'c', 'l', 'a', 'i', 'm', '_', 'g', 'e',
        'n', 'e', 'r', 'a', 't', 'o', 'r',
        0x68U, 'O', 'p', 'e', 'n', 'M', 'e', 't', 'a',
        0x6AU, 'a', 's', 's', 'e', 'r', 't', 'i', 'o', 'n', 's',
        0x82U, 0x01U, 0x02U,
        0x69U, 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e',
        0x63U, 's', 'i', 'g',
        0x6AU, 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', 's',
        0x81U,
        0xA1U,
        0x63U, 'a', 'l', 'g',
        0x65U, 'e', 's', '2', '5', '6',
        0xA2U,
        0x6FU, 'c', 'l', 'a', 'i', 'm', '_', 'g', 'e',
        'n', 'e', 'r', 'a', 't', 'o', 'r',
        0x65U, 'O', 't', 'h', 'e', 'r',
        0x6AU, 'a', 's', 's', 'e', 'r', 't', 'i', 'o', 'n', 's',
        0x81U, 0x03U
    };
    omc_u8 jumbf[384];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    jumbf_size = make_jumbf_with_cbor(jumbf, k_cbor_payload,
                                      sizeof(k_cbor_payload));
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.assertion_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_linked_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_orphan_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.manifest_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.active_manifest_count"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.active_manifest.prefix"),
        "box.0.1.cbor.manifests.active_manifest");

    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.manifest.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.manifest.0.is_active"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.assertion_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_linked_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_orphan_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.cross_claim_link_count"),
                             0U);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.semantic.claim.0.prefix"),
                             "box.0.1.cbor.manifests.active_manifest.claims[0]");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.semantic.claim.1.prefix"),
                             "box.0.1.cbor.manifests.active_manifest.claims[1]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.assertion_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.assertion_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.signature_key_hits"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.signature_key_hits"),
                             0U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.claim.0.claim_generator"),
        "OpenMeta");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.claim.1.claim_generator"),
        "Other");

    assert_text_entry_equals(
        &store,
        find_jumbf_field(
            &store, "c2pa.semantic.claim.0.assertion.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].assertions[0]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(
            &store, "c2pa.semantic.claim.0.assertion.1.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].assertions[1]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(
            &store, "c2pa.semantic.claim.1.assertion.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1].assertions[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.assertion.0.key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.assertion.1.key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.assertion.0.key_hits"),
                             1U);

    assert_text_entry_equals(
        &store,
        find_jumbf_field(
            &store, "c2pa.semantic.claim.0.signature.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].signatures[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.signature.0.key_hits"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(
            &store, "c2pa.semantic.claim.0.signature.0.algorithm"),
        "es256");
    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.signature.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].signatures[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.key_hits"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.signature.0.algorithm"),
        "es256");
    assert(find_jumbf_field(
               &store, "c2pa.semantic.claim.1.signature.0.prefix")
           == (const omc_entry*)0);

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
test_jumbf_resolves_detached_payload_from_direct_claim(void)
{
    static const omc_u8 k_claim_bytes[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[768];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;
    omc_jumbf_res meas;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 1U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bytes,
                      sizeof(k_claim_bytes));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_bytes, sizeof(k_claim_bytes));

    meas = omc_jumbf_meas(jumbf, jumbf_size, (const omc_jumbf_opts*)0);
    assert(meas.status == OMC_JUMBF_OK);
    assert(meas.boxes_decoded == res.boxes_decoded);
    assert(meas.cbor_items == res.cbor_items);

    omc_store_fini(&store);
}

static void
test_jumbf_resolves_detached_payload_from_explicit_claim_ref(void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x44U };
    omc_u8 cbor_payload[768];
    omc_size cbor_size;
    omc_u8 jumbf[1024];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 1);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_good, sizeof(k_claim_good));

    omc_store_fini(&store);
}

static void
test_jumbf_resolves_detached_payload_from_explicit_label(void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 cbor_box[1152];
    omc_size cbor_box_size;
    omc_u8 claim_bad_jumb[128];
    omc_size claim_bad_jumb_size;
    omc_u8 claim_good_jumb[128];
    omc_size claim_good_jumb_size;
    omc_u8 root_payload[1536];
    omc_size root_payload_size;
    omc_u8 jumbf[1792];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "jumbf_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb, "c2pa.claim.bad",
                                              k_claim_bad,
                                              sizeof(k_claim_bad));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb,
                                               "c2pa.claim.good",
                                               k_claim_good,
                                               sizeof(k_claim_good));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_bad_jumb,
                 claim_bad_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_good_jumb,
                 claim_good_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_label_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_good, sizeof(k_claim_good));

    omc_store_fini(&store);
}

static void
test_jumbf_does_not_fallback_when_explicit_payload_ref_is_unresolved(void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_other[] = { 0xA1U, 0x61U, 'a', 0x77U };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 999);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_other,
                      sizeof(k_claim_other));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             1U);
    assert(find_jumbf_field(&store, "c2pa.signature.0.payload_bytes")
           == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_per_manifest_projection_fields(void)
{
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[640];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 2U);

    append_cbor_text(cbor_payload, &cbor_size, "manifest0");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "assertions");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");

    append_cbor_text(cbor_payload, &cbor_size, "manifest1");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "assertions");
    append_cbor_array(cbor_payload, &cbor_size, 2U);
    append_cbor_i64(cbor_payload, &cbor_size, 2);
    append_cbor_i64(cbor_payload, &cbor_size, 3);
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.manifest_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.active_manifest_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.assertion_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_linked_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_orphan_count"),
                             0U);

    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.manifest.0.prefix"),
        "box.0.1.cbor.manifests.manifest0");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.manifest.0.is_active"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.assertion_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_linked_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_orphan_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.cross_claim_link_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.explicit_reference_signature_count"),
                             0U);

    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.manifest.1.prefix"),
        "box.0.1.cbor.manifests.manifest1");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.manifest.1.is_active"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.assertion_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.signature_linked_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.signature_orphan_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.cross_claim_link_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.1.explicit_reference_signature_count"),
                             0U);

    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.active_manifest.prefix"), "");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_per_claim_projection_fields(void)
{
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[640];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "claim_generator");
    append_cbor_text(cbor_payload, &cbor_size, "OpenMeta");
    append_cbor_text(cbor_payload, &cbor_size, "assertions");
    append_cbor_array(cbor_payload, &cbor_size, 2U);
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_i64(cbor_payload, &cbor_size, 2);
    append_cbor_text(cbor_payload, &cbor_size, "signature");
    append_cbor_text(cbor_payload, &cbor_size, "sig");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim_generator");
    append_cbor_text(cbor_payload, &cbor_size, "Other");
    append_cbor_text(cbor_payload, &cbor_size, "assertions");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_i64(cbor_payload, &cbor_size, 3);

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.assertion_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_linked_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_orphan_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.manifest_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.active_manifest_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.active_manifest_present"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.active_manifest.prefix"),
        "box.0.1.cbor.manifests.active_manifest");
    assert(find_jumbf_field(&store, "c2pa.semantic.signature_key_hits")
           != (const omc_entry*)0);
    assert(find_jumbf_field(&store, "c2pa.semantic.signature_key_hits")
               ->value.u.u64
           >= 2U);

    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.manifest.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.manifest.0.is_active"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.assertion_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_linked_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.signature_orphan_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.cross_claim_link_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.explicit_reference_signature_count"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.assertion_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.assertion_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.signature_key_hits"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.signature_key_hits"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.assertion.0.key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.assertion.1.key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.assertion.0.key_hits"),
                             1U);

    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.claim.0.claim_generator"),
        "OpenMeta");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.claim.1.claim_generator"),
        "Other");

    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0]");
    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.claim.1.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");

    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.assertion.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].assertions[0]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.assertion.1.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].assertions[1]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.1.assertion.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1].assertions[0]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.signature.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].signatures[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.signature.0.key_hits"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.signature.0.algorithm"),
        "es256");
    assert_text_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.semantic.signature.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].signatures[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.key_hits"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store, "c2pa.semantic.signature.0.algorithm"),
        "es256");
    assert(find_jumbf_field(&store, "c2pa.semantic.claim.1.signature.0.prefix")
           == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_reference_linked_projection_fields(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x62U, 0x02U };
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[640];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload, &cbor_size, 1);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload, &cbor_size, 0);

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_linked_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature_orphan_count"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.reference_key_hits"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.cross_claim_link_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.referenced_by_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.referenced_by_signature_count"),
                             1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.reference_key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.reference_key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.linked_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.linked_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.cross_claim_link_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.cross_claim_link_count"),
                             1U);

    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.1.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_ingredient_projection_fields(void)
{
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[768];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "parent");
    append_cbor_text(cbor_payload, &cbor_size, "relationship");
    append_cbor_text(cbor_payload, &cbor_size, "parentOf");
    append_cbor_text(cbor_payload, &cbor_size, "thumbnailUrl");
    append_cbor_text(cbor_payload, &cbor_size, "https://example.test/parent");

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "component");
    append_cbor_text(cbor_payload, &cbor_size, "relationship");
    append_cbor_text(cbor_payload, &cbor_size, "componentOf");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 1U);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "leaf");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.active_manifest_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(&store,
                                              "c2pa.semantic.ingredient_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_key_hits"),
                             6U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_relationship."
                                 "parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_relationship."
                                 "componentOf_count"),
                             1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.ingredient_count"),
                             3U);
    assert_scalar_u64_equals(
        find_jumbf_field(&store,
                         "c2pa.semantic.manifest.0.ingredient_relationship_count"),
        2U);
    assert_scalar_u64_equals(
        find_jumbf_field(
            &store,
            "c2pa.semantic.manifest.0.ingredient_thumbnail_url_count"),
        1U);
    assert_scalar_u64_equals(
        find_jumbf_field(
            &store,
            "c2pa.semantic.manifest.0.ingredient_relationship_kind_count"),
        2U);
    assert_scalar_u64_equals(
        find_jumbf_field(
            &store,
            "c2pa.semantic.manifest.0.ingredient_relationship.parentOf_count"),
        1U);
    assert_scalar_u64_equals(
        find_jumbf_field(
            &store,
            "c2pa.semantic.manifest.0.ingredient_relationship."
            "componentOf_count"),
        1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.ingredient_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "ingredient_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "ingredient_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "ingredient_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "ingredient_relationship.parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "ingredient_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.ingredient_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1."
                                 "ingredient_relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1."
                                 "ingredient_thumbnail_url_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1."
                                 "ingredient_relationship_kind_count"),
                             0U);

    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].ingredients[0]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.1.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0].ingredients[1]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.1.ingredient.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1].ingredients[0]");

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.ingredient.0.key_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.ingredient.1.key_hits"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.ingredient.0.key_hits"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.0.title"),
        "parent");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.0.relationship"),
        "parentOf");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.0.thumbnail_url"),
        "https://example.test/parent");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.1.title"),
        "component");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.0.ingredient.1.relationship"),
        "componentOf");
    assert(find_jumbf_field(
               &store,
               "c2pa.semantic.claim.0.ingredient.1.thumbnail_url")
           == (const omc_entry*)0);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.claim.1.ingredient.0.title"),
        "leaf");
    assert(find_jumbf_field(
               &store,
               "c2pa.semantic.claim.1.ingredient.0.relationship")
           == (const omc_entry*)0);
    assert(find_jumbf_field(
               &store,
               "c2pa.semantic.claim.1.ingredient.0.thumbnail_url")
           == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_ingredient_signature_topology_fields(void)
{
    omc_u8 cbor_payload[1024];
    omc_u8 jumbf[1408];
    omc_size cbor_size;
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 3U);

    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "base");
    append_cbor_text(cbor_payload, &cbor_size, "thumbnailUrl");
    append_cbor_text(cbor_payload, &cbor_size, "https://example.test/base");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload, &cbor_size, 0);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "c");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_claim_with_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_claim_referenced_by_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_manifest_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.reference_key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_linked_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_linked_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_linked_direct_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_linked_cross_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_cross_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_linked_signature_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_thumbnail_url_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "direct_source_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "cross_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "mixed_source_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_source_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "mixed_source_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_title_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_thumbnail_url_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0.ingredient_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_direct_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_cross_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_cross_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "cross_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.referenced_by_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_direct_ingredient_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_cross_ingredient_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0.linked_ingredient_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_thumbnail_url_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_direct_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_cross_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1."
                                 "linked_ingredient_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.2."
                                 "linked_ingredient_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_claim_count"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_direct_ingredient_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_cross_ingredient_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_claim_count"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.1.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "reference_key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_ingredient_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_direct_ingredient_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_cross_ingredient_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.2."
                                 "linked_claim_count"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.2.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[2]");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.2."
                                 "linked_ingredient_claim_count"),
                             0U);

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
test_jumbf_emits_c2pa_ingredient_signature_relationship_kind_fields(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x62U, 0x02U };
    static const omc_u8 k_claim2[] = { 0xA1U, 0x61U, 0x63U, 0x03U };
    omc_u8 cbor_payload[1024];
    omc_u8 jumbf[1408];
    omc_size cbor_size;
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 3U);

    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 2U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "relationship");
    append_cbor_text(cbor_payload, &cbor_size, "parentOf");
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "Ingredient 0");
    append_cbor_text(cbor_payload, &cbor_size, "thumbnailUrl");
    append_cbor_text(cbor_payload, &cbor_size, "https://example.test/thumb0");
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "relationship");
    append_cbor_text(cbor_payload, &cbor_size, "componentOf");
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "Ingredient 1");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 0U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload, &cbor_size, 0);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim2, sizeof(k_claim2));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_relationship.parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "linked_ingredient_thumbnail_url_count"),
                             1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_ingredient_title_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_ingredient_relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_ingredient_relationship_kind_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "linked_ingredient_thumbnail_url_count"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_relationship.parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_direct_title_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_cross_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_direct_relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_cross_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_direct_thumbnail_url_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_cross_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_direct_title_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_cross_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_direct_relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_cross_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count"),
                             1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_relationship.parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_direct_title_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_cross_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_direct_relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_cross_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_direct_thumbnail_url_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_cross_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_direct_title_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_cross_title_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_direct_relationship_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_cross_relationship_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_direct_thumbnail_url_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_cross_thumbnail_url_count"),
                             1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_linked_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_unresolved_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_ambiguous_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_unresolved_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_ambiguous_count"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_relationship.parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_relationship_kind_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_relationship.parentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_unresolved_"
                                 "relationship_kind_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.0."
                                 "linked_ingredient_explicit_reference_ambiguous_"
                                 "relationship_kind_count"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_status_fields(void)
{
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2304];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 3U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 23);

    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "parent");
    append_cbor_text(cbor_payload, &cbor_size, "relationship");
    append_cbor_text(cbor_payload, &cbor_size, "componentOf");
    append_cbor_text(cbor_payload, &cbor_size, "thumbnailUrl");
    append_cbor_text(cbor_payload, &cbor_size, "https://example.test/parent");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "reference");
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "id");
    append_cbor_i64(cbor_payload, &cbor_size, 2);
    append_cbor_text(cbor_payload, &cbor_size, "uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.missing0");
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "c2pa.claim.missing0");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "c");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_text_entry_equals(&store, find_jumbf_field(&store, "box.0.label"),
                             "c2pa");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.reference_key_hits"),
                             5U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.ingredient_explicit_reference_ambiguous_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_unresolved_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_ambiguous_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_index_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "cross_source_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "ambiguous_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "ambiguous_direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "cross_source_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "relationship_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "relationship_kind_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_direct_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_relationship_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_relationship_kind_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_relationship.componentOf_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_claim_explicit_reference_"
                                 "ambiguous_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "direct_source_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "relationship_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_direct_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.manifest.0."
                                 "ingredient_linked_signature_explicit_reference_"
                                 "ambiguous_relationship.componentOf_count"),
                             1U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_unresolved"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.reference_key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_resolved_claim_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_ambiguous"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_index_hits"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.reference_key_hits"),
                             4U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.linked_ingredient_explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.linked_ingredient_explicit_reference_ambiguous_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.linked_ingredient_explicit_reference_ambiguous_title_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.linked_ingredient_explicit_reference_ambiguous_relationship_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1.linked_ingredient_explicit_reference_ambiguous_thumbnail_url_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1."
                                 "linked_ingredient_explicit_reference_ambiguous_relationship_kind_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.claim.1."
                                 "linked_ingredient_explicit_reference_ambiguous_relationship.componentOf_count"),
                             1U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_ingredient_explicit_reference_status_topology_fields(
    void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x62U, 0x02U };
    static const omc_u8 k_claim2[] = { 0xA1U, 0x61U, 0x63U, 0x03U };
    static const test_u64_field_expect k_expected[] = {
        { "c2pa.semantic.explicit_reference_signature_count", 2U },
        { "c2pa.semantic.explicit_reference_unresolved_signature_count", 1U },
        { "c2pa.semantic.explicit_reference_ambiguous_signature_count", 1U },
        { "c2pa.semantic.ingredient_signature_count", 2U },
        { "c2pa.semantic.ingredient_linked_claim_count", 1U },
        { "c2pa.semantic.ingredient_linked_claim_direct_source_count", 1U },
        { "c2pa.semantic.ingredient_linked_claim_cross_source_count", 0U },
        { "c2pa.semantic.ingredient_linked_claim_mixed_source_count", 0U },
        { "c2pa.semantic.ingredient_linked_claim_explicit_reference_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_direct_source_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_cross_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_mixed_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_unresolved_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_unresolved_"
          "direct_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_unresolved_"
          "cross_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_unresolved_"
          "mixed_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_ambiguous_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_ambiguous_"
          "direct_source_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_ambiguous_"
          "cross_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_claim_explicit_reference_ambiguous_"
          "mixed_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_explicit_reference_unresolved_signature_count",
          1U },
        { "c2pa.semantic."
          "ingredient_explicit_reference_ambiguous_signature_count",
          1U },
        { "c2pa.semantic.manifest.0.ingredient_signature_count", 2U },
        { "c2pa.semantic.manifest.0.ingredient_linked_claim_count", 1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_direct_source_count",
          1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_cross_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_mixed_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_count",
          1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_direct_source_count",
          1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_cross_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_mixed_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_unresolved_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_unresolved_"
          "direct_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_unresolved_"
          "cross_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_unresolved_"
          "mixed_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_ambiguous_count",
          1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_ambiguous_"
          "direct_source_count",
          1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_ambiguous_"
          "cross_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_linked_claim_explicit_reference_ambiguous_"
          "mixed_source_count",
          0U },
        { "c2pa.semantic.manifest.0."
          "ingredient_explicit_reference_unresolved_signature_count",
          1U },
        { "c2pa.semantic.manifest.0."
          "ingredient_explicit_reference_ambiguous_signature_count",
          1U },
        { "c2pa.semantic.claim.0.linked_ingredient_signature_count", 0U },
        { "c2pa.semantic.claim.1.linked_ingredient_signature_count", 1U },
        { "c2pa.semantic.claim.1."
          "linked_direct_ingredient_signature_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_cross_ingredient_signature_count",
          0U },
        { "c2pa.semantic.claim.1.linked_ingredient_title_count", 1U },
        { "c2pa.semantic.claim.1.linked_ingredient_relationship_count", 1U },
        { "c2pa.semantic.claim.1.linked_ingredient_thumbnail_url_count", 1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_signature_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_direct_signature_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_cross_signature_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_title_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_relationship_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_thumbnail_url_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_relationship_kind_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_relationship.componentOf_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_relationship_kind_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_relationship."
          "componentOf_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_signature_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_"
          "direct_signature_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_"
          "cross_signature_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_title_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_"
          "relationship_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_"
          "thumbnail_url_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_unresolved_"
          "relationship_kind_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_signature_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_"
          "direct_signature_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_"
          "cross_signature_count",
          0U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_title_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_"
          "relationship_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_"
          "thumbnail_url_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_"
          "relationship_kind_count",
          1U },
        { "c2pa.semantic.claim.1."
          "linked_ingredient_explicit_reference_ambiguous_"
          "relationship.componentOf_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_direct_claim_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_cross_claim_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_direct_source_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_cross_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_mixed_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_direct_title_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_cross_title_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_"
          "direct_relationship_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_"
          "cross_relationship_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_"
          "direct_thumbnail_url_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_"
          "cross_thumbnail_url_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_title_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "direct_claim_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "cross_claim_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "direct_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "cross_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "mixed_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "title_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "relationship_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "relationship_kind_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_unresolved_"
          "thumbnail_url_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "direct_claim_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "cross_claim_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "direct_source_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "cross_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "mixed_source_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "direct_title_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "cross_title_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "direct_relationship_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "cross_relationship_count",
          0U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "direct_thumbnail_url_count",
          1U },
        { "c2pa.semantic."
          "ingredient_linked_signature_explicit_reference_ambiguous_"
          "cross_thumbnail_url_count",
          0U }
    };
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2304];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 3U);

    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "base");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 999);

    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));
    append_cbor_text(cbor_payload, &cbor_size, "ingredients");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "title");
    append_cbor_text(cbor_payload, &cbor_size, "parent");
    append_cbor_text(cbor_payload, &cbor_size, "relationship");
    append_cbor_text(cbor_payload, &cbor_size, "componentOf");
    append_cbor_text(cbor_payload, &cbor_size, "thumbnailUrl");
    append_cbor_text(cbor_payload, &cbor_size, "https://example.test/parent");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "reference");
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "id");
    append_cbor_i64(cbor_payload, &cbor_size, 2);
    append_cbor_text(cbor_payload, &cbor_size, "uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.missing0");
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "c2pa.claim.missing0");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim2, sizeof(k_claim2));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_jumbf_u64_fields(&store, k_expected,
                            sizeof(k_expected) / sizeof(k_expected[0]));

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_multi_claim_prefixes(void)
{
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2304];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 4U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload, &cbor_size, 2);
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 3);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "c");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "d");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.reference_key_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_index_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_ambiguous"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.linked_claim_count"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "cross_claim_link_count"),
                             3U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.1.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[2]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.2.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[3]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_href_map_field(void)
{
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "references");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "href");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/media?claim-index=1");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_ambiguous"),
                             0U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_nested_claim_id_field(void)
{
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "reference");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload, &cbor_size, 1);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.linked_claim_count"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_link_map_field(void)
{
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "references");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "link");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/media?claim-index=1");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_ambiguous"),
                             0U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_multi_signature_claim_ref_id_deterministic(void)
{
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2304];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 3U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "c2pa.claim.missing0");
    append_cbor_text(cbor_payload, &cbor_size, "claim_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.missing0");

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "reference");
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload, &cbor_size, 0);
    append_cbor_text(cbor_payload, &cbor_size, "uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.missing1");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "c");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             2U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "explicit_reference_unresolved"),
                             0U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.1.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[0]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_multi_signature_claim_ref_id_unresolved_no_fallback(void)
{
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2304];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 3U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 999);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "reference");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim_id");
    append_cbor_i64(cbor_payload, &cbor_size, 888);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "c");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             2U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "explicit_reference_resolved_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "explicit_reference_unresolved"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.linked_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1.linked_claim_count"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_scoped_prefix_does_not_cross_match_index10(void)
{
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2560];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;
    omc_u32 i;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 11U);
    for (i = 0U; i < 11U; ++i) {
        if (i == 10U) {
            append_cbor_map(cbor_payload, &cbor_size, 2U);
            append_cbor_text(cbor_payload, &cbor_size, "alg");
            append_cbor_text(cbor_payload, &cbor_size, "es256");
            append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
            append_cbor_i64(cbor_payload, &cbor_size, 1);
        } else {
            append_cbor_map(cbor_payload, &cbor_size, 1U);
            append_cbor_text(cbor_payload, &cbor_size, "alg");
            append_cbor_text(cbor_payload, &cbor_size, "es256");
        }
    }

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.semantic.signature_count"),
                             11U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "explicit_reference_present"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.1."
                                 "explicit_reference_resolved_claim_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.10."
                                 "explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.10."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_text_entry_equals(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.10.linked_claim.0.prefix"),
        "box.0.1.cbor.manifests.active_manifest.claims[1]");

    omc_store_fini(&store);
}

static void
test_jumbf_plain_signature_url_does_not_trigger_explicit_reference(void)
{
    omc_u8 cbor_payload[768];
    omc_size cbor_size;
    omc_u8 jumbf[1024];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 1U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "url");
    append_cbor_text(cbor_payload, &cbor_size, "https://example.test/no-ref");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.reference_key_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_present"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_nested_map_index_label_href_conflict_is_ambiguous(void)
{
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "references");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "index");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "claims[0]");
    append_cbor_text(cbor_payload, &cbor_size, "href");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/a?claim-index=1");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_ambiguous"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_nested_map_index_label_href_consistent(void)
{
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "a");
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "references");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "index");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/c?claim-index=1");
    append_cbor_text(cbor_payload, &cbor_size, "href");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/b?claim-index=1");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_text(cbor_payload, &cbor_size, "b");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_ambiguous"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_from_jumbf_uri_key(void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 0x61U, 0x2AU };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 cbor_box[1152];
    omc_size cbor_box_size;
    omc_u8 claim_bad_jumb[128];
    omc_size claim_bad_jumb_size;
    omc_u8 claim_good_jumb[128];
    omc_size claim_good_jumb_size;
    omc_u8 root_payload[1536];
    omc_size root_payload_size;
    omc_u8 jumbf[1792];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "jumbf_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb, "c2pa.claim.bad",
                                              k_claim_bad,
                                              sizeof(k_claim_bad));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb,
                                               "c2pa.claim.good",
                                               k_claim_good,
                                               sizeof(k_claim_good));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_bad_jumb,
                 claim_bad_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_good_jumb,
                 claim_good_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.reference_key_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_index_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_label_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.linked_claim_count"),
                             1U);
    assert_text_entry_contains(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "claims[1]");

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_mixed_index_label_uri_deterministic(void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim_one[] = { 0xA1U, 0x61U, 0x61U, 0x11U };
    static const omc_u8 k_claim_two[] = { 0xA1U, 0x61U, 0x61U, 0x22U };
    static const omc_u8 k_claim_three[] = { 0xA1U, 0x61U, 0x61U, 0x33U };
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 cbor_box[2176];
    omc_size cbor_box_size;
    omc_u8 claim_one_jumb[128];
    omc_size claim_one_jumb_size;
    omc_u8 claim_two_jumb[128];
    omc_size claim_two_jumb_size;
    omc_u8 claim_three_jumb[128];
    omc_size claim_three_jumb_size;
    omc_u8 root_payload[2560];
    omc_size root_payload_size;
    omc_u8 jumbf[2816];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 4U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "c2pa.claim.two");
    append_cbor_text(cbor_payload, &cbor_size, "claim_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.three");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_one,
                      sizeof(k_claim_one));

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_two,
                      sizeof(k_claim_two));

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_three,
                      sizeof(k_claim_three));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_one_jumb_size = make_claim_jumb_box(claim_one_jumb, "c2pa.claim.one",
                                              k_claim_one,
                                              sizeof(k_claim_one));
    claim_two_jumb_size = make_claim_jumb_box(claim_two_jumb, "c2pa.claim.two",
                                              k_claim_two,
                                              sizeof(k_claim_two));
    claim_three_jumb_size = make_claim_jumb_box(claim_three_jumb,
                                                "c2pa.claim.three",
                                                k_claim_three,
                                                sizeof(k_claim_three));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_one_jumb,
                 claim_one_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_two_jumb,
                 claim_two_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_three_jumb,
                 claim_three_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.reference_key_hits"),
                             3U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_unresolved"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.linked_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.cross_claim_link_count"),
                             1U);
    assert_text_entry_contains(
        &store,
        find_jumbf_field(&store,
                         "c2pa.semantic.signature.0.linked_claim.0.prefix"),
        "claims[1]");
    assert(find_jumbf_field(&store,
                            "c2pa.semantic.signature.0.linked_claim.1.prefix")
           == (const omc_entry*)0);
    assert(find_jumbf_field(&store,
                            "c2pa.semantic.signature.0.linked_claim.2.prefix")
           == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_index_label_uri_conflict_is_ambiguous(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x61U, 0x02U };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 cbor_box[1152];
    omc_size cbor_box_size;
    omc_u8 claim_one_jumb[128];
    omc_size claim_one_jumb_size;
    omc_u8 claim_two_jumb[128];
    omc_size claim_two_jumb_size;
    omc_u8 root_payload[1536];
    omc_size root_payload_size;
    omc_u8 jumbf[1792];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "reference_index");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/a?jumbf=c2pa.claim.one");
    append_cbor_text(cbor_payload, &cbor_size, "claim_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/a?jumbf=c2pa.claim.two");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_one_jumb_size = make_claim_jumb_box(claim_one_jumb, "c2pa.claim.one",
                                              k_claim0, sizeof(k_claim0));
    claim_two_jumb_size = make_claim_jumb_box(claim_two_jumb, "c2pa.claim.two",
                                              k_claim1, sizeof(k_claim1));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_one_jumb,
                 claim_one_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_two_jumb,
                 claim_two_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_index_hits"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_label_hits"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_present"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_unresolved"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_emits_c2pa_explicit_reference_index_label_uri_consistent_references_collapse(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x61U, 0x02U };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 cbor_box[1152];
    omc_size cbor_box_size;
    omc_u8 claim_one_jumb[128];
    omc_size claim_one_jumb_size;
    omc_u8 claim_two_jumb[128];
    omc_size claim_two_jumb_size;
    omc_u8 root_payload[1536];
    omc_size root_payload_size;
    omc_u8 jumbf[1792];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "reference_index");
    append_cbor_i64(cbor_payload, &cbor_size, 1);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/b?jumbf=c2pa.claim.two");
    append_cbor_text(cbor_payload, &cbor_size, "claim_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/c?jumbf=c2pa.claim.two");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_one_jumb_size = make_claim_jumb_box(claim_one_jumb, "c2pa.claim.one",
                                              k_claim0, sizeof(k_claim0));
    claim_two_jumb_size = make_claim_jumb_box(claim_two_jumb, "c2pa.claim.two",
                                              k_claim1, sizeof(k_claim1));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_one_jumb,
                 claim_one_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_two_jumb,
                 claim_two_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);

    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_signature_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_ambiguous_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0.explicit_reference_ambiguous"),
                             0U);

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
test_jumbf_verify_scaffold_not_requested_by_default(void)
{
    omc_store store;
    omc_jumbf_res res;

    omc_store_init(&store);
    res = omc_jumbf_dec(k_sample_jumbf_box, sizeof(k_sample_jumbf_box), &store,
                        0U, OMC_ENTRY_FLAG_NONE,
                        (const omc_jumbf_opts*)0);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_NOT_REQUESTED);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "not_requested");
    assert_scalar_u8_equals(find_jumbf_field(&store, "c2pa.verify.requested"),
                            0U);
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.enabled_in_build"),
                            0U);
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            0U);
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.backend_requested"),
                             "auto");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.backend_selected"),
                             "none");

    omc_store_fini(&store);
}

static void
test_jumbf_verify_scaffold_requested(void)
{
    omc_u8 cbor_payload[256];
    omc_size cbor_size;
    omc_u8 jumbf[512];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "signature");
    append_cbor_text(cbor_payload, &cbor_size, "ok");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store, "c2pa.verify.requested"),
                            1U);
    assert_scalar_u8_equals(find_jumbf_field(
                                &store,
                                "c2pa.verify.require_resolved_references"),
                            0U);
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.enabled_in_build"),
                            0U);
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.backend_requested"),
                             "auto");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.backend_selected"),
                             "none");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.profile_status"),
                             "not_checked");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.profile_reason"),
                             "not_checked");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.chain_status"),
                             "not_checked");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.chain_reason"),
                             "not_checked");

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_with_detached_payload_resolution(void)
{
    static const omc_u8 k_claim_bytes[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[768];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bytes,
                      sizeof(k_claim_bytes));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store, "c2pa.verify.requested"),
                            1U);
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_bytes, sizeof(k_claim_bytes));

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_with_explicit_claim_ref_detached_payload_resolution(
    void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x44U };
    omc_u8 cbor_payload[768];
    omc_size cbor_size;
    omc_u8 jumbf[1024];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 1);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_resolved_claim_count"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_good, sizeof(k_claim_good));

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_with_explicit_label_detached_payload_resolution(
    void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 cbor_box[1152];
    omc_size cbor_box_size;
    omc_u8 claim_bad_jumb[128];
    omc_size claim_bad_jumb_size;
    omc_u8 claim_good_jumb[128];
    omc_size claim_good_jumb_size;
    omc_u8 root_payload[1536];
    omc_size root_payload_size;
    omc_u8 jumbf[1792];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "jumbf_uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.good");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb, "c2pa.claim.bad",
                                              k_claim_bad,
                                              sizeof(k_claim_bad));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb,
                                               "c2pa.claim.good",
                                               k_claim_good,
                                               sizeof(k_claim_good));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_bad_jumb,
                 claim_bad_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_good_jumb,
                 claim_good_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_good, sizeof(k_claim_good));

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_unresolved_detached_payload_ref_skips_fallback(
    void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_other[] = { 0xA1U, 0x61U, 'a', 0x77U };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 999);

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_other,
                      sizeof(k_claim_other));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             1U);
    assert(find_jumbf_field(&store, "c2pa.signature.0.payload_bytes")
           == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_hyphen_reference_unresolved_skips_fallback(void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_other[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 jumbf[1280];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "claim-ref-index");
    append_cbor_i64(cbor_payload, &cbor_size, 999);
    append_cbor_text(cbor_payload, &cbor_size, "claim-reference");
    append_cbor_text(cbor_payload, &cbor_size, "c2pa.claim.missing");
    append_cbor_text(cbor_payload, &cbor_size, "claim-uri");
    append_cbor_text(cbor_payload, &cbor_size,
                     "https://example.test/asset?jumbf=c2pa.claim.missing");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_other,
                      sizeof(k_claim_other));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.semantic.signature.0."
                                 "explicit_reference_unresolved"),
                             1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             1U);
    assert(find_jumbf_field(&store, "c2pa.signature.0.payload_bytes")
           == (const omc_entry*)0);

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_percent_encoded_claim_ref_detached_payload_resolution(
    void)
{
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[2048];
    omc_size cbor_size;
    omc_u8 jumbf[2560];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;
    omc_u32 i;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 12U);

    for (i = 0U; i < 11U; ++i) {
        omc_u8 claim_value[4];

        claim_value[0] = 0xA1U;
        claim_value[1] = 0x61U;
        claim_value[2] = (omc_u8)'a';
        claim_value[3] = (omc_u8)(i + 1U);

        append_cbor_map(cbor_payload, &cbor_size, (i == 0U) ? 2U : 1U);
        append_cbor_text(cbor_payload, &cbor_size, "claim");
        append_cbor_bytes(cbor_payload, &cbor_size, claim_value,
                          sizeof(claim_value));
        if (i == 0U) {
            append_cbor_text(cbor_payload, &cbor_size, "signatures");
            append_cbor_array(cbor_payload, &cbor_size, 1U);
            append_cbor_map(cbor_payload, &cbor_size, 3U);
            append_cbor_text(cbor_payload, &cbor_size, "alg");
            append_cbor_text(cbor_payload, &cbor_size, "es256");
            append_cbor_text(cbor_payload, &cbor_size, "payload");
            append_cbor_text(cbor_payload, &cbor_size, "null");
            append_cbor_text(cbor_payload, &cbor_size, "claim_ref");
            append_cbor_text(
                cbor_payload, &cbor_size,
                "https://example.test/media/%63%6C%61%69%6D%73%5B11%5D");
        }
    }

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_good, sizeof(k_claim_good));

    omc_store_fini(&store);
}

static void
test_jumbf_verify_requested_percent_encoded_jumbf_label_detached_payload_resolution(
    void)
{
    static const omc_u8 k_claim_bad[] = { 0xA1U, 0x61U, 'a', 0x01U };
    static const omc_u8 k_claim_good[] = { 0xA1U, 0x61U, 'a', 0x2AU };
    omc_u8 cbor_payload[1024];
    omc_size cbor_size;
    omc_u8 cbor_box[1152];
    omc_size cbor_box_size;
    omc_u8 claim_bad_jumb[128];
    omc_size claim_bad_jumb_size;
    omc_u8 claim_good_jumb[128];
    omc_size claim_good_jumb_size;
    omc_u8 root_payload[1536];
    omc_size root_payload_size;
    omc_u8 jumbf[1792];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_bad,
                      sizeof(k_claim_bad));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 4U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "payload");
    append_cbor_text(cbor_payload, &cbor_size, "null");
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "c2pa.claim.bad");
    append_cbor_text(cbor_payload, &cbor_size, "claim_uri");
    append_cbor_text(
        cbor_payload, &cbor_size,
        "https://example.test/asset?jumbf=%63%32%70%61%2E%63%6C%61%69%6D%2E%67%6F%6F%64");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim_good,
                      sizeof(k_claim_good));

    cbor_box_size = 0U;
    append_box(cbor_box, &cbor_box_size, "cbor", cbor_payload, cbor_size);
    claim_bad_jumb_size = make_claim_jumb_box(claim_bad_jumb, "c2pa.claim.bad",
                                              k_claim_bad,
                                              sizeof(k_claim_bad));
    claim_good_jumb_size = make_claim_jumb_box(claim_good_jumb,
                                               "c2pa.claim.good",
                                               k_claim_good,
                                               sizeof(k_claim_good));

    root_payload_size = 0U;
    append_bytes(root_payload, &root_payload_size, claim_bad_jumb,
                 claim_bad_jumb_size);
    append_bytes(root_payload, &root_payload_size, claim_good_jumb,
                 claim_good_jumb_size);
    append_bytes(root_payload, &root_payload_size, cbor_box, cbor_box_size);

    jumbf_size = make_jumb_box_with_label(jumbf, "c2pa", root_payload,
                                          root_payload_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_text_entry_equals(&store,
                             find_jumbf_field(&store, "c2pa.verify.status"),
                             "disabled_by_build");
    assert_scalar_u8_equals(find_jumbf_field(&store,
                                             "c2pa.verify.signatures_present"),
                            1U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store, "c2pa.signature.0.payload_is_null"),
                             0U);
    assert_bytes_entry_equals(
        &store, find_jumbf_field(&store, "c2pa.signature.0.payload_bytes"),
        k_claim_good, sizeof(k_claim_good));

    omc_store_fini(&store);
}

static void
test_jumbf_verify_require_resolved_references_unresolved_disabled_by_build(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x61U, 0x02U };
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[768];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 99);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 0);

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;
    opts.verify_backend = OMC_C2PA_VERIFY_BACKEND_OPENSSL;
    opts.verify_require_resolved_references = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_scalar_u8_equals(find_jumbf_field(
                                &store,
                                "c2pa.verify.require_resolved_references"),
                            1U);
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.profile_status"),
                             "not_checked");
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.profile_reason"),
                             "not_checked");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.verify.explicit_reference_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.verify.explicit_reference_unresolved_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.verify.explicit_reference_ambiguous_signature_count"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_verify_require_resolved_references_ambiguous_disabled_by_build(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x61U, 0x02U };
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[768];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 3U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 0);
    append_cbor_text(cbor_payload, &cbor_size, "claim_reference");
    append_cbor_text(cbor_payload, &cbor_size, "claims[1]");

    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;
    opts.verify_backend = OMC_C2PA_VERIFY_BACKEND_OPENSSL;
    opts.verify_require_resolved_references = 1;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_scalar_u8_equals(find_jumbf_field(
                                &store,
                                "c2pa.verify.require_resolved_references"),
                            1U);
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.profile_reason"),
                             "not_checked");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.verify.explicit_reference_signature_count"),
                             0U);
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.verify.explicit_reference_ambiguous_signature_count"),
                             0U);

    omc_store_fini(&store);
}

static void
test_jumbf_verify_require_resolved_references_disabled_policy_does_not_fail(void)
{
    static const omc_u8 k_claim0[] = { 0xA1U, 0x61U, 0x61U, 0x01U };
    static const omc_u8 k_claim1[] = { 0xA1U, 0x61U, 0x61U, 0x02U };
    omc_u8 cbor_payload[512];
    omc_size cbor_size;
    omc_u8 jumbf[768];
    omc_size jumbf_size;
    omc_store store;
    omc_jumbf_opts opts;
    omc_jumbf_res res;

    cbor_size = 0U;
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "manifests");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "active_manifest");
    append_cbor_map(cbor_payload, &cbor_size, 1U);
    append_cbor_text(cbor_payload, &cbor_size, "claims");
    append_cbor_array(cbor_payload, &cbor_size, 2U);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim0, sizeof(k_claim0));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 99);

    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "claim");
    append_cbor_bytes(cbor_payload, &cbor_size, k_claim1, sizeof(k_claim1));
    append_cbor_text(cbor_payload, &cbor_size, "signatures");
    append_cbor_array(cbor_payload, &cbor_size, 1U);
    append_cbor_map(cbor_payload, &cbor_size, 2U);
    append_cbor_text(cbor_payload, &cbor_size, "alg");
    append_cbor_text(cbor_payload, &cbor_size, "es256");
    append_cbor_text(cbor_payload, &cbor_size, "claim_ref_id");
    append_cbor_i64(cbor_payload, &cbor_size, 0);

    jumbf_size = make_jumbf_with_cbor(jumbf, cbor_payload, cbor_size);
    omc_store_init(&store);
    omc_jumbf_opts_init(&opts);
    opts.verify_c2pa = 1;
    opts.verify_backend = OMC_C2PA_VERIFY_BACKEND_OPENSSL;
    opts.verify_require_resolved_references = 0;

    res = omc_jumbf_dec(jumbf, jumbf_size, &store, 0U, OMC_ENTRY_FLAG_NONE,
                        &opts);
    assert(res.status == OMC_JUMBF_OK);
    assert(res.verify_status == OMC_C2PA_VERIFY_DISABLED_BY_BUILD);
    assert(res.verify_backend_selected == OMC_C2PA_VERIFY_BACKEND_NONE);

    assert_scalar_u8_equals(find_jumbf_field(
                                &store,
                                "c2pa.verify.require_resolved_references"),
                            0U);
    assert_text_entry_equals(&store,
                             find_jumbf_field(&store,
                                              "c2pa.verify.profile_reason"),
                             "not_checked");
    assert_scalar_u64_equals(find_jumbf_field(
                                 &store,
                                 "c2pa.verify.explicit_reference_unresolved_signature_count"),
                             0U);

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
    test_jumbf_emits_c2pa_manifest_claim_signature_projection();
    test_jumbf_emits_c2pa_per_manifest_projection_fields();
    test_jumbf_emits_c2pa_per_claim_projection_fields();
    test_jumbf_emits_c2pa_reference_linked_projection_fields();
    test_jumbf_emits_c2pa_ingredient_projection_fields();
    test_jumbf_emits_c2pa_ingredient_signature_topology_fields();
    test_jumbf_emits_c2pa_ingredient_signature_relationship_kind_fields();
    test_jumbf_emits_c2pa_explicit_reference_status_fields();
    test_jumbf_emits_c2pa_ingredient_explicit_reference_status_topology_fields();
    test_jumbf_emits_c2pa_explicit_reference_multi_claim_prefixes();
    test_jumbf_emits_c2pa_explicit_reference_href_map_field();
    test_jumbf_emits_c2pa_explicit_reference_nested_claim_id_field();
    test_jumbf_emits_c2pa_explicit_reference_link_map_field();
    test_jumbf_emits_c2pa_explicit_reference_multi_signature_claim_ref_id_deterministic();
    test_jumbf_emits_c2pa_explicit_reference_multi_signature_claim_ref_id_unresolved_no_fallback();
    test_jumbf_emits_c2pa_explicit_reference_scoped_prefix_does_not_cross_match_index10();
    test_jumbf_plain_signature_url_does_not_trigger_explicit_reference();
    test_jumbf_emits_c2pa_explicit_reference_nested_map_index_label_href_conflict_is_ambiguous();
    test_jumbf_emits_c2pa_explicit_reference_nested_map_index_label_href_consistent();
    test_jumbf_emits_c2pa_explicit_reference_from_jumbf_uri_key();
    test_jumbf_emits_c2pa_explicit_reference_mixed_index_label_uri_deterministic();
    test_jumbf_emits_c2pa_explicit_reference_index_label_uri_conflict_is_ambiguous();
    test_jumbf_emits_c2pa_explicit_reference_index_label_uri_consistent_references_collapse();
    test_jumbf_extracts_cose_signature_fields();
    test_jumbf_resolves_detached_payload_from_direct_claim();
    test_jumbf_resolves_detached_payload_from_explicit_claim_ref();
    test_jumbf_resolves_detached_payload_from_explicit_label();
    test_jumbf_does_not_fallback_when_explicit_payload_ref_is_unresolved();
    test_jumbf_limit_entries();
    test_jumbf_cbor_half_and_simple_scalars();
    test_jumbf_cbor_indefinite_and_paths();
    test_jumbf_verify_scaffold_not_requested_by_default();
    test_jumbf_verify_scaffold_requested();
    test_jumbf_verify_requested_with_detached_payload_resolution();
    test_jumbf_verify_requested_with_explicit_claim_ref_detached_payload_resolution();
    test_jumbf_verify_requested_with_explicit_label_detached_payload_resolution();
    test_jumbf_verify_requested_unresolved_detached_payload_ref_skips_fallback();
    test_jumbf_verify_requested_hyphen_reference_unresolved_skips_fallback();
    test_jumbf_verify_requested_percent_encoded_claim_ref_detached_payload_resolution();
    test_jumbf_verify_requested_percent_encoded_jumbf_label_detached_payload_resolution();
    test_jumbf_verify_require_resolved_references_unresolved_disabled_by_build();
    test_jumbf_verify_require_resolved_references_ambiguous_disabled_by_build();
    test_jumbf_verify_require_resolved_references_disabled_policy_does_not_fail();
    test_jumbf_cbor_composite_key_fallback();
    return 0;
}
