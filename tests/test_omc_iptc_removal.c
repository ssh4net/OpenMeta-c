#include "omc/omc_transfer.h"
#include "omc/omc_transfer_package.h"
#include "omc/omc_transfer_payload.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static void
append(omc_arena* arena, const void* bytes, omc_size size)
{
    omc_byte_ref ref;
    assert(omc_arena_append(arena, bytes, size, &ref) == OMC_STATUS_OK);
}

static void
append_u16be(omc_arena* arena, omc_u16 value)
{
    omc_u8 bytes[2];
    bytes[0] = (omc_u8)(value >> 8);
    bytes[1] = (omc_u8)value;
    append(arena, bytes, 2U);
}

static void
append_resource(omc_arena* arena, omc_u16 id, const char* payload,
                omc_u32 payload_size)
{
    omc_u8 size_bytes[4];
    append(arena, "8BIM", 4U);
    append_u16be(arena, id);
    append(arena, "\0\0", 2U);
    size_bytes[0] = (omc_u8)(payload_size >> 24);
    size_bytes[1] = (omc_u8)(payload_size >> 16);
    size_bytes[2] = (omc_u8)(payload_size >> 8);
    size_bytes[3] = (omc_u8)payload_size;
    append(arena, size_bytes, 4U);
    append(arena, payload, (omc_size)payload_size);
    if ((payload_size & 1U) != 0U)
        append(arena, "\0", 1U);
}

static void
append_app13(omc_arena* file, const omc_arena* resources)
{
    append(file, "\377\355", 2U);
    append_u16be(file, (omc_u16)(16U + resources->size));
    append(file, "Photoshop 3.0\0", 14U);
    append(file, resources->data, resources->size);
}

static void
put_u16(omc_u8* bytes, omc_u16 value, int little_endian)
{
    if (little_endian) {
        bytes[0] = (omc_u8)value;
        bytes[1] = (omc_u8)(value >> 8);
    } else {
        bytes[0] = (omc_u8)(value >> 8);
        bytes[1] = (omc_u8)value;
    }
}

static void
put_u32(omc_u8* bytes, omc_u32 value, int little_endian)
{
    if (little_endian) {
        bytes[0] = (omc_u8)value;
        bytes[1] = (omc_u8)(value >> 8);
        bytes[2] = (omc_u8)(value >> 16);
        bytes[3] = (omc_u8)(value >> 24);
    } else {
        bytes[0] = (omc_u8)(value >> 24);
        bytes[1] = (omc_u8)(value >> 16);
        bytes[2] = (omc_u8)(value >> 8);
        bytes[3] = (omc_u8)value;
    }
}

static void
put_u64(omc_u8* bytes, omc_u64 value, int little_endian)
{
    unsigned int i;
    for (i = 0U; i < 8U; ++i) {
        unsigned int shift;
        shift = little_endian ? i * 8U : (7U - i) * 8U;
        bytes[i] = (omc_u8)(value >> shift);
    }
}

static omc_u16
get_u16(const omc_u8* bytes, int little_endian)
{
    return little_endian
               ? (omc_u16)((omc_u16)bytes[0] | ((omc_u16)bytes[1] << 8))
               : (omc_u16)(((omc_u16)bytes[0] << 8) | (omc_u16)bytes[1]);
}

static omc_u32
get_u32(const omc_u8* bytes, int little_endian)
{
    return little_endian
               ? (omc_u32)bytes[0] | ((omc_u32)bytes[1] << 8) |
                     ((omc_u32)bytes[2] << 16) | ((omc_u32)bytes[3] << 24)
               : ((omc_u32)bytes[0] << 24) | ((omc_u32)bytes[1] << 16) |
                     ((omc_u32)bytes[2] << 8) | (omc_u32)bytes[3];
}

static omc_u64
get_u64(const omc_u8* bytes, int little_endian)
{
    omc_u64 value;
    unsigned int i;
    value = 0U;
    for (i = 0U; i < 8U; ++i) {
        unsigned int index;
        index = little_endian ? 7U - i : i;
        value = (value << 8) | (omc_u64)bytes[index];
    }
    return value;
}

static void
write_tiff_entry(omc_u8* entry, int big_tiff, int little_endian,
                 omc_u16 tag, omc_u16 type, omc_u64 count,
                 omc_u32 inline_u32, const char* inline_bytes,
                 omc_size inline_size)
{
    memset(entry, 0, big_tiff ? 20U : 12U);
    put_u16(entry, tag, little_endian);
    put_u16(entry + 2U, type, little_endian);
    if (big_tiff) {
        put_u64(entry + 4U, count, little_endian);
        if (type == 4U && count == 1U)
            put_u32(entry + 12U, inline_u32, little_endian);
        else if (inline_size != 0U)
            memcpy(entry + 12U, inline_bytes, inline_size);
    } else {
        put_u32(entry + 4U, (omc_u32)count, little_endian);
        if (type == 4U && count == 1U)
            put_u32(entry + 8U, inline_u32, little_endian);
        else if (inline_size != 0U)
            memcpy(entry + 8U, inline_bytes, inline_size);
    }
}

static void
make_tiff(omc_arena* file, int big_tiff, int little_endian, int has_iptc)
{
    omc_u8 bytes[160];
    omc_size root_offset;
    omc_size entry_offset;
    omc_size entry_size;
    omc_size count_size;
    omc_size pixel_offset;
    omc_u16 count;

    memset(bytes, 0, sizeof(bytes));
    bytes[0] = little_endian ? (omc_u8)'I' : (omc_u8)'M';
    bytes[1] = bytes[0];
    root_offset = big_tiff ? 16U : 8U;
    entry_size = big_tiff ? 20U : 12U;
    count_size = big_tiff ? 8U : 2U;
    pixel_offset = big_tiff ? 120U : 96U;
    count = has_iptc ? 3U : 2U;

    put_u16(bytes + 2U, big_tiff ? 43U : 42U, little_endian);
    if (big_tiff) {
        put_u16(bytes + 4U, 8U, little_endian);
        put_u16(bytes + 6U, 0U, little_endian);
        put_u64(bytes + 8U, (omc_u64)root_offset, little_endian);
        put_u64(bytes + root_offset, count, little_endian);
    } else {
        put_u32(bytes + 4U, (omc_u32)root_offset, little_endian);
        put_u16(bytes + root_offset, count, little_endian);
    }
    entry_offset = root_offset + count_size;
    write_tiff_entry(bytes + entry_offset, big_tiff, little_endian,
                     273U, 4U, 1U, (omc_u32)pixel_offset, NULL, 0U);
    write_tiff_entry(bytes + entry_offset + entry_size, big_tiff, little_endian,
                     279U, 4U, 1U, 4U, NULL, 0U);
    if (has_iptc)
        write_tiff_entry(bytes + entry_offset + 2U * entry_size, big_tiff,
                         little_endian, 33723U, 7U, 3U, 0U, "OLD", 3U);
    memcpy(bytes + pixel_offset, "PIX!", 4U);
    append(file, bytes, big_tiff ? 124U : 100U);
}

static void
make_tiff_only_iptc(omc_arena* file, int little_endian, int duplicate)
{
    omc_u8 bytes[100];
    omc_u16 count;
    omc_size offset;
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = little_endian ? (omc_u8)'I' : (omc_u8)'M';
    bytes[1] = bytes[0];
    put_u16(bytes + 2U, 42U, little_endian);
    put_u32(bytes + 4U, 8U, little_endian);
    count = duplicate ? 2U : 1U;
    put_u16(bytes + 8U, count, little_endian);
    offset = 10U;
    write_tiff_entry(bytes + offset, 0, little_endian, 33723U, 7U, 3U, 0U,
                     "ONE", 3U);
    if (duplicate)
        write_tiff_entry(bytes + offset + 12U, 0, little_endian, 33723U, 7U,
                         3U, 0U, "TWO", 3U);
    memcpy(bytes + 96U, "PIX!", 4U);
    append(file, bytes, sizeof(bytes));
}

static void
add_deleted_iptc(omc_store* store)
{
    omc_entry entry;
    omc_byte_ref ref;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    assert(omc_arena_append(&store->arena, "deleted", 7U, &ref) ==
           OMC_STATUS_OK);
    omc_val_make_bytes(&entry.value, ref);
    entry.flags = OMC_ENTRY_FLAG_DIRTY | OMC_ENTRY_FLAG_DELETED;
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, (omc_u32*)0) == OMC_STATUS_OK);
}

static void
add_active_iptc(omc_store* store)
{
    omc_entry entry;
    omc_byte_ref ref;
    memset(&entry, 0, sizeof(entry));
    omc_key_make_iptc_dataset(&entry.key, 2U, 5U);
    assert(omc_arena_append(&store->arena, "active", 6U, &ref) ==
           OMC_STATUS_OK);
    omc_val_make_bytes(&entry.value, ref);
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, (omc_u32*)0) == OMC_STATUS_OK);
}

static void
add_exif_iptc(omc_store* store)
{
    omc_entry entry;
    omc_byte_ref ifd;
    omc_byte_ref ref;
    memset(&entry, 0, sizeof(entry));
    assert(omc_arena_append(&store->arena, "ifd0", 4U, &ifd) ==
           OMC_STATUS_OK);
    omc_key_make_exif_tag(&entry.key, ifd, 33723U);
    assert(omc_arena_append(&store->arena, "EXIF", 4U, &ref) ==
           OMC_STATUS_OK);
    omc_val_make_bytes(&entry.value, ref);
    entry.flags = OMC_ENTRY_FLAG_DIRTY;
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    assert(omc_store_add_entry(store, &entry, (omc_u32*)0) == OMC_STATUS_OK);
}

static void
prepare_exec(const omc_u8* file_bytes, omc_size file_size,
             const omc_store* source, omc_scan_fmt format,
             omc_dng_target_mode dng_mode, int include_iptc,
             omc_transfer_bundle* bundle, omc_transfer_exec* exec)
{
    omc_transfer_prepare_opts opts;
    omc_transfer_prepare_opts_init(&opts);
    opts.format = format;
    opts.dng_target_mode = dng_mode;
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    opts.include_iptc = include_iptc;
    assert(omc_transfer_prepare(file_bytes, file_size, source, &opts, bundle) ==
           OMC_STATUS_OK);
    assert(bundle->status == OMC_TRANSFER_OK);
    assert(omc_transfer_compile(bundle, exec) == OMC_STATUS_OK);
    assert(exec->status == OMC_TRANSFER_OK);
}

static int
tiff_has_iptc(const omc_arena* file, int big_tiff, int little_endian,
              omc_u32 expected_pixel_offset)
{
    omc_u64 root;
    omc_u64 count;
    omc_size count_size;
    omc_size entry_size;
    omc_size entry_offset;
    omc_u64 i;
    int found;

    root = big_tiff ? get_u64(file->data + 8U, little_endian)
                    : (omc_u64)get_u32(file->data + 4U, little_endian);
    count_size = big_tiff ? 8U : 2U;
    entry_size = big_tiff ? 20U : 12U;
    count = big_tiff ? get_u64(file->data + (omc_size)root, little_endian)
                     : (omc_u64)get_u16(file->data + (omc_size)root,
                                        little_endian);
    entry_offset = (omc_size)root + count_size;
    found = 0;
    for (i = 0U; i < count; ++i) {
        const omc_u8* entry;
        omc_u16 tag;
        entry = file->data + entry_offset + (omc_size)i * entry_size;
        tag = get_u16(entry, little_endian);
        if (tag == 33723U)
            found = 1;
        if (tag == 273U) {
            omc_u32 pixel_offset;
            pixel_offset = big_tiff ? get_u32(entry + 12U, little_endian)
                                    : get_u32(entry + 8U, little_endian);
            assert(pixel_offset == expected_pixel_offset);
        }
    }
    assert((omc_size)root + count_size + (omc_size)count * entry_size +
               (big_tiff ? 8U : 4U) <= file->size);
    assert(memcmp(file->data + expected_pixel_offset, "PIX!", 4U) == 0);
    return found;
}

static omc_u64
tiff_root_count(const omc_arena* file, int big_tiff, int little_endian)
{
    omc_u64 root;
    root = big_tiff ? get_u64(file->data + 8U, little_endian)
                    : (omc_u64)get_u32(file->data + 4U, little_endian);
    assert(root < file->size);
    return big_tiff ? get_u64(file->data + (omc_size)root, little_endian)
                    : (omc_u64)get_u16(file->data + (omc_size)root,
                                       little_endian);
}

static void
make_minimal_jpeg(omc_arena* file)
{
    append(file, "\377\330\377\331", 4U);
}

static void
make_truncated_tiff(omc_arena* file)
{
    omc_u8 bytes[14];
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = (omc_u8)'I';
    bytes[1] = (omc_u8)'I';
    put_u16(bytes + 2U, 42U, 1);
    put_u32(bytes + 4U, 8U, 1);
    put_u16(bytes + 8U, 1U, 1);
    put_u16(bytes + 10U, 33723U, 1);
    append(file, bytes, sizeof(bytes));
}

static void
assert_arena_bytes(const omc_arena* arena, const char* expected,
                   omc_size expected_size)
{
    assert(arena->size == expected_size);
    assert(memcmp(arena->data, expected, expected_size) == 0);
}

static void
test_remove_managed_resources(void)
{
    omc_arena file;
    omc_arena resources;
    omc_arena mixed_resources;
    omc_arena expected;
    omc_arena edited;
    omc_arena disabled_output;
    omc_arena sidecar;
    omc_store source;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;

    omc_arena_init(&file);
    omc_arena_init(&resources);
    omc_arena_init(&mixed_resources);
    omc_arena_init(&expected);
    omc_arena_init(&edited);
    omc_arena_init(&disabled_output);
    omc_arena_init(&sidecar);
    omc_store_init(&source);

    append(&file, "\377\330", 2U);
    append_resource(&mixed_resources, 0x040DU, "odd", 3U);
    mixed_resources.data[7U] = 0xA7U;
    mixed_resources.data[mixed_resources.size - 1U] = 0xB5U;
    append_resource(&mixed_resources, 0x0404U, "old", 3U);
    append_app13(&file, &mixed_resources);
    append_resource(&resources, 0x0404U, "other", 5U);
    append_app13(&file, &resources);
    append(&file, "\377\331", 2U);

    add_deleted_iptc(&source);

    omc_arena_reset(&resources);
    append(&expected, "\377\330", 2U);
    append_resource(&resources, 0x040DU, "odd", 3U);
    resources.data[7U] = 0xA7U;
    resources.data[resources.size - 1U] = 0xB5U;
    append_app13(&expected, &resources);
    append(&expected, "\377\331", 2U);

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    assert(omc_transfer_prepare(file.data, file.size, &source, &opts, &bundle) ==
           OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK);
    assert(bundle.include_iptc == 1);
    assert(bundle.remove_existing_iptc == 1);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(exec.remove_existing_iptc == 1);
    append(&edited, file.data, file.size);
    assert(omc_transfer_execute(edited.data, edited.size, &source, &edited,
                                &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(result.edited_present == 1);
    assert(edited.size == expected.size);
    assert(memcmp(edited.data, expected.data, expected.size) == 0);

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_JPEG,
                 OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD, 0, &bundle, &exec);
    assert(bundle.remove_existing_iptc == 0);
    assert(omc_transfer_execute(file.data, file.size, &source, &disabled_output,
                                &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(disabled_output.size == file.size);
    assert(memcmp(disabled_output.data, file.data, file.size) == 0);

    omc_store_fini(&source);
    omc_arena_fini(&file);
    omc_arena_fini(&resources);
    omc_arena_fini(&mixed_resources);
    omc_arena_fini(&expected);
    omc_arena_fini(&edited);
    omc_arena_fini(&disabled_output);
    omc_arena_fini(&sidecar);
}

static void
test_tiff_carrier(int big_tiff, int little_endian, int use_dng_template)
{
    omc_arena file;
    omc_arena edited;
    omc_arena sidecar;
    omc_arena previous;
    omc_store source;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;
    omc_scan_fmt format;
    omc_dng_target_mode dng_mode;
    omc_u32 pixel_offset;

    omc_arena_init(&file);
    omc_arena_init(&edited);
    omc_arena_init(&sidecar);
    omc_arena_init(&previous);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    make_tiff(&file, big_tiff, little_endian, 1);
    format = use_dng_template ? OMC_SCAN_FMT_DNG : OMC_SCAN_FMT_TIFF;
    dng_mode = use_dng_template == 1 ? OMC_DNG_TARGET_TEMPLATE
                                     : OMC_DNG_TARGET_EXISTING;
    pixel_offset = big_tiff ? 120U : 96U;
    prepare_exec(file.data, file.size, &source, format, dng_mode, 1,
                 &bundle, &exec);
    assert(bundle.remove_existing_iptc == 1);
    assert(omc_transfer_execute(file.data, file.size, &source, &edited, &sidecar,
                                &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(!tiff_has_iptc(&edited, big_tiff, little_endian, pixel_offset));

    append(&previous, edited.data, edited.size);
    assert(omc_transfer_execute(edited.data, edited.size, &source, &edited,
                                &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(edited.size == previous.size);
    assert(memcmp(edited.data, previous.data, previous.size) == 0);

    omc_store_fini(&source);
    omc_arena_fini(&file);
    omc_arena_fini(&edited);
    omc_arena_fini(&sidecar);
    omc_arena_fini(&previous);
}

static void
test_tiff_no_tag_repeat(int big_tiff, int little_endian)
{
    omc_arena file;
    omc_arena edited;
    omc_arena sidecar;
    omc_store source;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;

    omc_arena_init(&file);
    omc_arena_init(&edited);
    omc_arena_init(&sidecar);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    make_tiff(&file, big_tiff, little_endian, 0);
    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_TIFF,
                 OMC_DNG_TARGET_EXISTING, 1, &bundle, &exec);
    assert(bundle.remove_existing_iptc == 1);
    assert(omc_transfer_execute(file.data, file.size, &source, &edited, &sidecar,
                                &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(edited.size == file.size);
    assert(memcmp(edited.data, file.data, file.size) == 0);
    assert(!tiff_has_iptc(&edited, big_tiff, little_endian,
                          big_tiff ? 120U : 96U));
    assert(omc_transfer_execute(edited.data, edited.size, &source, &edited,
                                &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(edited.size == file.size);
    assert(memcmp(edited.data, file.data, file.size) == 0);

    omc_store_fini(&source);
    omc_arena_fini(&file);
    omc_arena_fini(&edited);
    omc_arena_fini(&sidecar);
}

static void
test_empty_and_duplicate_tiff_directories(void)
{
    int duplicate;
    for (duplicate = 0; duplicate <= 1; ++duplicate) {
        omc_arena file;
        omc_arena edited;
        omc_arena sidecar;
        omc_store source;
        omc_transfer_bundle bundle;
        omc_transfer_exec exec;
        omc_transfer_res result;
        omc_arena_init(&file);
        omc_arena_init(&edited);
        omc_arena_init(&sidecar);
        omc_store_init(&source);
        add_deleted_iptc(&source);
        make_tiff_only_iptc(&file, 1, duplicate);
        prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_TIFF,
                     OMC_DNG_TARGET_EXISTING, 1, &bundle, &exec);
        assert(omc_transfer_execute(file.data, file.size, &source, &edited,
                                    &sidecar, &exec, &result) == OMC_STATUS_OK);
        assert(result.status == OMC_TRANSFER_OK);
        assert(tiff_root_count(&edited, 0, 1) == 0U);
        assert(memcmp(edited.data + 96U, "PIX!", 4U) == 0);
        omc_store_fini(&source);
        omc_arena_fini(&file);
        omc_arena_fini(&edited);
        omc_arena_fini(&sidecar);
    }
}

static void
test_exif_copied_tag_is_removed(void)
{
    omc_arena file;
    omc_arena from_exif;
    omc_arena removed;
    omc_arena sidecar;
    omc_store source;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;

    omc_arena_init(&file);
    omc_arena_init(&from_exif);
    omc_arena_init(&removed);
    omc_arena_init(&sidecar);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    add_exif_iptc(&source);
    make_tiff(&file, 0, 1, 0);

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_TIFF,
                 OMC_DNG_TARGET_EXISTING, 0, &bundle, &exec);
    assert(bundle.remove_existing_iptc == 0);
    assert(omc_transfer_execute(file.data, file.size, &source, &from_exif,
                                &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(tiff_has_iptc(&from_exif, 0, 1, 96U));

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_TIFF,
                 OMC_DNG_TARGET_EXISTING, 1, &bundle, &exec);
    assert(bundle.remove_existing_iptc == 1);
    assert(omc_transfer_execute(file.data, file.size, &source, &removed,
                                &sidecar, &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_OK);
    assert(!tiff_has_iptc(&removed, 0, 1, 96U));

    omc_store_fini(&source);
    omc_arena_fini(&file);
    omc_arena_fini(&from_exif);
    omc_arena_fini(&removed);
    omc_arena_fini(&sidecar);
}

static void
test_dng_fresh_rejects_removal(void)
{
    omc_arena edited;
    omc_arena sidecar;
    omc_store source;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;

    omc_arena_init(&edited);
    omc_arena_init(&sidecar);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    append(&edited, "old-file", 8U);
    append(&sidecar, "old-sidecar", 11U);

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_DNG;
    opts.dng_target_mode = OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD;
    opts.writeback_mode = OMC_XMP_WRITEBACK_SIDECAR_ONLY;
    assert(omc_transfer_prepare(NULL, 0U, &source, &opts, &bundle) ==
           OMC_STATUS_OK);
    assert(bundle.status == OMC_TRANSFER_OK && bundle.remove_existing_iptc);
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_OK);
    assert(omc_transfer_execute(NULL, 0U, &source, &edited, &sidecar, &exec,
                                &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_UNSUPPORTED);
    assert_arena_bytes(&edited, "old-file", 8U);
    assert_arena_bytes(&sidecar, "old-sidecar", 11U);

    omc_store_fini(&source);
    omc_arena_fini(&edited);
    omc_arena_fini(&sidecar);
}

static void
test_invalid_flags_and_stale_source(void)
{
    omc_arena file;
    omc_arena edited;
    omc_arena sidecar;
    omc_store source;
    omc_transfer_prepare_opts opts;
    omc_transfer_bundle bundle;
    omc_transfer_bundle bundle_before;
    omc_transfer_exec exec;
    omc_transfer_exec exec_before;
    omc_transfer_res result;

    omc_arena_init(&file);
    omc_arena_init(&edited);
    omc_arena_init(&sidecar);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    make_minimal_jpeg(&file);
    append(&edited, "keep-edited", 11U);
    append(&sidecar, "keep-sidecar", 12U);

    omc_transfer_prepare_opts_init(&opts);
    opts.format = OMC_SCAN_FMT_JPEG;
    opts.include_iptc = 2;
    memset(&bundle, 0xA5, sizeof(bundle));
    bundle_before = bundle;
    assert(omc_transfer_prepare(file.data, file.size, &source, &opts, &bundle) ==
           OMC_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&bundle, &bundle_before, sizeof(bundle)) == 0);

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_JPEG,
                 OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD, 1, &bundle, &exec);
    bundle.include_iptc = 2;
    memset(&exec_before, 0x5A, sizeof(exec_before));
    exec = exec_before;
    assert(omc_transfer_compile(&bundle, &exec) == OMC_STATUS_INVALID_ARGUMENT);
    assert(memcmp(&exec, &exec_before, sizeof(exec)) == 0);

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_JPEG,
                 OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD, 1, &bundle, &exec);
    exec.include_iptc = 2;
    result.status = OMC_TRANSFER_LIMIT;
    assert(omc_transfer_execute(file.data, file.size, &source, &edited, &sidecar,
                                &exec, &result) == OMC_STATUS_INVALID_ARGUMENT);
    assert_arena_bytes(&edited, "keep-edited", 11U);
    assert_arena_bytes(&sidecar, "keep-sidecar", 12U);
    assert(result.status == OMC_TRANSFER_LIMIT);

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_JPEG,
                 OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD, 1, &bundle, &exec);
    exec.remove_existing_iptc = 0;
    assert(omc_transfer_execute(file.data, file.size, &source, &edited, &sidecar,
                                &exec, &result) == OMC_STATUS_INVALID_ARGUMENT);
    assert_arena_bytes(&edited, "keep-edited", 11U);
    assert_arena_bytes(&sidecar, "keep-sidecar", 12U);

    prepare_exec(file.data, file.size, &source, OMC_SCAN_FMT_JPEG,
                 OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD, 1, &bundle, &exec);
    add_active_iptc(&source);
    assert(omc_transfer_execute(file.data, file.size, &source, &edited, &sidecar,
                                &exec, &result) == OMC_STATUS_INVALID_ARGUMENT);
    assert_arena_bytes(&edited, "keep-edited", 11U);
    assert_arena_bytes(&sidecar, "keep-sidecar", 12U);

    omc_store_fini(&source);
    omc_arena_fini(&file);
    omc_arena_fini(&edited);
    omc_arena_fini(&sidecar);
}

static void
test_payload_builders_reject_removal_without_publication(void)
{
    omc_arena storage;
    omc_store source;
    omc_transfer_payload_build_opts payload_opts;
    omc_transfer_payload_batch payload_batch;
    omc_transfer_payload_io_res payload_res;
    omc_transfer_package_build_opts package_opts;
    omc_transfer_package_batch package_batch;
    omc_transfer_package_io_res package_res;

    omc_arena_init(&storage);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    append(&storage, "preserve-storage", 16U);

    omc_transfer_payload_build_opts_init(&payload_opts);
    payload_opts.format = OMC_SCAN_FMT_JPEG;
    assert(omc_transfer_payload_batch_build(&source, &payload_opts, &storage,
                                            &payload_batch, &payload_res) ==
           OMC_STATUS_OK);
    assert(payload_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(payload_batch.payload_count == 0U);
    assert_arena_bytes(&storage, "preserve-storage", 16U);

    omc_transfer_package_build_opts_init(&package_opts);
    package_opts.format = OMC_SCAN_FMT_JPEG;
    assert(omc_transfer_package_batch_build(&source, &package_opts, &storage,
                                            &package_batch, &package_res) ==
           OMC_STATUS_OK);
    assert(package_res.status == OMC_TRANSFER_UNSUPPORTED);
    assert(package_batch.chunk_count == 0U);
    assert_arena_bytes(&storage, "preserve-storage", 16U);

    omc_store_fini(&source);
    omc_arena_fini(&storage);
}

static void
test_malformed_carriers_preserve_outputs(void)
{
    omc_arena jpeg;
    omc_arena malformed_tiff;
    omc_arena resources;
    omc_arena edited;
    omc_arena sidecar;
    omc_store source;
    omc_transfer_bundle bundle;
    omc_transfer_exec exec;
    omc_transfer_res result;

    omc_arena_init(&jpeg);
    omc_arena_init(&malformed_tiff);
    omc_arena_init(&resources);
    omc_arena_init(&edited);
    omc_arena_init(&sidecar);
    omc_store_init(&source);
    add_deleted_iptc(&source);
    append(&jpeg, "\377\330", 2U);
    append(&resources, "?", 1U);
    append_app13(&jpeg, &resources);
    append(&jpeg, "\377\331", 2U);
    append(&edited, "keep-edited", 11U);
    append(&sidecar, "keep-sidecar", 12U);
    prepare_exec(jpeg.data, jpeg.size, &source, OMC_SCAN_FMT_JPEG,
                 OMC_DNG_TARGET_MINIMAL_FRESH_SCAFFOLD, 1, &bundle, &exec);
    assert(omc_transfer_execute(jpeg.data, jpeg.size, &source, &edited, &sidecar,
                                &exec, &result) == OMC_STATUS_OK);
    assert(result.status == OMC_TRANSFER_MALFORMED);
    assert_arena_bytes(&edited, "keep-edited", 11U);
    assert_arena_bytes(&sidecar, "keep-sidecar", 12U);

    make_truncated_tiff(&malformed_tiff);
    prepare_exec(malformed_tiff.data, malformed_tiff.size, &source,
                 OMC_SCAN_FMT_TIFF, OMC_DNG_TARGET_EXISTING, 1, &bundle,
                 &exec);
    result.status = OMC_TRANSFER_OK;
    assert(omc_transfer_execute(malformed_tiff.data, malformed_tiff.size,
                                &source, &edited, &sidecar, &exec, &result) ==
           OMC_STATUS_OK);
    assert(result.status != OMC_TRANSFER_OK);
    assert_arena_bytes(&edited, "keep-edited", 11U);
    assert_arena_bytes(&sidecar, "keep-sidecar", 12U);

    omc_store_fini(&source);
    omc_arena_fini(&jpeg);
    omc_arena_fini(&malformed_tiff);
    omc_arena_fini(&resources);
    omc_arena_fini(&edited);
    omc_arena_fini(&sidecar);
}

int
main(void)
{
    int big_tiff;
    int little_endian;
    test_remove_managed_resources();
    for (big_tiff = 0; big_tiff <= 1; ++big_tiff) {
        for (little_endian = 0; little_endian <= 1; ++little_endian) {
            test_tiff_carrier(big_tiff, little_endian, 0);
            test_tiff_no_tag_repeat(big_tiff, little_endian);
        }
    }
    test_empty_and_duplicate_tiff_directories();
    test_tiff_carrier(0, 1, 1);
    test_tiff_carrier(0, 1, 2);
    test_exif_copied_tag_is_removed();
    test_dng_fresh_rejects_removal();
    test_invalid_flags_and_stale_source();
    test_payload_builders_reject_removal_without_publication();
    test_malformed_carriers_preserve_outputs();
    return 0;
}
