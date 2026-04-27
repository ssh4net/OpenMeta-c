#include "omc/omc_xmp_dump.h"
#include "omc/omc_exif_name.h"

#include <stdio.h>
#include <string.h>

typedef struct omc_xmp_dump_writer {
    omc_u8* out;
    omc_size cap;
    omc_u64 needed;
    omc_u64 written;
} omc_xmp_dump_writer;

typedef struct omc_xmp_ns_view {
    const omc_u8* data;
    omc_size size;
} omc_xmp_ns_view;

typedef enum omc_xmp_dump_pass {
    OMC_XMP_DUMP_PASS_EXISTING = 0,
    OMC_XMP_DUMP_PASS_EXIF = 1,
    OMC_XMP_DUMP_PASS_IPTC = 2,
    OMC_XMP_DUMP_PASS_COUNT = 3
} omc_xmp_dump_pass;

typedef enum omc_xmp_dump_container_kind {
    OMC_XMP_DUMP_CONTAINER_NONE = 0,
    OMC_XMP_DUMP_CONTAINER_SEQ = 1,
    OMC_XMP_DUMP_CONTAINER_BAG = 2,
    OMC_XMP_DUMP_CONTAINER_ALT = 3,
    OMC_XMP_DUMP_CONTAINER_STRUCT_RESOURCE = 4,
    OMC_XMP_DUMP_CONTAINER_STRUCT_SEQ = 5,
    OMC_XMP_DUMP_CONTAINER_STRUCT_BAG = 6
} omc_xmp_dump_container_kind;

#define OMC_XMP_DUMP_MAX_STRUCT_SEG 4U

typedef struct omc_xmp_dump_path_seg {
    omc_xmp_ns_view prefix;
    omc_xmp_ns_view name;
    omc_u32 index;
    omc_xmp_ns_view lang;
} omc_xmp_dump_path_seg;

typedef enum omc_xmp_dump_value_mode {
    OMC_XMP_DUMP_VALUE_NORMAL = 0,
    OMC_XMP_DUMP_VALUE_BYTES_TEXT = 1,
    OMC_XMP_DUMP_VALUE_EXIF_DATE = 2,
    OMC_XMP_DUMP_VALUE_XP_UTF16LE = 3,
    OMC_XMP_DUMP_VALUE_EXIF_FOCAL_LENGTH = 4,
    OMC_XMP_DUMP_VALUE_EXIF_SHUTTER_SPEED = 5,
    OMC_XMP_DUMP_VALUE_EXIF_LENS_SPEC = 6,
    OMC_XMP_DUMP_VALUE_EXIF_GPS_VERSION = 7,
    OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD = 8,
    OMC_XMP_DUMP_VALUE_EXIF_GPS_TIME = 9,
    OMC_XMP_DUMP_VALUE_EXIF_GPS_ALTITUDE = 10,
    OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL = 11,
    OMC_XMP_DUMP_VALUE_EXIF_FNUMBER = 12,
    OMC_XMP_DUMP_VALUE_EXIF_APEX_FNUMBER = 13,
    OMC_XMP_DUMP_VALUE_EXIF_SRATIONAL_DECIMAL = 14
} omc_xmp_dump_value_mode;

typedef struct omc_xmp_dump_property {
    const omc_store* store;
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_xmp_ns_view exif_ifd;
    omc_u16 exif_tag;
    const omc_val* value;
    const omc_arena* arena;
    omc_xmp_dump_value_mode value_mode;
    omc_xmp_dump_container_kind container_kind;
    omc_u32 item_index;
    omc_xmp_ns_view lang;
    omc_u32 struct_seg_count;
    omc_xmp_dump_path_seg struct_seg[OMC_XMP_DUMP_MAX_STRUCT_SEG];
} omc_xmp_dump_property;

static int
omc_xmp_dump_property_has_output(const omc_xmp_dump_property* prop);

static int
omc_xmp_dump_rational_to_double(omc_urational r, double* out_value);

static int
omc_xmp_dump_schema_from_prefix(omc_xmp_ns_view prefix,
                                omc_xmp_ns_view* out_schema);

static const char k_xmp_ns_xap[] = "http://ns.adobe.com/xap/1.0/";
static const char k_xmp_ns_dc[] = "http://purl.org/dc/elements/1.1/";
static const char k_xmp_ns_ps[] = "http://ns.adobe.com/photoshop/1.0/";
static const char k_xmp_ns_exif[] = "http://ns.adobe.com/exif/1.0/";
static const char k_xmp_ns_tiff[] = "http://ns.adobe.com/tiff/1.0/";
static const char k_xmp_ns_mm[] = "http://ns.adobe.com/xap/1.0/mm/";
static const char k_xmp_ns_st_ref[]
    = "http://ns.adobe.com/xap/1.0/sType/ResourceRef#";
static const char k_xmp_ns_st_evt[]
    = "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#";
static const char k_xmp_ns_st_ver[]
    = "http://ns.adobe.com/xap/1.0/sType/Version#";
static const char k_xmp_ns_st_mfs[]
    = "http://ns.adobe.com/xap/1.0/sType/ManifestItem#";
static const char k_xmp_ns_rights[] = "http://ns.adobe.com/xap/1.0/rights/";
static const char k_xmp_ns_pdf[] = "http://ns.adobe.com/pdf/1.3/";
static const char k_xmp_ns_lr[] = "http://ns.adobe.com/lightroom/1.0/";
static const char k_xmp_ns_plus[] = "http://ns.useplus.org/ldf/xmp/1.0/";
static const char k_xmp_ns_iptc4xmp[]
    = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
static const char k_xmp_ns_iptc4xmp_ext[]
    = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";

static const char k_prop_make[] = "Make";
static const char k_prop_model[] = "Model";
static const char k_prop_datetime[] = "DateTime";
static const char k_prop_compression[] = "Compression";
static const char k_prop_image_width[] = "ImageWidth";
static const char k_prop_image_length[] = "ImageLength";
static const char k_prop_image_height[] = "ImageHeight";
static const char k_prop_bits_per_sample[] = "BitsPerSample";
static const char k_prop_orientation[] = "Orientation";
static const char k_prop_photometric_interpretation[]
    = "PhotometricInterpretation";
static const char k_prop_samples_per_pixel[] = "SamplesPerPixel";
static const char k_prop_planar_configuration[] = "PlanarConfiguration";
static const char k_prop_sample_format[] = "SampleFormat";
static const char k_prop_resolution_unit[] = "ResolutionUnit";
static const char k_prop_ycbcr_positioning[] = "YCbCrPositioning";
static const char k_prop_exposure_time[] = "ExposureTime";
static const char k_prop_fnumber[] = "FNumber";
static const char k_prop_exposure_bias_value[] = "ExposureBiasValue";
static const char k_prop_exposure_compensation[] = "ExposureCompensation";
static const char k_prop_iso_speed_ratings[] = "ISOSpeedRatings";
static const char k_prop_iso[] = "ISO";
static const char k_prop_pixel_x_dimension[] = "PixelXDimension";
static const char k_prop_exif_image_width[] = "ExifImageWidth";
static const char k_prop_pixel_y_dimension[] = "PixelYDimension";
static const char k_prop_exif_image_height[] = "ExifImageHeight";
static const char k_prop_focal_length_35mm_film[] = "FocalLengthIn35mmFilm";
static const char k_prop_focal_length_35mm_format[]
    = "FocalLengthIn35mmFormat";
static const char k_prop_datetime_original[] = "DateTimeOriginal";
static const char k_prop_datetime_digitized[] = "DateTimeDigitized";
static const char k_prop_preview_datetime[] = "PreviewDateTime";
static const char k_prop_metering_mode[] = "MeteringMode";
static const char k_prop_exposure_program[] = "ExposureProgram";
static const char k_prop_light_source[] = "LightSource";
static const char k_prop_focal_length[] = "FocalLength";
static const char k_prop_aperture_value[] = "ApertureValue";
static const char k_prop_max_aperture_value[] = "MaxApertureValue";
static const char k_prop_brightness_value[] = "BrightnessValue";
static const char k_prop_shutter_speed_value[] = "ShutterSpeedValue";
static const char k_prop_digital_zoom_ratio[] = "DigitalZoomRatio";
static const char k_prop_gain_control[] = "GainControl";
static const char k_prop_contrast[] = "Contrast";
static const char k_prop_saturation[] = "Saturation";
static const char k_prop_sharpness[] = "Sharpness";
static const char k_prop_subject_distance_range[]
    = "SubjectDistanceRange";
static const char k_prop_focal_plane_x_resolution[]
    = "FocalPlaneXResolution";
static const char k_prop_focal_plane_y_resolution[]
    = "FocalPlaneYResolution";
static const char k_prop_color_space[] = "ColorSpace";
static const char k_prop_focal_plane_resolution_unit[]
    = "FocalPlaneResolutionUnit";
static const char k_prop_file_source[] = "FileSource";
static const char k_prop_lens_specification[] = "LensSpecification";
static const char k_prop_gps_version_id[] = "GPSVersionID";
static const char k_prop_gps_latitude_ref[] = "GPSLatitudeRef";
static const char k_prop_gps_latitude[] = "GPSLatitude";
static const char k_prop_gps_longitude_ref[] = "GPSLongitudeRef";
static const char k_prop_gps_longitude[] = "GPSLongitude";
static const char k_prop_gps_altitude_ref[] = "GPSAltitudeRef";
static const char k_prop_gps_altitude[] = "GPSAltitude";
static const char k_prop_gps_timestamp[] = "GPSTimeStamp";
static const char k_prop_gps_datetime[] = "GPSDateTime";
static const char k_prop_gps_satellites[] = "GPSSatellites";
static const char k_prop_gps_status[] = "GPSStatus";
static const char k_prop_gps_measure_mode[] = "GPSMeasureMode";
static const char k_prop_gps_dop[] = "GPSDOP";
static const char k_prop_gps_speed_ref[] = "GPSSpeedRef";
static const char k_prop_gps_speed[] = "GPSSpeed";
static const char k_prop_gps_track_ref[] = "GPSTrackRef";
static const char k_prop_gps_track[] = "GPSTrack";
static const char k_prop_gps_img_direction_ref[] = "GPSImgDirectionRef";
static const char k_prop_gps_img_direction[] = "GPSImgDirection";
static const char k_prop_gps_map_datum[] = "GPSMapDatum";
static const char k_prop_gps_dest_latitude_ref[] = "GPSDestLatitudeRef";
static const char k_prop_gps_dest_latitude[] = "GPSDestLatitude";
static const char k_prop_gps_dest_longitude_ref[] = "GPSDestLongitudeRef";
static const char k_prop_gps_dest_longitude[] = "GPSDestLongitude";
static const char k_prop_gps_dest_bearing_ref[] = "GPSDestBearingRef";
static const char k_prop_gps_dest_bearing[] = "GPSDestBearing";
static const char k_prop_gps_dest_distance_ref[] = "GPSDestDistanceRef";
static const char k_prop_gps_dest_distance[] = "GPSDestDistance";
static const char k_prop_gps_date_stamp[] = "GPSDateStamp";
static const char k_prop_gps_differential[] = "GPSDifferential";
static const char k_prop_gps_h_positioning_error[]
    = "GPSHPositioningError";
static const char k_prop_xp_title[] = "XPTitle";
static const char k_prop_xp_comment[] = "XPComment";
static const char k_prop_xp_author[] = "XPAuthor";
static const char k_prop_xp_keywords[] = "XPKeywords";
static const char k_prop_xp_subject[] = "XPSubject";
static const char k_prop_title[] = "title";
static const char k_prop_description[] = "description";
static const char k_prop_rights[] = "rights";
static const char k_prop_creator[] = "creator";
static const char k_prop_subject[] = "subject";
static const char k_prop_category[] = "Category";
static const char k_prop_supplemental_categories[] = "SupplementalCategories";
static const char k_prop_city[] = "City";
static const char k_prop_location[] = "Location";
static const char k_prop_state[] = "State";
static const char k_prop_country[] = "Country";
static const char k_prop_country_code[] = "CountryCode";
static const char k_prop_headline[] = "Headline";
static const char k_prop_credit[] = "Credit";
static const char k_prop_source[] = "Source";
static const char k_prop_caption_writer[] = "CaptionWriter";
static const char k_prop_owner[] = "Owner";
static const char k_prop_hierarchical_subject[] = "hierarchicalSubject";
static const char k_prop_maker_note[] = "MakerNote";
static const char k_prop_dng_private_data[] = "DNGPrivateData";
static void
omc_xmp_dump_writer_init(omc_xmp_dump_writer* writer, omc_u8* out,
                         omc_size out_cap)
{
    writer->out = out;
    writer->cap = out_cap;
    writer->needed = 0U;
    writer->written = 0U;
}

static const char*
omc_xmp_dump_value_kind_name(omc_val_kind kind)
{
    switch (kind) {
    case OMC_VAL_EMPTY: return "empty";
    case OMC_VAL_SCALAR: return "scalar";
    case OMC_VAL_ARRAY: return "array";
    case OMC_VAL_BYTES: return "bytes";
    case OMC_VAL_TEXT: return "text";
    default: return "unknown";
    }
}

static const char*
omc_xmp_dump_elem_type_name(omc_elem_type type)
{
    switch (type) {
    case OMC_ELEM_U8: return "u8";
    case OMC_ELEM_I8: return "i8";
    case OMC_ELEM_U16: return "u16";
    case OMC_ELEM_I16: return "i16";
    case OMC_ELEM_U32: return "u32";
    case OMC_ELEM_I32: return "i32";
    case OMC_ELEM_U64: return "u64";
    case OMC_ELEM_I64: return "i64";
    case OMC_ELEM_F32_BITS: return "f32_bits";
    case OMC_ELEM_F64_BITS: return "f64_bits";
    case OMC_ELEM_URATIONAL: return "urational";
    case OMC_ELEM_SRATIONAL: return "srational";
    default: return "unknown";
    }
}

static const char*
omc_xmp_dump_text_encoding_name(omc_text_encoding enc)
{
    switch (enc) {
    case OMC_TEXT_UNKNOWN: return "unknown";
    case OMC_TEXT_ASCII: return "ascii";
    case OMC_TEXT_UTF8: return "utf8";
    case OMC_TEXT_UTF16LE: return "utf16le";
    case OMC_TEXT_UTF16BE: return "utf16be";
    default: return "unknown";
    }
}

static const char*
omc_xmp_dump_wire_family_name(omc_wire_family family)
{
    switch (family) {
    case OMC_WIRE_NONE: return "none";
    case OMC_WIRE_TIFF: return "tiff";
    case OMC_WIRE_OTHER: return "other";
    default: return "unknown";
    }
}

static omc_size
omc_xmp_dump_elem_size(omc_elem_type type)
{
    switch (type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_I8: return 1U;
    case OMC_ELEM_U16:
    case OMC_ELEM_I16: return 2U;
    case OMC_ELEM_U32:
    case OMC_ELEM_I32:
    case OMC_ELEM_F32_BITS: return 4U;
    case OMC_ELEM_U64:
    case OMC_ELEM_I64:
    case OMC_ELEM_F64_BITS: return 8U;
    case OMC_ELEM_URATIONAL:
    case OMC_ELEM_SRATIONAL: return 8U;
    default: return 0U;
    }
}

static void
omc_xmp_dump_write_bytes(omc_xmp_dump_writer* writer, const char* bytes,
                         omc_size size)
{
    omc_size copy;

    if (writer == (omc_xmp_dump_writer*)0 || bytes == (const char*)0) {
        return;
    }

    if (writer->written < writer->cap) {
        copy = writer->cap - (omc_size)writer->written;
        if (copy > size) {
            copy = size;
        }
        if (copy != 0U) {
            memcpy(writer->out + writer->written, bytes, copy);
            writer->written += (omc_u64)copy;
        }
    }

    writer->needed += (omc_u64)size;
}

static void
omc_xmp_dump_write_byte(omc_xmp_dump_writer* writer, char byte)
{
    omc_xmp_dump_write_bytes(writer, &byte, 1U);
}

static int
omc_xmp_dump_is_name_start(omc_u8 c)
{
    return ((c >= (omc_u8)'A' && c <= (omc_u8)'Z')
            || (c >= (omc_u8)'a' && c <= (omc_u8)'z') || c == (omc_u8)'_');
}

static int
omc_xmp_dump_is_name_char(omc_u8 c)
{
    return omc_xmp_dump_is_name_start(c)
           || (c >= (omc_u8)'0' && c <= (omc_u8)'9') || c == (omc_u8)'-'
           || c == (omc_u8)'.';
}

static int
omc_xmp_dump_is_simple_name(omc_xmp_ns_view name)
{
    omc_size i;

    if (name.data == (const omc_u8*)0 || name.size == 0U) {
        return 0;
    }
    if (!omc_xmp_dump_is_name_start(name.data[0])) {
        return 0;
    }

    for (i = 1U; i < name.size; ++i) {
        if (!omc_xmp_dump_is_name_char(name.data[i])) {
            return 0;
        }
    }

    return 1;
}

static int
omc_xmp_dump_views_equal(omc_xmp_ns_view a, omc_xmp_ns_view b)
{
    if (a.size != b.size) {
        return 0;
    }
    if (a.size == 0U) {
        return 1;
    }
    return memcmp(a.data, b.data, a.size) == 0;
}

static int
omc_xmp_dump_view_equal_lit(omc_xmp_ns_view view, const char* lit)
{
    omc_size lit_size;

    lit_size = strlen(lit);
    if (view.size != lit_size) {
        return 0;
    }
    return memcmp(view.data, lit, lit_size) == 0;
}

static omc_xmp_ns_view
omc_xmp_dump_view_from_ref(const omc_arena* arena, omc_byte_ref ref)
{
    omc_const_bytes bytes;
    omc_xmp_ns_view view;

    view.data = (const omc_u8*)0;
    view.size = 0U;

    bytes = omc_arena_view(arena, ref);
    if (bytes.data == (const omc_u8*)0) {
        return view;
    }

    view.data = bytes.data;
    view.size = bytes.size;
    return view;
}

static omc_xmp_ns_view
omc_xmp_dump_view_from_lit(const char* lit)
{
    omc_xmp_ns_view view;

    view.data = (const omc_u8*)lit;
    view.size = strlen(lit);
    return view;
}

static omc_xmp_dump_container_kind
omc_xmp_dump_existing_container_kind(omc_xmp_ns_view schema_ns,
                                     omc_xmp_ns_view property_name)
{
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)
        && omc_xmp_dump_view_equal_lit(property_name, k_prop_creator)) {
        return OMC_XMP_DUMP_CONTAINER_SEQ;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)
        && (omc_xmp_dump_view_equal_lit(property_name, k_prop_subject)
            || omc_xmp_dump_view_equal_lit(property_name, "language")
            || omc_xmp_dump_view_equal_lit(property_name, "contributor")
            || omc_xmp_dump_view_equal_lit(property_name, "publisher")
            || omc_xmp_dump_view_equal_lit(property_name, "relation")
            || omc_xmp_dump_view_equal_lit(property_name, "type"))) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_ps)
        && omc_xmp_dump_view_equal_lit(property_name,
                                       k_prop_supplemental_categories)) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_xap)
        && (omc_xmp_dump_view_equal_lit(property_name, "Identifier")
            || omc_xmp_dump_view_equal_lit(property_name, "Advisory"))) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_rights)
        && omc_xmp_dump_view_equal_lit(property_name, k_prop_owner)) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_lr)
        && omc_xmp_dump_view_equal_lit(property_name,
                                       k_prop_hierarchical_subject)) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_mm)
        && (omc_xmp_dump_view_equal_lit(property_name, "Ingredients")
            || omc_xmp_dump_view_equal_lit(property_name, "Pantry"))) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_plus)
        && omc_xmp_dump_view_equal_lit(property_name,
                                       "ImageAlterationConstraints")) {
        return OMC_XMP_DUMP_CONTAINER_BAG;
    }
    return OMC_XMP_DUMP_CONTAINER_SEQ;
}

static omc_xmp_dump_container_kind
omc_xmp_dump_structured_root_container_kind(omc_xmp_ns_view schema_ns,
                                            omc_xmp_ns_view property_name)
{
    if (omc_xmp_dump_existing_container_kind(schema_ns, property_name)
        == OMC_XMP_DUMP_CONTAINER_BAG) {
        return OMC_XMP_DUMP_CONTAINER_STRUCT_BAG;
    }
    return OMC_XMP_DUMP_CONTAINER_STRUCT_SEQ;
}

static int
omc_xmp_dump_parse_indexed_property_name(omc_xmp_ns_view property_path,
                                         omc_xmp_ns_view* out_base_name,
                                         omc_u32* out_index)
{
    omc_size i;
    omc_size base_len;
    omc_u32 index;
    int saw_digit;

    if (out_base_name == (omc_xmp_ns_view*)0 || out_index == (omc_u32*)0) {
        return 0;
    }
    if (property_path.data == (const omc_u8*)0 || property_path.size == 0U) {
        return 0;
    }

    *out_base_name = property_path;
    *out_index = 0U;
    if (omc_xmp_dump_is_simple_name(property_path)) {
        return 1;
    }
    if (property_path.size < 4U || property_path.data[property_path.size - 1U] != (omc_u8)']') {
        return 0;
    }

    i = property_path.size - 2U;
    saw_digit = 0;
    index = 0U;
    while (i < property_path.size && property_path.data[i] != (omc_u8)'[') {
        omc_u8 c;
        saw_digit = 1;
        c = property_path.data[i];
        if (c < (omc_u8)'0' || c > (omc_u8)'9') {
            return 0;
        }
        if (index > (((omc_u32)~0U) / 10U)) {
            return 0;
        }
        index = (index * 10U) + (omc_u32)(c - (omc_u8)'0');
        if (i == 0U) {
            break;
        }
        i -= 1U;
    }
    if (!saw_digit || i >= property_path.size || property_path.data[i] != (omc_u8)'[') {
        return 0;
    }

    base_len = i;
    if (base_len == 0U) {
        return 0;
    }

    out_base_name->data = property_path.data;
    out_base_name->size = base_len;
    if (!omc_xmp_dump_is_simple_name(*out_base_name)) {
        return 0;
    }

    {
        omc_u32 normalized;
        omc_size p;

        normalized = 0U;
        for (p = i + 1U; p + 1U < property_path.size; ++p) {
            normalized = normalized * 10U
                         + (omc_u32)(property_path.data[p] - (omc_u8)'0');
        }
        if (normalized == 0U) {
            return 0;
        }
        *out_index = normalized;
    }
    return 1;
}

static int
omc_xmp_dump_parse_lang_alt_property_name(omc_xmp_ns_view property_path,
                                          omc_xmp_ns_view* out_base_name,
                                          omc_xmp_ns_view* out_lang)
{
    static const char k_lang_prefix[] = "[@xml:lang=";
    omc_size prefix_size;
    omc_size i;

    if (out_base_name == (omc_xmp_ns_view*)0
        || out_lang == (omc_xmp_ns_view*)0) {
        return 0;
    }

    out_base_name->data = (const omc_u8*)0;
    out_base_name->size = 0U;
    out_lang->data = (const omc_u8*)0;
    out_lang->size = 0U;

    if (property_path.data == (const omc_u8*)0 || property_path.size < 15U
        || property_path.data[property_path.size - 1U] != (omc_u8)']') {
        return 0;
    }

    prefix_size = sizeof(k_lang_prefix) - 1U;
    for (i = 0U; i + prefix_size < property_path.size; ++i) {
        if (property_path.data[i] != (omc_u8)'[') {
            continue;
        }
        if (memcmp(property_path.data + i, k_lang_prefix, prefix_size) != 0) {
            continue;
        }
        if (i == 0U) {
            return 0;
        }
        out_base_name->data = property_path.data;
        out_base_name->size = i;
        if (!omc_xmp_dump_is_simple_name(*out_base_name)) {
            return 0;
        }
        out_lang->data = property_path.data + i + prefix_size;
        out_lang->size = property_path.size - (i + prefix_size) - 1U;
        if (out_lang->size == 0U) {
            return 0;
        }
        return 1;
    }

    return 0;
}

static int
omc_xmp_dump_parse_struct_segment(omc_xmp_ns_view segment,
                                  omc_xmp_dump_path_seg* out_seg)
{
    static const char k_lang_prefix[] = "[@xml:lang=";
    omc_xmp_ns_view base_name;
    omc_size i;
    omc_size prefix_size;

    if (out_seg == (omc_xmp_dump_path_seg*)0
        || segment.data == (const omc_u8*)0 || segment.size == 0U) {
        return 0;
    }

    out_seg->prefix.data = (const omc_u8*)0;
    out_seg->prefix.size = 0U;
    out_seg->name = segment;
    out_seg->index = 0U;
    out_seg->lang.data = (const omc_u8*)0;
    out_seg->lang.size = 0U;

    base_name = segment;
    prefix_size = sizeof(k_lang_prefix) - 1U;
    if (segment.size >= prefix_size + 2U
        && segment.data[segment.size - 1U] == (omc_u8)']') {
        for (i = 0U; i + prefix_size < segment.size; ++i) {
            if (segment.data[i] != (omc_u8)'[') {
                continue;
            }
            if (memcmp(segment.data + i, k_lang_prefix, prefix_size) != 0) {
                continue;
            }
            if (i == 0U) {
                return 0;
            }
            base_name.data = segment.data;
            base_name.size = i;
            out_seg->lang.data = segment.data + i + prefix_size;
            out_seg->lang.size = segment.size - (i + prefix_size) - 1U;
            if (out_seg->lang.size == 0U) {
                return 0;
            }
            break;
        }
    }

    if (out_seg->lang.size == 0U && segment.size >= 3U
        && segment.data[segment.size - 1U] == (omc_u8)']') {
        omc_size open_pos;
        omc_u32 index;
        int saw_digit;

        open_pos = segment.size - 2U;
        saw_digit = 0;
        index = 0U;
        while (open_pos < segment.size
               && segment.data[open_pos] != (omc_u8)'[') {
            omc_u8 c;

            c = segment.data[open_pos];
            if (c < (omc_u8)'0' || c > (omc_u8)'9') {
                saw_digit = 0;
                break;
            }
            saw_digit = 1;
            if (open_pos == 0U) {
                break;
            }
            open_pos -= 1U;
        }
        if (saw_digit && open_pos < segment.size
            && segment.data[open_pos] == (omc_u8)'[' && open_pos != 0U) {
            omc_size p;

            index = 0U;
            for (p = open_pos + 1U; p + 1U < segment.size; ++p) {
                index = index * 10U
                        + (omc_u32)(segment.data[p] - (omc_u8)'0');
            }
            if (index == 0U) {
                return 0;
            }
            base_name.data = segment.data;
            base_name.size = open_pos;
            out_seg->index = index;
        }
    }

    out_seg->name = base_name;
    for (i = 0U; i < base_name.size; ++i) {
        if (base_name.data[i] == (omc_u8)':') {
            if (i == 0U || i + 1U >= out_seg->name.size) {
                return 0;
            }
            out_seg->prefix.data = base_name.data;
            out_seg->prefix.size = i;
            out_seg->name.data = base_name.data + i + 1U;
            out_seg->name.size = base_name.size - i - 1U;
            if (!omc_xmp_dump_is_simple_name(out_seg->prefix)
                || !omc_xmp_dump_is_simple_name(out_seg->name)) {
                return 0;
            }
            break;
        }
    }

    if (out_seg->prefix.size == 0U
        && !omc_xmp_dump_is_simple_name(out_seg->name)) {
        return 0;
    }
    return omc_xmp_dump_is_simple_name(out_seg->name);
}

static int
omc_xmp_dump_parse_structured_property_path(omc_xmp_ns_view property_path,
                                            omc_u32* out_count,
                                            omc_xmp_dump_path_seg* out_seg)
{
    omc_size start;
    omc_u32 count;

    if (out_count == (omc_u32*)0 || out_seg == (omc_xmp_dump_path_seg*)0
        || property_path.data == (const omc_u8*)0 || property_path.size == 0U) {
        return 0;
    }

    *out_count = 0U;
    start = 0U;
    count = 0U;
    while (start < property_path.size) {
        omc_size end;
        omc_xmp_ns_view seg;

        if (count >= OMC_XMP_DUMP_MAX_STRUCT_SEG) {
            return 0;
        }
        end = start;
        while (end < property_path.size
               && property_path.data[end] != (omc_u8)'/') {
            end += 1U;
        }
        if (end == start) {
            return 0;
        }
        seg.data = property_path.data + start;
        seg.size = end - start;
        if (!omc_xmp_dump_parse_struct_segment(seg, &out_seg[count])) {
            return 0;
        }
        count += 1U;
        if (end == property_path.size) {
            break;
        }
        start = end + 1U;
    }

    if (count < 2U) {
        return 0;
    }
    *out_count = count;
    return 1;
}

static int
omc_xmp_dump_path_seg_equal(const omc_xmp_dump_path_seg* a,
                            const omc_xmp_dump_path_seg* b)
{
    return omc_xmp_dump_views_equal(a->prefix, b->prefix)
           && omc_xmp_dump_views_equal(a->name, b->name)
           && a->index == b->index
           && omc_xmp_dump_views_equal(a->lang, b->lang);
}

static int
omc_xmp_dump_path_seg_name_equal(const omc_xmp_dump_path_seg* a,
                                 const omc_xmp_dump_path_seg* b)
{
    return omc_xmp_dump_views_equal(a->prefix, b->prefix)
           && omc_xmp_dump_views_equal(a->name, b->name);
}

static int
omc_xmp_dump_existing_namespace_is_standard(omc_xmp_ns_view schema_ns)
{
    return omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_xap)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_ps)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_exif)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_tiff)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_mm)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp_ext)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_rights)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_pdf)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_lr)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_plus);
}

static int
omc_xmp_dump_existing_namespace_allowed(omc_xmp_ns_view schema_ns,
                                        const omc_xmp_sidecar_opts* opts)
{
    if (omc_xmp_dump_existing_namespace_is_standard(schema_ns)) {
        return 1;
    }
    return opts != (const omc_xmp_sidecar_opts*)0
           && opts->existing_namespace_policy
                  == OMC_XMP_NS_PRESERVE_CUSTOM;
}

static int
omc_xmp_dump_existing_promotes_scalar_to_lang_alt(omc_xmp_ns_view schema_ns,
                                                  omc_xmp_ns_view base_name)
{
    if (!omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)) {
        return 0;
    }
    return omc_xmp_dump_view_equal_lit(base_name, k_prop_title)
           || omc_xmp_dump_view_equal_lit(base_name, k_prop_description)
           || omc_xmp_dump_view_equal_lit(base_name, k_prop_rights);
}

static int
omc_xmp_dump_existing_promotes_scalar_to_indexed(omc_xmp_ns_view schema_ns,
                                                 omc_xmp_ns_view base_name)
{
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)) {
        return omc_xmp_dump_view_equal_lit(base_name, k_prop_subject)
               || omc_xmp_dump_view_equal_lit(base_name, k_prop_creator)
               || omc_xmp_dump_view_equal_lit(base_name, "language")
               || omc_xmp_dump_view_equal_lit(base_name, "contributor")
               || omc_xmp_dump_view_equal_lit(base_name, "publisher")
               || omc_xmp_dump_view_equal_lit(base_name, "relation")
               || omc_xmp_dump_view_equal_lit(base_name, "type")
               || omc_xmp_dump_view_equal_lit(base_name, "date");
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_xap)) {
        return omc_xmp_dump_view_equal_lit(base_name, "Identifier")
               || omc_xmp_dump_view_equal_lit(base_name, "Advisory");
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_ps)) {
        return omc_xmp_dump_view_equal_lit(
            base_name, k_prop_supplemental_categories);
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_rights)) {
        return omc_xmp_dump_view_equal_lit(base_name, k_prop_owner);
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_lr)) {
        return omc_xmp_dump_view_equal_lit(base_name,
                                           k_prop_hierarchical_subject);
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_plus)) {
        return omc_xmp_dump_view_equal_lit(base_name,
                                           "ImageAlterationConstraints");
    }
    return 0;
}

static int
omc_xmp_dump_structured_child_promotes_scalar_to_lang_alt(
    omc_xmp_ns_view schema_ns, omc_xmp_ns_view child_name)
{
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp)) {
        return omc_xmp_dump_view_equal_lit(child_name, "CiAdrCity");
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp_ext)) {
        return omc_xmp_dump_view_equal_lit(child_name, "Name")
               || omc_xmp_dump_view_equal_lit(child_name, "PersonName")
               || omc_xmp_dump_view_equal_lit(child_name, "ProductName")
               || omc_xmp_dump_view_equal_lit(child_name,
                                              "ProductDescription")
               || omc_xmp_dump_view_equal_lit(child_name, "CvTermName")
               || omc_xmp_dump_view_equal_lit(child_name, "AOTitle")
               || omc_xmp_dump_view_equal_lit(child_name, "LocationName");
    }
    return 0;
}

static int
omc_xmp_dump_structured_child_promotes_scalar_to_indexed(
    omc_xmp_ns_view schema_ns, omc_xmp_ns_view child_name)
{
    if (!omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp_ext)) {
        return 0;
    }
    return omc_xmp_dump_view_equal_lit(child_name, "Role")
           || omc_xmp_dump_view_equal_lit(child_name, "LocationId")
           || omc_xmp_dump_view_equal_lit(child_name, "PersonId")
           || omc_xmp_dump_view_equal_lit(child_name, "AOCreator")
           || omc_xmp_dump_view_equal_lit(child_name, "AOStylePeriod")
           || omc_xmp_dump_view_equal_lit(child_name, "LinkQualifier");
}

static int
omc_xmp_dump_existing_has_explicit_lang_alt_base(const omc_store* store,
                                                 omc_xmp_ns_view schema_ns,
                                                 omc_xmp_ns_view base_name)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }
    for (i = 0U; i < store->entry_count; ++i) {
        omc_xmp_ns_view entry_schema;
        omc_xmp_ns_view path;
        omc_xmp_ns_view parsed_base;
        omc_xmp_ns_view parsed_lang;

        if (store->entries[i].key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        entry_schema = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.schema_ns);
        if (!omc_xmp_dump_views_equal(entry_schema, schema_ns)) {
            continue;
        }
        path = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.property_path);
        if (!omc_xmp_dump_parse_lang_alt_property_name(path, &parsed_base,
                                                       &parsed_lang)) {
            continue;
        }
        if (omc_xmp_dump_views_equal(parsed_base, base_name)) {
            return 1;
        }
    }
    return 0;
}

static int
omc_xmp_dump_existing_has_explicit_indexed_base(const omc_store* store,
                                                omc_xmp_ns_view schema_ns,
                                                omc_xmp_ns_view base_name)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }
    for (i = 0U; i < store->entry_count; ++i) {
        omc_xmp_ns_view entry_schema;
        omc_xmp_ns_view path;
        omc_xmp_ns_view parsed_base;
        omc_u32 parsed_index;

        if (store->entries[i].key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        entry_schema = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.schema_ns);
        if (!omc_xmp_dump_views_equal(entry_schema, schema_ns)) {
            continue;
        }
        path = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.property_path);
        if (!omc_xmp_dump_parse_indexed_property_name(path, &parsed_base,
                                                      &parsed_index)
            || parsed_index == 0U) {
            continue;
        }
        if (omc_xmp_dump_views_equal(parsed_base, base_name)) {
            return 1;
        }
    }
    return 0;
}

static int
omc_xmp_dump_scalar_or_text_supported(const omc_val* value,
                                      const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    switch (value->kind) {
    case OMC_VAL_SCALAR:
        switch (value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_I8:
        case OMC_ELEM_U16:
        case OMC_ELEM_I16:
        case OMC_ELEM_U32:
        case OMC_ELEM_I32:
        case OMC_ELEM_U64:
        case OMC_ELEM_I64:
        case OMC_ELEM_URATIONAL:
        case OMC_ELEM_SRATIONAL:
            return 1;
        default: return 0;
        }
    case OMC_VAL_TEXT:
        if (value->text_encoding != OMC_TEXT_ASCII
            && value->text_encoding != OMC_TEXT_UTF8) {
            return 0;
        }
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    default: return 0;
    }
}

static int
omc_xmp_dump_textish_supported(const omc_val* value, const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    if (value->kind == OMC_VAL_TEXT) {
        if (value->text_encoding != OMC_TEXT_ASCII
            && value->text_encoding != OMC_TEXT_UTF8) {
            return 0;
        }
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    }
    if (value->kind == OMC_VAL_BYTES) {
        bytes = omc_arena_view(arena, value->u.ref);
        return bytes.data != (const omc_u8*)0;
    }

    return 0;
}

static int
omc_xmp_dump_bytes_view_supported(const omc_val* value,
                                  const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }

    if (value->kind != OMC_VAL_BYTES && value->kind != OMC_VAL_TEXT
        && value->kind != OMC_VAL_ARRAY) {
        return 0;
    }
    if (value->kind == OMC_VAL_ARRAY && value->elem_type != OMC_ELEM_U8) {
        return 0;
    }
    if (value->kind == OMC_VAL_TEXT
        && value->text_encoding != OMC_TEXT_ASCII
        && value->text_encoding != OMC_TEXT_UTF8
        && value->text_encoding != OMC_TEXT_UTF16LE) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    return bytes.data != (const omc_u8*)0;
}

static int
omc_xmp_dump_scalar_u64_value(const omc_val* value, omc_u64* out_value)
{
    if (value == (const omc_val*)0 || out_value == (omc_u64*)0
        || value->kind != OMC_VAL_SCALAR) {
        return 0;
    }

    switch (value->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_U16:
    case OMC_ELEM_U32:
    case OMC_ELEM_U64:
        *out_value = value->u.u64;
        return 1;
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        if (value->u.i64 < 0) {
            return 0;
        }
        *out_value = (omc_u64)value->u.i64;
        return 1;
    default: return 0;
    }
}

static int
omc_xmp_dump_ascii_text_view(const omc_val* value, const omc_arena* arena,
                             omc_const_bytes* out_bytes)
{
    omc_const_bytes bytes;
    omc_size i;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0
        || out_bytes == (omc_const_bytes*)0) {
        return 0;
    }

    out_bytes->data = (const omc_u8*)0;
    out_bytes->size = 0U;

    if (value->kind != OMC_VAL_TEXT && value->kind != OMC_VAL_BYTES) {
        return 0;
    }
    if (value->kind == OMC_VAL_TEXT && value->text_encoding != OMC_TEXT_ASCII
        && value->text_encoding != OMC_TEXT_UTF8) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    if (bytes.data == (const omc_u8*)0) {
        return 0;
    }

    for (i = 0U; i < bytes.size; ++i) {
        omc_u8 c;
        c = bytes.data[i];
        if (c == 0U || c >= 0x80U) {
            return 0;
        }
    }

    *out_bytes = bytes;
    return 1;
}

static int
omc_xmp_dump_first_ref_char(const omc_val* value, const omc_arena* arena,
                            const char* allowed, char* out_ref)
{
    omc_const_bytes bytes;
    omc_size i;
    char c;

    if (allowed == (const char*)0 || out_ref == (char*)0
        || !omc_xmp_dump_ascii_text_view(value, arena, &bytes)
        || bytes.size == 0U) {
        return 0;
    }

    c = (char)bytes.data[0];
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }

    for (i = 0U; allowed[i] != '\0'; ++i) {
        if (c == allowed[i]) {
            *out_ref = c;
            return 1;
        }
    }

    return 0;
}

static int
omc_xmp_dump_urational_supported(const omc_val* value,
                                 const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }
    if (value->kind == OMC_VAL_SCALAR && value->elem_type == OMC_ELEM_URATIONAL) {
        return value->u.ur.denom != 0U;
    }
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_URATIONAL) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    return bytes.data != (const omc_u8*)0;
}

static int
omc_xmp_dump_srational_supported(const omc_val* value,
                                 const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }
    if (value->kind == OMC_VAL_SCALAR && value->elem_type == OMC_ELEM_SRATIONAL) {
        return value->u.sr.denom != 0;
    }
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_SRATIONAL) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    return bytes.data != (const omc_u8*)0;
}

static int
omc_xmp_dump_urational_array_supported(const omc_val* value,
                                       const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_URATIONAL
        || value->count == 0U) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    return bytes.data != (const omc_u8*)0;
}

static int
omc_xmp_dump_rational_triplet_supported(const omc_val* value,
                                        const omc_arena* arena)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0) {
        return 0;
    }
    if (value->kind != OMC_VAL_ARRAY || value->count < 3U) {
        return 0;
    }
    if (value->elem_type != OMC_ELEM_URATIONAL
        && value->elem_type != OMC_ELEM_SRATIONAL) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    return bytes.data != (const omc_u8*)0;
}

static int
omc_xmp_dump_existing_generated_replacement_exists(const omc_store* store,
                                                   omc_size skip_index,
                                                   const omc_xmp_sidecar_opts* opts,
                                                   omc_xmp_ns_view schema_ns,
                                                   omc_xmp_ns_view property_name);

static int
omc_xmp_dump_structured_schema_supported(omc_xmp_ns_view schema_ns)
{
    return omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp_ext)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_mm)
           || omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_plus);
}

static int
omc_xmp_dump_existing_has_structured_descendants(const omc_store* store,
                                                 omc_xmp_ns_view schema_ns,
                                                 omc_xmp_ns_view base_name)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }
    for (i = 0U; i < store->entry_count; ++i) {
        omc_xmp_ns_view entry_schema;
        omc_xmp_ns_view path;
        omc_u32 seg_count;
        omc_xmp_dump_path_seg seg[OMC_XMP_DUMP_MAX_STRUCT_SEG];

        if (store->entries[i].key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        entry_schema = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.schema_ns);
        if (!omc_xmp_dump_views_equal(entry_schema, schema_ns)) {
            continue;
        }
        path = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.property_path);
        if (omc_xmp_dump_parse_structured_property_path(path, &seg_count, seg)
            && omc_xmp_dump_views_equal(seg[0].name, base_name)) {
            return 1;
        }
    }
    return 0;
}

static int
omc_xmp_dump_structured_path_shadowed(const omc_store* store, omc_size skip_index,
                                      omc_xmp_ns_view schema_ns,
                                      omc_u32 seg_count,
                                      const omc_xmp_dump_path_seg* seg)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }

    for (i = 0U; i < store->entry_count; ++i) {
        omc_xmp_ns_view entry_schema;
        omc_xmp_ns_view path;
        omc_u32 other_count;
        omc_xmp_dump_path_seg other[OMC_XMP_DUMP_MAX_STRUCT_SEG];
        omc_u32 k;
        int prefix_ok;

        if (i == skip_index || store->entries[i].key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        entry_schema = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.schema_ns);
        if (!omc_xmp_dump_views_equal(entry_schema, schema_ns)) {
            continue;
        }
        path = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.property_path);
        if (!omc_xmp_dump_parse_structured_property_path(path, &other_count,
                                                         other)) {
            continue;
        }

        if (other_count == seg_count) {
            prefix_ok = 1;
            for (k = 0U; k < seg_count; ++k) {
                if (!omc_xmp_dump_views_equal(seg[k].prefix, other[k].prefix)
                    || !omc_xmp_dump_views_equal(seg[k].name, other[k].name)) {
                    prefix_ok = 0;
                    break;
                }
            }
            if (!prefix_ok) {
                continue;
            }
            for (k = 0U; k < seg_count; ++k) {
                if (seg[k].index == 0U && other[k].index != 0U) {
                    return 1;
                }
                if (seg[k].lang.size == 0U && other[k].lang.size != 0U) {
                    return 1;
                }
            }
            continue;
        }

        if (other_count <= seg_count) {
            continue;
        }
        prefix_ok = 1;
        for (k = 0U; k < seg_count; ++k) {
            if (!omc_xmp_dump_path_seg_equal(&seg[k], &other[k])) {
                prefix_ok = 0;
                break;
            }
        }
        if (prefix_ok) {
            return 1;
        }
    }

    return 0;
}

static int
omc_xmp_dump_structured_root_has_indexed(const omc_store* store,
                                         omc_xmp_ns_view schema_ns,
                                         omc_xmp_ns_view root_name)
{
    omc_size i;

    if (store == (const omc_store*)0) {
        return 0;
    }
    for (i = 0U; i < store->entry_count; ++i) {
        omc_xmp_ns_view entry_schema;
        omc_xmp_ns_view path;
        omc_u32 count;
        omc_xmp_dump_path_seg seg[OMC_XMP_DUMP_MAX_STRUCT_SEG];

        if (store->entries[i].key.kind != OMC_KEY_XMP_PROPERTY) {
            continue;
        }
        entry_schema = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.schema_ns);
        if (!omc_xmp_dump_views_equal(entry_schema, schema_ns)) {
            continue;
        }
        path = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.xmp_property.property_path);
        if (!omc_xmp_dump_parse_structured_property_path(path, &count, seg)) {
            continue;
        }
        if (count >= 2U && omc_xmp_dump_views_equal(seg[0].name, root_name)
            && seg[0].index != 0U) {
            return 1;
        }
    }
    return 0;
}

static int
omc_xmp_dump_extract_existing_property(const omc_store* store, omc_size index,
                                       const omc_xmp_sidecar_opts* opts,
                                       omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_xmp_ns_view base_name;
    omc_xmp_ns_view lang;
    omc_u32 item_index;
    int has_lang_alt;
    omc_u32 k;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (!opts->include_existing_xmp || index >= store->entry_count) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_XMP_PROPERTY) {
        return 0;
    }

    schema_ns = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.xmp_property.schema_ns);
    property_name = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.xmp_property.property_path);
    if (schema_ns.data == (const omc_u8*)0
        || property_name.data == (const omc_u8*)0) {
        return 0;
    }
    if (!omc_xmp_dump_existing_namespace_allowed(schema_ns, opts)) {
        return 0;
    }

    if (omc_xmp_dump_structured_schema_supported(schema_ns)) {
        omc_u32 struct_count;
        omc_xmp_dump_path_seg struct_seg[OMC_XMP_DUMP_MAX_STRUCT_SEG];
        int unsupported_child_prefix;

        if (omc_xmp_dump_parse_structured_property_path(property_name,
                                                        &struct_count,
                                                        struct_seg)) {
            unsupported_child_prefix = 0;
            if (struct_seg[0].prefix.size != 0U) {
                unsupported_child_prefix = 1;
            }
            for (k = 1U; !unsupported_child_prefix && k < struct_count; ++k) {
                omc_xmp_ns_view child_schema;

                if (struct_seg[k].prefix.size != 0U
                    && !omc_xmp_dump_schema_from_prefix(
                        struct_seg[k].prefix, &child_schema)) {
                    unsupported_child_prefix = 1;
                }
            }

            if (!unsupported_child_prefix) {
                if (omc_xmp_dump_structured_path_shadowed(
                        store, index, schema_ns, struct_count, struct_seg)) {
                    return 0;
                }
                if (opts->existing_standard_namespace_policy
                        == OMC_XMP_STD_NS_CANONICALIZE_MANAGED
                    && omc_xmp_dump_existing_namespace_is_standard(schema_ns)
                    && omc_xmp_dump_existing_generated_replacement_exists(
                        store, index, opts, schema_ns,
                        struct_seg[0].name)) {
                    return 0;
                }
                if (struct_count > 1U
                    && struct_seg[struct_count - 1U].lang.size == 0U
                    && struct_seg[struct_count - 1U].index == 0U) {
                    if (omc_xmp_dump_structured_child_promotes_scalar_to_lang_alt(
                            schema_ns, struct_seg[struct_count - 1U].name)) {
                        struct_seg[struct_count - 1U].lang
                            = omc_xmp_dump_view_from_lit("x-default");
                    } else if (omc_xmp_dump_structured_child_promotes_scalar_to_indexed(
                                   schema_ns,
                                   struct_seg[struct_count - 1U].name)) {
                        struct_seg[struct_count - 1U].index = 1U;
                    }
                }
                if (!omc_xmp_dump_scalar_or_text_supported(
                        &store->entries[index].value, &store->arena)) {
                    return 0;
                }

                out_prop->schema_ns = schema_ns;
                out_prop->property_name = struct_seg[0].name;
                out_prop->store = store;
                out_prop->exif_ifd.data = (const omc_u8*)0;
                out_prop->exif_ifd.size = 0U;
                out_prop->exif_tag = 0U;
                out_prop->value = &store->entries[index].value;
                out_prop->arena = &store->arena;
                out_prop->value_mode = OMC_XMP_DUMP_VALUE_NORMAL;
                out_prop->item_index = 0U;
                out_prop->lang.data = (const omc_u8*)0;
                out_prop->lang.size = 0U;
                out_prop->struct_seg_count = struct_count;
                for (k = 0U; k < struct_count; ++k) {
                    out_prop->struct_seg[k] = struct_seg[k];
                }
                for (; k < OMC_XMP_DUMP_MAX_STRUCT_SEG; ++k) {
                    out_prop->struct_seg[k].prefix.data = (const omc_u8*)0;
                    out_prop->struct_seg[k].prefix.size = 0U;
                    out_prop->struct_seg[k].name.data = (const omc_u8*)0;
                    out_prop->struct_seg[k].name.size = 0U;
                    out_prop->struct_seg[k].index = 0U;
                    out_prop->struct_seg[k].lang.data = (const omc_u8*)0;
                    out_prop->struct_seg[k].lang.size = 0U;
                }
                if (omc_xmp_dump_structured_root_has_indexed(
                        store, schema_ns, struct_seg[0].name)) {
                    out_prop->container_kind
                        = omc_xmp_dump_structured_root_container_kind(
                            schema_ns, struct_seg[0].name);
                } else {
                    out_prop->container_kind
                        = OMC_XMP_DUMP_CONTAINER_STRUCT_RESOURCE;
                }
                return 1;
            }
        }
    }

    lang.data = (const omc_u8*)0;
    lang.size = 0U;
    item_index = 0U;
    has_lang_alt = omc_xmp_dump_parse_lang_alt_property_name(
        property_name, &base_name, &lang);
    if (!has_lang_alt) {
        if (!omc_xmp_dump_parse_indexed_property_name(property_name, &base_name,
                                                      &item_index)) {
            return 0;
        }
    }

    if (omc_xmp_dump_structured_schema_supported(schema_ns)
        && omc_xmp_dump_existing_has_structured_descendants(
            store, schema_ns, base_name)) {
        return 0;
    }

    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_tiff)) {
        if (omc_xmp_dump_view_equal_lit(base_name, k_prop_image_length)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_image_height);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_dng_private_data)) {
            return 0;
        }
    } else if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_exif)) {
        if (omc_xmp_dump_view_equal_lit(base_name, k_prop_exposure_bias_value)) {
            base_name = omc_xmp_dump_view_from_lit(
                k_prop_exposure_compensation);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_iso_speed_ratings)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_iso);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_pixel_x_dimension)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_exif_image_width);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_pixel_y_dimension)) {
            base_name = omc_xmp_dump_view_from_lit(k_prop_exif_image_height);
        } else if (omc_xmp_dump_view_equal_lit(
                       base_name, k_prop_focal_length_35mm_film)) {
            base_name = omc_xmp_dump_view_from_lit(
                k_prop_focal_length_35mm_format);
        } else if (omc_xmp_dump_view_equal_lit(base_name,
                                               k_prop_maker_note)) {
            return 0;
        }
    }

    if (has_lang_alt) {
        item_index = 0U;
    } else if (item_index == 0U
               && omc_xmp_dump_existing_promotes_scalar_to_lang_alt(schema_ns,
                                                                    base_name)
               && omc_xmp_dump_scalar_or_text_supported(
                   &store->entries[index].value, &store->arena)) {
        if (omc_xmp_dump_existing_has_explicit_lang_alt_base(store, schema_ns,
                                                             base_name)) {
            return 0;
        }
        lang = omc_xmp_dump_view_from_lit("x-default");
        has_lang_alt = 1;
    } else if (item_index == 0U
               && omc_xmp_dump_existing_promotes_scalar_to_indexed(schema_ns,
                                                                   base_name)
               && omc_xmp_dump_scalar_or_text_supported(
                   &store->entries[index].value, &store->arena)) {
        if (omc_xmp_dump_existing_has_explicit_indexed_base(store, schema_ns,
                                                            base_name)) {
            return 0;
        }
        item_index = 1U;
    }

    if (opts->existing_standard_namespace_policy
            == OMC_XMP_STD_NS_CANONICALIZE_MANAGED
        && omc_xmp_dump_existing_namespace_is_standard(schema_ns)
        && omc_xmp_dump_existing_generated_replacement_exists(
            store, index, opts, schema_ns, base_name)) {
        if (!has_lang_alt || omc_xmp_dump_view_equal_lit(lang, "x-default")) {
            return 0;
        }
    }

    if (!omc_xmp_dump_scalar_or_text_supported(&store->entries[index].value,
                                               &store->arena)) {
        return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = base_name;
    out_prop->store = store;
    out_prop->exif_ifd.data = (const omc_u8*)0;
    out_prop->exif_ifd.size = 0U;
    out_prop->exif_tag = 0U;
    out_prop->value = &store->entries[index].value;
    out_prop->arena = &store->arena;
    out_prop->value_mode = OMC_XMP_DUMP_VALUE_NORMAL;
    out_prop->item_index = item_index;
    out_prop->lang = lang;
    out_prop->struct_seg_count = 0U;
    for (k = 0U; k < OMC_XMP_DUMP_MAX_STRUCT_SEG; ++k) {
        out_prop->struct_seg[k].prefix.data = (const omc_u8*)0;
        out_prop->struct_seg[k].prefix.size = 0U;
        out_prop->struct_seg[k].name.data = (const omc_u8*)0;
        out_prop->struct_seg[k].name.size = 0U;
        out_prop->struct_seg[k].index = 0U;
        out_prop->struct_seg[k].lang.data = (const omc_u8*)0;
        out_prop->struct_seg[k].lang.size = 0U;
    }
    if (has_lang_alt) {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_ALT;
    } else if (item_index != 0U) {
        out_prop->container_kind = omc_xmp_dump_existing_container_kind(
            schema_ns, base_name);
    } else {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_NONE;
    }
    return 1;
}

static int
omc_xmp_dump_extract_exif_property(const omc_store* store, omc_size index,
                                   const omc_xmp_sidecar_opts* opts,
                                   omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view ifd_name;
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_xmp_dump_value_mode value_mode;
    omc_u16 tag;
    omc_u32 k;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (!opts->include_exif || index >= store->entry_count) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_EXIF_TAG) {
        return 0;
    }

    ifd_name = omc_xmp_dump_view_from_ref(
        &store->arena, store->entries[index].key.u.exif_tag.ifd);
    if (ifd_name.data == (const omc_u8*)0) {
        return 0;
    }

    tag = store->entries[index].key.u.exif_tag.tag;
    value_mode = OMC_XMP_DUMP_VALUE_NORMAL;
    if (omc_xmp_dump_view_equal_lit(ifd_name, "ifd0")) {
        if (tag == 0x0100U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_image_width);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0101U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_image_height);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0102U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_bits_per_sample);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0103U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_compression);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0106U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_photometric_interpretation);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x010FU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_make);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0110U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_model);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0112U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_orientation);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0115U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_samples_per_pixel);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x011CU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_planar_configuration);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0128U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_resolution_unit);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0132U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_datetime);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0153U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_sample_format);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xC71BU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_preview_datetime);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0213U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_ycbcr_positioning);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9BU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_title);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9CU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_comment);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9DU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_author);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9EU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_keywords);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9C9FU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
            property_name = omc_xmp_dump_view_from_lit(k_prop_xp_subject);
            value_mode = OMC_XMP_DUMP_VALUE_XP_UTF16LE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else {
            return 0;
        }
    } else if (omc_xmp_dump_view_equal_lit(ifd_name, "exififd")) {
        if (tag == 0x829AU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_exposure_time);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x829DU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_fnumber);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_FNUMBER;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x8822U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_exposure_program);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9003U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_datetime_original);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9004U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_datetime_digitized);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_DATE;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9201U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_shutter_speed_value);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_SHUTTER_SPEED;
            if (!omc_xmp_dump_srational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9202U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_aperture_value);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_APEX_FNUMBER;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9203U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_brightness_value);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_SRATIONAL_DECIMAL;
            if (!omc_xmp_dump_srational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9204U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_exposure_compensation);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_SRATIONAL_DECIMAL;
            if (!omc_xmp_dump_srational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9205U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_max_aperture_value);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_APEX_FNUMBER;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9207U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_metering_mode);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x9208U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_light_source);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x920AU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_focal_length);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_FOCAL_LENGTH;
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA432U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_lens_specification);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_LENS_SPEC;
            if (!omc_xmp_dump_urational_array_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA404U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_digital_zoom_ratio);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA20EU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_focal_plane_x_resolution);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA20FU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_focal_plane_y_resolution);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA407U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_gain_control);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA408U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_contrast);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA409U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_saturation);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA40AU) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_sharpness);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA40CU) {
            schema_ns = omc_xmp_dump_view_from_lit(
                k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_subject_distance_range);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA001U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_color_space);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA002U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_exif_image_width);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA003U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_exif_image_height);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA210U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_focal_plane_resolution_unit);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0xA300U) {
            schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
            property_name = omc_xmp_dump_view_from_lit(k_prop_file_source);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else {
            return 0;
        }
    } else if (omc_xmp_dump_view_equal_lit(ifd_name, "gpsifd")) {
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
        if (tag == 0x0000U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_version_id);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_VERSION;
            if (!omc_xmp_dump_bytes_view_supported(&store->entries[index].value,
                                                   &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0001U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_latitude_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0002U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_latitude);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD;
            if (!omc_xmp_dump_rational_triplet_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0003U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_longitude_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0004U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_longitude);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD;
            if (!omc_xmp_dump_rational_triplet_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0005U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_altitude_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0006U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_altitude);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_ALTITUDE;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0007U) {
            if (opts->exiftool_gpsdatetime_alias) {
                property_name = omc_xmp_dump_view_from_lit(
                    k_prop_gps_datetime);
            } else {
                property_name = omc_xmp_dump_view_from_lit(
                    k_prop_gps_timestamp);
            }
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_TIME;
            if (!omc_xmp_dump_urational_array_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0008U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_satellites);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0009U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_status);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x000AU) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_measure_mode);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x000BU) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_dop);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x000CU) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_speed_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x000DU) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_speed);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x000EU) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_track_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x000FU) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_track);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0010U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_img_direction_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0011U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_img_direction);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0012U) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_map_datum);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0013U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_latitude_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0014U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_latitude);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD;
            if (!omc_xmp_dump_rational_triplet_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0015U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_longitude_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0016U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_longitude);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD;
            if (!omc_xmp_dump_rational_triplet_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0017U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_bearing_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0018U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_bearing);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x0019U) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_distance_ref);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x001AU) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_dest_distance);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else if (tag == 0x001DU) {
            property_name = omc_xmp_dump_view_from_lit(k_prop_gps_date_stamp);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x001EU) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_differential);
            if (!omc_xmp_dump_scalar_or_text_supported(
                    &store->entries[index].value, &store->arena)) {
                return 0;
            }
        } else if (tag == 0x001FU) {
            property_name = omc_xmp_dump_view_from_lit(
                k_prop_gps_h_positioning_error);
            value_mode = OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL;
            if (!omc_xmp_dump_urational_supported(&store->entries[index].value,
                                                  &store->arena)) {
                return 0;
            }
        } else {
            return 0;
        }
    } else {
        return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = property_name;
    out_prop->store = store;
    out_prop->exif_ifd = ifd_name;
    out_prop->exif_tag = tag;
    out_prop->value = &store->entries[index].value;
    out_prop->arena = &store->arena;
    out_prop->value_mode = value_mode;
    out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_NONE;
    out_prop->item_index = 0U;
    out_prop->lang.data = (const omc_u8*)0;
    out_prop->lang.size = 0U;
    out_prop->struct_seg_count = 0U;
    for (k = 0U; k < OMC_XMP_DUMP_MAX_STRUCT_SEG; ++k) {
        out_prop->struct_seg[k].prefix.data = (const omc_u8*)0;
        out_prop->struct_seg[k].prefix.size = 0U;
        out_prop->struct_seg[k].name.data = (const omc_u8*)0;
        out_prop->struct_seg[k].name.size = 0U;
        out_prop->struct_seg[k].index = 0U;
        out_prop->struct_seg[k].lang.data = (const omc_u8*)0;
        out_prop->struct_seg[k].lang.size = 0U;
    }
    if (omc_xmp_dump_existing_promotes_scalar_to_lang_alt(schema_ns,
                                                          property_name)) {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_ALT;
        out_prop->lang = omc_xmp_dump_view_from_lit("x-default");
    } else if (omc_xmp_dump_existing_promotes_scalar_to_indexed(
                   schema_ns, property_name)) {
        out_prop->container_kind = omc_xmp_dump_existing_container_kind(
            schema_ns, property_name);
    }
    return 1;
}

static int
omc_xmp_dump_extract_iptc_property(const omc_store* store, omc_size index,
                                   const omc_xmp_sidecar_opts* opts,
                                   omc_xmp_dump_property* out_prop)
{
    omc_xmp_ns_view schema_ns;
    omc_xmp_ns_view property_name;
    omc_u16 record;
    omc_u16 dataset;
    omc_u32 k;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0
        || out_prop == (omc_xmp_dump_property*)0) {
        return 0;
    }
    if (!opts->include_iptc || index >= store->entry_count) {
        return 0;
    }
    if (store->entries[index].key.kind != OMC_KEY_IPTC_DATASET) {
        return 0;
    }
    if (!omc_xmp_dump_textish_supported(&store->entries[index].value,
                                        &store->arena)) {
        return 0;
    }

    record = store->entries[index].key.u.iptc_dataset.record;
    dataset = store->entries[index].key.u.iptc_dataset.dataset;
    if (record != 2U) {
        return 0;
    }

    switch (dataset) {
    case 5U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_title);
        break;
    case 15U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_category);
        break;
    case 20U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name
            = omc_xmp_dump_view_from_lit(k_prop_supplemental_categories);
        break;
    case 25U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_subject);
        break;
    case 80U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_creator);
        break;
    case 90U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_city);
        break;
    case 92U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_iptc4xmp);
        property_name = omc_xmp_dump_view_from_lit(k_prop_location);
        break;
    case 95U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_state);
        break;
    case 100U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_country);
        break;
    case 101U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_iptc4xmp);
        property_name = omc_xmp_dump_view_from_lit(k_prop_country_code);
        break;
    case 105U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_headline);
        break;
    case 110U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_credit);
        break;
    case 115U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_source);
        break;
    case 116U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_rights);
        break;
    case 120U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        property_name = omc_xmp_dump_view_from_lit(k_prop_description);
        break;
    case 122U:
        schema_ns = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        property_name = omc_xmp_dump_view_from_lit(k_prop_caption_writer);
        break;
    default: return 0;
    }

    out_prop->schema_ns = schema_ns;
    out_prop->property_name = property_name;
    out_prop->store = store;
    out_prop->exif_ifd.data = (const omc_u8*)0;
    out_prop->exif_ifd.size = 0U;
    out_prop->exif_tag = 0U;
    out_prop->value = &store->entries[index].value;
    out_prop->arena = &store->arena;
    out_prop->value_mode = OMC_XMP_DUMP_VALUE_BYTES_TEXT;
    out_prop->item_index = 0U;
    out_prop->lang.data = (const omc_u8*)0;
    out_prop->lang.size = 0U;
    out_prop->struct_seg_count = 0U;
    for (k = 0U; k < OMC_XMP_DUMP_MAX_STRUCT_SEG; ++k) {
        out_prop->struct_seg[k].prefix.data = (const omc_u8*)0;
        out_prop->struct_seg[k].prefix.size = 0U;
        out_prop->struct_seg[k].name.data = (const omc_u8*)0;
        out_prop->struct_seg[k].name.size = 0U;
        out_prop->struct_seg[k].index = 0U;
        out_prop->struct_seg[k].lang.data = (const omc_u8*)0;
        out_prop->struct_seg[k].lang.size = 0U;
    }
    if (omc_xmp_dump_existing_promotes_scalar_to_lang_alt(schema_ns,
                                                          property_name)) {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_ALT;
        out_prop->lang = omc_xmp_dump_view_from_lit("x-default");
    } else if (omc_xmp_dump_existing_promotes_scalar_to_indexed(
                   schema_ns, property_name)) {
        out_prop->container_kind = omc_xmp_dump_existing_container_kind(
            schema_ns, property_name);
    } else {
        out_prop->container_kind = OMC_XMP_DUMP_CONTAINER_NONE;
    }
    return 1;
}

static int
omc_xmp_dump_extract_pass_property(const omc_store* store, omc_size index,
                                   const omc_xmp_sidecar_opts* opts,
                                   omc_xmp_dump_pass pass,
                                   omc_xmp_dump_property* out_prop)
{
    switch (pass) {
    case OMC_XMP_DUMP_PASS_EXISTING:
        return omc_xmp_dump_extract_existing_property(store, index, opts,
                                                      out_prop);
    case OMC_XMP_DUMP_PASS_EXIF:
        return omc_xmp_dump_extract_exif_property(store, index, opts, out_prop);
    case OMC_XMP_DUMP_PASS_IPTC:
        return omc_xmp_dump_extract_iptc_property(store, index, opts, out_prop);
    default: return 0;
    }
}

static int
omc_xmp_dump_existing_generated_replacement_exists(const omc_store* store,
                                                   omc_size skip_index,
                                                   const omc_xmp_sidecar_opts* opts,
                                                   omc_xmp_ns_view schema_ns,
                                                   omc_xmp_ns_view property_name)
{
    omc_size i;
    omc_xmp_dump_property prop;

    if (store == (const omc_store*)0 || opts == (const omc_xmp_sidecar_opts*)0) {
        return 0;
    }

    for (i = 0U; i < store->entry_count; ++i) {
        if (i == skip_index) {
            continue;
        }
        if (omc_xmp_dump_extract_exif_property(store, i, opts, &prop)
            || omc_xmp_dump_extract_iptc_property(store, i, opts, &prop)) {
            if (!omc_xmp_dump_property_has_output(&prop)) {
                continue;
            }
            if (omc_xmp_dump_views_equal(prop.schema_ns, schema_ns)
                && omc_xmp_dump_views_equal(prop.property_name,
                                            property_name)) {
                return 1;
            }
        }
    }

    return 0;
}

static int
omc_xmp_dump_property_keys_equal(const omc_xmp_dump_property* a,
                                 const omc_xmp_dump_property* b)
{
    return omc_xmp_dump_views_equal(a->schema_ns, b->schema_ns)
           && omc_xmp_dump_views_equal(a->property_name, b->property_name);
}

static int
omc_xmp_dump_property_needs_group(const omc_xmp_dump_property* prop)
{
    return prop->container_kind != OMC_XMP_DUMP_CONTAINER_NONE;
}

static void
omc_xmp_dump_fill_pass_order(const omc_xmp_sidecar_opts* opts,
                             omc_xmp_dump_pass* out_order)
{
    if (out_order == (omc_xmp_dump_pass*)0) {
        return;
    }

    if (opts != (const omc_xmp_sidecar_opts*)0
        && opts->conflict_policy == OMC_XMP_CONFLICT_CURRENT_BEHAVIOR) {
        out_order[0] = OMC_XMP_DUMP_PASS_EXIF;
        out_order[1] = OMC_XMP_DUMP_PASS_EXISTING;
        out_order[2] = OMC_XMP_DUMP_PASS_IPTC;
    } else if (opts != (const omc_xmp_sidecar_opts*)0
               && opts->conflict_policy
                      == OMC_XMP_CONFLICT_GENERATED_WINS) {
        out_order[0] = OMC_XMP_DUMP_PASS_EXIF;
        out_order[1] = OMC_XMP_DUMP_PASS_IPTC;
        out_order[2] = OMC_XMP_DUMP_PASS_EXISTING;
    } else {
        out_order[0] = OMC_XMP_DUMP_PASS_EXISTING;
        out_order[1] = OMC_XMP_DUMP_PASS_EXIF;
        out_order[2] = OMC_XMP_DUMP_PASS_IPTC;
    }
}

static int
omc_xmp_dump_portable_enum_text_override(const omc_xmp_dump_property* prop,
                                         const char** out_text)
{
    omc_u64 value;

    if (prop == (const omc_xmp_dump_property*)0 || out_text == (const char**)0
        || !omc_xmp_dump_scalar_u64_value(prop->value, &value)) {
        return 0;
    }

    *out_text = (const char*)0;

    if (omc_xmp_dump_view_equal_lit(prop->schema_ns, k_xmp_ns_tiff)) {
        switch (prop->exif_tag) {
        case 0x0103U:
            switch (value) {
            case 1U: *out_text = "Uncompressed"; return 1;
            case 4U: *out_text = "T6/Group 4 Fax"; return 1;
            case 6U: *out_text = "JPEG (old-style)"; return 1;
            case 7U: *out_text = "JPEG"; return 1;
            case 8U: *out_text = "Adobe Deflate"; return 1;
            case 9U: *out_text = "JBIG B&W"; return 1;
            case 32770U: *out_text = "Samsung SRW Compressed"; return 1;
            case 32773U: *out_text = "PackBits"; return 1;
            default: return 0;
            }
        case 0x011CU:
            switch (value) {
            case 1U: *out_text = "Chunky"; return 1;
            case 2U: *out_text = "Planar"; return 1;
            default: return 0;
            }
        case 0x0106U:
            switch (value) {
            case 0U: *out_text = "WhiteIsZero"; return 1;
            case 1U: *out_text = "BlackIsZero"; return 1;
            case 2U: *out_text = "RGB"; return 1;
            case 3U: *out_text = "RGB Palette"; return 1;
            case 4U: *out_text = "Transparency Mask"; return 1;
            case 5U: *out_text = "CMYK"; return 1;
            case 6U: *out_text = "YCbCr"; return 1;
            case 8U: *out_text = "CIELab"; return 1;
            case 9U: *out_text = "ICCLab"; return 1;
            case 10U: *out_text = "ITULab"; return 1;
            default: return 0;
            }
        case 0x0213U:
            switch (value) {
            case 1U: *out_text = "Centered"; return 1;
            case 2U: *out_text = "Co-sited"; return 1;
            default: return 0;
            }
        default: return 0;
        }
    }

    if (omc_xmp_dump_view_equal_lit(prop->schema_ns, k_xmp_ns_exif)) {
        switch (prop->exif_tag) {
        case 0x9208U:
            switch (value) {
            case 0U: *out_text = "Unknown"; return 1;
            case 1U: *out_text = "Daylight"; return 1;
            case 2U: *out_text = "Fluorescent"; return 1;
            case 3U: *out_text = "Tungsten (incandescent)"; return 1;
            case 4U: *out_text = "Flash"; return 1;
            case 9U: *out_text = "Fine weather"; return 1;
            case 10U: *out_text = "Cloudy"; return 1;
            case 11U: *out_text = "Shade"; return 1;
            case 12U: *out_text = "Daylight fluorescent"; return 1;
            case 13U: *out_text = "Day white fluorescent"; return 1;
            case 14U: *out_text = "Cool white fluorescent"; return 1;
            case 15U: *out_text = "White fluorescent"; return 1;
            case 17U: *out_text = "Standard light A"; return 1;
            case 18U: *out_text = "Standard light B"; return 1;
            case 19U: *out_text = "Standard light C"; return 1;
            case 20U: *out_text = "D55"; return 1;
            case 21U: *out_text = "D65"; return 1;
            case 22U: *out_text = "D75"; return 1;
            case 23U: *out_text = "D50"; return 1;
            case 24U: *out_text = "ISO studio tungsten"; return 1;
            case 255U: *out_text = "Other"; return 1;
            default: return 0;
            }
        case 0xA407U:
            switch (value) {
            case 0U: *out_text = "None"; return 1;
            case 1U: *out_text = "Low gain up"; return 1;
            case 2U: *out_text = "High gain up"; return 1;
            case 3U: *out_text = "Low gain down"; return 1;
            case 4U: *out_text = "High gain down"; return 1;
            default: return 0;
            }
        case 0xA408U:
            switch (value) {
            case 0U: *out_text = "Normal"; return 1;
            case 1U: *out_text = "Low"; return 1;
            case 2U: *out_text = "High"; return 1;
            default: return 0;
            }
        case 0xA409U:
            switch (value) {
            case 0U: *out_text = "Normal"; return 1;
            case 1U: *out_text = "Low"; return 1;
            case 2U: *out_text = "High"; return 1;
            default: return 0;
            }
        case 0xA40AU:
            switch (value) {
            case 0U: *out_text = "Normal"; return 1;
            case 1U: *out_text = "Soft"; return 1;
            case 2U: *out_text = "Hard"; return 1;
            default: return 0;
            }
        case 0xA40CU:
            switch (value) {
            case 0U: *out_text = "Unknown"; return 1;
            case 1U: *out_text = "Macro"; return 1;
            case 2U: *out_text = "Close"; return 1;
            case 3U: *out_text = "Distant"; return 1;
            default: return 0;
            }
        case 0xA001U:
            switch (value) {
            case 1U: *out_text = "sRGB"; return 1;
            case 2U: *out_text = "Adobe RGB"; return 1;
            case 0xFFFFU: *out_text = "Uncalibrated"; return 1;
            default: return 0;
            }
        case 0xA210U:
            switch (value) {
            case 2U: *out_text = "inches"; return 1;
            case 3U: *out_text = "cm"; return 1;
            case 4U: *out_text = "mm"; return 1;
            case 5U: *out_text = "um"; return 1;
            default: return 0;
            }
        case 0xA300U:
            if (value == 3U) {
                *out_text = "Digital Camera";
                return 1;
            }
            return 0;
        case 0x001EU:
            switch (value) {
            case 0U: *out_text = "No Correction"; return 1;
            case 1U: *out_text = "Differential Corrected"; return 1;
            default: return 0;
            }
        default: return 0;
        }
    }

    return 0;
}

static int
omc_xmp_dump_portable_gps_ref_text_override(const omc_xmp_dump_property* prop,
                                            const char** out_text)
{
    char ref;

    if (prop == (const omc_xmp_dump_property*)0 || out_text == (const char**)0
        || !omc_xmp_dump_view_equal_lit(prop->schema_ns, k_xmp_ns_exif)) {
        return 0;
    }

    *out_text = (const char*)0;

    switch (prop->exif_tag) {
    case 0x0009U:
        if (omc_xmp_dump_first_ref_char(prop->value, prop->arena, "AV",
                                        &ref)) {
            *out_text = (ref == 'A') ? "Measurement Active"
                                     : "Measurement Void";
            return 1;
        }
        return 0;
    case 0x000CU:
        if (omc_xmp_dump_first_ref_char(prop->value, prop->arena, "KMN",
                                        &ref)) {
            if (ref == 'K') {
                *out_text = "km/h";
            } else if (ref == 'M') {
                *out_text = "mph";
            } else {
                *out_text = "knots";
            }
            return 1;
        }
        return 0;
    case 0x000EU:
    case 0x0010U:
    case 0x0017U:
        if (omc_xmp_dump_first_ref_char(prop->value, prop->arena, "TM",
                                        &ref)) {
            *out_text = (ref == 'T') ? "True North" : "Magnetic North";
            return 1;
        }
        return 0;
    case 0x0019U:
        if (omc_xmp_dump_first_ref_char(prop->value, prop->arena, "KMN",
                                        &ref)) {
            if (ref == 'K') {
                *out_text = "Kilometers";
            } else if (ref == 'M') {
                *out_text = "Miles";
            } else {
                *out_text = "Knots";
            }
            return 1;
        }
        return 0;
    default: return 0;
    }
}

static int
omc_xmp_dump_claimed_before(const omc_store* store, omc_size index,
                            const omc_xmp_sidecar_opts* opts,
                            omc_xmp_dump_pass pass,
                            const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
    omc_u32 p;
    omc_size i;
    omc_size end;
    omc_xmp_dump_property prior;

    omc_xmp_dump_fill_pass_order(opts, order);

    for (p = 0U; p < OMC_XMP_DUMP_PASS_COUNT; ++p) {
        omc_xmp_dump_pass cur_pass;

        cur_pass = order[p];
        end = store->entry_count;
        if (cur_pass == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, cur_pass,
                                                    &prior)) {
                continue;
            }
            if (!omc_xmp_dump_property_has_output(&prior)) {
                continue;
            }
            if (omc_xmp_dump_property_keys_equal(&prior, prop)) {
                return 1;
            }
        }
        if (cur_pass == pass) {
            break;
        }
    }

    return 0;
}

static int
omc_xmp_dump_alt_lang_claimed_before(const omc_store* store, omc_size index,
                                     const omc_xmp_sidecar_opts* opts,
                                     omc_xmp_dump_pass pass,
                                     const omc_xmp_dump_property* prop,
                                     omc_xmp_ns_view lang)
{
    omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
    omc_u32 p;
    omc_size i;
    omc_size end;
    omc_xmp_dump_property prior;

    omc_xmp_dump_fill_pass_order(opts, order);

    for (p = 0U; p < OMC_XMP_DUMP_PASS_COUNT; ++p) {
        omc_xmp_dump_pass cur_pass;

        cur_pass = order[p];
        end = store->entry_count;
        if (cur_pass == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, cur_pass,
                                                    &prior)) {
                continue;
            }
            if (!omc_xmp_dump_property_keys_equal(&prior, prop)
                || prior.container_kind != OMC_XMP_DUMP_CONTAINER_ALT
                || prior.lang.data == (const omc_u8*)0
                || prior.lang.size == 0U) {
                continue;
            }
            if (omc_xmp_dump_views_equal(prior.lang, lang)) {
                return 1;
            }
        }
        if (cur_pass == pass) {
            break;
        }
    }

    return 0;
}

static int
omc_xmp_dump_schema_first_for_emitted(const omc_store* store, omc_size index,
                                      const omc_xmp_sidecar_opts* opts,
                                      omc_xmp_dump_pass pass,
                                      omc_xmp_ns_view schema_ns)
{
    omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
    omc_u32 p;
    omc_size i;
    omc_size end;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    omc_xmp_dump_fill_pass_order(opts, order);
    for (p = 0U; p < OMC_XMP_DUMP_PASS_COUNT; ++p) {
        omc_xmp_dump_pass cur_pass;

        cur_pass = order[p];
        end = store->entry_count;
        if (cur_pass == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, cur_pass,
                                                    &prop)) {
                continue;
            }
            if (!omc_xmp_dump_property_has_output(&prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, cur_pass, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                return 1;
            }
            if (omc_xmp_dump_views_equal(prop.schema_ns, schema_ns)) {
                return 0;
            }
            emitted += 1U;
        }
        if (cur_pass == pass) {
            break;
        }
    }

    return 1;
}

static int
omc_xmp_dump_known_prefix(omc_xmp_ns_view schema_ns, const char** out_prefix)
{
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_xap)) {
        *out_prefix = "xmp";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_dc)) {
        *out_prefix = "dc";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_ps)) {
        *out_prefix = "photoshop";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_exif)) {
        *out_prefix = "exif";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_tiff)) {
        *out_prefix = "tiff";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_mm)) {
        *out_prefix = "xmpMM";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_st_ref)) {
        *out_prefix = "stRef";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_st_evt)) {
        *out_prefix = "stEvt";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_st_ver)) {
        *out_prefix = "stVer";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_st_mfs)) {
        *out_prefix = "stMfs";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp)) {
        *out_prefix = "Iptc4xmpCore";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_iptc4xmp_ext)) {
        *out_prefix = "Iptc4xmpExt";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_rights)) {
        *out_prefix = "xmpRights";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_pdf)) {
        *out_prefix = "pdf";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_lr)) {
        *out_prefix = "lr";
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(schema_ns, k_xmp_ns_plus)) {
        *out_prefix = "plus";
        return 1;
    }

    return 0;
}

static int
omc_xmp_dump_schema_from_prefix(omc_xmp_ns_view prefix,
                                omc_xmp_ns_view* out_schema)
{
    if (out_schema == (omc_xmp_ns_view*)0) {
        return 0;
    }

    out_schema->data = (const omc_u8*)0;
    out_schema->size = 0U;

    if (omc_xmp_dump_view_equal_lit(prefix, "xmp")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_xap);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "dc")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_dc);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "photoshop")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_ps);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "exif")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_exif);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "tiff")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_tiff);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "xmpMM")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_mm);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "stRef")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_st_ref);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "stEvt")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_st_evt);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "stVer")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_st_ver);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "stMfs")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_st_mfs);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "Iptc4xmpCore")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_iptc4xmp);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "Iptc4xmpExt")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_iptc4xmp_ext);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "xmpRights")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_rights);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "pdf")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_pdf);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "lr")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_lr);
        return 1;
    }
    if (omc_xmp_dump_view_equal_lit(prefix, "plus")) {
        *out_schema = omc_xmp_dump_view_from_lit(k_xmp_ns_plus);
        return 1;
    }

    return 0;
}

static omc_u32
omc_xmp_dump_unknown_prefix_ordinal(const omc_store* store, omc_size index,
                                    const omc_xmp_sidecar_opts* opts,
                                    omc_xmp_dump_pass pass,
                                    omc_xmp_ns_view schema_ns)
{
    omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
    omc_u32 p;
    omc_size i;
    omc_size end;
    omc_u32 emitted;
    omc_u32 ordinal;
    omc_xmp_dump_property prop;
    const char* known_prefix;

    emitted = 0U;
    ordinal = 0U;
    omc_xmp_dump_fill_pass_order(opts, order);
    for (p = 0U; p < OMC_XMP_DUMP_PASS_COUNT; ++p) {
        omc_xmp_dump_pass cur_pass;

        cur_pass = order[p];
        end = store->entry_count;
        if (cur_pass == pass) {
            end = index;
        }
        for (i = 0U; i < end; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, cur_pass,
                                                    &prop)) {
                continue;
            }
            if (!omc_xmp_dump_property_has_output(&prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, cur_pass, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                return ordinal == 0U ? 1U : ordinal;
            }
            emitted += 1U;
            if (omc_xmp_dump_known_prefix(prop.schema_ns, &known_prefix)) {
                continue;
            }
            if (omc_xmp_dump_schema_first_for_emitted(store, i, opts, cur_pass,
                                                      prop.schema_ns)) {
                ordinal += 1U;
            }
        }
        if (cur_pass == pass) {
            break;
        }
    }

    if (ordinal == 0U) {
        ordinal = 1U;
    }
    if (!omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        return ordinal;
    }
    return 0U;
}

static void
omc_xmp_dump_write_u32_decimal(omc_xmp_dump_writer* writer, omc_u32 value)
{
    char buf[16];
    omc_u32 pos;

    pos = 0U;
    do {
        buf[pos++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (pos != 0U) {
        pos -= 1U;
        omc_xmp_dump_write_byte(writer, buf[pos]);
    }
}

static void
omc_xmp_dump_write_u64_decimal(omc_xmp_dump_writer* writer, omc_u64 value)
{
    char buf[32];
    omc_u32 pos;

    pos = 0U;
    do {
        buf[pos++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (pos != 0U) {
        pos -= 1U;
        omc_xmp_dump_write_byte(writer, buf[pos]);
    }
}

static void
omc_xmp_dump_write_i64_decimal(omc_xmp_dump_writer* writer, omc_s64 value)
{
    omc_u64 magnitude;

    if (value < 0) {
        omc_xmp_dump_write_byte(writer, '-');
        magnitude = (omc_u64)(-(value + 1)) + 1U;
    } else {
        magnitude = (omc_u64)value;
    }

    omc_xmp_dump_write_u64_decimal(writer, magnitude);
}

static void
omc_xmp_dump_write_hex_byte(omc_xmp_dump_writer* writer, omc_u8 value)
{
    static const char k_hex[] = "0123456789ABCDEF";

    omc_xmp_dump_write_byte(writer, k_hex[(value >> 4) & 0x0FU]);
    omc_xmp_dump_write_byte(writer, k_hex[value & 0x0FU]);
}

typedef struct omc_xmp_dump_base64_state {
    omc_u8 tail[3];
    omc_u32 tail_size;
} omc_xmp_dump_base64_state;

static void
omc_xmp_dump_base64_write_triplet(omc_xmp_dump_writer* writer,
                                  const omc_u8* triplet, omc_u32 size)
{
    static const char k_base64[]
        = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    omc_u32 block;
    char out4[4];

    block = ((omc_u32)triplet[0] << 16);
    if (size > 1U) {
        block |= ((omc_u32)triplet[1] << 8);
    }
    if (size > 2U) {
        block |= (omc_u32)triplet[2];
    }

    out4[0] = k_base64[(block >> 18) & 0x3FU];
    out4[1] = k_base64[(block >> 12) & 0x3FU];
    out4[2] = (size > 1U) ? k_base64[(block >> 6) & 0x3FU] : '=';
    out4[3] = (size > 2U) ? k_base64[block & 0x3FU] : '=';
    omc_xmp_dump_write_bytes(writer, out4, 4U);
}

static void
omc_xmp_dump_base64_feed(omc_xmp_dump_writer* writer,
                         omc_xmp_dump_base64_state* state, const omc_u8* data,
                         omc_size size)
{
    omc_size i;

    if (state == (omc_xmp_dump_base64_state*)0) {
        return;
    }

    i = 0U;
    while (i < size) {
        state->tail[state->tail_size++] = data[i++];
        if (state->tail_size == 3U) {
            omc_xmp_dump_base64_write_triplet(writer, state->tail, 3U);
            state->tail_size = 0U;
        }
    }
}

static void
omc_xmp_dump_base64_finish(omc_xmp_dump_writer* writer,
                           omc_xmp_dump_base64_state* state)
{
    if (state == (omc_xmp_dump_base64_state*)0 || state->tail_size == 0U) {
        return;
    }
    omc_xmp_dump_base64_write_triplet(writer, state->tail, state->tail_size);
    state->tail_size = 0U;
}

static void
omc_xmp_dump_write_xml_escaped(omc_xmp_dump_writer* writer,
                               const omc_u8* bytes, omc_size size,
                               int for_attribute)
{
    omc_size i;
    omc_u8 c;

    for (i = 0U; i < size; ++i) {
        c = bytes[i];
        switch (c) {
        case (omc_u8)'&':
            omc_xmp_dump_write_bytes(writer, "&amp;", 5U);
            break;
        case (omc_u8)'<':
            omc_xmp_dump_write_bytes(writer, "&lt;", 4U);
            break;
        case (omc_u8)'>':
            omc_xmp_dump_write_bytes(writer, "&gt;", 4U);
            break;
        case (omc_u8)'"':
            (void)for_attribute;
            omc_xmp_dump_write_bytes(writer, "&quot;", 6U);
            break;
        case (omc_u8)'\'':
            omc_xmp_dump_write_bytes(writer, "&apos;", 6U);
            break;
        default:
            if (c < 0x20U && c != (omc_u8)'\t' && c != (omc_u8)'\r'
                && c != (omc_u8)'\n') {
                omc_xmp_dump_write_bytes(writer, "\\x", 2U);
                omc_xmp_dump_write_hex_byte(writer, c);
            } else {
                omc_xmp_dump_write_byte(writer, (char)c);
            }
            break;
        }
    }
}

static void
omc_xmp_dump_write_prefix(omc_xmp_dump_writer* writer, const omc_store* store,
                          omc_size index, const omc_xmp_sidecar_opts* opts,
                          omc_xmp_dump_pass pass, omc_xmp_ns_view schema_ns)
{
    const char* known_prefix;
    omc_u32 ordinal;

    if (omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        omc_xmp_dump_write_bytes(writer, known_prefix, strlen(known_prefix));
        return;
    }

    omc_xmp_dump_write_bytes(writer, "ns", 2U);
    ordinal = omc_xmp_dump_unknown_prefix_ordinal(store, index, opts, pass,
                                                  schema_ns);
    omc_xmp_dump_write_u32_decimal(writer, ordinal);
}

static void
omc_xmp_dump_write_utf8_codepoint(omc_xmp_dump_writer* writer,
                                  omc_u32 codepoint)
{
    char bytes[4];

    if (codepoint <= 0x7FU) {
        omc_xmp_dump_write_byte(writer, (char)codepoint);
        return;
    }
    if (codepoint <= 0x7FFU) {
        bytes[0] = (char)(0xC0U | ((codepoint >> 6) & 0x1FU));
        bytes[1] = (char)(0x80U | (codepoint & 0x3FU));
        omc_xmp_dump_write_bytes(writer, bytes, 2U);
        return;
    }
    if (codepoint <= 0xFFFFU) {
        bytes[0] = (char)(0xE0U | ((codepoint >> 12) & 0x0FU));
        bytes[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[2] = (char)(0x80U | (codepoint & 0x3FU));
        omc_xmp_dump_write_bytes(writer, bytes, 3U);
        return;
    }

    bytes[0] = (char)(0xF0U | ((codepoint >> 18) & 0x07U));
    bytes[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
    bytes[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
    bytes[3] = (char)(0x80U | (codepoint & 0x3FU));
    omc_xmp_dump_write_bytes(writer, bytes, 4U);
}

static void
omc_xmp_dump_write_xml_codepoint(omc_xmp_dump_writer* writer,
                                 omc_u32 codepoint)
{
    if (codepoint == (omc_u32)'&') {
        omc_xmp_dump_write_bytes(writer, "&amp;", 5U);
        return;
    }
    if (codepoint == (omc_u32)'<') {
        omc_xmp_dump_write_bytes(writer, "&lt;", 4U);
        return;
    }
    if (codepoint == (omc_u32)'>') {
        omc_xmp_dump_write_bytes(writer, "&gt;", 4U);
        return;
    }
    if (codepoint == (omc_u32)'"') {
        omc_xmp_dump_write_bytes(writer, "&quot;", 6U);
        return;
    }
    if (codepoint == (omc_u32)'\'') {
        omc_xmp_dump_write_bytes(writer, "&apos;", 6U);
        return;
    }
    if (codepoint < 0x20U && codepoint != (omc_u32)'\t'
        && codepoint != (omc_u32)'\r' && codepoint != (omc_u32)'\n') {
        omc_xmp_dump_write_bytes(writer, "\\x", 2U);
        omc_xmp_dump_write_hex_byte(writer, (omc_u8)codepoint);
        return;
    }

    omc_xmp_dump_write_utf8_codepoint(writer, codepoint);
}

static omc_size
omc_xmp_dump_trim_xp_utf16le_size(const omc_u8* data, omc_size size)
{
    while (size >= 2U && data[size - 2U] == 0U && data[size - 1U] == 0U) {
        size -= 2U;
    }
    return size;
}

static void
omc_xmp_dump_write_utf16le_xml_escaped(omc_xmp_dump_writer* writer,
                                       const omc_u8* data, omc_size size)
{
    omc_size i;
    omc_u16 lead;

    size = omc_xmp_dump_trim_xp_utf16le_size(data, size);
    i = 0U;
    while (i + 1U < size) {
        omc_u32 codepoint;

        lead = (omc_u16)(((omc_u16)data[i])
                         | (omc_u16)(((omc_u16)data[i + 1U]) << 8));
        i += 2U;

        if (lead >= 0xD800U && lead <= 0xDBFFU && i + 1U < size) {
            omc_u16 trail;

            trail = (omc_u16)(((omc_u16)data[i])
                              | (omc_u16)(((omc_u16)data[i + 1U]) << 8));
            if (trail >= 0xDC00U && trail <= 0xDFFFU) {
                codepoint = 0x10000U
                            + (((omc_u32)(lead - 0xD800U)) << 10)
                            + (omc_u32)(trail - 0xDC00U);
                i += 2U;
                omc_xmp_dump_write_xml_codepoint(writer, codepoint);
                continue;
            }
        }
        if (lead >= 0xDC00U && lead <= 0xDFFFU) {
            continue;
        }

        omc_xmp_dump_write_xml_codepoint(writer, (omc_u32)lead);
    }
}

static int
omc_xmp_dump_parse_two_digits(const omc_u8* data, omc_size pos,
                              omc_size size)
{
    if (pos + 1U >= size) {
        return -1;
    }
    if (data[pos] < (omc_u8)'0' || data[pos] > (omc_u8)'9'
        || data[pos + 1U] < (omc_u8)'0' || data[pos + 1U] > (omc_u8)'9') {
        return -1;
    }
    return (int)((data[pos] - (omc_u8)'0') * 10
                 + (data[pos + 1U] - (omc_u8)'0'));
}

static int
omc_xmp_dump_normalize_exif_datetime(const omc_u8* data, omc_size size,
                                     char* out, omc_size out_cap,
                                     omc_size* out_size)
{
    int year0;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    omc_size pos;
    int have_zone;
    char zone_buf[7];

    if (data == (const omc_u8*)0 || out == (char*)0
        || out_size == (omc_size*)0 || out_cap < 20U) {
        return 0;
    }
    if (size < 19U) {
        return 0;
    }

    if (data[0] < (omc_u8)'0' || data[0] > (omc_u8)'9'
        || data[1] < (omc_u8)'0' || data[1] > (omc_u8)'9'
        || data[2] < (omc_u8)'0' || data[2] > (omc_u8)'9'
        || data[3] < (omc_u8)'0' || data[3] > (omc_u8)'9') {
        return 0;
    }
    if ((data[4] != (omc_u8)':' && data[4] != (omc_u8)'-')
        || (data[7] != (omc_u8)':' && data[7] != (omc_u8)'-')
        || data[10] != (omc_u8)' ' || data[13] != (omc_u8)':'
        || data[16] != (omc_u8)':') {
        return 0;
    }

    year0 = (int)((data[0] - (omc_u8)'0') * 1000
                  + (data[1] - (omc_u8)'0') * 100
                  + (data[2] - (omc_u8)'0') * 10
                  + (data[3] - (omc_u8)'0'));
    month = omc_xmp_dump_parse_two_digits(data, 5U, size);
    day = omc_xmp_dump_parse_two_digits(data, 8U, size);
    hour = omc_xmp_dump_parse_two_digits(data, 11U, size);
    minute = omc_xmp_dump_parse_two_digits(data, 14U, size);
    second = omc_xmp_dump_parse_two_digits(data, 17U, size);
    if (month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0) {
        return 0;
    }

    out[0] = (char)data[0];
    out[1] = (char)data[1];
    out[2] = (char)data[2];
    out[3] = (char)data[3];
    out[4] = '-';
    out[5] = (char)data[5];
    out[6] = (char)data[6];
    out[7] = '-';
    out[8] = (char)data[8];
    out[9] = (char)data[9];
    out[10] = 'T';
    out[11] = (char)data[11];
    out[12] = (char)data[12];
    out[13] = ':';
    out[14] = (char)data[14];
    out[15] = (char)data[15];
    out[16] = ':';
    out[17] = (char)data[17];
    out[18] = (char)data[18];
    pos = 19U;

    have_zone = 0;
    if (size >= 23U && data[19] == (omc_u8)' ') {
        if (data[20] == (omc_u8)'U' && data[21] == (omc_u8)'T'
            && data[22] == (omc_u8)'C') {
            zone_buf[0] = 'Z';
            zone_buf[1] = '\0';
            have_zone = 1;
        }
    } else if (size >= 24U
               && (data[19] == (omc_u8)'+' || data[19] == (omc_u8)'-')) {
        int zh;
        int zm;

        zh = omc_xmp_dump_parse_two_digits(data, 20U, size);
        zm = omc_xmp_dump_parse_two_digits(data, 22U, size);
        if (zh >= 0 && zm >= 0) {
            zone_buf[0] = (char)data[19];
            zone_buf[1] = (char)data[20];
            zone_buf[2] = (char)data[21];
            zone_buf[3] = ':';
            zone_buf[4] = (char)data[22];
            zone_buf[5] = (char)data[23];
            zone_buf[6] = '\0';
            have_zone = 1;
        }
    }

    if (have_zone != 0) {
        omc_size zone_len;
        zone_len = strlen(zone_buf);
        if (pos + zone_len > out_cap) {
            return 0;
        }
        memcpy(out + pos, zone_buf, zone_len);
        pos += zone_len;
    }

    (void)year0;
    *out_size = pos;
    return 1;
}

static void
omc_xmp_dump_trim_decimal_string(char* buf)
{
    omc_size n;

    n = strlen(buf);
    while (n != 0U && buf[n - 1U] == '0') {
        buf[n - 1U] = '\0';
        n -= 1U;
    }
    if (n != 0U && buf[n - 1U] == '.') {
        buf[n - 1U] = '\0';
    }
}

static int
omc_xmp_dump_format_double_trimmed(double value, int decimals, char* buf,
                                   omc_size buf_size)
{
    if (buf == (char*)0 || buf_size == 0U) {
        return 0;
    }
    if (decimals < 0) {
        decimals = 0;
    }
    if (decimals > 9) {
        decimals = 9;
    }
    sprintf(buf, "%.*f", decimals, value);
    omc_xmp_dump_trim_decimal_string(buf);
    return 1;
}

static int
omc_xmp_dump_find_exif_entry(const omc_store* store, omc_xmp_ns_view ifd_name,
                             omc_u16 tag, const omc_entry** out_entry)
{
    omc_size i;
    omc_xmp_ns_view cand_ifd;

    if (out_entry == (const omc_entry**)0) {
        return 0;
    }
    *out_entry = (const omc_entry*)0;
    if (store == (const omc_store*)0) {
        return 0;
    }

    for (i = 0U; i < store->entry_count; ++i) {
        if (store->entries[i].key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        if (store->entries[i].key.u.exif_tag.tag != tag) {
            continue;
        }
        cand_ifd = omc_xmp_dump_view_from_ref(
            &store->arena, store->entries[i].key.u.exif_tag.ifd);
        if (!omc_xmp_dump_views_equal(cand_ifd, ifd_name)) {
            continue;
        }
        *out_entry = &store->entries[i];
        return 1;
    }

    return 0;
}

static int
omc_xmp_dump_entry_text_char(const omc_entry* entry, const omc_arena* arena,
                             char* out_char)
{
    omc_const_bytes bytes;

    if (entry == (const omc_entry*)0 || arena == (const omc_arena*)0
        || out_char == (char*)0) {
        return 0;
    }
    if (entry->value.kind != OMC_VAL_TEXT && entry->value.kind != OMC_VAL_BYTES) {
        return 0;
    }
    bytes = omc_arena_view(arena, entry->value.u.ref);
    if (bytes.data == (const omc_u8*)0 || bytes.size == 0U) {
        return 0;
    }
    *out_char = (char)bytes.data[0];
    return 1;
}

static int
omc_xmp_dump_first_urational(const omc_val* value, const omc_arena* arena,
                             omc_urational* out_r)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0
        || out_r == (omc_urational*)0) {
        return 0;
    }

    if (value->kind == OMC_VAL_SCALAR && value->elem_type == OMC_ELEM_URATIONAL) {
        if (value->u.ur.denom == 0U) {
            return 0;
        }
        *out_r = value->u.ur;
        return 1;
    }
    if (value->kind == OMC_VAL_ARRAY && value->elem_type == OMC_ELEM_URATIONAL
        && value->count != 0U) {
        omc_u32 i;

        bytes = omc_arena_view(arena, value->u.ref);
        if (bytes.data == (const omc_u8*)0 || bytes.size < sizeof(omc_urational)) {
            return 0;
        }
        for (i = 0U; i < value->count; ++i) {
            const omc_u8* ptr;
            if (bytes.size < (omc_size)((i + 1U) * sizeof(omc_urational))) {
                break;
            }
            ptr = bytes.data
                  + (omc_size)(i * (omc_u32)sizeof(omc_urational));
            memcpy(out_r, ptr, sizeof(omc_urational));
            if (out_r->denom != 0U) {
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

static int
omc_xmp_dump_first_srational(const omc_val* value, const omc_arena* arena,
                             omc_srational* out_r)
{
    omc_const_bytes bytes;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0
        || out_r == (omc_srational*)0) {
        return 0;
    }

    if (value->kind == OMC_VAL_SCALAR && value->elem_type == OMC_ELEM_SRATIONAL) {
        if (value->u.sr.denom == 0) {
            return 0;
        }
        *out_r = value->u.sr;
        return 1;
    }
    if (value->kind == OMC_VAL_ARRAY && value->elem_type == OMC_ELEM_SRATIONAL
        && value->count != 0U) {
        omc_u32 i;

        bytes = omc_arena_view(arena, value->u.ref);
        if (bytes.data == (const omc_u8*)0 || bytes.size < sizeof(omc_srational)) {
            return 0;
        }
        for (i = 0U; i < value->count; ++i) {
            const omc_u8* ptr;
            if (bytes.size < (omc_size)((i + 1U) * sizeof(omc_srational))) {
                break;
            }
            ptr = bytes.data
                  + (omc_size)(i * (omc_u32)sizeof(omc_srational));
            memcpy(out_r, ptr, sizeof(omc_srational));
            if (out_r->denom != 0) {
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

static int
omc_xmp_dump_urational_at(const omc_val* value, const omc_arena* arena,
                          omc_u32 index, omc_urational* out_r)
{
    omc_const_bytes bytes;
    const omc_u8* ptr;

    if (value == (const omc_val*)0 || arena == (const omc_arena*)0
        || out_r == (omc_urational*)0) {
        return 0;
    }
    if (value->kind != OMC_VAL_ARRAY || value->elem_type != OMC_ELEM_URATIONAL
        || index >= value->count) {
        return 0;
    }

    bytes = omc_arena_view(arena, value->u.ref);
    if (bytes.data == (const omc_u8*)0
        || bytes.size < (omc_size)((index + 1U) * sizeof(omc_urational))) {
        return 0;
    }
    ptr = bytes.data + (omc_size)(index * (omc_u32)sizeof(omc_urational));
    memcpy(out_r, ptr, sizeof(omc_urational));
    return out_r->denom != 0U;
}

static int
omc_xmp_dump_coord_component_double(const omc_val* value,
                                    const omc_arena* arena, omc_u32 index,
                                    double* out_value)
{
    omc_urational ur;
    omc_const_bytes bytes;

    if (out_value == (double*)0 || value == (const omc_val*)0
        || arena == (const omc_arena*)0) {
        return 0;
    }
    if (value->kind != OMC_VAL_ARRAY || index >= value->count) {
        return 0;
    }

    if (value->elem_type == OMC_ELEM_URATIONAL) {
        if (!omc_xmp_dump_urational_at(value, arena, index, &ur)) {
            return 0;
        }
        return omc_xmp_dump_rational_to_double(ur, out_value);
    }
    if (value->elem_type == OMC_ELEM_SRATIONAL) {
        omc_srational sr;
        const omc_u8* ptr;

        bytes = omc_arena_view(arena, value->u.ref);
        if (bytes.data == (const omc_u8*)0
            || bytes.size < (omc_size)((index + 1U) * sizeof(omc_srational))) {
            return 0;
        }
        ptr = bytes.data + (omc_size)(index * (omc_u32)sizeof(omc_srational));
        memcpy(&sr, ptr, sizeof(omc_srational));
        if (sr.denom == 0) {
            return 0;
        }
        *out_value = ((double)sr.numer) / ((double)sr.denom);
        return 1;
    }

    return 0;
}

static int
omc_xmp_dump_rational_to_double(omc_urational r, double* out_value)
{
    if (out_value == (double*)0 || r.denom == 0U) {
        return 0;
    }
    *out_value = ((double)r.numer) / ((double)r.denom);
    return 1;
}

static void
omc_xmp_dump_write_c_string(omc_xmp_dump_writer* writer, const char* text)
{
    if (text == (const char*)0) {
        return;
    }
    omc_xmp_dump_write_bytes(writer, text, strlen(text));
}

static void
omc_xmp_dump_write_text_element(omc_xmp_dump_writer* writer,
                                const char* name, const omc_u8* bytes,
                                omc_size size)
{
    omc_xmp_dump_write_byte(writer, '<');
    omc_xmp_dump_write_c_string(writer, name);
    omc_xmp_dump_write_byte(writer, '>');
    omc_xmp_dump_write_xml_escaped(writer, bytes, size, 0);
    omc_xmp_dump_write_bytes(writer, "</", 2U);
    omc_xmp_dump_write_c_string(writer, name);
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_write_text_element_lit(omc_xmp_dump_writer* writer,
                                    const char* name, const char* value)
{
    omc_xmp_dump_write_text_element(writer, name, (const omc_u8*)value,
                                    strlen(value));
}

static void
omc_xmp_dump_write_u64_element(omc_xmp_dump_writer* writer, const char* name,
                               omc_u64 value)
{
    omc_xmp_dump_write_byte(writer, '<');
    omc_xmp_dump_write_c_string(writer, name);
    omc_xmp_dump_write_byte(writer, '>');
    omc_xmp_dump_write_u64_decimal(writer, value);
    omc_xmp_dump_write_bytes(writer, "</", 2U);
    omc_xmp_dump_write_c_string(writer, name);
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_write_u16_hex4(omc_xmp_dump_writer* writer, omc_u16 value)
{
    omc_xmp_dump_write_hex_byte(writer, (omc_u8)((value >> 8) & 0xFFU));
    omc_xmp_dump_write_hex_byte(writer, (omc_u8)(value & 0xFFU));
}

static void
omc_xmp_dump_lossless_write_key_id(omc_xmp_dump_writer* writer,
                                   const omc_store* store,
                                   const omc_entry* entry)
{
    omc_const_bytes bytes;

    switch (entry->key.kind) {
    case OMC_KEY_EXIF_TAG:
        omc_xmp_dump_write_bytes(writer, "exif:", 5U);
        bytes = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        omc_xmp_dump_write_bytes(writer, ":0x", 3U);
        omc_xmp_dump_write_u16_hex4(writer, entry->key.u.exif_tag.tag);
        break;
    case OMC_KEY_EXR_ATTR:
        omc_xmp_dump_write_bytes(writer, "exr:part:", 9U);
        omc_xmp_dump_write_u64_decimal(writer,
                                       (omc_u64)entry->key.u.exr_attr.part_index);
        omc_xmp_dump_write_byte(writer, ':');
        bytes = omc_arena_view(&store->arena, entry->key.u.exr_attr.name);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        break;
    case OMC_KEY_XMP_PROPERTY:
        omc_xmp_dump_write_bytes(writer, "xmp:", 4U);
        bytes = omc_arena_view(&store->arena, entry->key.u.xmp_property.property_path);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        break;
    case OMC_KEY_PNG_TEXT:
        omc_xmp_dump_write_bytes(writer, "png:", 4U);
        bytes = omc_arena_view(&store->arena, entry->key.u.png_text.keyword);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        omc_xmp_dump_write_byte(writer, ':');
        bytes = omc_arena_view(&store->arena, entry->key.u.png_text.field);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        break;
    case OMC_KEY_COMMENT:
        omc_xmp_dump_write_bytes(writer, "comment", 7U);
        break;
    default:
        omc_xmp_dump_write_bytes(writer, "kind:", 5U);
        omc_xmp_dump_write_u64_decimal(writer, (omc_u64)entry->key.kind);
        break;
    }
}

static int
omc_xmp_dump_value_raw_size(const omc_store* store, const omc_val* value,
                            omc_u64* out_size)
{
    omc_const_bytes bytes;
    omc_size elem_size;

    if (store == (const omc_store*)0 || value == (const omc_val*)0
        || out_size == (omc_u64*)0) {
        return 0;
    }

    switch (value->kind) {
    case OMC_VAL_EMPTY:
        *out_size = 0U;
        return 1;
    case OMC_VAL_SCALAR:
        switch (value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_I8: *out_size = 1U; return 1;
        case OMC_ELEM_U16:
        case OMC_ELEM_I16: *out_size = 2U; return 1;
        case OMC_ELEM_U32:
        case OMC_ELEM_I32:
        case OMC_ELEM_F32_BITS: *out_size = 4U; return 1;
        case OMC_ELEM_U64:
        case OMC_ELEM_I64:
        case OMC_ELEM_F64_BITS:
        case OMC_ELEM_URATIONAL:
        case OMC_ELEM_SRATIONAL: *out_size = 8U; return 1;
        default: return 0;
        }
    case OMC_VAL_ARRAY:
        elem_size = omc_xmp_dump_elem_size(value->elem_type);
        if (elem_size == 0U) {
            return 0;
        }
        if (value->count > 0U
            && elem_size > (((omc_u64)~(omc_u64)0) / (omc_u64)value->count)) {
            return 0;
        }
        *out_size = (omc_u64)elem_size * (omc_u64)value->count;
        return 1;
    case OMC_VAL_BYTES:
    case OMC_VAL_TEXT:
        bytes = omc_arena_view(&store->arena, value->u.ref);
        if (bytes.data == (const omc_u8*)0) {
            return 0;
        }
        *out_size = (omc_u64)bytes.size;
        return 1;
    default: return 0;
    }
}

static void
omc_xmp_dump_base64_scalar_value(omc_xmp_dump_writer* writer,
                                 omc_xmp_dump_base64_state* state,
                                 const omc_val* value)
{
    omc_u8 tmp[8];

    switch (value->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_I8:
        tmp[0] = (omc_u8)value->u.u64;
        omc_xmp_dump_base64_feed(writer, state, tmp, 1U);
        break;
    case OMC_ELEM_U16:
    case OMC_ELEM_I16:
        tmp[0] = (omc_u8)(value->u.u64 & 0xFFU);
        tmp[1] = (omc_u8)((value->u.u64 >> 8) & 0xFFU);
        omc_xmp_dump_base64_feed(writer, state, tmp, 2U);
        break;
    case OMC_ELEM_U32:
    case OMC_ELEM_I32:
    case OMC_ELEM_F32_BITS:
        tmp[0] = (omc_u8)(value->u.u64 & 0xFFU);
        tmp[1] = (omc_u8)((value->u.u64 >> 8) & 0xFFU);
        tmp[2] = (omc_u8)((value->u.u64 >> 16) & 0xFFU);
        tmp[3] = (omc_u8)((value->u.u64 >> 24) & 0xFFU);
        omc_xmp_dump_base64_feed(writer, state, tmp, 4U);
        break;
    case OMC_ELEM_U64:
    case OMC_ELEM_I64:
    case OMC_ELEM_F64_BITS: {
        omc_u32 shift;
        shift = 0U;
        while (shift < 64U) {
            tmp[shift / 8U] = (omc_u8)((value->u.u64 >> shift) & (omc_u64)0xFFU);
            shift += 8U;
        }
        omc_xmp_dump_base64_feed(writer, state, tmp, 8U);
        break;
    }
    case OMC_ELEM_URATIONAL:
        tmp[0] = (omc_u8)(value->u.ur.numer & 0xFFU);
        tmp[1] = (omc_u8)((value->u.ur.numer >> 8) & 0xFFU);
        tmp[2] = (omc_u8)((value->u.ur.numer >> 16) & 0xFFU);
        tmp[3] = (omc_u8)((value->u.ur.numer >> 24) & 0xFFU);
        tmp[4] = (omc_u8)(value->u.ur.denom & 0xFFU);
        tmp[5] = (omc_u8)((value->u.ur.denom >> 8) & 0xFFU);
        tmp[6] = (omc_u8)((value->u.ur.denom >> 16) & 0xFFU);
        tmp[7] = (omc_u8)((value->u.ur.denom >> 24) & 0xFFU);
        omc_xmp_dump_base64_feed(writer, state, tmp, 8U);
        break;
    case OMC_ELEM_SRATIONAL:
        tmp[0] = (omc_u8)((omc_u32)value->u.sr.numer & 0xFFU);
        tmp[1] = (omc_u8)(((omc_u32)value->u.sr.numer >> 8) & 0xFFU);
        tmp[2] = (omc_u8)(((omc_u32)value->u.sr.numer >> 16) & 0xFFU);
        tmp[3] = (omc_u8)(((omc_u32)value->u.sr.numer >> 24) & 0xFFU);
        tmp[4] = (omc_u8)((omc_u32)value->u.sr.denom & 0xFFU);
        tmp[5] = (omc_u8)(((omc_u32)value->u.sr.denom >> 8) & 0xFFU);
        tmp[6] = (omc_u8)(((omc_u32)value->u.sr.denom >> 16) & 0xFFU);
        tmp[7] = (omc_u8)(((omc_u32)value->u.sr.denom >> 24) & 0xFFU);
        omc_xmp_dump_base64_feed(writer, state, tmp, 8U);
        break;
    default: break;
    }
}

static int
omc_xmp_dump_write_value_base64(omc_xmp_dump_writer* writer,
                                const omc_store* store, const omc_val* value)
{
    omc_xmp_dump_base64_state state;
    omc_const_bytes bytes;

    state.tail_size = 0U;
    if (value == (const omc_val*)0) {
        return 0;
    }

    switch (value->kind) {
    case OMC_VAL_EMPTY:
        break;
    case OMC_VAL_SCALAR:
        omc_xmp_dump_base64_scalar_value(writer, &state, value);
        break;
    case OMC_VAL_ARRAY:
    case OMC_VAL_BYTES:
    case OMC_VAL_TEXT:
        bytes = omc_arena_view(&store->arena, value->u.ref);
        if (bytes.data == (const omc_u8*)0) {
            return 0;
        }
        omc_xmp_dump_base64_feed(writer, &state, bytes.data, bytes.size);
        break;
    default: return 0;
    }

    omc_xmp_dump_base64_finish(writer, &state);
    return 1;
}

static void
omc_xmp_dump_lossless_emit_entry(omc_xmp_dump_writer* writer,
                                 const omc_store* store,
                                 const omc_entry* entry,
                                 const omc_xmp_lossless_opts* opts)
{
    omc_u64 value_bytes;
    char name_buf[128];
    omc_size name_len;

    omc_xmp_dump_write_bytes(writer, "<rdf:li rdf:parseType=\"Resource\">", 33U);

    omc_xmp_dump_write_bytes(writer, "<omd:keyId>", 11U);
    omc_xmp_dump_lossless_write_key_id(writer, store, entry);
    omc_xmp_dump_write_bytes(writer, "</omd:keyId>", 12U);

    if (entry->key.kind == OMC_KEY_EXIF_TAG && opts->include_names != 0) {
        omc_const_bytes ifd_bytes;

        ifd_bytes = omc_arena_view(&store->arena, entry->key.u.exif_tag.ifd);
        if (ifd_bytes.data != (const omc_u8*)0
            && ifd_bytes.size < sizeof(name_buf)) {
        memcpy(name_buf, ifd_bytes.data, ifd_bytes.size);
        name_buf[ifd_bytes.size] = '\0';
        if (omc_exif_tag_name(name_buf, entry->key.u.exif_tag.tag, name_buf,
                              sizeof(name_buf), &name_len)
            == OMC_EXIF_NAME_OK) {
            omc_xmp_dump_write_text_element(writer, "omd:tagName",
                                            (const omc_u8*)name_buf,
                                            name_len);
        }
        }
    }

    omc_xmp_dump_write_text_element_lit(writer, "omd:valueKind",
                                        omc_xmp_dump_value_kind_name(
                                            entry->value.kind));
    omc_xmp_dump_write_text_element_lit(writer, "omd:elemType",
                                        omc_xmp_dump_elem_type_name(
                                            entry->value.elem_type));
    omc_xmp_dump_write_text_element_lit(writer, "omd:textEncoding",
                                        omc_xmp_dump_text_encoding_name(
                                            entry->value.text_encoding));
    omc_xmp_dump_write_u64_element(writer, "omd:count",
                                   (omc_u64)entry->value.count);

    omc_xmp_dump_write_bytes(writer, "<omd:valueBase64>", 16U);
    (void)omc_xmp_dump_write_value_base64(writer, store, &entry->value);
    omc_xmp_dump_write_bytes(writer, "</omd:valueBase64>", 17U);

    value_bytes = 0U;
    if (omc_xmp_dump_value_raw_size(store, &entry->value, &value_bytes)) {
        omc_xmp_dump_write_u64_element(writer, "omd:valueBytes", value_bytes);
    }
    if (entry->value.kind == OMC_VAL_BYTES || entry->value.kind == OMC_VAL_TEXT) {
        omc_xmp_dump_write_text_element_lit(writer, "omd:valueEncoding", "raw");
    } else if (entry->value.kind == OMC_VAL_SCALAR
               || entry->value.kind == OMC_VAL_ARRAY) {
        omc_xmp_dump_write_text_element_lit(writer, "omd:valueEncoding", "le");
    }

    if (opts->include_origin != 0) {
        omc_xmp_dump_write_u64_element(writer, "omd:originBlock",
                                       (omc_u64)entry->origin.block);
        omc_xmp_dump_write_u64_element(writer, "omd:originOrder",
                                       (omc_u64)entry->origin.order_in_block);
    }
    if (opts->include_wire != 0) {
        omc_xmp_dump_write_text_element_lit(writer, "omd:wireFamily",
                                            omc_xmp_dump_wire_family_name(
                                                entry->origin.wire_type.family));
        omc_xmp_dump_write_u64_element(writer, "omd:wireTypeCode",
                                       (omc_u64)entry->origin.wire_type.code);
        omc_xmp_dump_write_u64_element(writer, "omd:wireCount",
                                       (omc_u64)entry->origin.wire_count);
        if (entry->key.kind == OMC_KEY_EXR_ATTR) {
            omc_const_bytes type_name;
            type_name = omc_arena_view(&store->arena, entry->origin.wire_type_name);
            if (type_name.data != (const omc_u8*)0 && type_name.size != 0U) {
                omc_xmp_dump_write_text_element(writer, "omd:exrTypeName",
                                                type_name.data, type_name.size);
            }
        }
    }
    if (opts->include_flags != 0) {
        omc_xmp_dump_write_u64_element(writer, "omd:flags",
                                       (omc_u64)entry->flags);
    }

    omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
}

static int
omc_xmp_dump_write_gps_coord(omc_xmp_dump_writer* writer,
                             const omc_xmp_dump_property* prop,
                             omc_u16 ref_tag)
{
    const omc_entry* ref_entry;
    double degrees;
    double minutes;
    double seconds;
    double minutes_total;
    char ref_char;
    char deg_buf[32];
    char min_buf[32];

    if (!omc_xmp_dump_find_exif_entry(prop->store, prop->exif_ifd, ref_tag,
                                      &ref_entry)
        || !omc_xmp_dump_entry_text_char(ref_entry, prop->arena, &ref_char)
        || !omc_xmp_dump_coord_component_double(prop->value, prop->arena, 0U,
                                                &degrees)
        || !omc_xmp_dump_coord_component_double(prop->value, prop->arena, 1U,
                                                &minutes)
        || !omc_xmp_dump_coord_component_double(prop->value, prop->arena, 2U,
                                                &seconds)
        || !omc_xmp_dump_format_double_trimmed(degrees, 8, deg_buf,
                                               sizeof(deg_buf))) {
        return 0;
    }

    minutes_total = minutes + (seconds / 60.0);
    if (!omc_xmp_dump_format_double_trimmed(minutes_total, 8, min_buf,
                                            sizeof(min_buf))) {
        return 0;
    }
    omc_xmp_dump_write_c_string(writer, deg_buf);
    omc_xmp_dump_write_byte(writer, ',');
    omc_xmp_dump_write_c_string(writer, min_buf);
    omc_xmp_dump_write_byte(writer, ref_char);
    return 1;
}

static int
omc_xmp_dump_apex_to_fnumber_text(const omc_val* value,
                                  const omc_arena* arena, char* buf,
                                  omc_size buf_size)
{
    omc_urational ur;
    omc_u32 half_steps;
    double fnum;
    omc_u32 i;

    if (buf == (char*)0 || buf_size == 0U
        || !omc_xmp_dump_first_urational(value, arena, &ur)
        || ur.denom == 0U || (ur.numer % ur.denom) != 0U) {
        return 0;
    }

    half_steps = ur.numer / ur.denom;
    fnum = 1.0;
    for (i = 0U; i < (half_steps / 2U); ++i) {
        fnum *= 2.0;
    }
    if ((half_steps & 1U) != 0U) {
        fnum *= 1.4142135623730951;
    }
    if (fnum > 1024.0) {
        return 0;
    }

    sprintf(buf, "%.1f", fnum);
    return 1;
}

static int
omc_xmp_dump_write_gps_time(omc_xmp_dump_writer* writer,
                            const omc_xmp_dump_property* prop)
{
    const omc_entry* date_entry;
    omc_const_bytes date_bytes;
    omc_urational hh;
    omc_urational mm;
    omc_urational ss;
    int hour;
    int minute;
    int second;
    char buf[32];

    if (!omc_xmp_dump_find_exif_entry(prop->store, prop->exif_ifd, 0x001DU,
                                      &date_entry)
        || date_entry->value.kind != OMC_VAL_TEXT) {
        return 0;
    }
    date_bytes = omc_arena_view(prop->arena, date_entry->value.u.ref);
    if (date_bytes.data == (const omc_u8*)0 || date_bytes.size < 10U) {
        return 0;
    }
    if (!omc_xmp_dump_urational_at(prop->value, prop->arena, 0U, &hh)
        || !omc_xmp_dump_urational_at(prop->value, prop->arena, 1U, &mm)
        || !omc_xmp_dump_urational_at(prop->value, prop->arena, 2U, &ss)) {
        return 0;
    }
    if (hh.denom == 0U || mm.denom == 0U || ss.denom == 0U
        || (hh.numer % hh.denom) != 0U || (mm.numer % mm.denom) != 0U
        || (ss.numer % ss.denom) != 0U) {
        return 0;
    }
    hour = (int)(hh.numer / hh.denom);
    minute = (int)(mm.numer / mm.denom);
    second = (int)(ss.numer / ss.denom);
    sprintf(buf, "%.4s-%.2s-%.2sT%02d:%02d:%02dZ",
            (const char*)date_bytes.data, (const char*)(date_bytes.data + 5U),
            (const char*)(date_bytes.data + 8U), hour, minute, second);
    omc_xmp_dump_write_c_string(writer, buf);
    return 1;
}

static int
omc_xmp_dump_write_lens_spec_seq(omc_xmp_dump_writer* writer,
                                 const omc_xmp_dump_property* prop)
{
    omc_u32 i;
    omc_urational r;
    double value;
    char buf[64];
    int had_item;

    had_item = 0;
    omc_xmp_dump_write_bytes(writer, "<rdf:Seq>", 9U);
    for (i = 0U; i < prop->value->count; ++i) {
        if (!omc_xmp_dump_urational_at(prop->value, prop->arena, i, &r)
            || !omc_xmp_dump_rational_to_double(r, &value)
            || !omc_xmp_dump_format_double_trimmed(value, 8, buf,
                                                   sizeof(buf))) {
            continue;
        }
        had_item = 1;
        omc_xmp_dump_write_bytes(writer, "<rdf:li>", 8U);
        omc_xmp_dump_write_c_string(writer, buf);
        omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
    }
    omc_xmp_dump_write_bytes(writer, "</rdf:Seq>", 10U);
    return had_item;
}

static int
omc_xmp_dump_property_has_output(const omc_xmp_dump_property* prop)
{
    omc_urational ur;
    omc_srational sr;
    omc_const_bytes bytes;

    if (prop == (const omc_xmp_dump_property*)0) {
        return 0;
    }

    switch (prop->value_mode) {
    case OMC_XMP_DUMP_VALUE_EXIF_FNUMBER:
        return omc_xmp_dump_first_urational(prop->value, prop->arena, &ur);
    case OMC_XMP_DUMP_VALUE_EXIF_APEX_FNUMBER:
        {
            char buf[32];
            return omc_xmp_dump_apex_to_fnumber_text(prop->value, prop->arena,
                                                     buf, sizeof(buf));
        }
    case OMC_XMP_DUMP_VALUE_EXIF_SRATIONAL_DECIMAL:
        return omc_xmp_dump_first_srational(prop->value, prop->arena, &sr);
    case OMC_XMP_DUMP_VALUE_EXIF_FOCAL_LENGTH:
        return omc_xmp_dump_first_urational(prop->value, prop->arena, &ur);
    case OMC_XMP_DUMP_VALUE_EXIF_SHUTTER_SPEED:
        return omc_xmp_dump_first_srational(prop->value, prop->arena, &sr)
               && sr.denom == 1 && sr.numer >= 0 && sr.numer < 31;
    case OMC_XMP_DUMP_VALUE_EXIF_LENS_SPEC:
        return omc_xmp_dump_write_lens_spec_seq(
                   (omc_xmp_dump_writer*)0, prop)
               != 0;
    case OMC_XMP_DUMP_VALUE_EXIF_GPS_VERSION:
        if (prop->value->kind != OMC_VAL_ARRAY
            || prop->value->elem_type != OMC_ELEM_U8) {
            return 0;
        }
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        return bytes.data != (const omc_u8*)0 && bytes.size != 0U;
    case OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD:
        if (prop->exif_tag == 0x0002U) {
            return omc_xmp_dump_write_gps_coord(
                       (omc_xmp_dump_writer*)0, prop, 0x0001U)
                   != 0;
        }
        if (prop->exif_tag == 0x0004U) {
            return omc_xmp_dump_write_gps_coord(
                       (omc_xmp_dump_writer*)0, prop, 0x0003U)
                   != 0;
        }
        if (prop->exif_tag == 0x0014U) {
            return omc_xmp_dump_write_gps_coord(
                       (omc_xmp_dump_writer*)0, prop, 0x0013U)
                   != 0;
        }
        if (prop->exif_tag == 0x0016U) {
            return omc_xmp_dump_write_gps_coord(
                       (omc_xmp_dump_writer*)0, prop, 0x0015U)
                   != 0;
        }
        return 0;
    case OMC_XMP_DUMP_VALUE_EXIF_GPS_TIME:
        return omc_xmp_dump_write_gps_time((omc_xmp_dump_writer*)0, prop) != 0;
    case OMC_XMP_DUMP_VALUE_EXIF_GPS_ALTITUDE:
    case OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL:
        return omc_xmp_dump_first_urational(prop->value, prop->arena, &ur);
    default: return 1;
    }
}

static void
omc_xmp_dump_write_value(omc_xmp_dump_writer* writer,
                         const omc_xmp_dump_property* prop)
{
    omc_const_bytes bytes;
    char date_buf[32];
    omc_size date_size;
    omc_urational ur;
    omc_srational sr;
    char buf[64];
    const char* override_text;
    double d;

    if (prop->value_mode == OMC_XMP_DUMP_VALUE_BYTES_TEXT) {
        if (prop->value->kind == OMC_VAL_BYTES || prop->value->kind == OMC_VAL_TEXT
            || prop->value->kind == OMC_VAL_ARRAY) {
            bytes = omc_arena_view(prop->arena, prop->value->u.ref);
            if (bytes.data != (const omc_u8*)0) {
                omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
            }
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_DATE) {
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        if (bytes.data != (const omc_u8*)0
            && omc_xmp_dump_normalize_exif_datetime(bytes.data, bytes.size,
                                                    date_buf, sizeof(date_buf),
                                                    &date_size)) {
            omc_xmp_dump_write_xml_escaped(writer, (const omc_u8*)date_buf,
                                           date_size, 0);
            return;
        }
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_XP_UTF16LE) {
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_utf16le_xml_escaped(writer, bytes.data,
                                                   bytes.size);
        }
        return;
    }
    override_text = (const char*)0;
    if (omc_xmp_dump_portable_enum_text_override(prop, &override_text)
        || omc_xmp_dump_portable_gps_ref_text_override(prop, &override_text)) {
        omc_xmp_dump_write_c_string(writer, override_text);
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_FOCAL_LENGTH) {
        if (omc_xmp_dump_first_urational(prop->value, prop->arena, &ur)
            && omc_xmp_dump_rational_to_double(ur, &d)) {
            sprintf(buf, "%.1f", d);
            omc_xmp_dump_write_c_string(writer, buf);
            omc_xmp_dump_write_bytes(writer, " mm", 3U);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_FNUMBER) {
        if (omc_xmp_dump_first_urational(prop->value, prop->arena, &ur)
            && omc_xmp_dump_rational_to_double(ur, &d)) {
            sprintf(buf, "%.1f", d);
            omc_xmp_dump_write_c_string(writer, buf);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_APEX_FNUMBER) {
        if (omc_xmp_dump_apex_to_fnumber_text(prop->value, prop->arena, buf,
                                              sizeof(buf))) {
            omc_xmp_dump_write_c_string(writer, buf);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_SRATIONAL_DECIMAL) {
        if (omc_xmp_dump_first_srational(prop->value, prop->arena, &sr)) {
            d = ((double)sr.numer) / ((double)sr.denom);
            if (omc_xmp_dump_format_double_trimmed(d, 15, buf, sizeof(buf))) {
                omc_xmp_dump_write_c_string(writer, buf);
            }
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_SHUTTER_SPEED) {
        if (omc_xmp_dump_first_srational(prop->value, prop->arena, &sr)
            && sr.denom == 1 && sr.numer >= 0 && sr.numer < 31) {
            omc_xmp_dump_write_bytes(writer, "1/", 2U);
            omc_xmp_dump_write_u32_decimal(writer, (omc_u32)1U << sr.numer);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_LENS_SPEC) {
        (void)omc_xmp_dump_write_lens_spec_seq(writer, prop);
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_GPS_VERSION) {
        if (prop->value->kind == OMC_VAL_ARRAY && prop->value->elem_type == OMC_ELEM_U8) {
            bytes = omc_arena_view(prop->arena, prop->value->u.ref);
            if (bytes.data != (const omc_u8*)0 && bytes.size != 0U) {
                omc_xmp_dump_write_u64_decimal(writer, bytes.data[0]);
            }
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_GPS_COORD) {
        if (prop->exif_tag == 0x0002U) {
            (void)omc_xmp_dump_write_gps_coord(writer, prop, 0x0001U);
        } else if (prop->exif_tag == 0x0004U) {
            (void)omc_xmp_dump_write_gps_coord(writer, prop, 0x0003U);
        } else if (prop->exif_tag == 0x0014U) {
            (void)omc_xmp_dump_write_gps_coord(writer, prop, 0x0013U);
        } else if (prop->exif_tag == 0x0016U) {
            (void)omc_xmp_dump_write_gps_coord(writer, prop, 0x0015U);
        }
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_GPS_TIME) {
        (void)omc_xmp_dump_write_gps_time(writer, prop);
        return;
    }
    if (prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_GPS_ALTITUDE
        || prop->value_mode == OMC_XMP_DUMP_VALUE_EXIF_GPS_URATIONAL) {
        if (omc_xmp_dump_first_urational(prop->value, prop->arena, &ur)
            && omc_xmp_dump_rational_to_double(ur, &d)
            && omc_xmp_dump_format_double_trimmed(d, 8, buf, sizeof(buf))) {
            omc_xmp_dump_write_c_string(writer, buf);
        }
        return;
    }

    switch (prop->value->kind) {
    case OMC_VAL_SCALAR:
        switch (prop->value->elem_type) {
        case OMC_ELEM_U8:
        case OMC_ELEM_U16:
        case OMC_ELEM_U32:
        case OMC_ELEM_U64:
            omc_xmp_dump_write_u64_decimal(writer, prop->value->u.u64);
            return;
        case OMC_ELEM_I8:
        case OMC_ELEM_I16:
        case OMC_ELEM_I32:
        case OMC_ELEM_I64:
            omc_xmp_dump_write_i64_decimal(writer, prop->value->u.i64);
            return;
        case OMC_ELEM_URATIONAL:
            omc_xmp_dump_write_u32_decimal(writer, prop->value->u.ur.numer);
            omc_xmp_dump_write_byte(writer, '/');
            omc_xmp_dump_write_u32_decimal(writer, prop->value->u.ur.denom);
            return;
        case OMC_ELEM_SRATIONAL:
            omc_xmp_dump_write_i64_decimal(writer, prop->value->u.sr.numer);
            omc_xmp_dump_write_byte(writer, '/');
            omc_xmp_dump_write_i64_decimal(writer, prop->value->u.sr.denom);
            return;
        default: return;
        }
    case OMC_VAL_TEXT:
        bytes = omc_arena_view(prop->arena, prop->value->u.ref);
        if (bytes.data != (const omc_u8*)0) {
            omc_xmp_dump_write_xml_escaped(writer, bytes.data, bytes.size, 0);
        }
        return;
    default: return;
    }
}

static void
omc_xmp_dump_write_property_open(omc_xmp_dump_writer* writer,
                                 const omc_store* store, omc_size index,
                                 const omc_xmp_sidecar_opts* opts,
                                 omc_xmp_dump_pass pass,
                                 const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_write_byte(writer, '<');
    omc_xmp_dump_write_prefix(writer, store, index, opts, pass,
                              prop->schema_ns);
    omc_xmp_dump_write_byte(writer, ':');
    omc_xmp_dump_write_xml_escaped(writer, prop->property_name.data,
                                   prop->property_name.size, 0);
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_write_property_close(omc_xmp_dump_writer* writer,
                                  const omc_store* store, omc_size index,
                                  const omc_xmp_sidecar_opts* opts,
                                  omc_xmp_dump_pass pass,
                                  const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_write_bytes(writer, "</", 2U);
    omc_xmp_dump_write_prefix(writer, store, index, opts, pass,
                              prop->schema_ns);
    omc_xmp_dump_write_byte(writer, ':');
    omc_xmp_dump_write_xml_escaped(writer, prop->property_name.data,
                                   prop->property_name.size, 0);
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_write_named_open_ex(omc_xmp_dump_writer* writer,
                                 const omc_store* store, omc_size index,
                                 const omc_xmp_sidecar_opts* opts,
                                 omc_xmp_dump_pass pass,
                                 omc_xmp_ns_view schema_ns,
                                 omc_xmp_ns_view property_name,
                                 int resource_attr,
                                 int local_ns_decl)
{
    const char* known_prefix;

    omc_xmp_dump_write_byte(writer, '<');
    omc_xmp_dump_write_prefix(writer, store, index, opts, pass, schema_ns);
    omc_xmp_dump_write_byte(writer, ':');
    omc_xmp_dump_write_xml_escaped(writer, property_name.data,
                                   property_name.size, 0);
    if (local_ns_decl
        && omc_xmp_dump_known_prefix(schema_ns, &known_prefix)) {
        omc_xmp_dump_write_bytes(writer, " xmlns:", 7U);
        omc_xmp_dump_write_bytes(writer, known_prefix, strlen(known_prefix));
        omc_xmp_dump_write_bytes(writer, "=\"", 2U);
        omc_xmp_dump_write_xml_escaped(writer, schema_ns.data,
                                       schema_ns.size, 1);
        omc_xmp_dump_write_byte(writer, '"');
    }
    if (resource_attr) {
        omc_xmp_dump_write_bytes(writer, " rdf:parseType=\"Resource\"", 25U);
    }
    omc_xmp_dump_write_byte(writer, '>');
}

static void
omc_xmp_dump_write_named_open(omc_xmp_dump_writer* writer,
                              const omc_store* store, omc_size index,
                              const omc_xmp_sidecar_opts* opts,
                              omc_xmp_dump_pass pass,
                              omc_xmp_ns_view schema_ns,
                              omc_xmp_ns_view property_name,
                              int resource_attr)
{
    omc_xmp_dump_write_named_open_ex(writer, store, index, opts, pass,
                                     schema_ns, property_name, resource_attr,
                                     0);
}

static void
omc_xmp_dump_write_named_close(omc_xmp_dump_writer* writer,
                               const omc_store* store, omc_size index,
                               const omc_xmp_sidecar_opts* opts,
                               omc_xmp_dump_pass pass,
                               omc_xmp_ns_view schema_ns,
                               omc_xmp_ns_view property_name)
{
    omc_xmp_dump_write_bytes(writer, "</", 2U);
    omc_xmp_dump_write_prefix(writer, store, index, opts, pass, schema_ns);
    omc_xmp_dump_write_byte(writer, ':');
    omc_xmp_dump_write_xml_escaped(writer, property_name.data,
                                   property_name.size, 0);
    omc_xmp_dump_write_byte(writer, '>');
}

static int
omc_xmp_dump_structured_root_matches(const omc_xmp_dump_property* item,
                                     const omc_xmp_dump_property* prop)
{
    return item->struct_seg_count >= 2U
           && omc_xmp_dump_property_keys_equal(item, prop);
}

static int
omc_xmp_dump_structured_prefix_matches(const omc_xmp_dump_property* item,
                                       const omc_xmp_dump_path_seg* prefix,
                                       omc_u32 prefix_count)
{
    omc_u32 k;

    if (item->struct_seg_count <= prefix_count) {
        return 0;
    }
    for (k = 0U; k < prefix_count; ++k) {
        if (!omc_xmp_dump_path_seg_equal(&item->struct_seg[k], &prefix[k])) {
            return 0;
        }
    }
    return 1;
}

static int
omc_xmp_dump_structured_child_group_seen(const omc_store* store,
                                         omc_size start_index,
                                         omc_size end_index,
                                         const omc_xmp_sidecar_opts* opts,
                                         omc_xmp_dump_pass pass,
                                         const omc_xmp_dump_property* prop,
                                         const omc_xmp_dump_path_seg* prefix,
                                         omc_u32 prefix_count,
                                         const omc_xmp_dump_path_seg* child)
{
    omc_size j;

    for (j = start_index; j < end_index; ++j) {
        omc_xmp_dump_property prior;

        if (!omc_xmp_dump_extract_pass_property(store, j, opts, pass, &prior)) {
            continue;
        }
        if (!omc_xmp_dump_structured_root_matches(&prior, prop)
            || !omc_xmp_dump_structured_prefix_matches(&prior, prefix,
                                                       prefix_count)
            || !omc_xmp_dump_path_seg_name_equal(
                   &prior.struct_seg[prefix_count], child)) {
            continue;
        }
        return 1;
    }

    return 0;
}

static void
omc_xmp_dump_emit_structured_children(omc_xmp_dump_writer* writer,
                                      const omc_store* store, omc_size index,
                                      const omc_xmp_sidecar_opts* opts,
                                      omc_xmp_dump_pass pass,
                                      const omc_xmp_dump_property* prop,
                                      const omc_xmp_dump_path_seg* prefix,
                                      omc_u32 prefix_count)
{
    omc_size i;

    for (i = index; i < store->entry_count; ++i) {
        omc_xmp_dump_property item;
        omc_xmp_dump_path_seg child;
        omc_xmp_ns_view child_schema;
        int child_local_ns_decl;
        int has_deeper;
        int has_lang;
        int has_index;
        omc_size j;

        if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass, &item)) {
            continue;
        }
        if (!omc_xmp_dump_structured_root_matches(&item, prop)
            || !omc_xmp_dump_structured_prefix_matches(&item, prefix,
                                                       prefix_count)) {
            continue;
        }

        child = item.struct_seg[prefix_count];
        child_schema = prop->schema_ns;
        child_local_ns_decl = 0;
        if (child.prefix.size != 0U) {
            if (!omc_xmp_dump_schema_from_prefix(child.prefix, &child_schema)) {
                continue;
            }
            child_local_ns_decl = 1;
        }
        if (omc_xmp_dump_structured_child_group_seen(
                store, index, i, opts, pass, prop, prefix, prefix_count,
                &child)) {
            continue;
        }

        has_deeper = 0;
        has_lang = 0;
        has_index = 0;
        for (j = index; j < store->entry_count; ++j) {
            omc_xmp_dump_property probe;

            if (!omc_xmp_dump_extract_pass_property(store, j, opts, pass,
                                                    &probe)) {
                continue;
            }
            if (!omc_xmp_dump_structured_root_matches(&probe, prop)
                || !omc_xmp_dump_structured_prefix_matches(&probe, prefix,
                                                           prefix_count)
                || !omc_xmp_dump_path_seg_name_equal(
                       &probe.struct_seg[prefix_count], &child)) {
                continue;
            }
            if (probe.struct_seg_count > prefix_count + 1U) {
                has_deeper = 1;
            }
            if (probe.struct_seg[prefix_count].lang.size != 0U) {
                has_lang = 1;
            }
            if (probe.struct_seg[prefix_count].index != 0U) {
                has_index = 1;
            }
        }

        if (has_deeper) {
            omc_xmp_dump_path_seg next_prefix[OMC_XMP_DUMP_MAX_STRUCT_SEG];
            omc_u32 k;

            for (k = 0U; k < prefix_count; ++k) {
                next_prefix[k] = prefix[k];
            }
            next_prefix[prefix_count] = child;
            next_prefix[prefix_count].index = 0U;
            next_prefix[prefix_count].lang.data = (const omc_u8*)0;
            next_prefix[prefix_count].lang.size = 0U;

            if (has_index) {
                omc_u32 want_index;

                omc_xmp_dump_write_named_open(writer, store, index, opts, pass,
                                              child_schema, child.name, 0);
                omc_xmp_dump_write_bytes(writer, "<rdf:Seq>", 9U);
                want_index = 1U;
                for (;;) {
                    int found;
                    omc_u32 next_index;

                    found = 0;
                    next_index = 0U;
                    for (j = index; j < store->entry_count; ++j) {
                        omc_xmp_dump_property probe;

                        if (!omc_xmp_dump_extract_pass_property(
                                store, j, opts, pass, &probe)) {
                            continue;
                        }
                        if (!omc_xmp_dump_structured_root_matches(&probe, prop)
                            || !omc_xmp_dump_structured_prefix_matches(
                                   &probe, prefix, prefix_count)
                            || !omc_xmp_dump_path_seg_name_equal(
                                   &probe.struct_seg[prefix_count], &child)
                            || probe.struct_seg[prefix_count].index == 0U) {
                            continue;
                        }
                        if (probe.struct_seg[prefix_count].index
                            == want_index) {
                            next_prefix[prefix_count].index = want_index;
                            omc_xmp_dump_write_bytes(
                                writer, "<rdf:li rdf:parseType=\"Resource\">",
                                34U);
                            omc_xmp_dump_emit_structured_children(
                                writer, store, index, opts, pass, prop,
                                next_prefix, prefix_count + 1U);
                            omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
                            found = 1;
                            want_index += 1U;
                            break;
                        }
                        if (probe.struct_seg[prefix_count].index > want_index
                            && (next_index == 0U
                                || probe.struct_seg[prefix_count].index
                                       < next_index)) {
                            next_index = probe.struct_seg[prefix_count].index;
                        }
                    }
                    if (found) {
                        continue;
                    }
                    if (next_index == 0U) {
                        break;
                    }
                    want_index = next_index;
                }
                omc_xmp_dump_write_bytes(writer, "</rdf:Seq>", 10U);
                omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                               child_schema, child.name);
            } else {
                omc_xmp_dump_write_named_open_ex(writer, store, index, opts,
                                                 pass, child_schema, child.name,
                                                 1, child_local_ns_decl);
                omc_xmp_dump_emit_structured_children(writer, store, index,
                                                      opts, pass, prop,
                                                      next_prefix,
                                                      prefix_count + 1U);
                omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                               child_schema, child.name);
            }
            continue;
        }

        if (has_lang) {
            omc_xmp_dump_write_named_open_ex(writer, store, index, opts, pass,
                                             child_schema, child.name, 0,
                                             child_local_ns_decl);
            omc_xmp_dump_write_bytes(writer, "<rdf:Alt>", 9U);
            for (j = index; j < store->entry_count; ++j) {
                omc_xmp_dump_property probe;

                if (!omc_xmp_dump_extract_pass_property(store, j, opts, pass,
                                                        &probe)) {
                    continue;
                }
                if (!omc_xmp_dump_structured_root_matches(&probe, prop)
                    || !omc_xmp_dump_structured_prefix_matches(
                           &probe, prefix, prefix_count)
                    || probe.struct_seg_count != prefix_count + 1U
                    || !omc_xmp_dump_path_seg_name_equal(
                           &probe.struct_seg[prefix_count], &child)
                    || probe.struct_seg[prefix_count].lang.size == 0U) {
                    continue;
                }
                omc_xmp_dump_write_bytes(writer, "<rdf:li xml:lang=\"", 18U);
                omc_xmp_dump_write_xml_escaped(
                    writer, probe.struct_seg[prefix_count].lang.data,
                    probe.struct_seg[prefix_count].lang.size, 1);
                omc_xmp_dump_write_bytes(writer, "\">", 2U);
                omc_xmp_dump_write_value(writer, &probe);
                omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
            }
            omc_xmp_dump_write_bytes(writer, "</rdf:Alt>", 10U);
            omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                           child_schema, child.name);
            continue;
        }

        if (has_index) {
            omc_u32 want_index;
            omc_xmp_dump_container_kind child_kind;

            child_kind = omc_xmp_dump_existing_container_kind(child_schema,
                                                              child.name);
            if (omc_xmp_dump_view_equal_lit(child_schema, k_xmp_ns_xap)
                && omc_xmp_dump_view_equal_lit(child.name, "Identifier")) {
                child_kind = OMC_XMP_DUMP_CONTAINER_BAG;
            } else if (omc_xmp_dump_view_equal_lit(child_schema,
                                                   k_xmp_ns_iptc4xmp_ext)
                       && (omc_xmp_dump_view_equal_lit(child.name,
                                                       "LocationId")
                           || omc_xmp_dump_view_equal_lit(child.name, "Role")
                           || omc_xmp_dump_view_equal_lit(child.name,
                                                          "PersonId"))) {
                child_kind = OMC_XMP_DUMP_CONTAINER_BAG;
            }

            omc_xmp_dump_write_named_open_ex(writer, store, index, opts, pass,
                                             child_schema, child.name, 0,
                                             child_local_ns_decl);
            if (child_kind == OMC_XMP_DUMP_CONTAINER_BAG) {
                omc_xmp_dump_write_bytes(writer, "<rdf:Bag>", 9U);
            } else {
                omc_xmp_dump_write_bytes(writer, "<rdf:Seq>", 9U);
            }
            want_index = 1U;
            for (;;) {
                int found;
                omc_u32 next_index;

                found = 0;
                next_index = 0U;
                for (j = index; j < store->entry_count; ++j) {
                    omc_xmp_dump_property probe;

                    if (!omc_xmp_dump_extract_pass_property(store, j, opts,
                                                            pass, &probe)) {
                        continue;
                    }
                    if (!omc_xmp_dump_structured_root_matches(&probe, prop)
                        || !omc_xmp_dump_structured_prefix_matches(
                               &probe, prefix, prefix_count)
                        || probe.struct_seg_count != prefix_count + 1U
                        || !omc_xmp_dump_path_seg_name_equal(
                               &probe.struct_seg[prefix_count], &child)
                        || probe.struct_seg[prefix_count].lang.size != 0U
                        || probe.struct_seg[prefix_count].index == 0U) {
                        continue;
                    }
                    if (probe.struct_seg[prefix_count].index == want_index) {
                        omc_xmp_dump_write_bytes(writer, "<rdf:li>", 8U);
                        omc_xmp_dump_write_value(writer, &probe);
                        omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
                        found = 1;
                        want_index += 1U;
                        break;
                    }
                    if (probe.struct_seg[prefix_count].index > want_index
                        && (next_index == 0U
                            || probe.struct_seg[prefix_count].index
                                   < next_index)) {
                        next_index = probe.struct_seg[prefix_count].index;
                    }
                }
                if (found) {
                    continue;
                }
                if (next_index == 0U) {
                    break;
                }
                want_index = next_index;
            }
            if (child_kind == OMC_XMP_DUMP_CONTAINER_BAG) {
                omc_xmp_dump_write_bytes(writer, "</rdf:Bag>", 10U);
            } else {
                omc_xmp_dump_write_bytes(writer, "</rdf:Seq>", 10U);
            }
            omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                           child_schema, child.name);
            continue;
        }

        omc_xmp_dump_write_named_open_ex(writer, store, index, opts, pass,
                                         child_schema, child.name, 0,
                                         child_local_ns_decl);
        omc_xmp_dump_write_value(writer, &item);
        omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                       child_schema, child.name);
    }
}

static void
omc_xmp_dump_emit_structured_property(omc_xmp_dump_writer* writer,
                                      const omc_store* store, omc_size index,
                                      const omc_xmp_sidecar_opts* opts,
                                      omc_xmp_dump_pass pass,
                                      const omc_xmp_dump_property* prop)
{
    omc_xmp_dump_path_seg prefix[OMC_XMP_DUMP_MAX_STRUCT_SEG];
    omc_u32 k;

    if (prop->struct_seg_count == 0U) {
        return;
    }

    for (k = 0U; k < prop->struct_seg_count; ++k) {
        prefix[k] = prop->struct_seg[k];
    }

    if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_STRUCT_SEQ
        || prop->container_kind == OMC_XMP_DUMP_CONTAINER_STRUCT_BAG) {
        omc_u32 want_index;
        int is_bag;

        omc_xmp_dump_write_named_open(writer, store, index, opts, pass,
                                      prop->schema_ns, prop->property_name, 0);
        is_bag = prop->container_kind == OMC_XMP_DUMP_CONTAINER_STRUCT_BAG;
        if (is_bag) {
            omc_xmp_dump_write_bytes(writer, "<rdf:Bag>", 9U);
        } else {
            omc_xmp_dump_write_bytes(writer, "<rdf:Seq>", 9U);
        }
        want_index = 1U;
        for (;;) {
            int found;
            omc_u32 next_index;
            omc_size i;

            found = 0;
            next_index = 0U;
            for (i = index; i < store->entry_count; ++i) {
                omc_xmp_dump_property item;

                if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                        &item)) {
                    continue;
                }
                if (!omc_xmp_dump_structured_root_matches(&item, prop)
                    || item.struct_seg[0].index == 0U) {
                    continue;
                }
                if (item.struct_seg[0].index == want_index) {
                    prefix[0] = item.struct_seg[0];
                    omc_xmp_dump_write_bytes(
                        writer, "<rdf:li rdf:parseType=\"Resource\">", 34U);
                    omc_xmp_dump_emit_structured_children(writer, store, index,
                                                          opts, pass, prop,
                                                          prefix, 1U);
                    omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
                    found = 1;
                    want_index += 1U;
                    break;
                }
                if (item.struct_seg[0].index > want_index
                    && (next_index == 0U
                        || item.struct_seg[0].index < next_index)) {
                    next_index = item.struct_seg[0].index;
                }
            }
            if (found) {
                continue;
            }
            if (next_index == 0U) {
                break;
            }
            want_index = next_index;
        }
        if (is_bag) {
            omc_xmp_dump_write_bytes(writer, "</rdf:Bag>", 10U);
        } else {
            omc_xmp_dump_write_bytes(writer, "</rdf:Seq>", 10U);
        }
        omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                       prop->schema_ns, prop->property_name);
        return;
    }

    omc_xmp_dump_write_named_open(writer, store, index, opts, pass,
                                  prop->schema_ns, prop->property_name, 1);
    omc_xmp_dump_emit_structured_children(writer, store, index, opts, pass,
                                          prop, prefix, 1U);
    omc_xmp_dump_write_named_close(writer, store, index, opts, pass,
                                   prop->schema_ns, prop->property_name);
}

static void
omc_xmp_dump_emit_group_items(omc_xmp_dump_writer* writer,
                              const omc_store* store, omc_size index,
                              const omc_xmp_sidecar_opts* opts,
                              omc_xmp_dump_pass pass,
                              const omc_xmp_dump_property* prop)
{
    omc_size i;
    omc_xmp_dump_property item;
    omc_u32 want_index;
    omc_u32 next_index;
    int found;

    if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_SEQ
        && prop->item_index != 0U) {
        want_index = 1U;
        for (;;) {
            found = 0;
            next_index = 0U;
            for (i = index; i < store->entry_count; ++i) {
                if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                        &item)) {
                    continue;
                }
                if (!omc_xmp_dump_property_keys_equal(&item, prop)) {
                    continue;
                }
                if (item.item_index == want_index) {
                    omc_xmp_dump_write_bytes(writer, "<rdf:li>", 8U);
                    omc_xmp_dump_write_value(writer, &item);
                    omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
                    found = 1;
                    want_index += 1U;
                    break;
                }
                if (item.item_index > want_index
                    && (next_index == 0U || item.item_index < next_index)) {
                    next_index = item.item_index;
                }
            }
            if (found != 0) {
                continue;
            }
            if (next_index == 0U) {
                break;
            }
            want_index = next_index;
        }
        return;
    }

    if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_ALT) {
        omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
        omc_u32 p;

        omc_xmp_dump_fill_pass_order(opts, order);
        for (p = 0U; p < OMC_XMP_DUMP_PASS_COUNT; ++p) {
            omc_xmp_dump_pass cur_pass;
            omc_size start;

            cur_pass = order[p];
            start = 0U;
            if (cur_pass == pass) {
                start = index;
            }
            for (i = start; i < store->entry_count; ++i) {
                if (!omc_xmp_dump_extract_pass_property(store, i, opts,
                                                        cur_pass, &item)) {
                    continue;
                }
                if (!omc_xmp_dump_property_keys_equal(&item, prop)
                    || item.container_kind != OMC_XMP_DUMP_CONTAINER_ALT
                    || item.lang.data == (const omc_u8*)0
                    || item.lang.size == 0U
                    || omc_xmp_dump_alt_lang_claimed_before(
                           store, i, opts, cur_pass, prop, item.lang)) {
                    continue;
                }
                omc_xmp_dump_write_bytes(writer, "<rdf:li xml:lang=\"", 18U);
                omc_xmp_dump_write_xml_escaped(writer, item.lang.data,
                                               item.lang.size, 1);
                omc_xmp_dump_write_bytes(writer, "\">", 2U);
                omc_xmp_dump_write_value(writer, &item);
                omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
            }
            if (cur_pass == pass) {
                continue;
            }
        }
        return;
    }

    for (i = index; i < store->entry_count; ++i) {
        if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass, &item)) {
            continue;
        }
        if (!omc_xmp_dump_property_keys_equal(&item, prop)) {
            continue;
        }
        omc_xmp_dump_write_bytes(writer, "<rdf:li>", 8U);
        omc_xmp_dump_write_value(writer, &item);
        omc_xmp_dump_write_bytes(writer, "</rdf:li>", 9U);
    }
}

static void
omc_xmp_dump_emit_one_property(omc_xmp_dump_writer* writer,
                               const omc_store* store, omc_size index,
                               const omc_xmp_sidecar_opts* opts,
                               omc_xmp_dump_pass pass,
                               const omc_xmp_dump_property* prop)
{
    if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_STRUCT_RESOURCE
        || prop->container_kind == OMC_XMP_DUMP_CONTAINER_STRUCT_SEQ
        || prop->container_kind == OMC_XMP_DUMP_CONTAINER_STRUCT_BAG) {
        omc_xmp_dump_emit_structured_property(writer, store, index, opts, pass,
                                              prop);
        return;
    }

    omc_xmp_dump_write_property_open(writer, store, index, opts, pass, prop);
    if (omc_xmp_dump_property_needs_group(prop)) {
        if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_SEQ) {
            omc_xmp_dump_write_bytes(writer, "<rdf:Seq>", 9U);
            omc_xmp_dump_emit_group_items(writer, store, index, opts, pass,
                                          prop);
            omc_xmp_dump_write_bytes(writer, "</rdf:Seq>", 10U);
        } else if (prop->container_kind == OMC_XMP_DUMP_CONTAINER_ALT) {
            omc_xmp_dump_write_bytes(writer, "<rdf:Alt>", 9U);
            omc_xmp_dump_emit_group_items(writer, store, index, opts, pass,
                                          prop);
            omc_xmp_dump_write_bytes(writer, "</rdf:Alt>", 10U);
        } else {
            omc_xmp_dump_write_bytes(writer, "<rdf:Bag>", 9U);
            omc_xmp_dump_emit_group_items(writer, store, index, opts, pass,
                                          prop);
            omc_xmp_dump_write_bytes(writer, "</rdf:Bag>", 10U);
        }
    } else {
        omc_xmp_dump_write_value(writer, prop);
    }
    omc_xmp_dump_write_property_close(writer, store, index, opts, pass, prop);
}

static void
omc_xmp_dump_emit_namespace_decls(omc_xmp_dump_writer* writer,
                                  const omc_store* store,
                                  const omc_xmp_sidecar_opts* opts,
                                  int* out_limit_hit)
{
    omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
    omc_u32 pass_index;
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    omc_xmp_dump_fill_pass_order(opts, order);
    for (pass_index = 0U; pass_index < OMC_XMP_DUMP_PASS_COUNT;
         ++pass_index) {
        omc_xmp_dump_pass pass;

        pass = order[pass_index];
        for (i = 0U; i < store->entry_count; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                    &prop)) {
                continue;
            }
            if (!omc_xmp_dump_property_has_output(&prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, pass, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                *out_limit_hit = 1;
                continue;
            }
            if (!omc_xmp_dump_schema_first_for_emitted(store, i, opts, pass,
                                                       prop.schema_ns)) {
                emitted += 1U;
                continue;
            }

            omc_xmp_dump_write_bytes(writer, " xmlns:", 7U);
            omc_xmp_dump_write_prefix(writer, store, i, opts, pass,
                                      prop.schema_ns);
            omc_xmp_dump_write_bytes(writer, "=\"", 2U);
            omc_xmp_dump_write_xml_escaped(writer, prop.schema_ns.data,
                                           prop.schema_ns.size, 1);
            omc_xmp_dump_write_byte(writer, '"');
            emitted += 1U;
        }
    }
}

static omc_u32
omc_xmp_dump_emit_properties(omc_xmp_dump_writer* writer, const omc_store* store,
                             const omc_xmp_sidecar_opts* opts,
                             int* out_limit_hit)
{
    omc_xmp_dump_pass order[OMC_XMP_DUMP_PASS_COUNT];
    omc_u32 pass_index;
    omc_size i;
    omc_u32 emitted;
    omc_xmp_dump_property prop;

    emitted = 0U;
    omc_xmp_dump_fill_pass_order(opts, order);
    for (pass_index = 0U; pass_index < OMC_XMP_DUMP_PASS_COUNT;
         ++pass_index) {
        omc_xmp_dump_pass pass;

        pass = order[pass_index];
        for (i = 0U; i < store->entry_count; ++i) {
            if (!omc_xmp_dump_extract_pass_property(store, i, opts, pass,
                                                    &prop)) {
                continue;
            }
            if (!omc_xmp_dump_property_has_output(&prop)) {
                continue;
            }
            if (omc_xmp_dump_claimed_before(store, i, opts, pass, &prop)) {
                continue;
            }
            if (opts->limits.max_entries != 0U
                && emitted >= opts->limits.max_entries) {
                *out_limit_hit = 1;
                continue;
            }

            omc_xmp_dump_emit_one_property(writer, store, i, opts, pass,
                                           &prop);
            emitted += 1U;
        }
    }

    return emitted;
}

void
omc_xmp_sidecar_opts_init(omc_xmp_sidecar_opts* opts)
{
    if (opts == (omc_xmp_sidecar_opts*)0) {
        return;
    }

    opts->limits.max_output_bytes = 0U;
    opts->limits.max_entries = 0U;
    opts->include_existing_xmp = 1;
    opts->include_exif = 0;
    opts->include_iptc = 0;
    opts->existing_namespace_policy = OMC_XMP_NS_KNOWN_PORTABLE_ONLY;
    opts->existing_standard_namespace_policy
        = OMC_XMP_STD_NS_PRESERVE_ALL;
    opts->conflict_policy = OMC_XMP_CONFLICT_EXISTING_WINS;
    opts->exiftool_gpsdatetime_alias = 0;
}

void
omc_xmp_portable_opts_init(omc_xmp_portable_opts* opts)
{
    if (opts == (omc_xmp_portable_opts*)0) {
        return;
    }

    opts->limits.max_output_bytes = 0U;
    opts->limits.max_entries = 0U;
    opts->include_existing_xmp = 0;
    opts->include_exif = 1;
    opts->include_iptc = 1;
    opts->existing_namespace_policy = OMC_XMP_NS_KNOWN_PORTABLE_ONLY;
    opts->existing_standard_namespace_policy
        = OMC_XMP_STD_NS_PRESERVE_ALL;
    opts->conflict_policy = OMC_XMP_CONFLICT_CURRENT_BEHAVIOR;
    opts->exiftool_gpsdatetime_alias = 0;
}

void
omc_xmp_lossless_opts_init(omc_xmp_lossless_opts* opts)
{
    if (opts == (omc_xmp_lossless_opts*)0) {
        return;
    }

    opts->limits.max_output_bytes = 0U;
    opts->limits.max_entries = 0U;
    opts->include_origin = 1;
    opts->include_wire = 1;
    opts->include_flags = 1;
    opts->include_names = 1;
}

void
omc_xmp_sidecar_cfg_init(omc_xmp_sidecar_cfg* cfg)
{
    if (cfg == (omc_xmp_sidecar_cfg*)0) {
        return;
    }

    cfg->format = OMC_XMP_SIDECAR_LOSSLESS;
    omc_xmp_lossless_opts_init(&cfg->lossless);
    omc_xmp_portable_opts_init(&cfg->portable);
    cfg->initial_output_bytes = 0U;
}

void
omc_xmp_sidecar_req_init(omc_xmp_sidecar_req* req)
{
    if (req == (omc_xmp_sidecar_req*)0) {
        return;
    }

    req->format = OMC_XMP_SIDECAR_LOSSLESS;
    req->limits.max_output_bytes = 0U;
    req->limits.max_entries = 0U;
    req->include_exif = 1;
    req->include_iptc = 1;
    req->include_existing_xmp = 0;
    req->portable_existing_namespace_policy
        = OMC_XMP_NS_KNOWN_PORTABLE_ONLY;
    req->portable_existing_standard_namespace_policy
        = OMC_XMP_STD_NS_PRESERVE_ALL;
    req->portable_conflict_policy = OMC_XMP_CONFLICT_CURRENT_BEHAVIOR;
    req->portable_exiftool_gpsdatetime_alias = 0;
    req->include_origin = 1;
    req->include_wire = 1;
    req->include_flags = 1;
    req->include_names = 1;
    req->initial_output_bytes = 0U;
}

void
omc_xmp_sidecar_cfg_from_req(omc_xmp_sidecar_cfg* out_cfg,
                             const omc_xmp_sidecar_req* req)
{
    if (out_cfg == (omc_xmp_sidecar_cfg*)0) {
        return;
    }
    omc_xmp_sidecar_cfg_init(out_cfg);
    if (req == (const omc_xmp_sidecar_req*)0) {
        return;
    }

    out_cfg->format = req->format;
    out_cfg->initial_output_bytes = req->initial_output_bytes;
    out_cfg->lossless.limits = req->limits;
    out_cfg->lossless.include_origin = req->include_origin;
    out_cfg->lossless.include_wire = req->include_wire;
    out_cfg->lossless.include_flags = req->include_flags;
    out_cfg->lossless.include_names = req->include_names;
    out_cfg->portable.limits = req->limits;
    out_cfg->portable.include_exif = req->include_exif;
    out_cfg->portable.include_iptc = req->include_iptc;
    out_cfg->portable.include_existing_xmp = req->include_existing_xmp;
    out_cfg->portable.existing_namespace_policy
        = req->portable_existing_namespace_policy;
    out_cfg->portable.existing_standard_namespace_policy
        = req->portable_existing_standard_namespace_policy;
    out_cfg->portable.conflict_policy = req->portable_conflict_policy;
    out_cfg->portable.exiftool_gpsdatetime_alias
        = req->portable_exiftool_gpsdatetime_alias;
}

static omc_u64
omc_xmp_dump_cfg_limit_bytes(const omc_xmp_sidecar_cfg* cfg)
{
    if (cfg == (const omc_xmp_sidecar_cfg*)0) {
        return 0U;
    }
    if (cfg->format == OMC_XMP_SIDECAR_PORTABLE) {
        return cfg->portable.limits.max_output_bytes;
    }
    return cfg->lossless.limits.max_output_bytes;
}

omc_status
omc_xmp_dump_sidecar(const omc_store* store, omc_u8* out, omc_size out_cap,
                     const omc_xmp_sidecar_opts* opts,
                     omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_opts local_opts;
    omc_xmp_dump_writer writer;
    int limit_hit;
    static const char k_packet_open[]
        = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
          "x:xmptk=\"OpenMeta-c\"><rdf:RDF><rdf:Description";
    static const char k_packet_close[]
        = "</rdf:Description></rdf:RDF></x:xmpmeta>";

    if (store == (const omc_store*)0 || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == (omc_u8*)0 && out_cap != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_sidecar_opts*)0) {
        omc_xmp_sidecar_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_dump_writer_init(&writer, out, out_cap);
    limit_hit = 0;

    omc_xmp_dump_write_bytes(&writer, k_packet_open,
                             sizeof(k_packet_open) - 1U);
    omc_xmp_dump_emit_namespace_decls(&writer, store, opts, &limit_hit);
    omc_xmp_dump_write_byte(&writer, '>');
    out_res->entries
        = omc_xmp_dump_emit_properties(&writer, store, opts, &limit_hit);
    omc_xmp_dump_write_bytes(&writer, k_packet_close,
                             sizeof(k_packet_close) - 1U);

    out_res->needed = writer.needed;
    out_res->written = writer.written;

    if (opts->limits.max_output_bytes != 0U
        && writer.needed > opts->limits.max_output_bytes) {
        limit_hit = 1;
    }

    if (limit_hit != 0) {
        out_res->status = OMC_XMP_DUMP_LIMIT;
    } else if (writer.needed > out_cap) {
        out_res->status = OMC_XMP_DUMP_TRUNCATED;
    } else {
        out_res->status = OMC_XMP_DUMP_OK;
    }

    return OMC_STATUS_OK;
}

omc_status
omc_xmp_dump_portable(const omc_store* store, omc_u8* out, omc_size out_cap,
                      const omc_xmp_portable_opts* opts,
                      omc_xmp_dump_res* out_res)
{
    return omc_xmp_dump_sidecar(store, out, out_cap, opts, out_res);
}

omc_status
omc_xmp_dump_lossless(const omc_store* store, omc_u8* out, omc_size out_cap,
                      const omc_xmp_lossless_opts* opts,
                      omc_xmp_dump_res* out_res)
{
    omc_xmp_lossless_opts local_opts;
    omc_xmp_dump_writer writer;
    omc_size i;
    omc_u32 emitted;
    int limit_hit;
    static const char k_packet_open[]
        = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
          "xmlns:omd=\"urn:openmeta:dump:1.0\" "
          "x:xmptk=\"OpenMeta-c\"><rdf:RDF><rdf:Description>";
    static const char k_packet_close[]
        = "</rdf:Description></rdf:RDF></x:xmpmeta>";

    if (store == (const omc_store*)0 || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (out == (omc_u8*)0 && out_cap != 0U) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (opts == (const omc_xmp_lossless_opts*)0) {
        omc_xmp_lossless_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_xmp_dump_writer_init(&writer, out, out_cap);
    limit_hit = 0;
    emitted = 0U;

    omc_xmp_dump_write_bytes(&writer, k_packet_open, sizeof(k_packet_open) - 1U);
    omc_xmp_dump_write_u64_element(&writer, "omd:formatVersion", 1U);
    omc_xmp_dump_write_u64_element(&writer, "omd:blockCount",
                                   (omc_u64)store->block_count);
    omc_xmp_dump_write_bytes(&writer, "<omd:entries><rdf:Seq>",
                             sizeof("<omd:entries><rdf:Seq>") - 1U);

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;

        entry = &store->entries[i];
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        if (opts->limits.max_entries != 0U
            && emitted >= opts->limits.max_entries) {
            limit_hit = 1;
            continue;
        }
        omc_xmp_dump_lossless_emit_entry(&writer, store, entry, opts);
        emitted += 1U;
    }

    omc_xmp_dump_write_bytes(&writer, "</rdf:Seq></omd:entries>",
                             sizeof("</rdf:Seq></omd:entries>") - 1U);
    omc_xmp_dump_write_u64_element(&writer, "omd:entriesWritten",
                                   (omc_u64)emitted);
    omc_xmp_dump_write_bytes(&writer, k_packet_close,
                             sizeof(k_packet_close) - 1U);

    out_res->entries = emitted;
    out_res->needed = writer.needed;
    out_res->written = writer.written;
    if (opts->limits.max_output_bytes != 0U
        && writer.needed > opts->limits.max_output_bytes) {
        limit_hit = 1;
    }
    if (limit_hit != 0) {
        out_res->status = OMC_XMP_DUMP_LIMIT;
    } else if (writer.needed > out_cap) {
        out_res->status = OMC_XMP_DUMP_TRUNCATED;
    } else {
        out_res->status = OMC_XMP_DUMP_OK;
    }

    return OMC_STATUS_OK;
}

omc_status
omc_xmp_dump_sidecar_arena(const omc_store* store, omc_arena* out,
                           const omc_xmp_sidecar_opts* opts,
                           omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_opts local_opts;
    omc_size target_cap;
    omc_status status;

    if (store == (const omc_store*)0 || out == (omc_arena*)0
        || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }

    if (opts == (const omc_xmp_sidecar_opts*)0) {
        omc_xmp_sidecar_opts_init(&local_opts);
        opts = &local_opts;
    }

    omc_arena_reset(out);
    target_cap = out->capacity;
    if (target_cap == 0U) {
        target_cap = 256U;
    }
    if (opts->limits.max_output_bytes != 0U
        && (omc_u64)target_cap > opts->limits.max_output_bytes) {
        if (opts->limits.max_output_bytes > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }
        target_cap = (omc_size)opts->limits.max_output_bytes;
        if (target_cap == 0U) {
            target_cap = 1U;
        }
    }

    for (;;) {
        status = omc_arena_reserve(out, target_cap);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        status = omc_xmp_dump_sidecar(store, out->data, out->capacity, opts,
                                      out_res);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        out->size = (omc_size)out_res->written;
        if (out_res->status != OMC_XMP_DUMP_TRUNCATED) {
            return OMC_STATUS_OK;
        }
        if (out_res->needed > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }

        target_cap = (omc_size)out_res->needed;
    }
}

omc_status
omc_xmp_dump_portable_arena(const omc_store* store, omc_arena* out,
                            const omc_xmp_portable_opts* opts,
                            omc_xmp_dump_res* out_res)
{
    return omc_xmp_dump_sidecar_arena(store, out, opts, out_res);
}

static omc_status
omc_xmp_dump_sidecar_cfg_once(const omc_store* store, omc_u8* out,
                              omc_size out_cap,
                              const omc_xmp_sidecar_cfg* cfg,
                              omc_xmp_dump_res* out_res)
{
    if (cfg->format == OMC_XMP_SIDECAR_PORTABLE) {
        return omc_xmp_dump_portable(store, out, out_cap, &cfg->portable,
                                     out_res);
    }
    return omc_xmp_dump_lossless(store, out, out_cap, &cfg->lossless,
                                 out_res);
}

omc_status
omc_xmp_dump_sidecar_cfg(const omc_store* store, omc_arena* out,
                         const omc_xmp_sidecar_cfg* cfg,
                         omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_cfg local_cfg;
    omc_size target_cap;
    omc_u64 limit_bytes;
    omc_status status;

    if (store == (const omc_store*)0 || out == (omc_arena*)0
        || out_res == (omc_xmp_dump_res*)0) {
        return OMC_STATUS_INVALID_ARGUMENT;
    }
    if (cfg == (const omc_xmp_sidecar_cfg*)0) {
        omc_xmp_sidecar_cfg_init(&local_cfg);
        cfg = &local_cfg;
    }

    omc_arena_reset(out);
    target_cap = out->capacity;
    if (cfg->initial_output_bytes != 0U) {
        if (cfg->initial_output_bytes > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }
        target_cap = (omc_size)cfg->initial_output_bytes;
    }
    if (target_cap == 0U) {
        target_cap = 256U;
    }

    limit_bytes = omc_xmp_dump_cfg_limit_bytes(cfg);
    if (limit_bytes != 0U && (omc_u64)target_cap > limit_bytes) {
        if (limit_bytes > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }
        target_cap = (omc_size)limit_bytes;
        if (target_cap == 0U) {
            target_cap = 1U;
        }
    }

    for (;;) {
        status = omc_arena_reserve(out, target_cap);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        status = omc_xmp_dump_sidecar_cfg_once(store, out->data, out->capacity,
                                               cfg, out_res);
        if (status != OMC_STATUS_OK) {
            return status;
        }

        out->size = (omc_size)out_res->written;
        if (out_res->status != OMC_XMP_DUMP_TRUNCATED) {
            return OMC_STATUS_OK;
        }
        if (out_res->needed > (omc_u64)(~(omc_size)0)) {
            return OMC_STATUS_OVERFLOW;
        }
        target_cap = (omc_size)out_res->needed;
    }
}

omc_status
omc_xmp_dump_sidecar_req(const omc_store* store, omc_arena* out,
                         const omc_xmp_sidecar_req* req,
                         omc_xmp_dump_res* out_res)
{
    omc_xmp_sidecar_cfg cfg;

    omc_xmp_sidecar_cfg_from_req(&cfg, req);
    return omc_xmp_dump_sidecar_cfg(store, out, &cfg, out_res);
}
