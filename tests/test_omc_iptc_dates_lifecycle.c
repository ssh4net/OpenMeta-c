#ifdef NDEBUG
#undef NDEBUG
#endif
#include "omc/omc_translation.h"
#include "omc/omc_iptc_serialize.h"
#include "omc/omc_read.h"
#include <assert.h>
#include <string.h>

static omc_byte_ref
append(omc_store *s, const char *text)
{
    omc_byte_ref ref;
    assert(omc_arena_append(&s->arena, text, strlen(text), &ref) == OMC_STATUS_OK);
    return ref;
}

static void
native(omc_store *s, omc_u16 dataset, const char *text)
{
    omc_entry e;
    memset(&e, 0, sizeof(e));
    omc_key_make_iptc_dataset(&e.key, 2U, dataset);
    omc_val_make_bytes(&e.value, append(s, text));
    e.origin.block = OMC_INVALID_BLOCK_ID;
    e.origin.order_in_block = dataset;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}

static int
matches(const omc_store *s, omc_byte_ref ref, const char *text)
{
    omc_const_bytes v;
    v = omc_arena_view(&s->arena, ref);
    return v.size == strlen(text) && memcmp(v.data, text, v.size) == 0;
}

static void
assert_xmp(const omc_store *s, unsigned int group, const char *expected)
{
    static const char *const ns[] = {"http://ns.adobe.com/photoshop/1.0/", "http://ns.adobe.com/xap/1.0/"};
    static const char *const path[] = {"DateCreated", "CreateDate"};
    omc_size i;
    unsigned int count;
    count = 0U;
    for (i = 0U; i < s->entry_count; ++i) {
        const omc_entry *e;
        e = &s->entries[i];
        if (e->key.kind == OMC_KEY_XMP_PROPERTY && !(e->flags & OMC_ENTRY_FLAG_DELETED) &&
            matches(s, e->key.u.xmp_property.schema_ns, ns[group]) &&
            matches(s, e->key.u.xmp_property.property_path, path[group])) {
            assert(e->value.kind == OMC_VAL_TEXT && matches(s, e->value.u.ref, expected));
            ++count;
        }
    }
    assert(count == 1U);
}

static void
assert_iim(const omc_store *s, omc_u16 date_dataset, const char *date, const char *time)
{
    omc_u8 wire[96];
    omc_u8 expected[96];
    omc_size n;
    omc_size size;
    omc_mut_bytes out;
    omc_iptc_iim_serialize_res result;
    n = 0U;
    size = strlen(date);
    expected[n++] = 0x1CU; expected[n++] = 2U; expected[n++] = (omc_u8)date_dataset;
    expected[n++] = 0U; expected[n++] = (omc_u8)size;
    memcpy(expected + n, date, size); n += size;
    if (time != NULL) {
        size = strlen(time);
        expected[n++] = 0x1CU; expected[n++] = 2U;
        expected[n++] = (omc_u8)(date_dataset == 55U ? 60U : 63U);
        expected[n++] = 0U; expected[n++] = (omc_u8)size;
        memcpy(expected + n, time, size); n += size;
    }
    out.data = wire; out.size = sizeof(wire);
    result = omc_serialize_iptc_iim(s, out, NULL);
    assert(result.status == OMC_IPTC_IIM_SERIALIZE_OK);
    assert(result.written == n && memcmp(wire, expected, n) == 0);
}

static void
jpeg_readback(const omc_store *source, unsigned int group, const char *text)
{
    static const omc_u8 jpeg[] = {0xFFU, 0xD8U, 0xFFU, 0xD9U};
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;
    omc_arena output;
    omc_arena sidecar;
    omc_store reread;
    omc_read_res read_result;
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[16];
    omc_u8 payload[16384];
    omc_u32 scratch[32];
    omc_arena_init(&output); omc_arena_init(&sidecar); omc_store_init(&reread);
    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    opts.embedded.packet.include_existing_xmp = 1;
    opts.embedded.packet.include_exif = 0;
    opts.embedded.packet.include_iptc = 0;
    assert(omc_transfer_prepare(jpeg, sizeof(jpeg), source, &opts, &bundle) == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(omc_transfer_execute(jpeg, sizeof(jpeg), source, &output, &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    read_result = omc_read_simple(output.data, output.size, &reread, blocks, 32U,
                                  ifds, 16U, payload, sizeof(payload), scratch, 32U, NULL);
    assert(read_result.xmp.status == OMC_XMP_OK);
    assert_xmp(&reread, group, text);
    omc_store_fini(&reread); omc_arena_fini(&sidecar); omc_arena_fini(&output);
}

static void
run_case(unsigned int group, const char *date, const char *time, const char *text, int partial)
{
    omc_store source;
    omc_store translated;
    omc_store decoded;
    omc_store reversed;
    omc_arena xml;
    omc_iptc_date_to_xmp_translation_res result;
    omc_xmp_portable_opts dump_opts;
    omc_xmp_dump_res dump;
    omc_xmp_res parsed;
    omc_translation_opts reverse_opts;
    omc_translation_res reverse_result;
    omc_u16 date_dataset;
    omc_u8 *saved_arena;
    omc_entry *saved_entries;
    omc_size saved_size;
    omc_size saved_count;
    date_dataset = (omc_u16)(group ? 62U : 55U);
    omc_store_init(&source); omc_store_init(&translated);
    omc_store_init(&decoded); omc_store_init(&reversed); omc_arena_init(&xml);
    native(&source, date_dataset, date);
    if (time != NULL) native(&source, (omc_u16)(group ? 63U : 60U), time);
    result = omc_translate_iptc_dates_to_xmp(&source, &translated, NULL);
    assert(result.status == OMC_IPTC_DATE_TO_XMP_TRANSLATION_OK && result.entries_added == 1U);
    omc_xmp_portable_opts_init(&dump_opts);
    dump_opts.include_existing_xmp = 1; dump_opts.include_exif = 0; dump_opts.include_iptc = 0;
    assert(omc_xmp_dump_portable_arena(&translated, &xml, &dump_opts, &dump) == OMC_STATUS_OK);
    assert(dump.status == OMC_XMP_DUMP_OK);
    parsed = omc_xmp_dec(xml.data, xml.size, &decoded, OMC_INVALID_BLOCK_ID, OMC_ENTRY_FLAG_DIRTY, NULL);
    assert(parsed.status == OMC_XMP_OK);
    assert_xmp(&decoded, group, text);
    jpeg_readback(&translated, group, text);
    omc_translation_opts_init(&reverse_opts);
    reverse_opts.all_sources = 1; reverse_opts.conflict = OMC_TRANSLATION_REPLACE;
    reverse_opts.mappings = group ? OMC_TRANSLATE_CREATE_IPTC : OMC_TRANSLATE_DATE_CREATED;
    native(&reversed, 5U, "sentinel");
    saved_arena = reversed.arena.data; saved_entries = reversed.entries;
    saved_size = reversed.arena.size; saved_count = reversed.entry_count;
    reverse_result = omc_translate_xmp(&decoded, &reversed, &reverse_opts, NULL);
    if (partial) {
        assert(reverse_result.status == OMC_TRANSLATION_INVALID_SOURCE);
        assert(reversed.arena.data == saved_arena && reversed.entries == saved_entries);
        assert(reversed.arena.size == saved_size && reversed.entry_count == saved_count);
        assert(matches(&reversed, reversed.entries[0].value.u.ref, "sentinel"));
    } else {
        assert(reverse_result.status == OMC_TRANSLATION_OK);
        assert_iim(&reversed, date_dataset, date, time);
    }
    omc_store_fini(&reversed); omc_store_fini(&decoded); omc_store_fini(&translated);
    omc_store_fini(&source); omc_arena_fini(&xml);
}

int main(void)
{
    unsigned int group;
    for (group = 0U; group < 2U; ++group) {
        run_case(group, "20260000", NULL, "2026", 1);
        run_case(group, "20261000", NULL, "2026-10", 1);
        run_case(group, "20240229", NULL, "2024-02-29", 0);
        run_case(group, "20261016", "123456", "2026-10-16T12:34:56", 0);
        run_case(group, "20261016", "123456-0000", "2026-10-16T12:34:56-00:00", 0);
        run_case(group, "20261016", "123456+0530", "2026-10-16T12:34:56+05:30", 0);
    }
    return 0;
}
