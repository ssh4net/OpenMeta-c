#ifndef OMC_TRANSLATION_H
#define OMC_TRANSLATION_H
#include "omc/omc_transfer.h"
#include "omc/omc_edit.h"
OMC_EXTERN_C_BEGIN

/* Individual mappings can be selected without introducing C++ owner objects. */
#define OMC_TRANSLATE_CREATE_EXIF 0x000001U
#define OMC_TRANSLATE_CREATE_IPTC 0x000002U
#define OMC_TRANSLATE_DATE_CREATED 0x000004U
#define OMC_TRANSLATE_DATE_ORIGINAL 0x000008U
#define OMC_TRANSLATE_MODIFY_DATE 0x000010U
#define OMC_TRANSLATE_MAKE 0x000020U
#define OMC_TRANSLATE_MODEL 0x000040U
#define OMC_TRANSLATE_SOFTWARE 0x000080U
#define OMC_TRANSLATE_EXPOSURE 0x000100U
#define OMC_TRANSLATE_FNUMBER 0x000200U
#define OMC_TRANSLATE_ISO 0x000400U
#define OMC_TRANSLATE_FOCAL_LENGTH 0x000800U
#define OMC_TRANSLATE_EXPOSURE_BIAS 0x001000U
#define OMC_TRANSLATE_TITLE 0x002000U
#define OMC_TRANSLATE_DESCRIPTION 0x004000U
#define OMC_TRANSLATE_CREATORS 0x008000U
#define OMC_TRANSLATE_KEYWORDS 0x010000U
#define OMC_TRANSLATE_RIGHTS 0x020000U
#define OMC_TRANSLATE_CREDIT 0x040000U
#define OMC_TRANSLATE_SOURCE 0x080000U
#define OMC_TRANSLATE_ORIENTATION 0x100000U
#define OMC_TRANSLATE_DIMENSIONS 0x200000U
#define OMC_TRANSLATE_ALL 0x3FFFFFU
#define OMC_TRANSLATE_DATES 0x00000FU
#define OMC_TRANSLATE_TECHNICAL 0x0000F0U
#define OMC_TRANSLATE_CAPTURE 0x001F00U
#define OMC_TRANSLATE_DESCRIPTIVE 0x0FE000U
#define OMC_TRANSLATE_GEOMETRY 0x300000U

/* Location mappings are selected only by omc_translate_xmp_location. The
 * existing OMC_TRANSLATE_ALL and omc_translate_xmp defaults stay unchanged. */
#define OMC_TRANSLATE_CITY 0x00400000U
#define OMC_TRANSLATE_SUBLOCATION 0x00800000U
#define OMC_TRANSLATE_STATE 0x01000000U
#define OMC_TRANSLATE_COUNTRY 0x02000000U
#define OMC_TRANSLATE_COUNTRY_CODE 0x04000000U
#define OMC_TRANSLATE_LOCATION 0x07C00000U

/* Separate mask domain for omc_translate_xmp_iptc and its failed_mapping. */
#define OMC_IPTC_TRANSLATE_TITLE 0x000001U
#define OMC_IPTC_TRANSLATE_DESCRIPTION 0x000002U
#define OMC_IPTC_TRANSLATE_CREATORS 0x000004U
#define OMC_IPTC_TRANSLATE_KEYWORDS 0x000008U
#define OMC_IPTC_TRANSLATE_RIGHTS 0x000010U
#define OMC_IPTC_TRANSLATE_CREDIT 0x000020U
#define OMC_IPTC_TRANSLATE_SOURCE 0x000040U
#define OMC_IPTC_TRANSLATE_CITY 0x000080U
#define OMC_IPTC_TRANSLATE_SUBLOCATION 0x000100U
#define OMC_IPTC_TRANSLATE_STATE 0x000200U
#define OMC_IPTC_TRANSLATE_COUNTRY 0x000400U
#define OMC_IPTC_TRANSLATE_COUNTRY_CODE 0x000800U
#define OMC_IPTC_TRANSLATE_HEADLINE 0x001000U
#define OMC_IPTC_TRANSLATE_INSTRUCTIONS 0x002000U
#define OMC_IPTC_TRANSLATE_TRANSMISSION_REFERENCE 0x004000U
#define OMC_IPTC_TRANSLATE_AUTHORS_POSITION 0x008000U
#define OMC_IPTC_TRANSLATE_CAPTION_WRITER 0x010000U
#define OMC_IPTC_TRANSLATE_CATEGORY 0x020000U
#define OMC_IPTC_TRANSLATE_SUPPLEMENTAL_CATEGORIES 0x040000U
#define OMC_IPTC_TRANSLATE_URGENCY 0x080000U
#define OMC_IPTC_TRANSLATE_DESCRIPTIVE 0x00007FU
#define OMC_IPTC_TRANSLATE_LOCATION 0x000F80U
#define OMC_IPTC_TRANSLATE_EDITORIAL 0x007000U
#define OMC_IPTC_TRANSLATE_WORKFLOW 0x0F8000U
#define OMC_IPTC_TRANSLATE_ALL 0x0FFFFFU

/* Separate mask domain for the primary GPS reverse-translation slice. */
#define OMC_GPS_TRANSLATE_LATITUDE 0x000001U
#define OMC_GPS_TRANSLATE_LONGITUDE 0x000002U
#define OMC_GPS_TRANSLATE_ALTITUDE 0x000004U
#define OMC_GPS_TRANSLATE_VERSION 0x000008U
#define OMC_GPS_TRANSLATE_ALL 0x000007U

/* Additional GPS group domains. The primary API above remains source and ABI
 * compatible; each function below is one output-preserving transaction. */
#define OMC_GPS_NAV_TRANSLATE_TIMESTAMP 0x000001U
#define OMC_GPS_NAV_TRANSLATE_SPEED 0x000002U
#define OMC_GPS_NAV_TRANSLATE_TRACK 0x000004U
#define OMC_GPS_NAV_TRANSLATE_DIRECTION 0x000008U
#define OMC_GPS_NAV_TRANSLATE_ALL 0x00000FU
#define OMC_GPS_DEST_TRANSLATE_LATITUDE 0x000001U
#define OMC_GPS_DEST_TRANSLATE_LONGITUDE 0x000002U
#define OMC_GPS_DEST_TRANSLATE_BEARING 0x000004U
#define OMC_GPS_DEST_TRANSLATE_DISTANCE 0x000008U
#define OMC_GPS_DEST_TRANSLATE_ALL 0x00000FU
#define OMC_GPS_QUALITY_TRANSLATE_STATUS 0x000001U
#define OMC_GPS_QUALITY_TRANSLATE_MEASURE_MODE 0x000002U
#define OMC_GPS_QUALITY_TRANSLATE_DOP 0x000004U
#define OMC_GPS_QUALITY_TRANSLATE_DIFFERENTIAL 0x000008U
#define OMC_GPS_QUALITY_TRANSLATE_ERROR 0x000010U
#define OMC_GPS_QUALITY_TRANSLATE_ALL 0x00001FU
#define OMC_GPS_TEXT_TRANSLATE_SATELLITES 0x000001U
#define OMC_GPS_TEXT_TRANSLATE_MAP_DATUM 0x000002U
#define OMC_GPS_TEXT_TRANSLATE_PROCESSING_METHOD 0x000004U
#define OMC_GPS_TEXT_TRANSLATE_AREA_INFORMATION 0x000008U
#define OMC_GPS_TEXT_TRANSLATE_ALL 0x00000FU

/* First bounded EXIF text/version family. The remaining exifEX text fields
 * are qualified in a later batch. */
#define OMC_EXIF_TEXT_TRANSLATE_EXIF_VERSION 0x000001U
#define OMC_EXIF_TEXT_TRANSLATE_FLASHPIX_VERSION 0x000002U
#define OMC_EXIF_TEXT_TRANSLATE_USER_COMMENT 0x000004U
#define OMC_EXIF_TEXT_TRANSLATE_IMAGE_TITLE 0x000008U
#define OMC_EXIF_TEXT_TRANSLATE_ALL 0x00000FU

typedef enum omc_translation_conflict {
    OMC_TRANSLATION_PRESERVE = 0,
    OMC_TRANSLATION_FAIL = 1,
    OMC_TRANSLATION_REPLACE = 2
} omc_translation_conflict;

typedef enum omc_translation_status {
    OMC_TRANSLATION_OK = 0,
    OMC_TRANSLATION_INVALID_OPTIONS,
    OMC_TRANSLATION_INVALID_SOURCE,
    OMC_TRANSLATION_AMBIGUOUS_SOURCE,
    OMC_TRANSLATION_UNSUPPORTED_PRECISION,
    OMC_TRANSLATION_NATIVE_CONFLICT,
    OMC_TRANSLATION_ENCODING_CONFLICT,
    OMC_TRANSLATION_TARGET_REQUIRED,
    OMC_TRANSLATION_TARGET_MISMATCH,
    OMC_TRANSLATION_LIMIT,
    OMC_TRANSLATION_NO_MEMORY,
    OMC_TRANSLATION_VALUE_TOO_LONG
} omc_translation_status;

typedef struct omc_translation_opts {
    omc_u32 mappings;
    int all_sources;
    omc_translation_conflict conflict;
    omc_u32 max_source_properties;
    omc_u32 max_added_entries;
    omc_u32 max_operations;
    omc_u32 max_text_bytes_per_property;
    omc_u64 max_total_text_bytes;
} omc_translation_opts;

typedef struct omc_location_translation_opts {
    omc_u32 mappings;
    int all_sources;
    omc_translation_conflict conflict;
    omc_u32 max_source_properties;
    omc_u32 max_added_entries;
    omc_u32 max_operations;
    omc_u64 max_total_text_bytes;
} omc_location_translation_opts;

typedef struct omc_iptc_translation_opts {
    omc_u32 mappings;
    int all_sources;
    omc_translation_conflict conflict;
    omc_u32 max_source_properties;
    omc_u32 max_added_entries;
    omc_u32 max_operations;
    omc_u64 max_total_text_bytes;
} omc_iptc_translation_opts;

typedef enum omc_gps_translation_status {
    OMC_GPS_TRANSLATION_OK = 0,
    OMC_GPS_TRANSLATION_NULL_OUTPUT,
    OMC_GPS_TRANSLATION_INVALID_OPTIONS,
    OMC_GPS_TRANSLATION_AMBIGUOUS_SOURCE,
    OMC_GPS_TRANSLATION_INCOMPLETE_SOURCE,
    OMC_GPS_TRANSLATION_INVALID_SOURCE,
    OMC_GPS_TRANSLATION_VALUE_OUT_OF_RANGE,
    OMC_GPS_TRANSLATION_UNSUPPORTED_PRECISION,
    OMC_GPS_TRANSLATION_UNSUPPORTED_VERSION,
    OMC_GPS_TRANSLATION_VALUE_TOO_LONG,
    OMC_GPS_TRANSLATION_SOURCE_LIMIT,
    OMC_GPS_TRANSLATION_NATIVE_CONFLICT,
    OMC_GPS_TRANSLATION_ENTRY_LIMIT,
    OMC_GPS_TRANSLATION_OPERATION_LIMIT,
    OMC_GPS_TRANSLATION_NO_MEMORY,
    OMC_GPS_TRANSLATION_INTERNAL
} omc_gps_translation_status;

typedef struct omc_gps_translation_opts {
    omc_u32 mappings;
    int all_sources;
    omc_translation_conflict conflict;
    omc_u32 max_source_properties;
    omc_u32 max_added_entries;
    omc_u32 max_operations;
    omc_u32 max_text_bytes_per_property;
    omc_u64 max_total_text_bytes;
} omc_gps_translation_opts;

typedef struct omc_gps_translation_res {
    omc_gps_translation_status status;
    omc_u32 failed_mapping;
    omc_entry_id failed_source;
    omc_u32 source_properties;
    omc_u32 groups_translated;
    omc_u32 groups_preserved;
    omc_u32 groups_unchanged;
    omc_u32 entries_added;
    omc_u32 entries_updated;
    omc_u32 entries_removed;
} omc_gps_translation_res;

typedef enum omc_exif_text_translation_status {
    OMC_EXIF_TEXT_TRANSLATION_OK = 0,
    OMC_EXIF_TEXT_TRANSLATION_NULL_OUTPUT,
    OMC_EXIF_TEXT_TRANSLATION_INVALID_OPTIONS,
    OMC_EXIF_TEXT_TRANSLATION_AMBIGUOUS_SOURCE,
    OMC_EXIF_TEXT_TRANSLATION_INVALID_SOURCE,
    OMC_EXIF_TEXT_TRANSLATION_UNSUPPORTED_SOURCE_SHAPE,
    OMC_EXIF_TEXT_TRANSLATION_INCOMPLETE_SOURCE,
    OMC_EXIF_TEXT_TRANSLATION_UNSUPPORTED_VERSION,
    OMC_EXIF_TEXT_TRANSLATION_NATIVE_CONFLICT,
    OMC_EXIF_TEXT_TRANSLATION_VALUE_TOO_LONG,
    OMC_EXIF_TEXT_TRANSLATION_SOURCE_LIMIT,
    OMC_EXIF_TEXT_TRANSLATION_ENTRY_LIMIT,
    OMC_EXIF_TEXT_TRANSLATION_OPERATION_LIMIT,
    OMC_EXIF_TEXT_TRANSLATION_NO_MEMORY,
    OMC_EXIF_TEXT_TRANSLATION_INTERNAL
} omc_exif_text_translation_status;

typedef struct omc_exif_text_translation_opts {
    omc_u32 mappings;
    int all_sources;
    omc_translation_conflict conflict;
    omc_u32 max_source_properties;
    omc_u32 max_added_entries;
    omc_u32 max_operations;
    omc_u32 max_text_bytes_per_property;
    omc_u64 max_total_text_bytes;
} omc_exif_text_translation_opts;

typedef struct omc_exif_text_translation_res {
    omc_exif_text_translation_status status;
    omc_u32 failed_mapping;
    omc_entry_id failed_source;
    omc_u32 source_properties;
    omc_u32 groups_translated;
    omc_u32 groups_preserved;
    omc_u32 groups_unchanged;
    omc_u32 entries_added;
    omc_u32 entries_updated;
    omc_u32 entries_removed;
} omc_exif_text_translation_res;

typedef struct omc_translation_res {
    omc_translation_status status;
    omc_u32 failed_mapping;
    omc_entry_id failed_source;
    omc_u32 groups_translated;
    omc_u32 groups_preserved;
    omc_u32 groups_unchanged;
    omc_u32 entries_added;
    omc_u32 entries_updated;
    omc_u32 entries_removed;
    int utf8_charset_added;
} omc_translation_res;

OMC_API void omc_translation_opts_init(omc_translation_opts *opts);

/* Explicit reverse translation. Source remains immutable. All selected groups
 * form one transaction; failure preserves out, including its borrowed views.
 * out must be initialized and distinct from source. target may be NULL unless
 * an eligible geometry property requires target image facts. */
OMC_API omc_translation_res omc_translate_xmp(
    const omc_store *source, omc_store *out, const omc_translation_opts *opts,
    const omc_transfer_target_image_spec *target);

OMC_API void
omc_location_translation_opts_init(omc_location_translation_opts *opts);

/* Explicit five-field IPTC location writeback, with the same source/output
 * ownership contract as omc_translate_xmp. Exact unqualified XMP paths only.
 * Defaults: all five mappings, dirty groups, fail on native conflict; limits
 * of 1024 sources, 6 additions, 4096 operations and 8 MiB inspected text.
 * Limits may be reduced. Dirty tombstones remove native values with REPLACE.
 * CountryCode accepts exactly two or three uppercase ASCII letters. */
OMC_API omc_translation_res
omc_translate_xmp_location(const omc_store *source, omc_store *out,
                           const omc_location_translation_opts *opts);

OMC_API void omc_iptc_translation_opts_init(omc_iptc_translation_opts *opts);

/* All 20 bounded IPTC text/priority groups in one output-preserving transaction.
 * Use OMC_IPTC_TRANSLATE_* masks, also reported in failed_mapping. Dates remain
 * in omc_translate_xmp. Defaults: all groups, dirty-only, fail on conflict,
 * 1024 sources, 1025 additions, 4096 operations and 8 MiB inspected text.
 * Repeated groups retain numeric XMP index order. Category is 1-3 ASCII letters;
 * Urgency is text or an integer scalar 1-8. Planning may allocate bounded
 * scratch for repeated groups; edit/commit use owning arenas as usual.
 * Source and initialized out must be distinct. Failure preserves both. */
OMC_API omc_translation_res omc_translate_xmp_iptc(
    const omc_store *source, omc_store *out, const omc_iptc_translation_opts *opts);

OMC_API void omc_gps_translation_opts_init(omc_gps_translation_opts *opts);

/* Primary GPS reverse translation. The selected XMP properties are
 * exif:GPSLatitude, exif:GPSLongitude and the complete
 * exif:GPSAltitude/exif:GPSAltitudeRef pair. Coordinates are converted to
 * exact unsigned GPS DMS rationals. The source and initialized output must
 * be distinct. Preparation may allocate, while commit preserves output on
 * every failure. GPSVersionID is retained or added as 2.3.0.0 when selected
 * native output needs it; OMC_GPS_TRANSLATE_VERSION is diagnostic only. */
OMC_API omc_gps_translation_res omc_translate_xmp_gps(
    const omc_store *source, omc_store *out,
    const omc_gps_translation_opts *opts);

OMC_API omc_gps_translation_res omc_translate_xmp_gps_navigation(
    const omc_store *source, omc_store *out,
    const omc_gps_translation_opts *opts);
OMC_API omc_gps_translation_res omc_translate_xmp_gps_destination(
    const omc_store *source, omc_store *out,
    const omc_gps_translation_opts *opts);
OMC_API omc_gps_translation_res omc_translate_xmp_gps_quality(
    const omc_store *source, omc_store *out,
    const omc_gps_translation_opts *opts);
OMC_API omc_gps_translation_res omc_translate_xmp_gps_text(
    const omc_store *source, omc_store *out,
    const omc_gps_translation_opts *opts);

OMC_API void
omc_exif_text_translation_opts_init(omc_exif_text_translation_opts *opts);

/* Translate ExifVersion, FlashpixVersion, UserComment and ImageTitle as one
 * bounded output-preserving transaction. Source and output are distinct
 * initialized stores. Dirty tombstones remove native values only with REPLACE. */
OMC_API omc_exif_text_translation_res
omc_translate_xmp_exif_text(const omc_store *source, omc_store *out,
                            const omc_exif_text_translation_opts *opts);

OMC_EXTERN_C_END
#endif
