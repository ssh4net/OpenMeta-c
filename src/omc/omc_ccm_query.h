#ifndef OMC_CCM_QUERY_H
#define OMC_CCM_QUERY_H

#include "omc/omc_base.h"
#include "omc/omc_store.h"
#include "omc/omc_types.h"

OMC_EXTERN_C_BEGIN

typedef enum omc_ccm_query_status {
    OMC_CCM_QUERY_OK = 0,
    OMC_CCM_QUERY_LIMIT_EXCEEDED = 1,
    OMC_CCM_QUERY_INVALID_ARGUMENT = 2,
    OMC_CCM_QUERY_NOMEM = 3
} omc_ccm_query_status;

typedef enum omc_ccm_validation_mode {
    OMC_CCM_VALIDATE_NONE = 0,
    OMC_CCM_VALIDATE_DNG_SPEC_WARNINGS = 1
} omc_ccm_validation_mode;

typedef enum omc_ccm_issue_severity {
    OMC_CCM_WARNING = 0,
    OMC_CCM_ERROR = 1
} omc_ccm_issue_severity;

typedef enum omc_ccm_issue_code {
    OMC_CCM_ISSUE_DECODE_FAILED = 0,
    OMC_CCM_ISSUE_NON_FINITE_VALUE = 1,
    OMC_CCM_ISSUE_UNEXPECTED_COUNT = 2,
    OMC_CCM_ISSUE_MATRIX_COUNT_NOT_DIVISIBLE_BY_3 = 3,
    OMC_CCM_ISSUE_NON_POSITIVE_VALUE = 4,
    OMC_CCM_ISSUE_AS_SHOT_CONFLICT = 5,
    OMC_CCM_ISSUE_MISSING_COMPANION_TAG = 6,
    OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE = 7,
    OMC_CCM_ISSUE_CALIBRATION_SIGNATURE_MISMATCH = 8,
    OMC_CCM_ISSUE_MISSING_ILLUMINANT_DATA = 9,
    OMC_CCM_ISSUE_INVALID_ILLUMINANT_CODE = 10,
    OMC_CCM_ISSUE_WHITE_XY_OUT_OF_RANGE = 11
} omc_ccm_issue_code;

typedef enum omc_ccm_field_kind {
    OMC_CCM_FIELD_NONE = 0,
    OMC_CCM_FIELD_COLOR_MATRIX1 = 1,
    OMC_CCM_FIELD_COLOR_MATRIX2 = 2,
    OMC_CCM_FIELD_COLOR_MATRIX3 = 3,
    OMC_CCM_FIELD_CAMERA_CALIBRATION1 = 4,
    OMC_CCM_FIELD_CAMERA_CALIBRATION2 = 5,
    OMC_CCM_FIELD_CAMERA_CALIBRATION3 = 6,
    OMC_CCM_FIELD_FORWARD_MATRIX1 = 7,
    OMC_CCM_FIELD_FORWARD_MATRIX2 = 8,
    OMC_CCM_FIELD_FORWARD_MATRIX3 = 9,
    OMC_CCM_FIELD_REDUCTION_MATRIX1 = 10,
    OMC_CCM_FIELD_REDUCTION_MATRIX2 = 11,
    OMC_CCM_FIELD_REDUCTION_MATRIX3 = 12,
    OMC_CCM_FIELD_ANALOG_BALANCE = 13,
    OMC_CCM_FIELD_AS_SHOT_NEUTRAL = 14,
    OMC_CCM_FIELD_AS_SHOT_WHITE_XY = 15,
    OMC_CCM_FIELD_CALIBRATION_ILLUMINANT1 = 16,
    OMC_CCM_FIELD_CALIBRATION_ILLUMINANT2 = 17,
    OMC_CCM_FIELD_CALIBRATION_ILLUMINANT3 = 18
} omc_ccm_field_kind;

typedef struct omc_ccm_query_limits {
    omc_u32 max_fields;
    omc_u32 max_values_per_field;
} omc_ccm_query_limits;

typedef struct omc_ccm_query_opts {
    int require_dng_context;
    int include_reduction_matrices;
    omc_ccm_validation_mode validation_mode;
    omc_ccm_query_limits limits;
} omc_ccm_query_opts;

typedef struct omc_ccm_field {
    omc_ccm_field_kind kind;
    omc_byte_ref ifd;
    omc_u16 tag;
    omc_u32 rows;
    omc_u32 cols;
    omc_u32 values_offset;
    omc_u32 values_count;
} omc_ccm_field;

typedef struct omc_ccm_issue {
    omc_ccm_issue_severity severity;
    omc_ccm_issue_code code;
    omc_ccm_field_kind field_kind;
    omc_byte_ref ifd;
    omc_u16 tag;
} omc_ccm_issue;

typedef struct omc_ccm_query_res {
    omc_ccm_query_status status;
    omc_u32 fields_written;
    omc_u32 fields_needed;
    omc_u32 fields_dropped;
    omc_u32 values_written;
    omc_u32 values_needed;
    omc_u32 issues_written;
    omc_u32 issues_needed;
    omc_u32 warning_count;
    omc_u32 error_count;
} omc_ccm_query_res;

OMC_API void
omc_ccm_query_opts_init(omc_ccm_query_opts* opts);

OMC_API const char*
omc_ccm_query_status_name(omc_ccm_query_status status);

OMC_API const char*
omc_ccm_issue_code_name(omc_ccm_issue_code code);

OMC_API const char*
omc_ccm_field_kind_name(omc_ccm_field_kind kind);

OMC_API omc_ccm_query_res
omc_ccm_collect_fields(const omc_store* store,
                       omc_ccm_field* out_fields, omc_u32 field_cap,
                       double* out_values, omc_u32 value_cap,
                       omc_ccm_issue* out_issues, omc_u32 issue_cap,
                       const omc_ccm_query_opts* opts);

OMC_EXTERN_C_END

#endif
