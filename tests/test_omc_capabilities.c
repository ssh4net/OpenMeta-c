#include "omc/omc_capabilities.h"

#include "omc_test_assert.h"

static void
check_capability_names(void)
{
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_family_name(
                              OMC_METADATA_CAPABILITY_EXIF),
                          4U, "exif", 4U);
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_family_name(
                              OMC_METADATA_CAPABILITY_EXR_ATTRIBUTE),
                          13U, "exr_attribute", 13U);
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_family_name(
                              (omc_metadata_capability_family)255),
                          7U, "unknown", 7U);

    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_support_name(
                              OMC_METADATA_CAPABILITY_UNSUPPORTED),
                          11U, "unsupported", 11U);
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_support_name(
                              OMC_METADATA_CAPABILITY_SUPPORTED),
                          9U, "supported", 9U);
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_support_name(
                              OMC_METADATA_CAPABILITY_BOUNDED),
                          7U, "bounded", 7U);
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_support_name(
                              OMC_METADATA_CAPABILITY_DISABLED),
                          8U, "disabled", 8U);
    OMC_TEST_CHECK_MEM_EQ(omc_metadata_capability_support_name(
                              (omc_metadata_capability_support)255),
                          7U, "unknown", 7U);

    OMC_TEST_CHECK(!omc_metadata_capability_available(
        OMC_METADATA_CAPABILITY_UNSUPPORTED));
    OMC_TEST_CHECK(
        omc_metadata_capability_available(OMC_METADATA_CAPABILITY_SUPPORTED));
    OMC_TEST_CHECK(
        omc_metadata_capability_available(OMC_METADATA_CAPABILITY_BOUNDED));
    OMC_TEST_CHECK(
        !omc_metadata_capability_available(OMC_METADATA_CAPABILITY_DISABLED));
}

static void
check_primary_transfer_targets(void)
{
    static const omc_scan_fmt formats[]
        = { OMC_SCAN_FMT_JPEG, OMC_SCAN_FMT_TIFF, OMC_SCAN_FMT_DNG,
            OMC_SCAN_FMT_PNG,  OMC_SCAN_FMT_WEBP, OMC_SCAN_FMT_JP2,
            OMC_SCAN_FMT_JXL,  OMC_SCAN_FMT_HEIF, OMC_SCAN_FMT_AVIF,
            OMC_SCAN_FMT_CR3 };
    unsigned i;

    for (i = 0U; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        omc_metadata_capability exif_cap;
        omc_metadata_capability xmp_cap;
        omc_metadata_capability icc_cap;

        exif_cap = omc_metadata_capability_query(formats[i],
                                                 OMC_METADATA_CAPABILITY_EXIF);
        xmp_cap  = omc_metadata_capability_query(formats[i],
                                                 OMC_METADATA_CAPABILITY_XMP);
        icc_cap  = omc_metadata_capability_query(formats[i],
                                                 OMC_METADATA_CAPABILITY_ICC);

        OMC_TEST_CHECK(omc_metadata_capability_available(exif_cap.read));
        OMC_TEST_CHECK(
            omc_metadata_capability_available(exif_cap.transfer_prepare));
        OMC_TEST_CHECK(omc_metadata_capability_available(xmp_cap.read));
        OMC_TEST_CHECK(
            omc_metadata_capability_available(xmp_cap.transfer_prepare));
        OMC_TEST_CHECK(omc_metadata_capability_available(icc_cap.read));
        OMC_TEST_CHECK(
            omc_metadata_capability_available(icc_cap.transfer_prepare));
    }
}

static void
check_bounded_family_details(void)
{
    omc_metadata_capability cap;

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_HEIF,
                                        OMC_METADATA_CAPABILITY_EXIF);
    OMC_TEST_CHECK_U64_EQ(cap.target_edit, OMC_METADATA_CAPABILITY_BOUNDED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_HEIF,
                                        OMC_METADATA_CAPABILITY_BMFF_FIELDS);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_SUPPORTED);
    OMC_TEST_CHECK_U64_EQ(cap.structured_decode,
                          OMC_METADATA_CAPABILITY_SUPPORTED);
    OMC_TEST_CHECK_U64_EQ(cap.transfer_prepare,
                          OMC_METADATA_CAPABILITY_UNSUPPORTED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_JPEG,
                                        OMC_METADATA_CAPABILITY_MAKERNOTE);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_BOUNDED);
    OMC_TEST_CHECK_U64_EQ(cap.target_edit, OMC_METADATA_CAPABILITY_BOUNDED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_PNG,
                                        OMC_METADATA_CAPABILITY_MAKERNOTE);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_UNSUPPORTED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_JPEG,
                                        OMC_METADATA_CAPABILITY_PHOTOSHOP_IRB);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_BOUNDED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_WEBP,
                                        OMC_METADATA_CAPABILITY_JUMBF);
    OMC_TEST_CHECK_U64_EQ(cap.transfer_prepare,
                          OMC_METADATA_CAPABILITY_UNSUPPORTED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_JXL,
                                        OMC_METADATA_CAPABILITY_JUMBF);
    OMC_TEST_CHECK_U64_EQ(cap.transfer_prepare,
                          OMC_METADATA_CAPABILITY_BOUNDED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_HEIF,
                                        OMC_METADATA_CAPABILITY_C2PA);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_BOUNDED);
    OMC_TEST_CHECK_U64_EQ(cap.target_edit, OMC_METADATA_CAPABILITY_UNSUPPORTED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_JP2,
                                        OMC_METADATA_CAPABILITY_GEOTIFF);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_SUPPORTED);
    OMC_TEST_CHECK_U64_EQ(cap.transfer_prepare,
                          OMC_METADATA_CAPABILITY_UNSUPPORTED);

    cap = omc_metadata_capability_query(OMC_SCAN_FMT_EXR,
                                        OMC_METADATA_CAPABILITY_EXR_ATTRIBUTE);
    OMC_TEST_CHECK_U64_EQ(cap.read, OMC_METADATA_CAPABILITY_SUPPORTED);
    OMC_TEST_CHECK_U64_EQ(cap.transfer_prepare,
                          OMC_METADATA_CAPABILITY_UNSUPPORTED);
}

int
main(void)
{
    check_capability_names();
    check_primary_transfer_targets();
    check_bounded_family_details();
    return omc_test_finish();
}
