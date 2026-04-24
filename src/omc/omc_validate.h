#ifndef OMC_VALIDATE_H
#define OMC_VALIDATE_H

#include "omc/omc_base.h"
#include "omc/omc_ccm_query.h"
#include "omc/omc_read.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_validate_status {
    OMC_VALIDATE_OK = 0,
    OMC_VALIDATE_OPEN_FAILED = 1,
    OMC_VALIDATE_TOO_LARGE = 2,
    OMC_VALIDATE_READ_FAILED = 3
} omc_validate_status;

typedef enum omc_validate_issue_severity {
    OMC_VALIDATE_WARNING = 0,
    OMC_VALIDATE_ERROR = 1
} omc_validate_issue_severity;

typedef enum omc_validate_issue_category {
    OMC_VALIDATE_ISSUE_FILE = 0,
    OMC_VALIDATE_ISSUE_SCAN = 1,
    OMC_VALIDATE_ISSUE_PAYLOAD = 2,
    OMC_VALIDATE_ISSUE_EXIF = 3,
    OMC_VALIDATE_ISSUE_XMP = 4,
    OMC_VALIDATE_ISSUE_EXR = 5,
    OMC_VALIDATE_ISSUE_JUMBF = 6,
    OMC_VALIDATE_ISSUE_XMP_SIDECAR = 7,
    OMC_VALIDATE_ISSUE_CCM = 8,
    OMC_VALIDATE_ISSUE_READ = 9
} omc_validate_issue_category;

typedef enum omc_validate_issue_code {
    OMC_VALIDATE_CODE_OPEN_FAILED = 0,
    OMC_VALIDATE_CODE_TOO_LARGE = 1,
    OMC_VALIDATE_CODE_READ_FAILED = 2,
    OMC_VALIDATE_CODE_OUTPUT_TRUNCATED = 3,
    OMC_VALIDATE_CODE_MALFORMED = 4,
    OMC_VALIDATE_CODE_LIMIT_EXCEEDED = 5,
    OMC_VALIDATE_CODE_INVALID_OR_MALFORMED_XML_TEXT = 6,
    OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED = 7,
    OMC_VALIDATE_CODE_RETRY_LIMIT_EXCEEDED = 8,
    OMC_VALIDATE_CODE_NO_MEMORY = 9,
    OMC_VALIDATE_CODE_CCM_DECODE_FAILED = 10,
    OMC_VALIDATE_CODE_CCM_NON_FINITE_VALUE = 11,
    OMC_VALIDATE_CODE_CCM_UNEXPECTED_COUNT = 12,
    OMC_VALIDATE_CODE_CCM_MATRIX_COUNT_NOT_DIVISIBLE_BY_3 = 13,
    OMC_VALIDATE_CODE_CCM_NON_POSITIVE_VALUE = 14,
    OMC_VALIDATE_CODE_CCM_AS_SHOT_CONFLICT = 15,
    OMC_VALIDATE_CODE_CCM_MISSING_COMPANION_TAG = 16,
    OMC_VALIDATE_CODE_CCM_TRIPLE_ILLUMINANT_RULE = 17,
    OMC_VALIDATE_CODE_CCM_CALIBRATION_SIGNATURE_MISMATCH = 18,
    OMC_VALIDATE_CODE_CCM_MISSING_ILLUMINANT_DATA = 19,
    OMC_VALIDATE_CODE_CCM_INVALID_ILLUMINANT_CODE = 20,
    OMC_VALIDATE_CODE_CCM_WHITE_XY_OUT_OF_RANGE = 21
} omc_validate_issue_code;

typedef struct omc_validate_issue {
    omc_validate_issue_severity severity;
    omc_validate_issue_category category;
    omc_validate_issue_code code;
} omc_validate_issue;

typedef struct omc_validate_opts {
    omc_read_opts read;
    omc_ccm_query_opts ccm;
    omc_u64 max_file_bytes;
    int include_xmp_sidecar;
    int warnings_as_errors;
    omc_u32 max_retry_passes;
} omc_validate_opts;

typedef struct omc_validate_res {
    omc_validate_status status;
    omc_u64 file_size;
    omc_read_res read;
    omc_ccm_query_res ccm;
    omc_u32 ccm_fields;
    omc_u32 entries;
    omc_u32 warning_count;
    omc_u32 error_count;
    int failed;
    omc_u32 issues_written;
    omc_u32 issues_needed;
} omc_validate_res;

OMC_API void
omc_validate_opts_init(omc_validate_opts* opts);

OMC_API const char*
omc_validate_status_name(omc_validate_status status);

OMC_API const char*
omc_validate_issue_category_name(omc_validate_issue_category category);

OMC_API const char*
omc_validate_issue_code_name(omc_validate_issue_code code);

OMC_API omc_validate_res
omc_validate_file(const char* path, omc_validate_issue* out_issues,
                  omc_u32 issue_cap, const omc_validate_opts* opts);

OMC_EXTERN_C_END

#endif
