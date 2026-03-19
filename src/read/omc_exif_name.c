#include "omc/omc_exif_name.h"

#include <string.h>

typedef struct omc_exif_name_map {
    const char* ifd_name;
    omc_u16 tag;
    const char* name;
} omc_exif_name_map;

static int
omc_exif_name_starts_with(const char* text, const char* prefix)
{
    omc_size prefix_len;

    if (text == (const char*)0 || prefix == (const char*)0) {
        return 0;
    }
    prefix_len = strlen(prefix);
    return strncmp(text, prefix, prefix_len) == 0;
}

static omc_exif_name_status
omc_exif_name_write(const char* name, char* out_name, omc_size out_cap,
                    omc_size* out_len)
{
    omc_size name_len;

    if (out_len != (omc_size*)0) {
        *out_len = 0U;
    }
    if (name == (const char*)0 || name[0] == '\0') {
        if (out_name != (char*)0 && out_cap != 0U) {
            out_name[0] = '\0';
        }
        return OMC_EXIF_NAME_EMPTY;
    }

    name_len = strlen(name);
    if (out_len != (omc_size*)0) {
        *out_len = name_len;
    }
    if (out_name == (char*)0 || out_cap == 0U) {
        return OMC_EXIF_NAME_OK;
    }
    if (name_len + 1U <= out_cap) {
        memcpy(out_name, name, name_len + 1U);
        return OMC_EXIF_NAME_OK;
    }

    if (out_cap > 1U) {
        memcpy(out_name, name, out_cap - 1U);
        out_name[out_cap - 1U] = '\0';
    } else {
        out_name[0] = '\0';
    }
    return OMC_EXIF_NAME_TRUNCATED;
}

static omc_exif_name_status
omc_exif_name_write_placeholder(const char* prefix, omc_u16 tag,
                                char* out_name, omc_size out_cap,
                                omc_size* out_len)
{
    static const char hex[] = "0123456789abcdef";
    char name[64];
    omc_size prefix_len;

    if (prefix == (const char*)0) {
        return omc_exif_name_write((const char*)0, out_name, out_cap, out_len);
    }

    prefix_len = strlen(prefix);
    if (prefix_len + 7U >= sizeof(name)) {
        return OMC_EXIF_NAME_INVALID;
    }

    memcpy(name, prefix, prefix_len);
    name[prefix_len + 0U] = '_';
    name[prefix_len + 1U] = '0';
    name[prefix_len + 2U] = 'x';
    name[prefix_len + 3U] = hex[(tag >> 12) & 0x0FU];
    name[prefix_len + 4U] = hex[(tag >> 8) & 0x0FU];
    name[prefix_len + 5U] = hex[(tag >> 4) & 0x0FU];
    name[prefix_len + 6U] = hex[(tag >> 0) & 0x0FU];
    name[prefix_len + 7U] = '\0';
    return omc_exif_name_write(name, out_name, out_cap, out_len);
}

static const char*
omc_exif_lookup_exact_name(const char* ifd_name, omc_u16 tag)
{
    static const omc_exif_name_map k_names[] = {
        { "ifd0", 0x0100U, "ImageWidth" },
        { "ifd0", 0x010FU, "Make" },
        { "ifd0", 0x0110U, "Model" },
        { "ifd0", 0xC4A5U, "PrintIM" },
        { "ifd1", 0x0201U, "JPEGInterchangeFormat" },
        { "exififd", 0x9003U, "DateTimeOriginal" },
        { "exififd", 0x927CU, "MakerNote" },
        { "gpsifd", 0x0001U, "GPSLatitudeRef" },
        { "gpsifd", 0x0002U, "GPSLatitude" },
        { "gpsifd", 0x0011U, "GPSImgDirection" },
        { "mpf0", 0xB001U, "NumberOfImages" },
        { "mk_apple0", 0x0001U, "MakerNoteVersion" },
        { "mk_apple0", 0x0045U, "FrontFacingCamera" },
        { "mk_casio0", 0x0E00U, "PrintIM" },
        { "mk_casio_type2_0", 0x0002U, "PreviewImageSize" },
        { "mk_casio_type2_0", 0x0008U, "QualityMode" },
        { "mk_fuji0", 0x1304U, "GEImageSize" },
        { "mk_fuji0", 0x144AU, "WBRed" },
        { "mk_kodak0", 0x0028U, "Distance1" },
        { "mk_minolta0", 0x0018U, "ImageStabilization" },
        { "mk_minolta0", 0x0103U, "MinoltaImageSize" },
        { "mk_minolta0", 0x0113U, "ImageStabilization" },
        { "mk_nikonsettings_main_0", 0x0001U, "ISOAutoHiLimit" },
        { "mk_nikonsettings_main_0", 0x0046U, "OpticalVR" },
        { "mk_olympus_focusinfo_0", 0x1600U, "ImageStabilization" },
        { "mk_motorola0", 0x6420U, "CustomRendered" },
        { "mk_panasonic0", 0x0004U, "Model" },
        { "mk_panasonic0", 0x000CU, "Model" },
        { "mk_panasonic0", 0x0016U, "Model" },
        { "mk_panasonic0", 0x0058U, "ThumbnailWidth" },
        { "mk_panasonic0", 0x00DEU, "AFAreaSize" },
        { "mk_pentax0", 0x0062U, "RawDevelopmentProcess" },
        { "mk_ricoh0", 0x1002U, "DriveMode" },
        { "mk_ricoh0", 0x1003U, "Sharpness" },
        { "mk_samsung_type2_0", 0xA002U, "SerialNumber" },
        { "mk_samsung_type2_0", 0xA003U, "LensType" },
        { "mk_canon0", 0x0038U, "BatteryType" },
        { "mk_canon_shotinfo_0", 0x000EU, "AFPointsInFocus" },
        { "mk_canon_camerasettings_0", 0x0021U, "AESetting" },
        { "mk_canon_colordata4_0", 0x02CFU, "NormalWhiteLevel" },
        { "mk_canon_colorcalib_0", 0x0038U, "CameraColorCalibration15" },
        { "mk_canon_camerainfo1d_0", 0x0048U, "ColorTemperature" },
        { "mk_canon_camerainfo600d_0", 0x00EAU, "LensType" },
        { "mk_canoncustom_functions2_0", 0x0103U, "ISOSpeedRange" },
        { "mk_canoncustom_functions2_0", 0x010CU, "ShutterSpeedRange" },
        { "mk_canoncustom_functions2_0", 0x0510U, "VFDisplayIllumination" },
        { "mk_canoncustom_functions2_0", 0x0701U, "Shutter-AELock" }
    };
    omc_size i;

    if (ifd_name == (const char*)0) {
        return (const char*)0;
    }

    for (i = 0U; i < (sizeof(k_names) / sizeof(k_names[0])); ++i) {
        if (k_names[i].tag == tag && strcmp(k_names[i].ifd_name, ifd_name) == 0) {
            return k_names[i].name;
        }
    }
    return (const char*)0;
}

static omc_exif_name_status
omc_exif_tag_name_impl(const char* ifd_name, omc_u16 tag,
                       char* out_name, omc_size out_cap, omc_size* out_len)
{
    const char* exact;

    exact = omc_exif_lookup_exact_name(ifd_name, tag);
    if (exact != (const char*)0) {
        return omc_exif_name_write(exact, out_name, out_cap, out_len);
    }

    if (ifd_name == (const char*)0) {
        return omc_exif_name_write((const char*)0, out_name, out_cap, out_len);
    }
    if (omc_exif_name_starts_with(ifd_name, "subifd")) {
        return omc_exif_tag_name_impl("ifd0", tag, out_name, out_cap, out_len);
    }
    if (omc_exif_name_starts_with(ifd_name, "mk_apple")) {
        return omc_exif_name_write_placeholder("Apple", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_fuji0") == 0) {
        return omc_exif_name_write_placeholder("FujiFilm", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_casio_type2_0") == 0) {
        return omc_exif_name_write_placeholder("Casio_Type2", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_samsung_ifd_0") == 0) {
        return omc_exif_name_write_placeholder("Samsung_IFD", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_samsung_type2_0") == 0) {
        return omc_exif_name_write_placeholder("Samsung_Type2", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_minolta0") == 0) {
        return omc_exif_name_write_placeholder("Minolta", tag,
                                               out_name, out_cap, out_len);
    }
    if (omc_exif_name_starts_with(ifd_name, "mk_nikonsettings_main_")) {
        return omc_exif_name_write_placeholder("NikonSettings", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_panasonic0") == 0) {
        return omc_exif_name_write_placeholder("Panasonic", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_ricoh0") == 0) {
        return omc_exif_name_write_placeholder("Ricoh", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_pentax0") == 0) {
        return omc_exif_name_write_placeholder("Pentax", tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_canon0") == 0) {
        return omc_exif_name_write_placeholder("Canon", tag,
                                               out_name, out_cap, out_len);
    }
    return omc_exif_name_write((const char*)0, out_name, out_cap, out_len);
}

OMC_API omc_exif_name_status
omc_exif_tag_name(const char* ifd_name, omc_u16 tag,
                  char* out_name, omc_size out_cap, omc_size* out_len)
{
    return omc_exif_tag_name_impl(ifd_name, tag, out_name, out_cap, out_len);
}

OMC_API omc_exif_name_status
omc_exif_entry_name(const omc_store* store, const omc_entry* entry,
                    omc_exif_name_policy policy,
                    char* out_name, omc_size out_cap, omc_size* out_len)
{
    omc_const_bytes ifd_view;
    char ifd_name[64];
    omc_size ifd_size;
    omc_exif_name_status status;

    if (out_len != (omc_size*)0) {
        *out_len = 0U;
    }
    if (store == (const omc_store*)0 || entry == (const omc_entry*)0
        || entry->key.kind != OMC_KEY_EXIF_TAG) {
        return OMC_EXIF_NAME_INVALID;
    }

    ifd_view = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
    ifd_size = ifd_view.size;
    if (ifd_size >= sizeof(ifd_name)) {
        ifd_size = sizeof(ifd_name) - 1U;
    }
    if (ifd_size != 0U) {
        memcpy(ifd_name, ifd_view.data, ifd_size);
    }
    ifd_name[ifd_size] = '\0';

    status = omc_exif_tag_name_impl(ifd_name, entry->key.u.exif_tag.tag,
                                    out_name, out_cap, out_len);
    if (policy != OMC_EXIF_NAME_EXIFTOOL_COMPAT) {
        return status;
    }

    if (strcmp(ifd_name, "mk_samsung_type2_0") == 0
        && entry->key.u.exif_tag.tag == 0xA002U) {
        return omc_exif_name_write("Samsung_Type2_0xa002",
                                   out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_minolta0") == 0
        && (entry->key.u.exif_tag.tag == 0x0018U
            || entry->key.u.exif_tag.tag == 0x0113U)) {
        return omc_exif_name_write_placeholder("Minolta",
                                               entry->key.u.exif_tag.tag,
                                               out_name, out_cap, out_len);
    }
    if (strcmp(ifd_name, "mk_panasonic0") == 0) {
        switch (entry->key.u.exif_tag.tag) {
        case 0x0004U:
        case 0x000CU:
        case 0x0016U:
            if (entry->value.kind != OMC_VAL_TEXT) {
                return omc_exif_name_write_placeholder("Panasonic",
                                                       entry->key.u.exif_tag.tag,
                                                       out_name, out_cap,
                                                       out_len);
            }
            break;
        case 0x0058U:
        case 0x005AU:
        case 0x005CU:
        case 0x00DEU:
        case 0x00E9U:
        case 0x00F1U:
        case 0x00F3U:
        case 0x00F4U:
        case 0x00F5U:
            return omc_exif_name_write_placeholder("Panasonic",
                                                   entry->key.u.exif_tag.tag,
                                                   out_name, out_cap, out_len);
        case 0x00C4U:
            if (entry->value.kind == OMC_VAL_SCALAR
                && entry->value.u.u64 == 65535U) {
                return omc_exif_name_write_placeholder("Panasonic",
                                                       entry->key.u.exif_tag.tag,
                                                       out_name, out_cap,
                                                       out_len);
            }
            break;
        default:
            break;
        }
    }
    if ((entry->flags & OMC_ENTRY_FLAG_CONTEXTUAL_NAME) == 0U) {
        return status;
    }

    switch (entry->origin.name_context_kind) {
    case OMC_ENTRY_NAME_CTX_CASIO_TYPE2_LEGACY:
        if (entry->origin.name_context_variant == 1U) {
            if (entry->key.u.exif_tag.tag == 0x0002U) {
                return omc_exif_name_write("Quality",
                                           out_name, out_cap, out_len);
            }
            if (entry->key.u.exif_tag.tag == 0x0E00U) {
                return omc_exif_name_write("PrintIM",
                                           out_name, out_cap, out_len);
            }
            return omc_exif_name_write_placeholder("Casio",
                                                   entry->key.u.exif_tag.tag,
                                                   out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_FUJIFILM_MAIN_1304:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write_placeholder("FujiFilm",
                                                   entry->key.u.exif_tag.tag,
                                                   out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_OLYMPUS_FOCUSINFO_1600:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("ImageStabilization",
                                       out_name, out_cap, out_len);
        }
        if (entry->origin.name_context_variant == 2U) {
            return omc_exif_name_write("Olympus_FocusInfo_0x1600",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_KODAK_MAIN_0028:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("KodakModel",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_MOTOROLA_MAIN_6420:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("Motorola_0x6420",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_RICOH_MAIN_COMPAT:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write_placeholder("Ricoh",
                                                   entry->key.u.exif_tag.tag,
                                                   out_name, out_cap, out_len);
        }
        if (entry->origin.name_context_variant == 2U) {
            return omc_exif_name_write("WhiteBalance",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_NIKONSETTINGS_MAIN:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write_placeholder("NikonSettings",
                                                   entry->key.u.exif_tag.tag,
                                                   out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_PENTAX_MAIN_0062:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("Pentax_0x0062",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_MAIN_0038:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("Canon_0x0038",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_SHOTINFO_000E:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("MinFocalLength",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CAMSET_0021:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("WB_RGGBLevelsKelvin",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_COLORDATA4_00EA:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("WB_RGGBLevelsUnknown7",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_COLORDATA4_00EE:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("MaxFocalLength",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_COLORDATA4_02CF:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("PerChannelBlackLevel",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_COLORCALIB_0038:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("BatteryType",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CAMERAINFO1D_0048:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("Sharpness",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CAMERAINFO600D_00EA:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("MinFocalLength",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0103:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("ISOExpansion",
                                       out_name, out_cap, out_len);
        }
        if (entry->origin.name_context_variant == 2U) {
            return omc_exif_name_write("ISOSpeedRange",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_010C:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("CanonCustom_Functions2_0x010c",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0510:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("SuperimposedDisplay",
                                       out_name, out_cap, out_len);
        }
        break;
    case OMC_ENTRY_NAME_CTX_CANON_CUSTOMFUNC2_0701:
        if (entry->origin.name_context_variant == 1U) {
            return omc_exif_name_write("ShutterButtonAFOnButton",
                                       out_name, out_cap, out_len);
        }
        if (entry->origin.name_context_variant == 2U) {
            return omc_exif_name_write("AFAndMeteringButtons",
                                       out_name, out_cap, out_len);
        }
        break;
    default:
        break;
    }

    return status;
}
