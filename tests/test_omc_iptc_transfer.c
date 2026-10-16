#include "omc/omc_iptc_serialize.h"
#include "omc/omc_transfer_package.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void add(omc_store* s, omc_u16 record, omc_u16 dataset,
                const omc_u8* data, omc_size size, omc_u32 order)
{
    omc_entry e;
    omc_byte_ref ref;
    memset(&e, 0, sizeof(e));
    omc_key_make_iptc_dataset(&e.key, record, dataset);
    assert(omc_arena_append(&s->arena, data, size, &ref) == OMC_STATUS_OK);
    omc_val_make_bytes(&e.value, ref); e.origin.order_in_block = order;
    e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}
static void check(const omc_store* s, const omc_u8* expected, omc_size size,
                  omc_u32 skipped)
{
    static const omc_scan_fmt formats[] = {OMC_SCAN_FMT_JPEG, OMC_SCAN_FMT_TIFF, OMC_SCAN_FMT_DNG};
    unsigned int i;
    for (i = 0U; i < 3U; ++i) {
        omc_transfer_payload_build_opts opts;
        omc_transfer_payload_batch batch;
        omc_transfer_payload_io_res res;
        omc_transfer_package_build_opts package_opts;
        omc_transfer_package_batch package;
        omc_transfer_package_io_res package_res;
        omc_arena storage;
        omc_size off;
        omc_arena_init(&storage); omc_transfer_payload_build_opts_init(&opts);
        opts.format = formats[i]; opts.include_exif = 0; opts.include_xmp = 0;
        opts.include_icc = 0; opts.include_jumbf = 0;
        assert(omc_transfer_payload_batch_build(s, &opts, &storage, &batch, &res) == OMC_STATUS_OK);
        assert(res.status == OMC_TRANSFER_OK && res.iptc_entries_skipped == skipped);
        assert(batch.payload_count == (size != 0U ? 1U : 0U));
        if (size != 0U) {
            off = i == 0U ? 12U : 0U;
            assert(batch.payloads[0].semantic_kind == OMC_TRANSFER_SEMANTIC_IPTC);
            assert(batch.payloads[0].payload.size == off + size + (i == 0U ? (size & 1U) : 0U));
            assert(memcmp(batch.payloads[0].payload.data + off, expected, size) == 0);
        }
        omc_transfer_package_build_opts_init(&package_opts);
        package_opts.format = formats[i]; package_opts.include_exif = 0;
        package_opts.include_xmp = 0; package_opts.include_icc = 0; package_opts.include_jumbf = 0;
        assert(omc_transfer_package_batch_build(s, &package_opts, &storage, &package, &package_res) == OMC_STATUS_OK);
        assert(package_res.status == OMC_TRANSFER_OK && package_res.iptc_entries_skipped == skipped);
        omc_arena_fini(&storage);
    }
}
static void test_shared_wire(void)
{
    omc_store s;
    omc_mut_bytes output;
    omc_iptc_iim_serialize_res res;
    omc_u8 canonical[64];
    omc_entry opaque;
    omc_byte_ref ref;
    omc_store_init(&s);
    add(&s, 2U, 25U, (const omc_u8*)"Second", 6U, 9U);
    add(&s, 2U, 5U, (const omc_u8*)"First", 5U, 1U);
    add(&s, 1U, 90U, (const omc_u8*)"\033%G", 3U, 10U);
    output.data = canonical; output.size = sizeof(canonical);
    res = omc_serialize_iptc_iim(&s, output, NULL);
    assert(res.status == OMC_IPTC_IIM_SERIALIZE_OK);
    check(&s, canonical, (omc_size)res.written, 0U);
    /* An opaque carrier wins while native datasets remain clean. */
    memset(&opaque, 0, sizeof(opaque)); omc_key_make_photoshop_irb(&opaque.key, 0x0404U);
    assert(omc_arena_append(&s.arena, "opaque", 6U, &ref) == OMC_STATUS_OK);
    omc_val_make_bytes(&opaque.value, ref);
    assert(omc_store_add_entry(&s, &opaque, NULL) == OMC_STATUS_OK);
    check(&s, (const omc_u8*)"opaque", 6U, 0U);
    s.entries[0].flags = OMC_ENTRY_FLAG_DIRTY;
    check(&s, canonical, (omc_size)res.written, 0U);
    add(&s, 2U, 10U, (const omc_u8*)"9", 1U, 0U);
    s.entries[s.entry_count - 1U].flags = OMC_ENTRY_FLAG_UNREADABLE;
    check(&s, canonical, (omc_size)res.written, 1U);
    s.entries[0].flags |= OMC_ENTRY_FLAG_DELETED;
    s.entries[1].flags = OMC_ENTRY_FLAG_DELETED; s.entries[2].flags = OMC_ENTRY_FLAG_DELETED;
    check(&s, NULL, 0U, 1U);
    omc_store_fini(&s);
}
static void test_limit(void)
{
    omc_store s;
    omc_u8* bytes;
    omc_transfer_payload_build_opts opts;
    omc_transfer_payload_batch batch;
    omc_transfer_payload_io_res res;
    omc_arena storage;
    omc_size n;
    n = 8U * 1024U * 1024U + 1U; bytes = (omc_u8*)malloc(n); assert(bytes != NULL);
    memset(bytes, 0x41, n); omc_store_init(&s); omc_arena_init(&storage);
    add(&s, 2U, 5U, bytes, n, 0U); free(bytes);
    omc_transfer_payload_build_opts_init(&opts); opts.format = OMC_SCAN_FMT_TIFF;
    opts.include_exif = 0; opts.include_xmp = 0; opts.include_icc = 0; opts.include_jumbf = 0;
    assert(omc_transfer_payload_batch_build(&s, &opts, &storage, &batch, &res) == OMC_STATUS_OK);
    assert(res.status == OMC_TRANSFER_LIMIT && batch.payload_count == 0U);
    omc_arena_fini(&storage); omc_store_fini(&s);
}
int main(void) { test_shared_wire(); test_limit(); return 0; }
