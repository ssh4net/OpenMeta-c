#include "omc/omc_exif_name.h"
#include "omc/omc_key.h"
#include "omc/omc_store.h"
#include "omc/omc_val.h"

#include <assert.h>
#include <string.h>

static void
expect_tag_name(const char* ifd_name, omc_u16 tag, const char* expected)
{
    char name[64];
    omc_size name_len;
    omc_exif_name_status status;

    status = omc_exif_tag_name(ifd_name, tag, name, sizeof(name), &name_len);
    if (expected == (const char*)0) {
        assert(status == OMC_EXIF_NAME_EMPTY);
        assert(name_len == 0U);
        assert(name[0] == '\0');
        return;
    }

    assert(status == OMC_EXIF_NAME_OK);
    assert(name_len == strlen(expected));
    assert(strcmp(name, expected) == 0);
}

static const omc_entry*
add_test_entry(omc_store* store, const char* ifd_name, omc_u16 tag,
               omc_entry_name_ctx_kind kind, omc_u8 variant,
               omc_u64 scalar_value, int set_scalar)
{
    omc_byte_ref ifd_ref;
    omc_entry entry;
    omc_status st;

    st = omc_arena_append(&store->arena, ifd_name, strlen(ifd_name), &ifd_ref);
    assert(st == OMC_STATUS_OK);

    memset(&entry, 0, sizeof(entry));
    omc_key_make_exif_tag(&entry.key, ifd_ref, tag);
    omc_val_init(&entry.value);
    if (set_scalar) {
        omc_val_make_u64(&entry.value, scalar_value);
    }
    entry.origin.block = OMC_INVALID_BLOCK_ID;
    entry.origin.name_context_kind = kind;
    entry.origin.name_context_variant = variant;
    if (kind != OMC_ENTRY_NAME_CTX_NONE) {
        entry.flags |= OMC_ENTRY_FLAG_CONTEXTUAL_NAME;
    }

    st = omc_store_add_entry(store, &entry, (omc_entry_id*)0);
    assert(st == OMC_STATUS_OK);
    return &store->entries[store->entry_count - 1U];
}

static const omc_entry*
add_context_entry(omc_store* store, const char* ifd_name, omc_u16 tag,
                  omc_entry_name_ctx_kind kind, omc_u8 variant)
{
    return add_test_entry(store, ifd_name, tag, kind, variant, 0U, 0);
}

static const omc_entry*
add_plain_entry(omc_store* store, const char* ifd_name, omc_u16 tag)
{
    return add_test_entry(store, ifd_name, tag, OMC_ENTRY_NAME_CTX_NONE, 0U,
                          0U, 0);
}

static const omc_entry*
add_plain_scalar_entry(omc_store* store, const char* ifd_name, omc_u16 tag,
                       omc_u64 scalar_value)
{
    return add_test_entry(store, ifd_name, tag, OMC_ENTRY_NAME_CTX_NONE, 0U,
                          scalar_value, 1);
}

static void
expect_entry_name(const omc_store* store, const omc_entry* entry,
                  omc_exif_name_policy policy, const char* expected)
{
    char name[96];
    omc_size name_len;
    omc_exif_name_status status;

    status = omc_exif_entry_name(store, entry, policy,
                                 name, sizeof(name), &name_len);
    if (expected == (const char*)0) {
        assert(status == OMC_EXIF_NAME_EMPTY);
        assert(name_len == 0U);
        assert(name[0] == '\0');
        return;
    }

    assert(status == OMC_EXIF_NAME_OK);
    assert(name_len == strlen(expected));
    assert(strcmp(name, expected) == 0);
}

static void
test_tag_name_basics(void)
{
    expect_tag_name("ifd0", 0x010FU, "Make");
    expect_tag_name("mk_apple0", 0x0045U, "FrontFacingCamera");
    expect_tag_name("mk_casio0", 0x0E00U, "PrintIM");
    expect_tag_name("mk_casio_type2_0", 0x0002U, "PreviewImageSize");
    expect_tag_name("mk_casio_type2_0", 0x2002U, "Casio_Type2_0x2002");
    expect_tag_name("mk_canon0", 0x0038U, "BatteryType");
    expect_tag_name("mk_nikonsettings_main_0", 0x0001U, "ISOAutoHiLimit");
    expect_tag_name("mk_fuji0", 0x1200U, "FujiFilm_0x1200");
    expect_tag_name("mk_minolta0", 0x0103U, "MinoltaImageSize");
    expect_tag_name("mk_minolta0", 0x0106U, "Minolta_0x0106");
    expect_tag_name("mk_panasonic0", 0x0022U, "Panasonic_0x0022");
    expect_tag_name("mk_panasonic0", 0x0058U, "ThumbnailWidth");
    expect_tag_name("mk_pentax0", 0x0062U, "RawDevelopmentProcess");
    expect_tag_name("mk_ricoh0", 0x1002U, "DriveMode");
    expect_tag_name("mk_samsung_ifd_0", 0x0002U, "Samsung_IFD_0x0002");
    expect_tag_name("mk_samsung_type2_0", 0xA002U, "SerialNumber");
    expect_tag_name("unknown_ifd", 0x010FU, (const char*)0);
}

static void
test_contextual_entry_names(void)
{
    omc_store store;
    const omc_entry* entry;

    omc_store_init(&store);

    entry = add_context_entry(&store, "mk_fuji0", 0x1304U,
                              OMC_ENTRY_NAME_CTX_FUJIFILM_MAIN_1304, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "GEImageSize");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "FujiFilm_0x1304");

    entry = add_context_entry(&store, "mk_casio_type2_0", 0x0002U,
                              OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "PreviewImageSize");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Quality");

    entry = add_context_entry(&store, "mk_casio_type2_0", 0x0008U,
                              OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "QualityMode");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Casio_0x0008");

    entry = add_context_entry(&store, "mk_casio_type2_0", 0x0E00U,
                              OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "Casio_Type2_0x0e00");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "PrintIM");

    entry = add_context_entry(&store, "mk_kodak0", 0x0028U,
                              OMC_ENTRY_NAME_CTX_KODAK_MAIN_0028, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "Distance1");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "KodakModel");

    entry = add_context_entry(&store, "mk_olympus_focusinfo_0", 0x1600U,
                              OMC_ENTRY_NAME_CTX_OLYMPUS_FOCUSINFO_1600, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ImageStabilization");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "ImageStabilization");

    entry = add_context_entry(&store, "mk_olympus_focusinfo_0", 0x1600U,
                              OMC_ENTRY_NAME_CTX_OLYMPUS_FOCUSINFO_1600, 2U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ImageStabilization");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Olympus_FocusInfo_0x1600");

    entry = add_context_entry(&store, "mk_motorola0", 0x6420U,
                              OMC_ENTRY_NAME_CTX_MOTOROLA_MAIN_6420, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "CustomRendered");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Motorola_0x6420");

    entry = add_context_entry(&store, "mk_nikonsettings_main_0", 0x0001U,
                              OMC_ENTRY_NAME_CTX_NIKONSETTINGS_MAIN, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "ISOAutoHiLimit");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "NikonSettings_0x0001");

    entry = add_context_entry(&store, "mk_ricoh0", 0x1002U,
                              OMC_ENTRY_NAME_CTX_RICOH_MAIN_COMPAT, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "DriveMode");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Ricoh_0x1002");

    entry = add_context_entry(&store, "mk_ricoh0", 0x1003U,
                              OMC_ENTRY_NAME_CTX_RICOH_MAIN_COMPAT, 2U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "Sharpness");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "WhiteBalance");

    entry = add_context_entry(&store, "mk_pentax0", 0x0062U,
                              OMC_ENTRY_NAME_CTX_PENTAX_MAIN_0062, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "RawDevelopmentProcess");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Pentax_0x0062");

    entry = add_context_entry(&store, "mk_canon0", 0x0038U,
                              OMC_ENTRY_NAME_CTX_CANON_MAIN_0038, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "BatteryType");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Canon_0x0038");

    entry = add_context_entry(&store, "mk_canon_colorcalib_0", 0x0038U,
                              OMC_ENTRY_NAME_CTX_CANON_COLORCALIB_0038, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "CameraColorCalibration15");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "BatteryType");

    entry = add_context_entry(&store, "mk_canon_camerainfo1d_0", 0x0048U,
                              OMC_ENTRY_NAME_CTX_CANON_CAMERAINFO1D_0048, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ColorTemperature");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Sharpness");

    entry = add_context_entry(&store, "mk_canoncustom_functions2_0", 0x0103U,
                              OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0103, 1U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ISOSpeedRange");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "ISOExpansion");

    entry = add_context_entry(&store, "mk_canoncustom_functions2_0", 0x0103U,
                              OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0103, 2U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ISOSpeedRange");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "ISOSpeedRange");

    omc_store_fini(&store);
}

static void
test_compat_entry_names_without_context_flag(void)
{
    omc_store store;
    const omc_entry* entry;

    omc_store_init(&store);

    entry = add_plain_entry(&store, "mk_samsung_type2_0", 0xA002U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "SerialNumber");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Samsung_Type2_0xa002");

    entry = add_plain_entry(&store, "mk_minolta0", 0x0018U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ImageStabilization");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Minolta_0x0018");

    entry = add_plain_entry(&store, "mk_panasonic0", 0x0004U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL, "Model");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Panasonic_0x0004");

    entry = add_plain_entry(&store, "mk_panasonic0", 0x0058U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "ThumbnailWidth");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Panasonic_0x0058");

    entry = add_plain_scalar_entry(&store, "mk_panasonic0", 0x00C4U, 65535U);
    expect_entry_name(&store, entry, OMC_EXIF_NAME_CANONICAL,
                      "Panasonic_0x00c4");
    expect_entry_name(&store, entry, OMC_EXIF_NAME_EXIFTOOL_COMPAT,
                      "Panasonic_0x00c4");

    omc_store_fini(&store);
}

int
main(void)
{
    test_tag_name_basics();
    test_contextual_entry_names();
    test_compat_entry_names_without_context_flag();
    return 0;
}
