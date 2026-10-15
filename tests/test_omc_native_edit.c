#include "omc/omc_transfer_package.h"
#include "omc/omc_read.h"
#include "omc/omc_translation.h"
#include "edit/omc_exif_write.h"
#include "core/omc_value_internal.h"
#include "omc_test_assert.h"
#include "native_edit_fixture.h"
#include <stdio.h>

static omc_entry*
add_native(omc_store* store, const char* ifd, omc_u16 tag, omc_u32 flags)
{
    omc_entry e;
    omc_byte_ref ref;
    memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&store->arena, ifd, strlen(ifd), &ref) == OMC_STATUS_OK);
    omc_key_make_exif_tag(&e.key, ref, tag);
    omc_val_make_u16(&e.value, 0U);
    e.flags = flags;
    e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &e, NULL) == OMC_STATUS_OK);
    return &store->entries[store->entry_count - 1U];
}

static void add_text(omc_store* s, const char* ifd, omc_u16 tag,
                     const char* value, omc_u32 flags)
{
    omc_entry* e;
    omc_byte_ref ref;
    e = add_native(s, ifd, tag, flags);
    assert(omc_arena_append(&s->arena, value, strlen(value), &ref) == OMC_STATUS_OK);
    omc_val_make_text(&e->value, ref, OMC_TEXT_ASCII);
}

static void add_version(omc_store* s, const char* ifd, omc_u16 tag,
                        const omc_u8 bytes[4], omc_u32 flags)
{
    omc_entry* e;
    omc_byte_ref ref;
    e = add_native(s, ifd, tag, flags);
    assert(omc_arena_append(&s->arena, bytes, 4U, &ref) == OMC_STATUS_OK);
    omc_val_make_array(&e->value, OMC_ELEM_U8, 4U, ref, OMC_BYTE_ORDER_NATIVE);
}

static omc_u64 child_ifd(const omc_arena* file, omc_u64 dir, omc_u16 tag,
                          int big, int little)
{
    const omc_u8* e;
    e = native_find(file->data, file->size, dir, tag, big, little);
    return e == NULL ? 0U : native_get(e + (big ? 12U : 8U), big ? 8U : 4U, little);
}

static void prepare(const omc_u8* file, omc_size size, const omc_store* source,
                     omc_scan_fmt format, omc_dng_target_mode mode,
                     omc_transfer_bundle* bundle, omc_transfer_exec* exec)
{
    omc_transfer_prepare_opts opts;
    omc_transfer_prepare_opts_init(&opts);
    opts.format = format;
    opts.dng_target_mode = mode;
    opts.writeback_mode = OMC_XMP_WRITEBACK_EMBEDDED_AND_SIDECAR;
    assert(omc_transfer_prepare(file, size, source, &opts, bundle) == OMC_STATUS_OK);
    assert(bundle->status == OMC_TRANSFER_OK);
    assert(omc_transfer_compile(bundle, exec) == OMC_STATUS_OK);
    assert(exec->status == OMC_TRANSFER_OK);
}

static void test_collection(void)
{
    omc_store source;
    omc_native_edit_intent intent, before;
    const omc_u32 removed = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    omc_store_init(&source);
    add_native(&source, "ifd0", 0x010FU, removed);
    add_native(&source, "ifd0", 0x010FU, removed);
    add_native(&source, "ifd0", 0x010FU, 0U);
    add_native(&source, "ifd0", 0x0110U, OMC_ENTRY_FLAG_DELETED);
    add_native(&source, "ifd0", 0x0131U, removed);
    add_native(&source, "exififd", 0xA462U, removed);
    add_native(&source, "exififd", 0x927CU, removed);
    add_native(&source, "exififd", 0xA001U, removed);
    add_native(&source, "gpsifd", 0U, removed);
    add_native(&source, "gpsifd", 31U, removed);
    add_native(&source, "gpsifd", 1U, removed);
    assert(omc_native_edit_intent_collect(&source, &intent) == OMC_STATUS_OK);
    assert(intent.ifd0_removal_count == 1U && intent.ifd0_removals[0] == 0x0131U);
    assert(intent.exif_removal_count == 1U && intent.exif_removals[0] == 0xA462U);
    assert(intent.gps_removal_count == 2U && intent.gps_removals[0] == 1U &&
           intent.gps_removals[1] == 31U && intent.merge_gps && intent.merge_exif);
    before = intent;
    add_native(&source, "ifd0", 0x8769U, removed);
    assert(omc_native_edit_intent_collect(&source, &intent) == OMC_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&before, &intent, sizeof(intent)) == 0);
    intent = before;
    intent.gps_removals[0] = 0U;
    assert(omc_native_edit_intent_validate(&intent) == OMC_STATUS_INVALID_ARGUMENT);
    intent = before;
    intent.exif_removals[0] = 0xA001U;
    assert(omc_native_edit_intent_validate(&intent) == OMC_STATUS_INVALID_ARGUMENT);
    intent = before;
    intent.gps_removals[1] = intent.gps_removals[0];
    assert(omc_native_edit_intent_validate(&intent) == OMC_STATUS_INVALID_ARGUMENT);
    intent = before;
    intent.gps_removal_count = OMC_NATIVE_GPS_REMOVAL_CAPACITY + 1U;
    assert(omc_native_edit_intent_validate(&intent) == OMC_STATUS_INVALID_ARGUMENT);
    omc_store_fini(&source);
}

typedef struct replay_state { omc_arena bytes; unsigned calls; } replay_state;
static omc_transfer_status replay_begin(void* user, omc_scan_fmt fmt, omc_u32 count)
{
    replay_state* s = (replay_state*)user;
    assert(fmt == OMC_SCAN_FMT_TIFF || fmt == OMC_SCAN_FMT_DNG);
    assert(count != 0U);
    s->calls++;
    return OMC_TRANSFER_OK;
}
static omc_transfer_status replay_chunk(void* user, const omc_transfer_package_view* view)
{
    omc_byte_ref ref;
    replay_state* s = (replay_state*)user;
    assert(view->output_offset == s->bytes.size);
    assert(omc_arena_append(&s->bytes, view->bytes.data, view->bytes.size, &ref) == OMC_STATUS_OK);
    s->calls++;
    return OMC_TRANSFER_OK;
}
static omc_transfer_status replay_end(void* user, omc_scan_fmt fmt)
{
    replay_state* s = (replay_state*)user;
    assert(fmt == OMC_SCAN_FMT_TIFF || fmt == OMC_SCAN_FMT_DNG);
    s->calls++;
    return OMC_TRANSFER_OK;
}

static void test_package(const omc_u8* input, const omc_arena* edited,
                          const omc_transfer_res* execute)
{
    omc_arena storage, wire, parsed_storage, materialized;
    omc_transfer_package_batch batch, parsed;
    omc_transfer_package_io_res io;
    omc_transfer_package_replay_callbacks cb;
    omc_transfer_package_replay_res rr;
    replay_state state;
    omc_arena_init(&storage); omc_arena_init(&wire);
    omc_arena_init(&parsed_storage); omc_arena_init(&materialized);
    omc_arena_init(&state.bytes); state.calls = 0U;
    assert(omc_transfer_package_batch_build_executed_output(input, 1024U,
        edited->data, edited->size, execute, &storage, &batch, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    assert(omc_transfer_package_batch_serialize(&batch, &wire, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    omc_arena_fini(&storage);
    assert(omc_transfer_package_batch_deserialize(wire.data, wire.size,
        &parsed_storage, &parsed, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK);
    assert(omc_transfer_package_batch_materialize(&parsed, &materialized, &io) == OMC_STATUS_OK);
    assert(io.status == OMC_TRANSFER_OK && materialized.size == edited->size &&
           memcmp(materialized.data, edited->data, edited->size) == 0);
    cb.begin_batch = replay_begin; cb.emit_chunk = replay_chunk;
    cb.end_batch = replay_end; cb.user = &state;
    assert(omc_transfer_package_batch_replay(&parsed, &cb, &rr) == OMC_STATUS_OK);
    assert(rr.status == OMC_TRANSFER_OK && state.calls == parsed.chunk_count + 2U);
    assert(state.bytes.size == edited->size && memcmp(state.bytes.data, edited->data, edited->size) == 0);
    omc_arena_fini(&state.bytes); omc_arena_fini(&materialized);
    omc_arena_fini(&parsed_storage); omc_arena_fini(&wire);
}

static void test_removal(int big, int little, omc_scan_fmt format, omc_dng_target_mode mode)
{
    omc_u8 file[1024], original[1024];
    omc_store source, decoded;
    omc_arena edited, sidecar;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;
    omc_u64 root, exif, gps;
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[32];
    omc_u8 payload[8192];
    omc_u32 scratch[32];
    omc_read_res read;
    const omc_u32 removed = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    native_fixture(file, big, little); memcpy(original, file, sizeof(file));
    omc_store_init(&source); omc_store_init(&decoded);
    omc_arena_init(&edited); omc_arena_init(&sidecar);
    add_native(&source, "ifd0", 0x010FU, removed);
    add_native(&source, "exififd", 0x9003U, removed);
    add_native(&source, "gpsifd", 1U, removed);
    add_native(&source, "exififd", 0x829AU, OMC_ENTRY_FLAG_DELETED);
    add_native(&source, "exififd", 0x927CU, removed);
    prepare(file, sizeof(file), &source, format, mode, &bundle, &exec);
    assert(bundle.native_edits.ifd0_removal_count == 1U &&
           bundle.native_edits.exif_removal_count == 1U && bundle.native_edits.gps_removal_count == 1U);
    assert(memcmp(&bundle.native_edits, &exec.native_edits, sizeof(exec.native_edits)) == 0);
    memset(&bundle.native_edits, 0, sizeof(bundle.native_edits));
    assert(omc_transfer_execute(file, sizeof(file), &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
    if (result.status != OMC_TRANSFER_OK)
        fprintf(stderr, "native removal failed: big=%d little=%d format=%d status=%d\n", big, little, (int)format, (int)result.status);
    assert(result.status == OMC_TRANSFER_OK && result.edited_present && result.sidecar_present);
    assert(memcmp(file, original, sizeof(file)) == 0);
    assert(edited.size >= sizeof(file) && memcmp(edited.data + 944U, file + 944U, 40U) == 0);
    root = native_get(edited.data + (big ? 8U : 4U), big ? 8U : 4U, little);
    exif = child_ifd(&edited, root, 0x8769U, big, little);
    gps = child_ifd(&edited, root, 0x8825U, big, little);
    assert(native_find(edited.data, edited.size, root, 0x010FU, big, little) == NULL);
    assert(native_find(edited.data, edited.size, exif, 0x9003U, big, little) == NULL);
    assert(native_find(edited.data, edited.size, gps, 1U, big, little) == NULL);
    assert(native_find(edited.data, edited.size, gps, 0U, big, little) != NULL);
    assert(native_find(edited.data, edited.size, gps, 2U, big, little) != NULL);
    assert(native_find(edited.data, edited.size, exif, 0x829AU, big, little) != NULL);
    assert(native_find(edited.data, edited.size, exif, 0x927CU, big, little) != NULL);
    assert(child_ifd(&edited, exif, 0xA005U, big, little) == 640U);
    read = omc_read_simple(edited.data, edited.size, &decoded, blocks, 32U, ifds, 32U,
                           payload, sizeof(payload), scratch, 32U, NULL);
    assert(read.exif.status == OMC_EXIF_OK && read.entries_added != 0U);
    test_package(file, &edited, &result);
    omc_store_fini(&decoded); omc_store_fini(&source);
    omc_arena_fini(&sidecar); omc_arena_fini(&edited);
}

static void test_rejection(void)
{
    omc_u8 file[1024];
    omc_store source;
    omc_arena edited, sidecar, storage;
    omc_byte_ref ref;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec, before;
    omc_transfer_res result;
    omc_transfer_payload_build_opts po;
    omc_transfer_payload_batch pb;
    omc_transfer_payload_io_res pi;
    omc_transfer_package_build_opts ko;
    omc_transfer_package_batch kb;
    omc_transfer_package_io_res ki;
    omc_transfer_package_replay_callbacks cb;
    omc_transfer_package_replay_res rr;
    replay_state state;
    native_fixture(file, 0, 1);
    omc_store_init(&source);
    omc_arena_init(&edited); omc_arena_init(&sidecar); omc_arena_init(&storage);
    assert(omc_arena_append(&edited, "KEEP", 4U, &ref) == OMC_STATUS_OK);
    assert(omc_arena_append(&sidecar, "WAIT", 4U, &ref) == OMC_STATUS_OK);
    add_text(&source, "gpsifd", 1U, "S", OMC_ENTRY_FLAG_DIRTY);
    prepare(file, sizeof(file), &source, OMC_SCAN_FMT_TIFF, OMC_DNG_TARGET_EXISTING, &bundle, &exec);
    assert(omc_transfer_execute(file, sizeof(file), &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status != OMC_TRANSFER_OK && !result.edited_present && !result.sidecar_present);
    assert(edited.size == 4U && memcmp(edited.data, "KEEP", 4U) == 0);
    assert(sidecar.size == 4U && memcmp(sidecar.data, "WAIT", 4U) == 0);
    before = exec; bundle.native_edits.clear_gps = 1;
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&before, &exec, sizeof(exec)) == 0);
    bundle.native_edits.clear_gps = 0; bundle.format = OMC_SCAN_FMT_JXL;
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK && exec.status == OMC_TRANSFER_UNSUPPORTED);
    exec = before;
    assert(omc_transfer_execute(NULL, 0U, &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_UNSUPPORTED && edited.size == 4U && sidecar.size == 4U);
    omc_transfer_payload_build_opts_init(&po); po.format = OMC_SCAN_FMT_TIFF;
    assert(omc_transfer_payload_batch_build(&source, &po, &storage, &pb, &pi) == OMC_STATUS_OK);
    assert(pi.status == OMC_TRANSFER_UNSUPPORTED && pb.payload_count == 0U);
    omc_transfer_package_build_opts_init(&ko); ko.format = OMC_SCAN_FMT_TIFF;
    assert(omc_transfer_package_batch_build(&source, &ko, &storage, &kb, &ki) == OMC_STATUS_OK);
    assert(ki.status == OMC_TRANSFER_UNSUPPORTED && kb.chunk_count == 0U);
    omc_arena_init(&state.bytes); state.calls = 0U;
    cb.begin_batch = replay_begin; cb.emit_chunk = replay_chunk; cb.end_batch = replay_end; cb.user = &state;
    assert(omc_transfer_package_batch_replay(&kb, &cb, &rr) == OMC_STATUS_OK);
    assert(rr.status != OMC_TRANSFER_OK && state.calls == 0U);
    omc_arena_fini(&state.bytes); omc_arena_fini(&storage);
    omc_arena_fini(&sidecar); omc_arena_fini(&edited); omc_store_fini(&source);
}

static void test_dng_version(void)
{
    static const omc_u8 old_version[4] = {1U, 4U, 0U, 0U};
    omc_u8 file[1024];
    omc_store source;
    omc_arena edited, sidecar;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;
    const omc_u8* entry;
    omc_u64 root;
    unsigned dirty, mode;
    native_fixture(file, 0, 1);
    for (mode = 0U; mode < 2U; ++mode) for (dirty = 0U; dirty < 2U; ++dirty) {
        omc_store_init(&source); omc_arena_init(&edited); omc_arena_init(&sidecar);
        add_version(&source, "ifd0", 0xC612U, old_version, dirty ? OMC_ENTRY_FLAG_DIRTY : 0U);
        add_text(&source, "ifd0", 0x0131U, "Native edit", 0U);
        prepare(file, sizeof(file), &source, OMC_SCAN_FMT_DNG, (omc_dng_target_mode)mode, &bundle, &exec);
        assert(omc_transfer_execute(file, sizeof(file), &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
        assert(result.status == OMC_TRANSFER_OK);
        root = native_get(edited.data + 4U, 4U, 1);
        entry = native_find(edited.data, edited.size, root, 0xC612U, 0, 1);
        assert(entry != NULL && entry[8U] == 1U && entry[9U] == (dirty ? 4U : 6U));
        omc_arena_fini(&sidecar); omc_arena_fini(&edited); omc_store_fini(&source);
    }
}

static void test_output_controls(void)
{
    omc_u8 file[1024];
    omc_store source;
    omc_arena edited, sidecar;
    omc_byte_ref ref;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle, before;
    omc_transfer_exec exec;
    omc_transfer_res result;
    omc_exif_write_res written;
    omc_native_edit_intent intent;
    omc_transfer_payload_build_opts payload_opts;
    omc_transfer_payload_batch payload_batch;
    omc_transfer_payload_io_res payload_result;
    const omc_u8* entry;
    omc_u64 root, exif, gps;
    unsigned enabled;
    native_fixture(file, 0, 1);
    omc_store_init(&source); omc_arena_init(&edited); omc_arena_init(&sidecar);
    add_native(&source, "ifd0", 0x010FU, OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
    omc_val_make_u16(&add_native(&source, "exififd", 0xA001U, OMC_ENTRY_FLAG_DIRTY)->value, 99U);
    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_TIFF;
    opts.target_image_spec.has_exif_color_space = 1;
    opts.target_image_spec.exif_color_space = 2U;
    /* Disabling EXIF-to-XMP projection must not disable native EXIF edits. */
    opts.embedded.packet.include_exif = 0;
    for (enabled = 0U; enabled < 2U; ++enabled) {
        opts.include_exif = (int)enabled;
        assert(omc_transfer_prepare(file, sizeof(file), &source, &opts, &bundle) == OMC_STATUS_OK);
        assert(bundle.status == OMC_TRANSFER_OK && bundle.include_exif == (int)enabled);
        assert(bundle.native_edits.merge_exif == (int)enabled);
        assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK && exec.status == OMC_TRANSFER_OK);
        /* The input may alias the previous output. Publication happens last. */
        omc_arena_reset(&edited);
        assert(omc_arena_append(&edited, file, sizeof(file), &ref) == OMC_STATUS_OK);
        assert(omc_transfer_execute(edited.data, edited.size, &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
        assert(result.status == OMC_TRANSFER_OK && result.edited_present);
        root = native_get(edited.data + 4U, 4U, 1);
        exif = child_ifd(&edited, root, 0x8769U, 0, 1);
        assert((native_find(edited.data, edited.size, root, 0x010FU, 0, 1) == NULL) == (enabled != 0U));
        entry = native_find(edited.data, edited.size, exif, 0xA001U, 0, 1);
        assert(entry != NULL && native_get(entry + 8U, 2U, 1) == (enabled ? 2U : 1U));
        assert(native_find(edited.data, edited.size, exif, 0x9003U, 0, 1) != NULL);
        assert(child_ifd(&edited, exif, 0xA005U, 0, 1) == 640U);
    }
    omc_store_reset(&source);
    add_native(&source, "ifd0", 0x8769U, OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
    before = bundle;
    assert(omc_transfer_prepare(file, sizeof(file), &source, &opts, &bundle) == OMC_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&before, &bundle, sizeof(bundle)) == 0);

    /* An explicit removal list implies GPS merge, even without the flag. */
    omc_store_reset(&source);
    add_text(&source, "gpsifd", 1U, "S", 0U);
    add_text(&source, "gpsifd", 18U, "IGN", 0U);
    omc_native_edit_intent_init(&intent);
    intent.gps_removal_count = 1U; intent.gps_removals[0] = 1U;
    assert(omc_exif_write_embedded_edit(file, sizeof(file), &source, &intent,
        &edited, OMC_SCAN_FMT_TIFF, &written) == OMC_STATUS_OK);
    assert(written.status == OMC_EXIF_WRITE_OK);
    root = native_get(edited.data + 4U, 4U, 1);
    gps = child_ifd(&edited, root, 0x8825U, 0, 1);
    assert(native_find(edited.data, edited.size, gps, 1U, 0, 1) == NULL);
    entry = native_find(edited.data, edited.size, gps, 18U, 0, 1);
    assert(entry != NULL && memcmp(entry + 8U, "WGS", 4U) == 0);

    /* A host ColorSpace merge is valid for fresh output as well. */
    omc_store_reset(&source);
    opts.format = OMC_SCAN_FMT_DNG;
    opts.dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    assert(omc_transfer_prepare(NULL, 0U, &source, &opts, &bundle) == OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK && bundle.native_edits.merge_exif);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK && exec.status == OMC_TRANSFER_OK);
    assert(omc_transfer_execute(NULL, 0U, &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK && result.edited_present);
    root = native_get(edited.data + 4U, 4U, 1);
    exif = child_ifd(&edited, root, 0x8769U, 0, 1);
    entry = native_find(edited.data, edited.size, exif, 0xA001U, 0, 1);
    assert(entry != NULL && native_get(entry + 8U, 2U, 1) == 2U);
    omc_transfer_payload_build_opts_init(&payload_opts);
    payload_opts.format = OMC_SCAN_FMT_DNG;
    payload_opts.target_image_spec = opts.target_image_spec;
    assert(omc_transfer_payload_batch_build(&source, &payload_opts, &sidecar,
        &payload_batch, &payload_result) == OMC_STATUS_OK);
    assert(payload_result.status == OMC_TRANSFER_OK && payload_batch.payload_count != 0U);
    omc_arena_fini(&sidecar); omc_arena_fini(&edited); omc_store_fini(&source);
}

static void test_directory_cleanup(int big, int little)
{
    omc_u8 file[1024];
    omc_store source;
    omc_arena edited;
    omc_native_edit_intent intent;
    omc_exif_write_res result;
    omc_u64 root;
    omc_u16 tag;
    native_fixture(file, big, little);
    omc_store_init(&source); omc_arena_init(&edited);
    for (tag = 1U; tag <= 31U; ++tag)
        add_native(&source, "gpsifd", tag, OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED);
    assert(omc_native_edit_intent_collect(&source, &intent) == OMC_STATUS_OK);
    assert(intent.gps_removal_count == 31U);
    assert(omc_exif_write_embedded_edit(file, sizeof(file), &source, &intent,
        &edited, OMC_SCAN_FMT_TIFF, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_EXIF_WRITE_OK);
    root = native_get(edited.data + (big ? 8U : 4U), big ? 8U : 4U, little);
    assert(child_ifd(&edited, root, 0x8825U, big, little) == 0U);
    assert(child_ifd(&edited, root, 0x8769U, big, little) == 256U);
    assert(memcmp(edited.data + 944U, file + 944U, 40U) == 0);
    omc_store_reset(&source);
    omc_native_edit_intent_init(&intent);
    intent.clear_exif = 1; intent.clear_gps = 1;
    assert(omc_exif_write_embedded_edit(file, sizeof(file), &source, &intent,
        &edited, OMC_SCAN_FMT_TIFF, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_EXIF_WRITE_OK);
    root = native_get(edited.data + (big ? 8U : 4U), big ? 8U : 4U, little);
    assert(child_ifd(&edited, root, 0x8825U, big, little) == 0U);
    assert(child_ifd(&edited, root, 0x8769U, big, little) == 0U);
    assert(native_find(edited.data, edited.size, root, 0x010FU, big, little) != NULL);
    assert(memcmp(edited.data + 944U, file + 944U, 40U) == 0);
    omc_arena_fini(&edited); omc_store_fini(&source);
}

static void add_xmp(omc_store* s, const char* path, const char* text)
{
    static const char ns[] = "http://ns.adobe.com/exif/1.0/";
    omc_entry e;
    omc_byte_ref nsref, pathref, value;
    memset(&e, 0, sizeof(e));
    assert(omc_arena_append(&s->arena, ns, sizeof(ns) - 1U, &nsref) == OMC_STATUS_OK);
    assert(omc_arena_append(&s->arena, path, strlen(path), &pathref) == OMC_STATUS_OK);
    assert(omc_arena_append(&s->arena, text, strlen(text), &value) == OMC_STATUS_OK);
    omc_key_make_xmp_property(&e.key, nsref, pathref);
    omc_val_make_text(&e.value, value, OMC_TEXT_UTF8);
    e.flags = OMC_ENTRY_FLAG_DIRTY;
    e.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(s, &e, NULL) == OMC_STATUS_OK);
}

static const omc_entry* gps_entry(const omc_store* s, omc_u16 tag)
{
    omc_size i;
    omc_const_bytes ifd;
    for (i = 0U; i < s->entry_count; ++i) {
        const omc_entry* e = &s->entries[i];
        if (e->key.kind != OMC_KEY_EXIF_TAG || e->key.u.exif_tag.tag != tag ||
            (e->flags & OMC_ENTRY_FLAG_DELETED)) continue;
        ifd = omc_arena_view(&s->arena, e->key.u.exif_tag.ifd);
        if (ifd.size == 6U && memcmp(ifd.data, "gpsifd", 6U) == 0) return e;
    }
    return NULL;
}

static void same_scalar(const omc_val* a, const omc_val* b)
{
    assert(a->elem_type == b->elem_type);
    if (a->elem_type == OMC_ELEM_URATIONAL) {
        assert(a->u.ur.numer == b->u.ur.numer && a->u.ur.denom == b->u.ur.denom);
    } else assert(a->u.u64 == b->u.u64);
}

static void same_gps(const omc_store* expected, const omc_store* actual)
{
    omc_u16 tag;
    omc_u32 i;
    const omc_entry *a, *b;
    omc_const_bytes ab, bb;
    omc_val av, bv;
    for (tag = 0U; tag < 32U; ++tag) {
        a = gps_entry(expected, tag); b = gps_entry(actual, tag);
        assert(a != NULL && b != NULL);
        assert(a->value.kind == b->value.kind && a->value.count == b->value.count);
        if (a->value.kind == OMC_VAL_SCALAR) same_scalar(&a->value, &b->value);
        else if (a->value.kind == OMC_VAL_ARRAY) {
            for (i = 0U; i < a->value.count; ++i) {
                omc_value_array_scalar(&a->value, &expected->arena, i, &av);
                omc_value_array_scalar(&b->value, &actual->arena, i, &bv);
                same_scalar(&av, &bv);
            }
        } else {
            ab = omc_arena_view(&expected->arena, a->value.u.ref);
            bb = omc_arena_view(&actual->arena, b->value.u.ref);
            assert(ab.size == bb.size && memcmp(ab.data, bb.data, ab.size) == 0);
        }
    }
}

static void test_all_gps_carriers(void)
{
    static const char* const values[][2] = {
        {"GPSLatitude", "35,41.5N"}, {"GPSLongitude", "139,41.25E"},
        {"GPSAltitude", "12345/100"}, {"GPSAltitudeRef", "0"},
        {"GPSTimeStamp", "2024-03-01T00:30:12.125+01:00"},
        {"GPSSpeedRef", "K"}, {"GPSSpeed", "12345/100"},
        {"GPSTrackRef", "T"}, {"GPSTrack", "359.99"},
        {"GPSImgDirectionRef", "M"}, {"GPSImgDirection", "45.5"},
        {"GPSDestLatitude", "35,41.5S"}, {"GPSDestLongitude", "139,41.25W"},
        {"GPSDestBearingRef", "T"}, {"GPSDestBearing", "90"},
        {"GPSDestDistanceRef", "N"}, {"GPSDestDistance", "2.5"},
        {"GPSStatus", "A"}, {"GPSMeasureMode", "3"}, {"GPSDOP", "1.5"},
        {"GPSDifferential", "1"}, {"GPSHPositioningError", "0.25"},
        {"GPSSatellites", "8"}, {"GPSMapDatum", "WGS-84"},
        {"GPSProcessingMethod", "GPS"}, {"GPSAreaInformation", "Tokyo"}
    };
    typedef omc_gps_translation_res (*translate_fn)(const omc_store*, omc_store*, const omc_gps_translation_opts*);
    static const translate_fn translators[] = {omc_translate_xmp_gps,
        omc_translate_xmp_gps_navigation, omc_translate_xmp_gps_destination,
        omc_translate_xmp_gps_quality, omc_translate_xmp_gps_text};
    static const omc_u32 masks[] = {OMC_GPS_TRANSLATE_ALL, OMC_GPS_NAV_TRANSLATE_ALL,
        OMC_GPS_DEST_TRANSLATE_ALL, OMC_GPS_QUALITY_TRANSLATE_ALL, OMC_GPS_TEXT_TRANSLATE_ALL};
    static const omc_u8 jpeg[] = {0xFFU, 0xD8U, 0xFFU, 0xD9U};
    omc_store source, next, swap, decoded;
    omc_gps_translation_opts options;
    omc_gps_translation_res translated;
    omc_arena edited, sidecar;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;
    omc_u8 file[1024], payload[16384];
    const omc_u8* input;
    omc_size size, i;
    omc_blk_ref blocks[32];
    omc_exif_ifd_ref ifds[32];
    omc_u32 scratch[32];
    omc_read_res read;
    unsigned carrier;
    omc_scan_fmt format;
    omc_dng_target_mode mode;
    int big, little;
    omc_store_init(&source); omc_store_init(&next);
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); ++i) add_xmp(&source, values[i][0], values[i][1]);
    omc_gps_translation_opts_init(&options);
    for (i = 0U; i < 5U; ++i) {
        options.mappings = masks[i];
        translated = translators[i](&source, &next, &options);
        assert(translated.status == OMC_GPS_TRANSLATION_OK);
        swap = source; source = next; next = swap; omc_store_reset(&next);
    }
    for (carrier = 0U; carrier < 13U; ++carrier) {
        big = carrier != 0U && ((carrier - 1U) % 4U) >= 2U;
        little = carrier == 0U || ((carrier - 1U) % 2U) != 0U;
        native_fixture(file, big, little);
        input = carrier == 0U ? jpeg : file;
        size = carrier == 0U ? sizeof(jpeg) : sizeof(file);
        format = carrier == 0U ? OMC_SCAN_FMT_JPEG : carrier <= 4U ? OMC_SCAN_FMT_TIFF : OMC_SCAN_FMT_DNG;
        mode = carrier <= 8U ? OMC_DNG_TARGET_EXISTING : OMC_DNG_TARGET_TEMPLATE;
        omc_arena_init(&edited); omc_arena_init(&sidecar); omc_store_init(&decoded);
        prepare(input, size, &source, format, mode, &bundle, &exec);
        assert(omc_transfer_execute(input, size, &source, &edited, &sidecar, &exec, &result) == OMC_STATUS_OK);
        if (result.status != OMC_TRANSFER_OK) fprintf(stderr, "GPS carrier %u failed: %d\n", carrier, (int)result.status);
        assert(result.status == OMC_TRANSFER_OK);
        read = omc_read_simple(edited.data, edited.size, &decoded, blocks, 32U, ifds, 32U,
                               payload, sizeof(payload), scratch, 32U, NULL);
        assert(read.exif.status == OMC_EXIF_OK);
        same_gps(&source, &decoded);
        if (carrier != 0U) assert(memcmp(edited.data + 944U, file + 944U, 40U) == 0);
        omc_store_fini(&decoded); omc_arena_fini(&sidecar); omc_arena_fini(&edited);
    }
    omc_store_fini(&next); omc_store_fini(&source);
}

int main(void)
{
    int big, little;
    test_collection();
    for (big = 0; big < 2; ++big) for (little = 0; little < 2; ++little) {
        test_removal(big, little, OMC_SCAN_FMT_TIFF, OMC_DNG_TARGET_EXISTING);
        test_removal(big, little, OMC_SCAN_FMT_DNG, OMC_DNG_TARGET_TEMPLATE);
        test_directory_cleanup(big, little);
    }
    test_rejection(); test_dng_version(); test_output_controls();
    test_all_gps_carriers();
    return 0;
}
