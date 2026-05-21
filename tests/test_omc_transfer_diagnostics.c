#include "omc/omc_transfer.h"

#include "omc_test_assert.h"
#include <string.h>

static omc_byte_ref
append_text(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, (omc_size)strlen(text), &ref);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
    return ref;
}

static void
add_exif_u16(omc_store* store, const char* ifd, omc_u16 tag, omc_u16 value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_text(&store->arena, ifd), tag);
    omc_val_make_u16(&entry.value, value);
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_xmp_text(omc_store* store, const char* schema, const char* path,
             const char* value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_xmp_property(&entry.key, append_text(&store->arena, schema),
                              append_text(&store->arena, path));
    omc_val_make_text(&entry.value, append_text(&store->arena, value),
                      OMC_TEXT_UTF8);
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_icc_tag(omc_store* store, omc_u32 signature)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_icc_tag(&entry.key, signature);
    omc_val_make_u32(&entry.value, 1U);
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static void
add_jumbf_field(omc_store* store, const char* field)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_jumbf_field(&entry.key, append_text(&store->arena, field));
    omc_val_make_u16(&entry.value, 1U);
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    OMC_TEST_REQUIRE_U64_EQ(status, OMC_STATUS_OK);
}

static const omc_transfer_diagnostic*
find_diagnostic(const omc_transfer_diagnostic* diagnostics, omc_u32 count,
                omc_transfer_diagnostic_kind kind)
{
    omc_u32 i;

    for (i = 0U; i < count; ++i) {
        if (diagnostics[i].kind == kind) {
            return &diagnostics[i];
        }
    }
    return (const omc_transfer_diagnostic*)0;
}

static omc_u32
count_diagnostics(const omc_transfer_diagnostic* diagnostics, omc_u32 count,
                  omc_transfer_diagnostic_kind kind)
{
    omc_u32 found;
    omc_u32 i;

    found = 0U;
    for (i = 0U; i < count; ++i) {
        if (diagnostics[i].kind == kind) {
            found += 1U;
        }
    }
    return found;
}

static void
fill_diagnostic_store(omc_store* store)
{
    add_exif_u16(store, "ifd0", 0x0100U, 640U);
    add_exif_u16(store, "ifd0", 0xC621U, 1U);
    add_xmp_text(store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                 "crs:ProcessVersion", "1");
    add_icc_tag(store, 0x64657363U);
    add_exif_u16(store, "exififd", 0x927CU, 1U);
    add_jumbf_field(store, "jumb/assertion");
    add_jumbf_field(store, "c2pa/claim");
}

static void
check_diagnostic_names(void)
{
    OMC_TEST_CHECK_MEM_EQ(omc_transfer_diagnostic_kind_name(
                              OMC_TRANSFER_DIAGNOSTIC_IMAGE_PROPERTIES),
                          16U, "image_properties", 16U);
    OMC_TEST_CHECK_MEM_EQ(
        omc_transfer_diagnostic_action_name(
            OMC_TRANSFER_DIAGNOSTIC_REQUIRES_TARGET_IMAGE_SPEC),
        26U, "requires_target_image_spec", 26U);
    OMC_TEST_CHECK_MEM_EQ(omc_transfer_diagnostic_reason_name(
                              OMC_TRANSFER_DIAGNOSTIC_REASON_UNKNOWN),
                          7U, "unknown", 7U);
    OMC_TEST_CHECK_MEM_EQ(omc_transfer_diagnostic_reason_name(
                              OMC_TRANSFER_DIAGNOSTIC_REASON_RENDERED_UNSAFE),
                          15U, "rendered_unsafe", 15U);
    OMC_TEST_CHECK_MEM_EQ(omc_transfer_diagnostic_severity_name(
                              OMC_TRANSFER_DIAGNOSTIC_WARNING),
                          7U, "warning", 7U);
    OMC_TEST_CHECK_MEM_EQ(omc_transfer_diagnostic_message(
                              (const omc_transfer_diagnostic*)0),
                          65U,
                          "metadata has no safe automatic transfer action for "
                          "this mode",
                          65U);
}

static void
check_compatible_file_diagnostics(void)
{
    omc_store store;
    omc_transfer_diagnostic diagnostics[8];
    omc_transfer_diagnostics_res res;
    const omc_transfer_diagnostic* diagnostic;

    omc_store_init(&store);
    fill_diagnostic_store(&store);

    res = omc_transfer_diagnostics_from_store(
        &store, OMC_TRANSFER_SAFETY_COMPATIBLE_FILE, diagnostics, 8U);
    OMC_TEST_CHECK_U64_EQ(res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(res.needed, 7U);
    OMC_TEST_CHECK_U64_EQ(res.written, 7U);
    OMC_TEST_CHECK_U64_EQ(res.kept_count, 6U);
    OMC_TEST_CHECK_U64_EQ(res.dropped_count, 0U);
    OMC_TEST_CHECK_U64_EQ(res.requires_target_image_spec_count, 1U);
    OMC_TEST_CHECK_U64_EQ(res.rendered_unsafe_count, 0U);
    OMC_TEST_CHECK_U64_EQ(res.source_bound_count, 7U);

    diagnostic = find_diagnostic(diagnostics, res.written,
                                 OMC_TRANSFER_DIAGNOSTIC_IMAGE_PROPERTIES);
    OMC_TEST_REQUIRE(diagnostic != (const omc_transfer_diagnostic*)0);
    OMC_TEST_CHECK_U64_EQ(diagnostic->action,
                          OMC_TRANSFER_DIAGNOSTIC_REQUIRES_TARGET_IMAGE_SPEC);
    OMC_TEST_CHECK(diagnostic->requires_target_image_spec);
    OMC_TEST_CHECK_MEM_EQ(
        omc_transfer_diagnostic_message(diagnostic), 109U,
        "source value describes target-owned image properties; provide target "
        "image specs or write a target-correct value",
        109U);

    diagnostic = find_diagnostic(diagnostics, res.written,
                                 OMC_TRANSFER_DIAGNOSTIC_RAW_COLOR_CALIBRATION);
    OMC_TEST_REQUIRE(diagnostic != (const omc_transfer_diagnostic*)0);
    OMC_TEST_CHECK_U64_EQ(diagnostic->action, OMC_TRANSFER_DIAGNOSTIC_KEEP);
    OMC_TEST_CHECK(diagnostic->compatible_file_safe);
    OMC_TEST_CHECK(!diagnostic->rendered_image_safe);
    OMC_TEST_CHECK_MEM_EQ(omc_transfer_diagnostic_message(diagnostic), 52U,
                          "metadata is safe to keep for this transfer mode",
                          52U);

    omc_store_fini(&store);
}

static void
check_rendered_image_diagnostics(void)
{
    omc_store store;
    omc_transfer_diagnostic diagnostics[8];
    omc_transfer_diagnostics_res res;
    const omc_transfer_diagnostic* diagnostic;

    omc_store_init(&store);
    fill_diagnostic_store(&store);

    res = omc_transfer_diagnostics_from_store(
        &store, OMC_TRANSFER_SAFETY_RENDERED_IMAGE, diagnostics, 8U);
    OMC_TEST_CHECK_U64_EQ(res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(res.needed, 7U);
    OMC_TEST_CHECK_U64_EQ(res.written, 7U);
    OMC_TEST_CHECK_U64_EQ(res.kept_count, 0U);
    OMC_TEST_CHECK_U64_EQ(res.dropped_count, 6U);
    OMC_TEST_CHECK_U64_EQ(res.requires_target_image_spec_count, 1U);
    OMC_TEST_CHECK_U64_EQ(res.rendered_unsafe_count, 6U);
    OMC_TEST_CHECK_U64_EQ(res.source_bound_count, 7U);

    diagnostic = find_diagnostic(diagnostics, res.written,
                                 OMC_TRANSFER_DIAGNOSTIC_C2PA);
    OMC_TEST_REQUIRE(diagnostic != (const omc_transfer_diagnostic*)0);
    OMC_TEST_CHECK_U64_EQ(diagnostic->action, OMC_TRANSFER_DIAGNOSTIC_DROP);
    OMC_TEST_CHECK_U64_EQ(diagnostic->severity,
                          OMC_TRANSFER_DIAGNOSTIC_WARNING);
    OMC_TEST_CHECK_MEM_EQ(
        omc_transfer_diagnostic_message(diagnostic), 91U,
        "source C2PA metadata is bound to source bytes and will be dropped for "
        "rendered-image transfer",
        91U);

    omc_store_fini(&store);
}

static void
check_diagnostics_match_safety_audit(void)
{
    omc_store store;
    omc_transfer_diagnostic diagnostics[8];
    omc_transfer_diagnostics_res res;
    omc_transfer_safety_audit audit;
    omc_u32 source_total;
    omc_u32 rendered_drop_total;

    omc_store_init(&store);
    fill_diagnostic_store(&store);

    res = omc_transfer_diagnostics_from_store(
        &store, OMC_TRANSFER_SAFETY_RENDERED_IMAGE, diagnostics, 8U);
    audit = omc_transfer_safety_audit_from_store(
        &store, OMC_TRANSFER_SAFETY_RENDERED_IMAGE);

    source_total = audit.source_image_properties
                   + audit.source_raw_color_calibration
                   + audit.source_camera_raw_settings
                   + audit.source_icc_profiles + audit.source_makernotes
                   + audit.source_non_c2pa_jumbf + audit.source_c2pa;
    rendered_drop_total = audit.filtered_raw_color_calibration
                          + audit.filtered_camera_raw_settings
                          + audit.filtered_icc_profiles
                          + audit.filtered_makernotes
                          + audit.filtered_non_c2pa_jumbf
                          + audit.invalidated_c2pa;

    OMC_TEST_CHECK_U64_EQ(res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(res.needed, source_total);
    OMC_TEST_CHECK_U64_EQ(res.requires_target_image_spec_count,
                          audit.filtered_image_properties);
    OMC_TEST_CHECK_U64_EQ(res.dropped_count, rendered_drop_total);
    OMC_TEST_CHECK_U64_EQ(res.rendered_unsafe_count, rendered_drop_total);
    OMC_TEST_CHECK_U64_EQ(
        count_diagnostics(diagnostics, res.written,
                          OMC_TRANSFER_DIAGNOSTIC_IMAGE_PROPERTIES),
        audit.source_image_properties);
    OMC_TEST_CHECK_U64_EQ(
        count_diagnostics(diagnostics, res.written,
                          OMC_TRANSFER_DIAGNOSTIC_RAW_COLOR_CALIBRATION),
        audit.source_raw_color_calibration);
    OMC_TEST_CHECK_U64_EQ(
        count_diagnostics(diagnostics, res.written,
                          OMC_TRANSFER_DIAGNOSTIC_CAMERA_RAW_SETTINGS),
        audit.source_camera_raw_settings);
    OMC_TEST_CHECK_U64_EQ(count_diagnostics(diagnostics, res.written,
                                            OMC_TRANSFER_DIAGNOSTIC_ICC_PROFILE),
                          audit.source_icc_profiles);
    OMC_TEST_CHECK_U64_EQ(count_diagnostics(diagnostics, res.written,
                                            OMC_TRANSFER_DIAGNOSTIC_MAKERNOTE),
                          audit.source_makernotes);
    OMC_TEST_CHECK_U64_EQ(
        count_diagnostics(diagnostics, res.written,
                          OMC_TRANSFER_DIAGNOSTIC_NON_C2PA_JUMBF),
        audit.source_non_c2pa_jumbf);
    OMC_TEST_CHECK_U64_EQ(count_diagnostics(diagnostics, res.written,
                                            OMC_TRANSFER_DIAGNOSTIC_C2PA),
                          audit.source_c2pa);

    res = omc_transfer_diagnostics_from_store(
        &store, OMC_TRANSFER_SAFETY_COMPATIBLE_FILE,
        (omc_transfer_diagnostic*)0, 0U);
    audit = omc_transfer_safety_audit_from_store(
        &store, OMC_TRANSFER_SAFETY_COMPATIBLE_FILE);
    OMC_TEST_CHECK_U64_EQ(res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(res.written, 0U);
    OMC_TEST_CHECK_U64_EQ(res.needed, source_total);
    OMC_TEST_CHECK_U64_EQ(res.kept_count,
                          source_total - audit.filtered_image_properties);
    OMC_TEST_CHECK_U64_EQ(res.dropped_count, 0U);
    OMC_TEST_CHECK_U64_EQ(res.requires_target_image_spec_count,
                          audit.filtered_image_properties);

    omc_store_fini(&store);
}

static void
check_diagnostic_capacity(void)
{
    omc_store store;
    omc_transfer_diagnostic diagnostics[2];
    omc_transfer_diagnostics_res res;

    omc_store_init(&store);
    fill_diagnostic_store(&store);

    res = omc_transfer_diagnostics_from_store(
        &store, OMC_TRANSFER_SAFETY_RENDERED_IMAGE, diagnostics, 2U);
    OMC_TEST_CHECK_U64_EQ(res.status, OMC_TRANSFER_OK);
    OMC_TEST_CHECK_U64_EQ(res.needed, 7U);
    OMC_TEST_CHECK_U64_EQ(res.written, 2U);

    omc_store_fini(&store);
}

int
main(void)
{
    check_diagnostic_names();
    check_compatible_file_diagnostics();
    check_rendered_image_diagnostics();
    check_diagnostics_match_safety_audit();
    check_diagnostic_capacity();
    return omc_test_finish();
}
