#include "omc/omc_transfer.h"
#include "omc/omc_transfer_package.h"
#include "omc/omc_read.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void append(omc_arena* a, const void* p, omc_size n)
{
    omc_byte_ref ref;
    assert(omc_arena_append(a, p, n, &ref) == OMC_STATUS_OK);
}
static void u16(omc_arena* a, omc_u16 n)
{
    omc_u8 b[2]; b[0] = (omc_u8)(n >> 8); b[1] = (omc_u8)n; append(a, b, 2U);
}
static void resource(omc_arena* a, omc_u16 id, omc_size size, omc_u8 value)
{
    omc_u8 b[4];
    omc_u8* bytes;
    append(a, "8BIM", 4U); u16(a, id);
    /* Even name length needs a pad; nonzero padding must survive unchanged. */
    append(a, "\002Nm\243", 4U);
    b[0] = (omc_u8)(size >> 24); b[1] = (omc_u8)(size >> 16);
    b[2] = (omc_u8)(size >> 8); b[3] = (omc_u8)size; append(a, b, 4U);
    bytes = (omc_u8*)malloc(size == 0U ? 1U : size); assert(bytes != NULL);
    memset(bytes, value, size); append(a, bytes, size); free(bytes);
    if ((size & 1U) != 0U) append(a, "\265", 1U);
}
static void segment(omc_arena* a, const omc_arena* resources)
{
    append(a, "\377\355", 2U); u16(a, (omc_u16)(16U + resources->size));
    append(a, "Photoshop 3.0\0", 14U); append(a, resources->data, resources->size);
}
static omc_size find_bytes(const omc_arena* a, const omc_arena* needle)
{
    omc_size i;
    assert(needle->size <= a->size);
    for (i = 0U; i <= a->size - needle->size; ++i)
        if (memcmp(a->data + i, needle->data, needle->size) == 0) return i;
    assert(0); return 0U;
}
static void source_init(omc_store* s, int with_iptc)
{
    omc_entry e;
    omc_byte_ref ref, ns, path;
    omc_store_init(s); memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&s->arena, "http://ns.adobe.com/xap/1.0/",
        strlen("http://ns.adobe.com/xap/1.0/"), &ns) == OMC_STATUS_OK);
    assert(omc_arena_append(&s->arena, "CreatorTool", 11U, &path) == OMC_STATUS_OK);
    omc_key_make_xmp_property(&e.key, ns, path);
    assert(omc_arena_append(&s->arena, "IPTC test", 9U, &ref) == OMC_STATUS_OK);
    omc_val_make_text(&e.value, ref, OMC_TEXT_UTF8);
    e.flags = OMC_ENTRY_FLAG_DIRTY; e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
    if (with_iptc) {
        omc_key_make_iptc_dataset(&e.key, 2U, 5U);
        assert(omc_arena_append(&s->arena, "NEW", 3U, &ref) == OMC_STATUS_OK);
        omc_val_make_bytes(&e.value, ref);
        assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
    }
}
static omc_transfer_status execute(const omc_u8* bytes, omc_size size,
                                   const omc_store* source, omc_arena* out,
                                   omc_arena* sidecar, omc_transfer_res* res)
{
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG; opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_ONLY;
    assert(omc_transfer_prepare(bytes, size, source, &opts, &bundle) == OMC_STATUS_OK);
    if (bundle.status != OMC_TRANSFER_OK) return bundle.status;
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(omc_transfer_execute(bytes, size, source, out, sidecar, &exec, res) == OMC_STATUS_OK);
    return res->status;
}
static omc_transfer_status replay(void* user, const omc_transfer_package_view* v)
{
    append((omc_arena*)user, v->bytes.data, v->bytes.size); return OMC_TRANSFER_OK;
}
static void package_check(const omc_arena* input, const omc_arena* out,
                          const omc_transfer_res* result)
{
    omc_arena storage, wire, parsed_storage, materialized, replayed;
    omc_transfer_package_batch batch, parsed;
    omc_transfer_package_io_res io;
    omc_transfer_package_replay_callbacks cb;
    omc_transfer_package_replay_res rr;
    omc_arena_init(&storage); omc_arena_init(&wire); omc_arena_init(&parsed_storage);
    omc_arena_init(&materialized); omc_arena_init(&replayed);
    assert(omc_transfer_package_batch_build_executed_output(input->data, input->size,
        out->data, out->size, result, &storage, &batch, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    assert(omc_transfer_package_batch_serialize(&batch, &wire, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK); omc_arena_fini(&storage);
    assert(omc_transfer_package_batch_deserialize(wire.data, wire.size,
        &parsed_storage, &parsed, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK); omc_arena_fini(&wire);
    assert(omc_transfer_package_batch_materialize(&parsed, &materialized, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK && materialized.size == out->size);
    assert(memcmp(materialized.data, out->data, out->size) == 0);
    memset(&cb, 0, sizeof(cb)); cb.emit_chunk = replay; cb.user = &replayed;
    assert(omc_transfer_package_batch_replay(&parsed, &cb, &rr) == OMC_STATUS_OK);
    assert(rr.status == OMC_TRANSFER_OK && replayed.size == out->size);
    assert(memcmp(replayed.data, out->data, out->size) == 0);
    omc_arena_fini(&parsed_storage); omc_arena_fini(&materialized); omc_arena_fini(&replayed);
}
static void test_carriers(int large)
{
    omc_arena file, pure, mixed, residual, pure_segment, residual_segment;
    omc_arena out, sidecar, before;
    omc_store source;
    omc_transfer_res res;
    omc_size first, second, third;
    omc_arena_init(&file); omc_arena_init(&pure); omc_arena_init(&mixed);
    omc_arena_init(&residual); omc_arena_init(&pure_segment); omc_arena_init(&residual_segment);
    omc_arena_init(&out); omc_arena_init(&sidecar); omc_arena_init(&before);
    source_init(&source, 1);
    if (large) {
        omc_entry damaged;
        damaged = source.entries[1];
        damaged.flags = OMC_ENTRY_FLAG_UNREADABLE;
        assert(omc_store_add_entry(&source, &damaged, NULL) == OMC_STATUS_OK);
    }
    resource(&pure, 0x0400U, large ? 40001U : 3U, 0x61U); segment(&pure_segment, &pure);
    resource(&residual, 0x040DU, large ? 30001U : 3U, 0x62U);
    resource(&residual, 0x040DU, 3U, 0x63U); segment(&residual_segment, &residual);
    resource(&mixed, 0x0404U, 3U, 0x71U); append(&mixed, residual.data, residual.size);
    resource(&mixed, 0x0404U, 3U, 0x72U);
    append(&file, "\377\330", 2U); append(&file, pure_segment.data, pure_segment.size);
    append(&file, "\377\376\0\3A", 5U); segment(&file, &mixed);
    append(&file, "\377\376\0\3B", 5U); append(&file, pure_segment.data, pure_segment.size);
    append(&file, "\377\332\0\2\377\355BAD\377\331", 11U);
    assert(execute(file.data, file.size, &source, &out, &sidecar, &res) == OMC_TRANSFER_OK);
    assert(res.iptc_entries_skipped == (large ? 1U : 0U));
    first = find_bytes(&out, &pure_segment); second = find_bytes(&out, &residual_segment);
    assert(first < second && memcmp(out.data + first + pure_segment.size, "\377\376\0\3A", 5U) == 0);
    third = second + residual_segment.size;
    assert(memcmp(out.data + third, "\377\376\0\3B", 5U) == 0);
    assert(memcmp(out.data + third + 5U, pure_segment.data, pure_segment.size) == 0);
    assert(memcmp(out.data + out.size - 11U, file.data + file.size - 11U, 11U) == 0);
    package_check(&file, &out, &res);
    append(&before, out.data, out.size);
    assert(execute(out.data, out.size, &source, &out, &sidecar, &res) == OMC_TRANSFER_OK);
    assert(out.size == before.size && memcmp(out.data, before.data, out.size) == 0);
    omc_store_fini(&source); omc_arena_fini(&file); omc_arena_fini(&pure); omc_arena_fini(&mixed);
    omc_arena_fini(&residual); omc_arena_fini(&pure_segment); omc_arena_fini(&residual_segment);
    omc_arena_fini(&out); omc_arena_fini(&sidecar); omc_arena_fini(&before);
}
static void test_malformed(void)
{
    omc_arena file, irb, out, sidecar;
    omc_store source, no_iptc;
    omc_transfer_res res;
    omc_size cuts[5];
    unsigned int i;
    omc_arena_init(&file); omc_arena_init(&irb); omc_arena_init(&out); omc_arena_init(&sidecar);
    source_init(&source, 1); source_init(&no_iptc, 0);
    resource(&irb, 0x0404U, 3U, 0x73U);
    cuts[0] = 1U; cuts[1] = 7U; cuts[2] = 10U; cuts[3] = 16U; cuts[4] = 17U;
    for (i = 0U; i < 5U; ++i) {
        omc_size full;
        full = irb.size; irb.size = cuts[i]; omc_arena_reset(&file);
        append(&file, "\377\330", 2U); segment(&file, &irb); append(&file, "\377\331", 2U);
        irb.size = full; omc_arena_reset(&out); omc_arena_reset(&sidecar);
        append(&out, "OUT", 3U); append(&sidecar, "SIDE", 4U);
        assert(execute(file.data, file.size, &source, &out, &sidecar, &res) == OMC_TRANSFER_MALFORMED);
        assert(out.size == 3U && memcmp(out.data, "OUT", 3U) == 0);
        assert(sidecar.size == 4U && memcmp(sidecar.data, "SIDE", 4U) == 0);
        /* No IPTC update must bypass resource validation. */
        assert(execute(file.data, file.size, &no_iptc, &out, &sidecar, &res) == OMC_TRANSFER_OK);
    }
    omc_store_fini(&source); omc_store_fini(&no_iptc);
    omc_arena_fini(&file); omc_arena_fini(&irb); omc_arena_fini(&out); omc_arena_fini(&sidecar);
}
static void test_no_native_replacement(void)
{
    omc_arena file, irb, original_segment, out, sidecar;
    omc_store source;
    omc_transfer_res res;
    omc_entry opaque;
    omc_size i;
    omc_arena_init(&file); omc_arena_init(&irb); omc_arena_init(&original_segment);
    omc_arena_init(&out); omc_arena_init(&sidecar); source_init(&source, 1);
    source.entries[1].flags = OMC_ENTRY_FLAG_DELETED;
    resource(&irb, 0x0404U, 3U, 0x73U); segment(&original_segment, &irb);
    append(&file, "\377\330", 2U); append(&file, original_segment.data, original_segment.size);
    append(&file, "\377\331", 2U);
    assert(execute(file.data, file.size, &source, &out, &sidecar, &res) == OMC_TRANSFER_OK);
    /* A clean tombstone has no destination-removal authority. */
    (void)find_bytes(&out, &original_segment);
    opaque = source.entries[1]; omc_key_make_photoshop_irb(&opaque.key, 0x0404U);
    opaque.flags = 0U;
    assert(omc_store_add_entry(&source, &opaque, NULL) == OMC_STATUS_OK);
    source.entries[1].flags |= OMC_ENTRY_FLAG_DIRTY;
    assert(execute(file.data, file.size, &source, &out, &sidecar, &res) == OMC_TRANSFER_OK);
    /* Explicit native deletion wins over a stale opaque Photoshop resource. */
    for (i = 0U; i + 14U <= out.size; ++i)
        assert(memcmp(out.data + i, "Photoshop 3.0\0", 14U) != 0);
    omc_store_fini(&source); omc_arena_fini(&file); omc_arena_fini(&irb);
    omc_arena_fini(&original_segment); omc_arena_fini(&out); omc_arena_fini(&sidecar);
}
int main(void)
{
    test_carriers(0); test_carriers(1); test_malformed(); test_no_native_replacement(); return 0;
}
