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
    OMC_TRANSLATION_NO_MEMORY
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

OMC_EXTERN_C_END
#endif
