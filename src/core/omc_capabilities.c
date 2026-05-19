#include "omc/omc_capabilities.h"

static int
omc_capability_is_tiff_family(omc_scan_fmt format)
{
    return format == OMC_SCAN_FMT_TIFF || format == OMC_SCAN_FMT_DNG;
}

static int
omc_capability_is_bmff_family(omc_scan_fmt format)
{
    return format == OMC_SCAN_FMT_HEIF || format == OMC_SCAN_FMT_AVIF
           || format == OMC_SCAN_FMT_CR3;
}

static int
omc_capability_is_primary_transfer_target(omc_scan_fmt format)
{
    return format == OMC_SCAN_FMT_JPEG || omc_capability_is_tiff_family(format)
           || format == OMC_SCAN_FMT_PNG || format == OMC_SCAN_FMT_WEBP
           || format == OMC_SCAN_FMT_JP2 || format == OMC_SCAN_FMT_JXL
           || omc_capability_is_bmff_family(format);
}

static int
omc_capability_has_native_iptc_carrier(omc_scan_fmt format)
{
    return format == OMC_SCAN_FMT_JPEG || omc_capability_is_tiff_family(format);
}

static int
omc_capability_has_c_jumbf_transfer_lane(omc_scan_fmt format)
{
    return format == OMC_SCAN_FMT_JPEG || format == OMC_SCAN_FMT_JXL
           || omc_capability_is_bmff_family(format);
}

static void
omc_capability_set_exif(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_is_primary_transfer_target(cap->format)) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->transfer_prepare  = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->target_edit       = omc_capability_is_bmff_family(cap->format)
                                 ? OMC_METADATA_CAPABILITY_BOUNDED
                                 : OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
}

static void
omc_capability_set_xmp(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_is_primary_transfer_target(cap->format)) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->transfer_prepare  = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->target_edit       = omc_capability_is_bmff_family(cap->format)
                                 ? OMC_METADATA_CAPABILITY_BOUNDED
                                 : OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
}

static void
omc_capability_set_icc(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_is_primary_transfer_target(cap->format)) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->transfer_prepare  = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->target_edit       = omc_capability_is_bmff_family(cap->format)
                                 ? OMC_METADATA_CAPABILITY_BOUNDED
                                 : OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
}

static void
omc_capability_set_iptc(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_is_primary_transfer_target(cap->format)) {
        return;
    }
    if (omc_capability_has_native_iptc_carrier(cap->format)) {
        cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
        cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
        cap->target_edit       = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
    }
    cap->transfer_prepare = OMC_METADATA_CAPABILITY_BOUNDED;
}

static void
omc_capability_set_makernote(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0) {
        return;
    }
    if (cap->format == OMC_SCAN_FMT_JPEG
        || omc_capability_is_tiff_family(cap->format)
        || cap->format == OMC_SCAN_FMT_CR3) {
        cap->read              = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->structured_decode = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->transfer_prepare  = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->target_edit       = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
    }
}

static void
omc_capability_set_photoshop_irb(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0) {
        return;
    }
    if (cap->format == OMC_SCAN_FMT_JPEG
        || omc_capability_is_tiff_family(cap->format)) {
        cap->read              = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->structured_decode = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->transfer_prepare  = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->target_edit       = OMC_METADATA_CAPABILITY_BOUNDED;
        cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
    }
}

static void
omc_capability_set_jumbf(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_has_c_jumbf_transfer_lane(cap->format)) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->transfer_prepare  = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->target_edit       = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
}

static void
omc_capability_set_c2pa(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_has_c_jumbf_transfer_lane(cap->format)) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->transfer_prepare  = OMC_METADATA_CAPABILITY_BOUNDED;
    cap->target_edit       = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
}

static void
omc_capability_set_bmff_fields(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0
        || !omc_capability_is_bmff_family(cap->format)) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
}

static void
omc_capability_set_geotiff(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0) {
        return;
    }
    if (omc_capability_is_tiff_family(cap->format)
        || cap->format == OMC_SCAN_FMT_JP2) {
        cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
        cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
    }
}

static void
omc_capability_set_exr_attribute(omc_metadata_capability* cap)
{
    if (cap == (omc_metadata_capability*)0 || cap->format != OMC_SCAN_FMT_EXR) {
        return;
    }
    cap->read              = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->structured_decode = OMC_METADATA_CAPABILITY_SUPPORTED;
    cap->transfer_prepare  = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap->target_edit       = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap->raw_preservation  = OMC_METADATA_CAPABILITY_BOUNDED;
}

const char*
omc_metadata_capability_family_name(omc_metadata_capability_family family)
{
    switch (family) {
    case OMC_METADATA_CAPABILITY_EXIF: return "exif";
    case OMC_METADATA_CAPABILITY_XMP: return "xmp";
    case OMC_METADATA_CAPABILITY_ICC: return "icc";
    case OMC_METADATA_CAPABILITY_IPTC: return "iptc";
    case OMC_METADATA_CAPABILITY_MAKERNOTE: return "makernote";
    case OMC_METADATA_CAPABILITY_PHOTOSHOP_IRB: return "photoshop_irb";
    case OMC_METADATA_CAPABILITY_JUMBF: return "jumbf";
    case OMC_METADATA_CAPABILITY_C2PA: return "c2pa";
    case OMC_METADATA_CAPABILITY_BMFF_FIELDS: return "bmff_fields";
    case OMC_METADATA_CAPABILITY_GEOTIFF: return "geotiff";
    case OMC_METADATA_CAPABILITY_EXR_ATTRIBUTE: return "exr_attribute";
    default: break;
    }
    return "unknown";
}

const char*
omc_metadata_capability_support_name(omc_metadata_capability_support support)
{
    switch (support) {
    case OMC_METADATA_CAPABILITY_UNSUPPORTED: return "unsupported";
    case OMC_METADATA_CAPABILITY_SUPPORTED: return "supported";
    case OMC_METADATA_CAPABILITY_BOUNDED: return "bounded";
    case OMC_METADATA_CAPABILITY_DISABLED: return "disabled";
    default: break;
    }
    return "unknown";
}

int
omc_metadata_capability_available(omc_metadata_capability_support support)
{
    return support == OMC_METADATA_CAPABILITY_SUPPORTED
           || support == OMC_METADATA_CAPABILITY_BOUNDED;
}

omc_metadata_capability
omc_metadata_capability_query(omc_scan_fmt format,
                              omc_metadata_capability_family family)
{
    omc_metadata_capability cap;

    cap.format            = format;
    cap.family            = family;
    cap.read              = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap.structured_decode = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap.transfer_prepare  = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap.target_edit       = OMC_METADATA_CAPABILITY_UNSUPPORTED;
    cap.raw_preservation  = OMC_METADATA_CAPABILITY_UNSUPPORTED;

    switch (family) {
    case OMC_METADATA_CAPABILITY_EXIF: omc_capability_set_exif(&cap); break;
    case OMC_METADATA_CAPABILITY_XMP: omc_capability_set_xmp(&cap); break;
    case OMC_METADATA_CAPABILITY_ICC: omc_capability_set_icc(&cap); break;
    case OMC_METADATA_CAPABILITY_IPTC: omc_capability_set_iptc(&cap); break;
    case OMC_METADATA_CAPABILITY_MAKERNOTE:
        omc_capability_set_makernote(&cap);
        break;
    case OMC_METADATA_CAPABILITY_PHOTOSHOP_IRB:
        omc_capability_set_photoshop_irb(&cap);
        break;
    case OMC_METADATA_CAPABILITY_JUMBF: omc_capability_set_jumbf(&cap); break;
    case OMC_METADATA_CAPABILITY_C2PA: omc_capability_set_c2pa(&cap); break;
    case OMC_METADATA_CAPABILITY_BMFF_FIELDS:
        omc_capability_set_bmff_fields(&cap);
        break;
    case OMC_METADATA_CAPABILITY_GEOTIFF:
        omc_capability_set_geotiff(&cap);
        break;
    case OMC_METADATA_CAPABILITY_EXR_ATTRIBUTE:
        omc_capability_set_exr_attribute(&cap);
        break;
    default: break;
    }

    return cap;
}
