#include "omc/omc_ccm_query.h"

#include "omc_test_assert.h"

#include <string.h>

static omc_byte_ref
append_store_bytes(omc_arena* arena, const char* text)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, text, strlen(text), &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static omc_byte_ref
append_store_raw(omc_arena* arena, const void* data, omc_size size)
{
    omc_byte_ref ref;
    omc_status status;

    status = omc_arena_append(arena, data, size, &ref);
    assert(status == OMC_STATUS_OK);
    return ref;
}

static void
add_exif_u16_scalar(omc_store* store, const char* ifd_name, omc_u16 tag,
                    omc_u16 value)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_store_bytes(&store->arena, ifd_name),
                          tag);
    omc_val_make_u16(&entry.value, value);
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    assert(status == OMC_STATUS_OK);
}

static void
add_exif_u8_array(omc_store* store, const char* ifd_name, omc_u16 tag,
                  const omc_u8* values, omc_u32 count)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_store_bytes(&store->arena, ifd_name),
                          tag);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_U8;
    entry.value.count = count;
    entry.value.u.ref = append_store_raw(&store->arena, values, count);
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    assert(status == OMC_STATUS_OK);
}

static void
add_exif_urational_array(omc_store* store, const char* ifd_name, omc_u16 tag,
                         const omc_urational* values, omc_u32 count)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_store_bytes(&store->arena, ifd_name),
                          tag);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_URATIONAL;
    entry.value.count = count;
    entry.value.u.ref = append_store_raw(&store->arena, values,
                                         (omc_size)count * sizeof(values[0]));
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    assert(status == OMC_STATUS_OK);
}

static void
add_exif_srational_array(omc_store* store, const char* ifd_name, omc_u16 tag,
                         const omc_srational* values, omc_u32 count)
{
    omc_entry entry;
    omc_status status;

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, append_store_bytes(&store->arena, ifd_name),
                          tag);
    entry.value.kind = OMC_VAL_ARRAY;
    entry.value.elem_type = OMC_ELEM_SRATIONAL;
    entry.value.count = count;
    entry.value.u.ref = append_store_raw(&store->arena, values,
                                         (omc_size)count * sizeof(values[0]));
    status = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    assert(status == OMC_STATUS_OK);
}

static void
build_basic_dng_store(omc_store* store)
{
    static const omc_u8 k_dng_version[4] = { 1U, 4U, 0U, 0U };
    static const omc_srational k_color_matrix1[9] = {
        { 2, 1 }, { 0, 1 }, { 0, 1 },
        { 0, 1 }, { 3, 1 }, { 0, 1 },
        { 0, 1 }, { 0, 1 }, { 4, 1 }
    };
    static const omc_urational k_as_shot_neutral[3] = {
        { 1U, 2U }, { 1U, 1U }, { 2U, 1U }
    };

    add_exif_u8_array(store, "ifd0", 0xC612U, k_dng_version, 4U);
    add_exif_srational_array(store, "ifd0", 0xC621U, k_color_matrix1, 9U);
    add_exif_u16_scalar(store, "ifd0", 0xC65AU, 21U);
    add_exif_urational_array(store, "ifd0", 0xC628U, k_as_shot_neutral, 3U);
}

static void
build_no_context_store(omc_store* store)
{
    static const omc_srational k_color_matrix1[9] = {
        { 2, 1 }, { 0, 1 }, { 0, 1 },
        { 0, 1 }, { 3, 1 }, { 0, 1 },
        { 0, 1 }, { 0, 1 }, { 4, 1 }
    };

    add_exif_srational_array(store, "ifd0", 0xC621U, k_color_matrix1, 9U);
    add_exif_u16_scalar(store, "ifd0", 0xC65AU, 21U);
}

static void
build_issue_store(omc_store* store)
{
    static const omc_u8 k_dng_version[4] = { 1U, 4U, 0U, 0U };
    static const omc_urational k_as_shot_neutral[3] = {
        { 1U, 2U }, { 1U, 1U }, { 2U, 1U }
    };
    static const omc_urational k_as_shot_white_xy[2] = {
        { 3U, 10U }, { 3U, 10U }
    };

    add_exif_u8_array(store, "ifd0", 0xC612U, k_dng_version, 4U);
    add_exif_u16_scalar(store, "ifd0", 0xC65AU, 21U);
    add_exif_urational_array(store, "ifd0", 0xC628U, k_as_shot_neutral, 3U);
    add_exif_urational_array(store, "ifd0", 0xC629U, k_as_shot_white_xy, 2U);
}

static int
has_issue(const omc_ccm_issue* issues, omc_u32 count, omc_ccm_issue_code code)
{
    omc_u32 i;

    for (i = 0U; i < count; ++i) {
        if (issues[i].code == code) {
            return 1;
        }
    }
    return 0;
}

static double
double_abs(double value)
{
    if (value < 0.0) {
        return -value;
    }
    return value;
}

static void
test_collects_basic_dng_fields(void)
{
    omc_store store;
    omc_ccm_field fields[8];
    double values[32];
    omc_ccm_issue issues[8];
    omc_ccm_query_res res;

    omc_store_init(&store);
    build_basic_dng_store(&store);

    res = omc_ccm_collect_fields(&store, fields, 8U, values, 32U, issues, 8U,
                                 (const omc_ccm_query_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_CCM_QUERY_OK);
    OMC_TEST_CHECK_U64_EQ(res.fields_needed, 3U);
    OMC_TEST_CHECK_U64_EQ(res.fields_written, 3U);
    OMC_TEST_CHECK_U64_EQ(res.values_needed, 13U);
    OMC_TEST_CHECK_U64_EQ(res.values_written, 13U);
    OMC_TEST_CHECK_U64_EQ(res.issues_needed, 0U);
    OMC_TEST_CHECK_U64_EQ(fields[0].kind, OMC_CCM_FIELD_COLOR_MATRIX1);
    OMC_TEST_CHECK_U64_EQ(fields[0].rows, 3U);
    OMC_TEST_CHECK_U64_EQ(fields[0].cols, 3U);
    OMC_TEST_CHECK_U64_EQ(fields[0].values_offset, 0U);
    OMC_TEST_CHECK_U64_EQ(fields[0].values_count, 9U);
    OMC_TEST_CHECK(double_abs(values[0] - 2.0) < 1e-12);
    OMC_TEST_CHECK(double_abs(values[4] - 3.0) < 1e-12);
    OMC_TEST_CHECK(double_abs(values[8] - 4.0) < 1e-12);
    OMC_TEST_CHECK_U64_EQ(fields[1].kind, OMC_CCM_FIELD_CALIBRATION_ILLUMINANT1);
    OMC_TEST_CHECK_U64_EQ(fields[1].rows, 1U);
    OMC_TEST_CHECK_U64_EQ(fields[1].cols, 1U);
    OMC_TEST_CHECK(double_abs(values[fields[1].values_offset] - 21.0) < 1e-12);
    OMC_TEST_CHECK_U64_EQ(fields[2].kind, OMC_CCM_FIELD_AS_SHOT_NEUTRAL);
    OMC_TEST_CHECK_U64_EQ(fields[2].rows, 1U);
    OMC_TEST_CHECK_U64_EQ(fields[2].cols, 3U);
    OMC_TEST_CHECK(double_abs(values[fields[2].values_offset] - 0.5) < 1e-12);

    omc_store_fini(&store);
}

static void
test_requires_dng_context_by_default(void)
{
    omc_store store;
    omc_ccm_field fields[4];
    double values[16];
    omc_ccm_query_opts opts;
    omc_ccm_query_res res;

    omc_store_init(&store);
    build_no_context_store(&store);

    res = omc_ccm_collect_fields(&store, fields, 4U, values, 16U,
                                 (omc_ccm_issue*)0, 0U,
                                 (const omc_ccm_query_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_CCM_QUERY_OK);
    OMC_TEST_CHECK_U64_EQ(res.fields_needed, 0U);
    OMC_TEST_CHECK_U64_EQ(res.fields_written, 0U);

    omc_ccm_query_opts_init(&opts);
    opts.require_dng_context = 0;
    res = omc_ccm_collect_fields(&store, fields, 4U, values, 16U,
                                 (omc_ccm_issue*)0, 0U, &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_CCM_QUERY_OK);
    OMC_TEST_CHECK_U64_EQ(res.fields_needed, 2U);
    OMC_TEST_CHECK_U64_EQ(res.fields_written, 2U);

    omc_store_fini(&store);
}

static void
test_reports_field_and_cross_field_issues(void)
{
    omc_store store;
    omc_ccm_field fields[8];
    double values[16];
    omc_ccm_issue issues[8];
    omc_ccm_query_res res;

    omc_store_init(&store);
    build_issue_store(&store);

    res = omc_ccm_collect_fields(&store, fields, 8U, values, 16U, issues, 8U,
                                 (const omc_ccm_query_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_CCM_QUERY_OK);
    OMC_TEST_CHECK_U64_EQ(res.fields_needed, 3U);
    OMC_TEST_CHECK(res.warning_count >= 2U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_CCM_ISSUE_MISSING_COMPANION_TAG));
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_CCM_ISSUE_AS_SHOT_CONFLICT));

    omc_store_fini(&store);
}

static void
test_reports_limit_for_output_caps(void)
{
    omc_store store;
    omc_ccm_field fields[1];
    double values[9];
    omc_ccm_query_res res;

    omc_store_init(&store);
    build_basic_dng_store(&store);

    res = omc_ccm_collect_fields(&store, fields, 1U, values, 9U,
                                 (omc_ccm_issue*)0, 0U,
                                 (const omc_ccm_query_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_CCM_QUERY_LIMIT_EXCEEDED);
    OMC_TEST_CHECK_U64_EQ(res.fields_needed, 3U);
    OMC_TEST_CHECK_U64_EQ(res.fields_written, 1U);
    OMC_TEST_CHECK_U64_EQ(res.values_written, 9U);
    OMC_TEST_CHECK_U64_EQ(fields[0].kind, OMC_CCM_FIELD_COLOR_MATRIX1);

    omc_store_fini(&store);
}

int
main(void)
{
    test_collects_basic_dng_fields();
    test_requires_dng_context_by_default();
    test_reports_field_and_cross_field_issues();
    test_reports_limit_for_output_caps();
    return omc_test_finish();
}
