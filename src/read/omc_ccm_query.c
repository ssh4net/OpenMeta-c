#include "omc/omc_ccm_query.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#define OMC_CCM_TAG_CAMERA_CALIBRATION_SIGNATURE 0xC6F3U
#define OMC_CCM_TAG_PROFILE_CALIBRATION_SIGNATURE 0xC6F4U
#define OMC_CCM_TAG_ILLUMINANT_DATA1 0xCD35U
#define OMC_CCM_TAG_ILLUMINANT_DATA2 0xCD36U
#define OMC_CCM_TAG_ILLUMINANT_DATA3 0xCD37U
#define OMC_CCM_DEFAULT_MAX_FIELDS 128U
#define OMC_CCM_DEFAULT_MAX_VALUES_PER_FIELD 256U

typedef struct omc_ccm_tag_info {
    omc_u16 tag;
    omc_ccm_field_kind kind;
    const char* name;
    int is_matrix3xn;
    int is_reduction;
    int is_scalar_illuminant;
} omc_ccm_tag_info;

typedef struct omc_ccm_ifd_state {
    omc_byte_ref ifd_ref;

    int has_analog_balance;
    int has_as_shot_neutral;
    int has_as_shot_white_xy;
    omc_u32 analog_balance_count;
    omc_u32 as_shot_neutral_count;

    int has_cal_illum1;
    int has_cal_illum2;
    int has_cal_illum3;
    omc_s32 cal_illum1;
    omc_s32 cal_illum2;
    omc_s32 cal_illum3;

    int has_color1;
    int has_color2;
    int has_color3;
    omc_u32 color1_count;
    omc_u32 color2_count;
    omc_u32 color3_count;

    int has_forward1;
    int has_forward2;
    int has_forward3;
    omc_u32 forward1_count;
    omc_u32 forward2_count;
    omc_u32 forward3_count;

    int has_reduction1;
    int has_reduction2;
    int has_reduction3;
    omc_u32 reduction1_count;
    omc_u32 reduction2_count;
    omc_u32 reduction3_count;

    int has_cam_cal1;
    int has_cam_cal2;
    int has_cam_cal3;
    omc_u32 cam_cal1_count;
    omc_u32 cam_cal2_count;
    omc_u32 cam_cal3_count;

    int has_illum_data1;
    int has_illum_data2;
    int has_illum_data3;

    int has_camera_cal_sig;
    int has_profile_cal_sig;
    omc_const_bytes camera_cal_sig;
    omc_const_bytes profile_cal_sig;
} omc_ccm_ifd_state;

static const omc_ccm_tag_info omc_ccm_tags[] = {
    { 0xC621U, OMC_CCM_FIELD_COLOR_MATRIX1, "ColorMatrix1", 1, 0, 0 },
    { 0xC622U, OMC_CCM_FIELD_COLOR_MATRIX2, "ColorMatrix2", 1, 0, 0 },
    { 0xCD33U, OMC_CCM_FIELD_COLOR_MATRIX3, "ColorMatrix3", 1, 0, 0 },
    { 0xC623U, OMC_CCM_FIELD_CAMERA_CALIBRATION1, "CameraCalibration1", 1,
      0, 0 },
    { 0xC624U, OMC_CCM_FIELD_CAMERA_CALIBRATION2, "CameraCalibration2", 1,
      0, 0 },
    { 0xCD32U, OMC_CCM_FIELD_CAMERA_CALIBRATION3, "CameraCalibration3", 1,
      0, 0 },
    { 0xC714U, OMC_CCM_FIELD_FORWARD_MATRIX1, "ForwardMatrix1", 1, 0, 0 },
    { 0xC715U, OMC_CCM_FIELD_FORWARD_MATRIX2, "ForwardMatrix2", 1, 0, 0 },
    { 0xCD34U, OMC_CCM_FIELD_FORWARD_MATRIX3, "ForwardMatrix3", 1, 0, 0 },
    { 0xC625U, OMC_CCM_FIELD_REDUCTION_MATRIX1, "ReductionMatrix1", 1, 1,
      0 },
    { 0xC626U, OMC_CCM_FIELD_REDUCTION_MATRIX2, "ReductionMatrix2", 1, 1,
      0 },
    { 0xCD3AU, OMC_CCM_FIELD_REDUCTION_MATRIX3, "ReductionMatrix3", 1, 1,
      0 },
    { 0xC627U, OMC_CCM_FIELD_ANALOG_BALANCE, "AnalogBalance", 0, 0, 0 },
    { 0xC628U, OMC_CCM_FIELD_AS_SHOT_NEUTRAL, "AsShotNeutral", 0, 0, 0 },
    { 0xC629U, OMC_CCM_FIELD_AS_SHOT_WHITE_XY, "AsShotWhiteXY", 0, 0, 0 },
    { 0xC65AU, OMC_CCM_FIELD_CALIBRATION_ILLUMINANT1,
      "CalibrationIlluminant1", 0, 0, 1 },
    { 0xC65BU, OMC_CCM_FIELD_CALIBRATION_ILLUMINANT2,
      "CalibrationIlluminant2", 0, 0, 1 },
    { 0xCD31U, OMC_CCM_FIELD_CALIBRATION_ILLUMINANT3,
      "CalibrationIlluminant3", 0, 0, 1 }
};

static void
omc_ccm_init_res(omc_ccm_query_res* res)
{
    if (res == (omc_ccm_query_res*)0) {
        return;
    }

    memset(res, 0, sizeof(*res));
    res->status = OMC_CCM_QUERY_OK;
}

static void
omc_ccm_set_status_limit(omc_ccm_query_res* res)
{
    if (res == (omc_ccm_query_res*)0) {
        return;
    }
    if (res->status == OMC_CCM_QUERY_OK) {
        res->status = OMC_CCM_QUERY_LIMIT_EXCEEDED;
    }
}

static omc_const_bytes
omc_ccm_view_ref(const omc_store* store, omc_byte_ref ref)
{
    if (store == (const omc_store*)0) {
        omc_const_bytes empty;

        empty.data = (const omc_u8*)0;
        empty.size = 0U;
        return empty;
    }
    return omc_arena_view(&store->arena, ref);
}

static int
omc_ccm_ref_equal(const omc_store* store, omc_byte_ref a, omc_byte_ref b)
{
    omc_const_bytes a_view;
    omc_const_bytes b_view;

    a_view = omc_ccm_view_ref(store, a);
    b_view = omc_ccm_view_ref(store, b);
    if (a_view.size != b_view.size) {
        return 0;
    }
    if (a_view.size == 0U) {
        return 1;
    }
    if (a_view.data == (const omc_u8*)0 || b_view.data == (const omc_u8*)0) {
        return 0;
    }
    return memcmp(a_view.data, b_view.data, (size_t)a_view.size) == 0;
}

static int
omc_ccm_bytes_equal(omc_const_bytes a, omc_const_bytes b)
{
    if (a.size != b.size) {
        return 0;
    }
    if (a.size == 0U) {
        return 1;
    }
    if (a.data == (const omc_u8*)0 || b.data == (const omc_u8*)0) {
        return 0;
    }
    return memcmp(a.data, b.data, (size_t)a.size) == 0;
}

static const omc_ccm_tag_info*
omc_ccm_find_tag(omc_u16 tag)
{
    omc_u32 i;

    for (i = 0U; i < (omc_u32)(sizeof(omc_ccm_tags) / sizeof(omc_ccm_tags[0]));
         ++i) {
        if (omc_ccm_tags[i].tag == tag) {
            return &omc_ccm_tags[i];
        }
    }
    return (const omc_ccm_tag_info*)0;
}

static omc_const_bytes
omc_ccm_trim_text_like(const omc_store* store, const omc_val* value)
{
    omc_const_bytes bytes;
    omc_size n;

    bytes.data = (const omc_u8*)0;
    bytes.size = 0U;

    if (store == (const omc_store*)0 || value == (const omc_val*)0) {
        return bytes;
    }
    if (value->kind != OMC_VAL_TEXT && value->kind != OMC_VAL_BYTES
        && value->kind != OMC_VAL_ARRAY) {
        return bytes;
    }

    bytes = omc_arena_view(&store->arena, value->u.ref);
    if (bytes.data == (const omc_u8*)0) {
        bytes.size = 0U;
        return bytes;
    }

    for (n = 0U; n < bytes.size; ++n) {
        if (bytes.data[n] == 0U) {
            bytes.size = n;
            break;
        }
    }
    return bytes;
}

static int
omc_ccm_read_u16(const omc_u8* data, omc_size size, omc_size off,
                 omc_u16* out)
{
    omc_u16 value;

    if (data == (const omc_u8*)0 || out == (omc_u16*)0
        || off + sizeof(omc_u16) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_i16(const omc_u8* data, omc_size size, omc_size off,
                 omc_s16* out)
{
    omc_s16 value;

    if (data == (const omc_u8*)0 || out == (omc_s16*)0
        || off + sizeof(omc_s16) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_u32(const omc_u8* data, omc_size size, omc_size off,
                 omc_u32* out)
{
    omc_u32 value;

    if (data == (const omc_u8*)0 || out == (omc_u32*)0
        || off + sizeof(omc_u32) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_i32(const omc_u8* data, omc_size size, omc_size off,
                 omc_s32* out)
{
    omc_s32 value;

    if (data == (const omc_u8*)0 || out == (omc_s32*)0
        || off + sizeof(omc_s32) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_u64(const omc_u8* data, omc_size size, omc_size off,
                 omc_u64* out)
{
    omc_u64 value;

    if (data == (const omc_u8*)0 || out == (omc_u64*)0
        || off + sizeof(omc_u64) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_i64(const omc_u8* data, omc_size size, omc_size off,
                 omc_s64* out)
{
    omc_s64 value;

    if (data == (const omc_u8*)0 || out == (omc_s64*)0
        || off + sizeof(omc_s64) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_f32(const omc_u8* data, omc_size size, omc_size off,
                 float* out)
{
    float value;

    if (data == (const omc_u8*)0 || out == (float*)0
        || off + sizeof(float) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_read_f64(const omc_u8* data, omc_size size, omc_size off,
                 double* out)
{
    double value;

    if (data == (const omc_u8*)0 || out == (double*)0
        || off + sizeof(double) > size) {
        return 0;
    }
    memcpy(&value, data + off, sizeof(value));
    *out = value;
    return 1;
}

static int
omc_ccm_append_scalar(const omc_val* value, double* out_values,
                      omc_u32* out_count)
{
    float f32;
    double f64;

    if (value == (const omc_val*)0 || out_values == (double*)0
        || out_count == (omc_u32*)0) {
        return 0;
    }

    switch (value->elem_type) {
    case OMC_ELEM_U8:
    case OMC_ELEM_U16:
    case OMC_ELEM_U32:
    case OMC_ELEM_U64:
        out_values[0] = (double)value->u.u64;
        *out_count = 1U;
        return 1;
    case OMC_ELEM_I8:
    case OMC_ELEM_I16:
    case OMC_ELEM_I32:
    case OMC_ELEM_I64:
        out_values[0] = (double)value->u.i64;
        *out_count = 1U;
        return 1;
    case OMC_ELEM_F32_BITS:
        memcpy(&f32, &value->u.f32_bits, sizeof(f32));
        out_values[0] = (double)f32;
        *out_count = 1U;
        return 1;
    case OMC_ELEM_F64_BITS:
        memcpy(&f64, &value->u.f64_bits, sizeof(f64));
        out_values[0] = f64;
        *out_count = 1U;
        return 1;
    case OMC_ELEM_URATIONAL:
        if (value->u.ur.denom == 0U) {
            return 0;
        }
        out_values[0] = (double)value->u.ur.numer / (double)value->u.ur.denom;
        *out_count = 1U;
        return 1;
    case OMC_ELEM_SRATIONAL:
        if (value->u.sr.denom == 0) {
            return 0;
        }
        out_values[0] = (double)value->u.sr.numer / (double)value->u.sr.denom;
        *out_count = 1U;
        return 1;
    }

    return 0;
}

static int
omc_ccm_decode_values(const omc_store* store, const omc_val* value,
                      omc_u32 max_values, double* out_values,
                      omc_u32* out_count, int* out_value_limit_exceeded)
{
    omc_const_bytes raw;
    omc_u32 count;
    omc_u32 limit;
    omc_u32 i;

    if (out_count != (omc_u32*)0) {
        *out_count = 0U;
    }
    if (out_value_limit_exceeded != (int*)0) {
        *out_value_limit_exceeded = 0;
    }
    if (store == (const omc_store*)0 || value == (const omc_val*)0
        || out_values == (double*)0 || out_count == (omc_u32*)0) {
        return 0;
    }

    if (value->kind == OMC_VAL_SCALAR) {
        return omc_ccm_append_scalar(value, out_values, out_count);
    }
    if (value->kind != OMC_VAL_ARRAY) {
        return 0;
    }

    raw = omc_arena_view(&store->arena, value->u.ref);
    count = value->count;
    limit = count;
    if (max_values != 0U && limit > max_values) {
        limit = max_values;
        if (out_value_limit_exceeded != (int*)0) {
            *out_value_limit_exceeded = 1;
        }
    }

    for (i = 0U; i < limit; ++i) {
        omc_size off;
        omc_u16 u16v;
        omc_s16 i16v;
        omc_u32 u32v;
        omc_s32 i32v;
        omc_u64 u64v;
        omc_s64 i64v;
        float f32v;
        double f64v;
        omc_urational ur;
        omc_srational sr;

        switch (value->elem_type) {
        case OMC_ELEM_U8:
            if ((omc_size)i >= raw.size || raw.data == (const omc_u8*)0) {
                return 0;
            }
            out_values[i] = (double)raw.data[i];
            break;
        case OMC_ELEM_I8:
            if ((omc_size)i >= raw.size || raw.data == (const omc_u8*)0) {
                return 0;
            }
            out_values[i] = (double)(signed char)raw.data[i];
            break;
        case OMC_ELEM_U16:
            off = (omc_size)i * sizeof(omc_u16);
            if (!omc_ccm_read_u16(raw.data, raw.size, off, &u16v)) {
                return 0;
            }
            out_values[i] = (double)u16v;
            break;
        case OMC_ELEM_I16:
            off = (omc_size)i * sizeof(omc_s16);
            if (!omc_ccm_read_i16(raw.data, raw.size, off, &i16v)) {
                return 0;
            }
            out_values[i] = (double)i16v;
            break;
        case OMC_ELEM_U32:
            off = (omc_size)i * sizeof(omc_u32);
            if (!omc_ccm_read_u32(raw.data, raw.size, off, &u32v)) {
                return 0;
            }
            out_values[i] = (double)u32v;
            break;
        case OMC_ELEM_I32:
            off = (omc_size)i * sizeof(omc_s32);
            if (!omc_ccm_read_i32(raw.data, raw.size, off, &i32v)) {
                return 0;
            }
            out_values[i] = (double)i32v;
            break;
        case OMC_ELEM_U64:
            off = (omc_size)i * sizeof(omc_u64);
            if (!omc_ccm_read_u64(raw.data, raw.size, off, &u64v)) {
                return 0;
            }
            out_values[i] = (double)u64v;
            break;
        case OMC_ELEM_I64:
            off = (omc_size)i * sizeof(omc_s64);
            if (!omc_ccm_read_i64(raw.data, raw.size, off, &i64v)) {
                return 0;
            }
            out_values[i] = (double)i64v;
            break;
        case OMC_ELEM_F32_BITS:
            off = (omc_size)i * sizeof(float);
            if (!omc_ccm_read_f32(raw.data, raw.size, off, &f32v)) {
                return 0;
            }
            out_values[i] = (double)f32v;
            break;
        case OMC_ELEM_F64_BITS:
            off = (omc_size)i * sizeof(double);
            if (!omc_ccm_read_f64(raw.data, raw.size, off, &f64v)) {
                return 0;
            }
            out_values[i] = f64v;
            break;
        case OMC_ELEM_URATIONAL:
            off = (omc_size)i * sizeof(omc_urational);
            if (raw.data == (const omc_u8*)0 || off + sizeof(ur) > raw.size) {
                return 0;
            }
            memcpy(&ur, raw.data + off, sizeof(ur));
            if (ur.denom == 0U) {
                return 0;
            }
            out_values[i] = (double)ur.numer / (double)ur.denom;
            break;
        case OMC_ELEM_SRATIONAL:
            off = (omc_size)i * sizeof(omc_srational);
            if (raw.data == (const omc_u8*)0 || off + sizeof(sr) > raw.size) {
                return 0;
            }
            memcpy(&sr, raw.data + off, sizeof(sr));
            if (sr.denom == 0) {
                return 0;
            }
            out_values[i] = (double)sr.numer / (double)sr.denom;
            break;
        }
    }

    *out_count = limit;
    return 1;
}

static int
omc_ccm_is_finite(double value)
{
    if (value != value) {
        return 0;
    }
    if (value > DBL_MAX || value < -DBL_MAX) {
        return 0;
    }
    return 1;
}

static double
omc_ccm_abs_double(double value)
{
    if (value < 0.0) {
        return -value;
    }
    return value;
}

static omc_s64
omc_ccm_round_nearest(double value)
{
    if (value >= 0.0) {
        return (omc_s64)(value + 0.5);
    }
    return (omc_s64)(value - 0.5);
}

static int
omc_ccm_is_matrix_tag(omc_u16 tag)
{
    switch (tag) {
    case 0xC621U:
    case 0xC622U:
    case 0xCD33U:
    case 0xC623U:
    case 0xC624U:
    case 0xCD32U:
    case 0xC714U:
    case 0xC715U:
    case 0xCD34U:
    case 0xC625U:
    case 0xC626U:
    case 0xCD3AU: return 1;
    }
    return 0;
}

static int
omc_ccm_is_valid_calibration_illuminant(omc_s32 value)
{
    switch (value) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 255: return 1;
    }
    return 0;
}

static void
omc_ccm_infer_shape(const omc_ccm_tag_info* info, omc_u32 value_count,
                    omc_u32* out_rows, omc_u32* out_cols)
{
    if (out_rows == (omc_u32*)0 || out_cols == (omc_u32*)0) {
        return;
    }

    *out_rows = 1U;
    *out_cols = value_count;
    if (value_count == 0U || info == (const omc_ccm_tag_info*)0) {
        return;
    }
    if (info->is_scalar_illuminant) {
        *out_rows = 1U;
        *out_cols = 1U;
        return;
    }
    if (info->is_matrix3xn && (value_count % 3U) == 0U) {
        *out_rows = 3U;
        *out_cols = value_count / 3U;
    }
}

static void
omc_ccm_add_issue(omc_ccm_query_res* res, omc_ccm_issue* out_issues,
                  omc_u32 issue_cap, omc_ccm_issue_severity severity,
                  omc_ccm_issue_code code, omc_byte_ref ifd,
                  omc_ccm_field_kind kind, omc_u16 tag)
{
    omc_u32 index;

    if (res == (omc_ccm_query_res*)0) {
        return;
    }

    index = res->issues_needed;
    res->issues_needed += 1U;
    if (severity == OMC_CCM_ERROR) {
        res->error_count += 1U;
    } else {
        res->warning_count += 1U;
    }

    if (out_issues != (omc_ccm_issue*)0 && index < issue_cap) {
        out_issues[index].severity = severity;
        out_issues[index].code = code;
        out_issues[index].field_kind = kind;
        out_issues[index].ifd = ifd;
        out_issues[index].tag = tag;
        res->issues_written = index + 1U;
    }
}

static int
omc_ccm_validate_values(const omc_ccm_tag_info* info, omc_byte_ref ifd_ref,
                        omc_u32 value_count, const double* values,
                        omc_ccm_validation_mode validation_mode,
                        omc_ccm_issue* out_issues, omc_u32 issue_cap,
                        omc_ccm_query_res* res)
{
    omc_u32 i;

    if (info == (const omc_ccm_tag_info*)0 || values == (const double*)0
        || res == (omc_ccm_query_res*)0) {
        return 0;
    }

    for (i = 0U; i < value_count; ++i) {
        if (!omc_ccm_is_finite(values[i])) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_ERROR,
                              OMC_CCM_ISSUE_NON_FINITE_VALUE, ifd_ref,
                              info->kind, info->tag);
            return 0;
        }
    }

    if (validation_mode == OMC_CCM_VALIDATE_NONE) {
        return 1;
    }

    if (omc_ccm_is_matrix_tag(info->tag) && (value_count % 3U) != 0U) {
        omc_ccm_add_issue(
            res, out_issues, issue_cap, OMC_CCM_WARNING,
            OMC_CCM_ISSUE_MATRIX_COUNT_NOT_DIVISIBLE_BY_3, ifd_ref,
            info->kind, info->tag);
    }
    if (omc_ccm_is_matrix_tag(info->tag) && value_count > 36U) {
        omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                          OMC_CCM_ISSUE_UNEXPECTED_COUNT, ifd_ref, info->kind,
                          info->tag);
    }

    if (info->tag == 0xC629U && value_count != 2U) {
        omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                          OMC_CCM_ISSUE_UNEXPECTED_COUNT, ifd_ref, info->kind,
                          info->tag);
    } else if (info->tag == 0xC629U && value_count == 2U) {
        double x;
        double y;

        x = values[0];
        y = values[1];
        if (!(x > 0.0 && x < 1.0 && y > 0.0 && y < 1.0 && (x + y) <= 1.0)) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_WHITE_XY_OUT_OF_RANGE, ifd_ref,
                              info->kind, info->tag);
        }
    }

    if (info->is_scalar_illuminant && value_count != 1U) {
        omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                          OMC_CCM_ISSUE_UNEXPECTED_COUNT, ifd_ref, info->kind,
                          info->tag);
    } else if (info->is_scalar_illuminant && value_count == 1U) {
        omc_s64 rounded;
        omc_s32 code;

        rounded = omc_ccm_round_nearest(values[0]);
        code = (omc_s32)rounded;
        if (omc_ccm_abs_double(values[0] - (double)rounded) > 1e-9
            || !omc_ccm_is_valid_calibration_illuminant(code)) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_INVALID_ILLUMINANT_CODE, ifd_ref, info->kind,
                info->tag);
        }
    }

    if ((info->tag == 0xC627U || info->tag == 0xC628U) && value_count < 3U) {
        omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                          OMC_CCM_ISSUE_UNEXPECTED_COUNT, ifd_ref, info->kind,
                          info->tag);
    }

    if (info->tag == 0xC627U || info->tag == 0xC628U) {
        for (i = 0U; i < value_count; ++i) {
            if (values[i] <= 0.0) {
                omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                                  OMC_CCM_ISSUE_NON_POSITIVE_VALUE, ifd_ref,
                                  info->kind, info->tag);
                break;
            }
        }
    }

    return 1;
}

static omc_ccm_ifd_state*
omc_ccm_find_or_add_ifd_state(const omc_store* store,
                              omc_ccm_ifd_state* states, omc_u32* io_count,
                              omc_u32 capacity, omc_byte_ref ifd_ref)
{
    omc_u32 i;

    if (store == (const omc_store*)0 || states == (omc_ccm_ifd_state*)0
        || io_count == (omc_u32*)0) {
        return (omc_ccm_ifd_state*)0;
    }

    for (i = 0U; i < *io_count; ++i) {
        if (omc_ccm_ref_equal(store, states[i].ifd_ref, ifd_ref)) {
            return states + i;
        }
    }
    if (*io_count >= capacity) {
        return (omc_ccm_ifd_state*)0;
    }
    memset(states + *io_count, 0, sizeof(states[0]));
    states[*io_count].ifd_ref = ifd_ref;
    states[*io_count].cal_illum1 = -1;
    states[*io_count].cal_illum2 = -1;
    states[*io_count].cal_illum3 = -1;
    *io_count += 1U;
    return states + (*io_count - 1U);
}

static int
omc_ccm_has_dng_ifd(const omc_store* store, const omc_byte_ref* refs,
                    omc_u32 count, omc_byte_ref ifd_ref)
{
    omc_u32 i;

    if (store == (const omc_store*)0 || refs == (const omc_byte_ref*)0) {
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (omc_ccm_ref_equal(store, refs[i], ifd_ref)) {
            return 1;
        }
    }
    return 0;
}

static void
omc_ccm_mark_field_presence(omc_ccm_ifd_state* state, omc_u16 tag,
                            omc_u32 value_count, const double* values)
{
    if (state == (omc_ccm_ifd_state*)0) {
        return;
    }

    switch (tag) {
    case 0xC621U:
        state->has_color1 = 1;
        state->color1_count = value_count;
        break;
    case 0xC622U:
        state->has_color2 = 1;
        state->color2_count = value_count;
        break;
    case 0xCD33U:
        state->has_color3 = 1;
        state->color3_count = value_count;
        break;
    case 0xC714U:
        state->has_forward1 = 1;
        state->forward1_count = value_count;
        break;
    case 0xC715U:
        state->has_forward2 = 1;
        state->forward2_count = value_count;
        break;
    case 0xCD34U:
        state->has_forward3 = 1;
        state->forward3_count = value_count;
        break;
    case 0xC625U:
        state->has_reduction1 = 1;
        state->reduction1_count = value_count;
        break;
    case 0xC626U:
        state->has_reduction2 = 1;
        state->reduction2_count = value_count;
        break;
    case 0xCD3AU:
        state->has_reduction3 = 1;
        state->reduction3_count = value_count;
        break;
    case 0xC623U:
        state->has_cam_cal1 = 1;
        state->cam_cal1_count = value_count;
        break;
    case 0xC624U:
        state->has_cam_cal2 = 1;
        state->cam_cal2_count = value_count;
        break;
    case 0xCD32U:
        state->has_cam_cal3 = 1;
        state->cam_cal3_count = value_count;
        break;
    case 0xC627U:
        state->has_analog_balance = 1;
        state->analog_balance_count = value_count;
        break;
    case 0xC628U:
        state->has_as_shot_neutral = 1;
        state->as_shot_neutral_count = value_count;
        break;
    case 0xC629U:
        state->has_as_shot_white_xy = 1;
        break;
    case 0xC65AU:
        state->has_cal_illum1 = 1;
        if (values != (const double*)0 && value_count != 0U) {
            state->cal_illum1 = (omc_s32)omc_ccm_round_nearest(values[0]);
        }
        break;
    case 0xC65BU:
        state->has_cal_illum2 = 1;
        if (values != (const double*)0 && value_count != 0U) {
            state->cal_illum2 = (omc_s32)omc_ccm_round_nearest(values[0]);
        }
        break;
    case 0xCD31U:
        state->has_cal_illum3 = 1;
        if (values != (const double*)0 && value_count != 0U) {
            state->cal_illum3 = (omc_s32)omc_ccm_round_nearest(values[0]);
        }
        break;
    }
}

static void
omc_ccm_run_cross_field_validation(const omc_ccm_ifd_state* states,
                                   omc_u32 state_count,
                                   omc_ccm_issue* out_issues,
                                   omc_u32 issue_cap,
                                   omc_ccm_query_res* res)
{
    omc_u32 i;

    if (states == (const omc_ccm_ifd_state*)0 || res == (omc_ccm_query_res*)0) {
        return;
    }

    for (i = 0U; i < state_count; ++i) {
        const omc_ccm_ifd_state* s;

        s = states + i;
        if (s->has_as_shot_neutral && s->has_as_shot_white_xy) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_AS_SHOT_CONFLICT, s->ifd_ref,
                              OMC_CCM_FIELD_NONE, 0U);
        }
        if (s->has_analog_balance && s->has_as_shot_neutral
            && s->analog_balance_count != s->as_shot_neutral_count) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                OMC_CCM_FIELD_ANALOG_BALANCE, 0xC627U);
        }
        if (s->has_color1 && (s->color1_count % 3U) == 0U) {
            omc_u32 channels;

            channels = s->color1_count / 3U;
            if (s->has_as_shot_neutral
                && s->as_shot_neutral_count != channels) {
                omc_ccm_add_issue(
                    res, out_issues, issue_cap, OMC_CCM_WARNING,
                    OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                    OMC_CCM_FIELD_AS_SHOT_NEUTRAL, 0xC628U);
            }
            if (s->has_analog_balance
                && s->analog_balance_count != channels) {
                omc_ccm_add_issue(
                    res, out_issues, issue_cap, OMC_CCM_WARNING,
                    OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                    OMC_CCM_FIELD_ANALOG_BALANCE, 0xC627U);
            }
        }

        if (s->has_color1 && s->has_color2
            && s->color1_count != s->color2_count) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                              OMC_CCM_FIELD_COLOR_MATRIX2, 0xC622U);
        }
        if (s->has_color3 && s->has_color1
            && s->color3_count != s->color1_count) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                              OMC_CCM_FIELD_COLOR_MATRIX3, 0xCD33U);
        }
        if (s->has_cam_cal1 && s->has_cam_cal2
            && s->cam_cal1_count != s->cam_cal2_count) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                OMC_CCM_FIELD_CAMERA_CALIBRATION2, 0xC624U);
        }
        if (s->has_cam_cal3 && s->has_cam_cal1
            && s->cam_cal3_count != s->cam_cal1_count) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                OMC_CCM_FIELD_CAMERA_CALIBRATION3, 0xCD32U);
        }
        if (s->has_forward1 && s->has_forward2
            && s->forward1_count != s->forward2_count) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                              OMC_CCM_FIELD_FORWARD_MATRIX2, 0xC715U);
        }
        if (s->has_forward3 && s->has_forward1
            && s->forward3_count != s->forward1_count) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                              OMC_CCM_FIELD_FORWARD_MATRIX3, 0xCD34U);
        }
        if (s->has_reduction1 && s->has_reduction2
            && s->reduction1_count != s->reduction2_count) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                OMC_CCM_FIELD_REDUCTION_MATRIX2, 0xC626U);
        }
        if (s->has_reduction3 && s->has_reduction1
            && s->reduction3_count != s->reduction1_count) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_UNEXPECTED_COUNT, s->ifd_ref,
                OMC_CCM_FIELD_REDUCTION_MATRIX3, 0xCD3AU);
        }

        if (s->has_cal_illum1 && !s->has_color1) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_MISSING_COMPANION_TAG, s->ifd_ref,
                OMC_CCM_FIELD_CALIBRATION_ILLUMINANT1, 0xC65AU);
        }
        if (!s->has_cal_illum1 && s->has_color1) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_MISSING_COMPANION_TAG, s->ifd_ref,
                              OMC_CCM_FIELD_COLOR_MATRIX1, 0xC621U);
        }
        if (s->has_cal_illum2 && !s->has_color2) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_MISSING_COMPANION_TAG, s->ifd_ref,
                OMC_CCM_FIELD_CALIBRATION_ILLUMINANT2, 0xC65BU);
        }
        if (!s->has_cal_illum2 && s->has_color2) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_MISSING_COMPANION_TAG, s->ifd_ref,
                              OMC_CCM_FIELD_COLOR_MATRIX2, 0xC622U);
        }

        if (s->has_cal_illum3) {
            int any_forward;
            int any_reduction;

            if (!s->has_cal_illum1 || !s->has_cal_illum2) {
                omc_ccm_add_issue(
                    res, out_issues, issue_cap, OMC_CCM_WARNING,
                    OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE, s->ifd_ref,
                    OMC_CCM_FIELD_CALIBRATION_ILLUMINANT3, 0xCD31U);
            }
            if (!s->has_color3) {
                omc_ccm_add_issue(
                    res, out_issues, issue_cap, OMC_CCM_WARNING,
                    OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE, s->ifd_ref,
                    OMC_CCM_FIELD_COLOR_MATRIX3, 0xCD33U);
            }
            any_forward = s->has_forward1 || s->has_forward2
                          || s->has_forward3;
            if (any_forward
                && !(s->has_forward1 && s->has_forward2
                     && s->has_forward3)) {
                omc_ccm_add_issue(
                    res, out_issues, issue_cap, OMC_CCM_WARNING,
                    OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE, s->ifd_ref,
                    OMC_CCM_FIELD_NONE, 0xCD34U);
            }
            any_reduction = s->has_reduction1 || s->has_reduction2
                            || s->has_reduction3;
            if (any_reduction
                && !(s->has_reduction1 && s->has_reduction2
                     && s->has_reduction3)) {
                omc_ccm_add_issue(
                    res, out_issues, issue_cap, OMC_CCM_WARNING,
                    OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE, s->ifd_ref,
                    OMC_CCM_FIELD_NONE, 0xCD3AU);
            }
        } else if (s->has_color3 || s->has_forward3 || s->has_reduction3) {
            omc_ccm_add_issue(res, out_issues, issue_cap, OMC_CCM_WARNING,
                              OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE, s->ifd_ref,
                              OMC_CCM_FIELD_NONE, 0U);
        }

        if (s->has_cal_illum1 && s->cal_illum1 == 255
            && !s->has_illum_data1) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_MISSING_ILLUMINANT_DATA, s->ifd_ref,
                OMC_CCM_FIELD_NONE, OMC_CCM_TAG_ILLUMINANT_DATA1);
        }
        if (s->has_cal_illum2 && s->cal_illum2 == 255
            && !s->has_illum_data2) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_MISSING_ILLUMINANT_DATA, s->ifd_ref,
                OMC_CCM_FIELD_NONE, OMC_CCM_TAG_ILLUMINANT_DATA2);
        }
        if (s->has_cal_illum3 && s->cal_illum3 == 255
            && !s->has_illum_data3) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_MISSING_ILLUMINANT_DATA, s->ifd_ref,
                OMC_CCM_FIELD_NONE, OMC_CCM_TAG_ILLUMINANT_DATA3);
        }

        if ((s->has_cam_cal1 || s->has_cam_cal2 || s->has_cam_cal3)
            && s->has_camera_cal_sig && s->has_profile_cal_sig
            && !omc_ccm_bytes_equal(s->camera_cal_sig,
                                    s->profile_cal_sig)) {
            omc_ccm_add_issue(
                res, out_issues, issue_cap, OMC_CCM_WARNING,
                OMC_CCM_ISSUE_CALIBRATION_SIGNATURE_MISMATCH, s->ifd_ref,
                OMC_CCM_FIELD_NONE,
                OMC_CCM_TAG_CAMERA_CALIBRATION_SIGNATURE);
        }
    }
}

void
omc_ccm_query_opts_init(omc_ccm_query_opts* opts)
{
    if (opts == (omc_ccm_query_opts*)0) {
        return;
    }

    memset(opts, 0, sizeof(*opts));
    opts->require_dng_context = 1;
    opts->include_reduction_matrices = 1;
    opts->validation_mode = OMC_CCM_VALIDATE_DNG_SPEC_WARNINGS;
    opts->limits.max_fields = OMC_CCM_DEFAULT_MAX_FIELDS;
    opts->limits.max_values_per_field = OMC_CCM_DEFAULT_MAX_VALUES_PER_FIELD;
}

const char*
omc_ccm_query_status_name(omc_ccm_query_status status)
{
    switch (status) {
    case OMC_CCM_QUERY_OK: return "ok";
    case OMC_CCM_QUERY_LIMIT_EXCEEDED: return "limit_exceeded";
    case OMC_CCM_QUERY_INVALID_ARGUMENT: return "invalid_argument";
    case OMC_CCM_QUERY_NOMEM: return "no_memory";
    }
    return "unknown";
}

const char*
omc_ccm_issue_code_name(omc_ccm_issue_code code)
{
    switch (code) {
    case OMC_CCM_ISSUE_DECODE_FAILED: return "decode_failed";
    case OMC_CCM_ISSUE_NON_FINITE_VALUE: return "non_finite_value";
    case OMC_CCM_ISSUE_UNEXPECTED_COUNT: return "unexpected_count";
    case OMC_CCM_ISSUE_MATRIX_COUNT_NOT_DIVISIBLE_BY_3:
        return "matrix_count_not_divisible_by_3";
    case OMC_CCM_ISSUE_NON_POSITIVE_VALUE: return "non_positive_value";
    case OMC_CCM_ISSUE_AS_SHOT_CONFLICT: return "as_shot_conflict";
    case OMC_CCM_ISSUE_MISSING_COMPANION_TAG:
        return "missing_companion_tag";
    case OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE:
        return "triple_illuminant_rule";
    case OMC_CCM_ISSUE_CALIBRATION_SIGNATURE_MISMATCH:
        return "calibration_signature_mismatch";
    case OMC_CCM_ISSUE_MISSING_ILLUMINANT_DATA:
        return "missing_illuminant_data";
    case OMC_CCM_ISSUE_INVALID_ILLUMINANT_CODE:
        return "invalid_illuminant_code";
    case OMC_CCM_ISSUE_WHITE_XY_OUT_OF_RANGE:
        return "white_xy_out_of_range";
    }
    return "unknown";
}

const char*
omc_ccm_field_kind_name(omc_ccm_field_kind kind)
{
    switch (kind) {
    case OMC_CCM_FIELD_NONE: return "none";
    case OMC_CCM_FIELD_COLOR_MATRIX1: return "ColorMatrix1";
    case OMC_CCM_FIELD_COLOR_MATRIX2: return "ColorMatrix2";
    case OMC_CCM_FIELD_COLOR_MATRIX3: return "ColorMatrix3";
    case OMC_CCM_FIELD_CAMERA_CALIBRATION1: return "CameraCalibration1";
    case OMC_CCM_FIELD_CAMERA_CALIBRATION2: return "CameraCalibration2";
    case OMC_CCM_FIELD_CAMERA_CALIBRATION3: return "CameraCalibration3";
    case OMC_CCM_FIELD_FORWARD_MATRIX1: return "ForwardMatrix1";
    case OMC_CCM_FIELD_FORWARD_MATRIX2: return "ForwardMatrix2";
    case OMC_CCM_FIELD_FORWARD_MATRIX3: return "ForwardMatrix3";
    case OMC_CCM_FIELD_REDUCTION_MATRIX1: return "ReductionMatrix1";
    case OMC_CCM_FIELD_REDUCTION_MATRIX2: return "ReductionMatrix2";
    case OMC_CCM_FIELD_REDUCTION_MATRIX3: return "ReductionMatrix3";
    case OMC_CCM_FIELD_ANALOG_BALANCE: return "AnalogBalance";
    case OMC_CCM_FIELD_AS_SHOT_NEUTRAL: return "AsShotNeutral";
    case OMC_CCM_FIELD_AS_SHOT_WHITE_XY: return "AsShotWhiteXY";
    case OMC_CCM_FIELD_CALIBRATION_ILLUMINANT1:
        return "CalibrationIlluminant1";
    case OMC_CCM_FIELD_CALIBRATION_ILLUMINANT2:
        return "CalibrationIlluminant2";
    case OMC_CCM_FIELD_CALIBRATION_ILLUMINANT3:
        return "CalibrationIlluminant3";
    }
    return "unknown";
}

omc_ccm_query_res
omc_ccm_collect_fields(const omc_store* store,
                       omc_ccm_field* out_fields, omc_u32 field_cap,
                       double* out_values, omc_u32 value_cap,
                       omc_ccm_issue* out_issues, omc_u32 issue_cap,
                       const omc_ccm_query_opts* opts)
{
    omc_ccm_query_res res;
    omc_ccm_query_opts local_opts;
    const omc_ccm_query_opts* use_opts;
    omc_byte_ref* dng_ifds;
    omc_u32 dng_ifd_count;
    omc_ccm_ifd_state* states;
    omc_u32 state_count;
    omc_size entry_cap;
    omc_size i;

    omc_ccm_init_res(&res);

    if (store == (const omc_store*)0) {
        res.status = OMC_CCM_QUERY_INVALID_ARGUMENT;
        return res;
    }

    if (opts == (const omc_ccm_query_opts*)0) {
        omc_ccm_query_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        local_opts = *opts;
        use_opts = &local_opts;
    }

    entry_cap = store->entry_count;
    dng_ifds = (omc_byte_ref*)0;
    states = (omc_ccm_ifd_state*)0;
    dng_ifd_count = 0U;
    state_count = 0U;

    if (entry_cap != 0U) {
        dng_ifds = (omc_byte_ref*)malloc((size_t)entry_cap * sizeof(*dng_ifds));
        states = (omc_ccm_ifd_state*)malloc((size_t)entry_cap * sizeof(*states));
        if (dng_ifds == (omc_byte_ref*)0 || states == (omc_ccm_ifd_state*)0) {
            free(dng_ifds);
            free(states);
            res.status = OMC_CCM_QUERY_NOMEM;
            return res;
        }
    }

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_byte_ref ifd_ref;

        entry = store->entries + i;
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U) {
            continue;
        }
        if (entry->key.kind != OMC_KEY_EXIF_TAG || entry->key.u.exif_tag.tag != 0xC612U) {
            continue;
        }
        ifd_ref = entry->key.u.exif_tag.ifd;
        if (!omc_ccm_has_dng_ifd(store, dng_ifds, dng_ifd_count, ifd_ref)) {
            dng_ifds[dng_ifd_count++] = ifd_ref;
        }
    }

    if (use_opts->require_dng_context && dng_ifd_count == 0U) {
        free(dng_ifds);
        free(states);
        return res;
    }

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        omc_byte_ref ifd_ref;
        omc_ccm_ifd_state* state;

        entry = store->entries + i;
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }
        ifd_ref = entry->key.u.exif_tag.ifd;
        if (use_opts->require_dng_context
            && !omc_ccm_has_dng_ifd(store, dng_ifds, dng_ifd_count, ifd_ref)) {
            continue;
        }
        state = omc_ccm_find_or_add_ifd_state(store, states, &state_count,
                                              (omc_u32)entry_cap, ifd_ref);
        if (state == (omc_ccm_ifd_state*)0) {
            res.status = OMC_CCM_QUERY_NOMEM;
            free(dng_ifds);
            free(states);
            return res;
        }

        if (entry->key.u.exif_tag.tag == OMC_CCM_TAG_ILLUMINANT_DATA1) {
            state->has_illum_data1 = 1;
        } else if (entry->key.u.exif_tag.tag == OMC_CCM_TAG_ILLUMINANT_DATA2) {
            state->has_illum_data2 = 1;
        } else if (entry->key.u.exif_tag.tag == OMC_CCM_TAG_ILLUMINANT_DATA3) {
            state->has_illum_data3 = 1;
        } else if (entry->key.u.exif_tag.tag
                   == OMC_CCM_TAG_CAMERA_CALIBRATION_SIGNATURE) {
            state->camera_cal_sig = omc_ccm_trim_text_like(store, &entry->value);
            state->has_camera_cal_sig = 1;
        } else if (entry->key.u.exif_tag.tag
                   == OMC_CCM_TAG_PROFILE_CALIBRATION_SIGNATURE) {
            state->profile_cal_sig = omc_ccm_trim_text_like(store, &entry->value);
            state->has_profile_cal_sig = 1;
        }
    }

    for (i = 0U; i < store->entry_count; ++i) {
        const omc_entry* entry;
        const omc_ccm_tag_info* info;
        omc_byte_ref ifd_ref;
        omc_u32 temp_cap;
        double* temp_values;
        omc_u32 temp_count;
        int values_limit_exceeded;
        omc_ccm_ifd_state* state;
        int write_field;

        entry = store->entries + i;
        if ((entry->flags & OMC_ENTRY_FLAG_DELETED) != 0U
            || entry->key.kind != OMC_KEY_EXIF_TAG) {
            continue;
        }

        info = omc_ccm_find_tag(entry->key.u.exif_tag.tag);
        if (info == (const omc_ccm_tag_info*)0) {
            continue;
        }
        if (!use_opts->include_reduction_matrices && info->is_reduction) {
            continue;
        }

        ifd_ref = entry->key.u.exif_tag.ifd;
        if (use_opts->require_dng_context
            && !omc_ccm_has_dng_ifd(store, dng_ifds, dng_ifd_count, ifd_ref)) {
            continue;
        }

        if (use_opts->limits.max_fields != 0U
            && res.fields_needed >= use_opts->limits.max_fields) {
            omc_ccm_set_status_limit(&res);
            break;
        }

        temp_cap = entry->value.count;
        if (entry->value.kind == OMC_VAL_SCALAR) {
            temp_cap = 1U;
        }
        if (use_opts->limits.max_values_per_field != 0U
            && temp_cap > use_opts->limits.max_values_per_field) {
            temp_cap = use_opts->limits.max_values_per_field;
        }
        if (temp_cap == 0U) {
            continue;
        }
        if ((omc_size)temp_cap > ((omc_size)(~(omc_size)0) / sizeof(double))) {
            res.status = OMC_CCM_QUERY_NOMEM;
            free(dng_ifds);
            free(states);
            return res;
        }
        temp_values = (double*)malloc((size_t)temp_cap * sizeof(double));
        if (temp_values == (double*)0) {
            res.status = OMC_CCM_QUERY_NOMEM;
            free(dng_ifds);
            free(states);
            return res;
        }

        temp_count = 0U;
        values_limit_exceeded = 0;
        if (!omc_ccm_decode_values(store, &entry->value,
                                   use_opts->limits.max_values_per_field,
                                   temp_values, &temp_count,
                                   &values_limit_exceeded)) {
            if (use_opts->validation_mode != OMC_CCM_VALIDATE_NONE) {
                omc_ccm_add_issue(&res, out_issues, issue_cap, OMC_CCM_WARNING,
                                  OMC_CCM_ISSUE_DECODE_FAILED, ifd_ref,
                                  info->kind, info->tag);
            }
            free(temp_values);
            continue;
        }
        if (temp_count == 0U) {
            free(temp_values);
            continue;
        }
        if (values_limit_exceeded) {
            omc_ccm_set_status_limit(&res);
        }

        if (!omc_ccm_validate_values(info, ifd_ref, temp_count, temp_values,
                                     use_opts->validation_mode, out_issues,
                                     issue_cap, &res)) {
            res.fields_dropped += 1U;
            free(temp_values);
            continue;
        }

        state = (omc_ccm_ifd_state*)0;
        if (use_opts->validation_mode != OMC_CCM_VALIDATE_NONE) {
            state = omc_ccm_find_or_add_ifd_state(store, states, &state_count,
                                                  (omc_u32)entry_cap, ifd_ref);
            if (state == (omc_ccm_ifd_state*)0) {
                free(temp_values);
                res.status = OMC_CCM_QUERY_NOMEM;
                free(dng_ifds);
                free(states);
                return res;
            }
            omc_ccm_mark_field_presence(state, info->tag, temp_count,
                                        temp_values);
        }

        res.fields_needed += 1U;
        res.values_needed += temp_count;
        write_field = 0;
        if (out_fields != (omc_ccm_field*)0 && out_values != (double*)0
            && field_cap != 0U && value_cap != 0U
            && res.fields_written < field_cap
            && res.values_written <= value_cap
            && temp_count <= value_cap - res.values_written) {
            write_field = 1;
        }

        if (write_field) {
            omc_ccm_field* field;
            omc_u32 values_offset;

            field = out_fields + res.fields_written;
            values_offset = res.values_written;
            memset(field, 0, sizeof(*field));
            field->kind = info->kind;
            field->ifd = ifd_ref;
            field->tag = info->tag;
            field->values_offset = values_offset;
            field->values_count = temp_count;
            omc_ccm_infer_shape(info, temp_count, &field->rows, &field->cols);
            memcpy(out_values + values_offset, temp_values,
                   (size_t)temp_count * sizeof(double));
            res.values_written += temp_count;
            res.fields_written += 1U;
        } else if (out_fields != (omc_ccm_field*)0 || out_values != (double*)0
                   || field_cap != 0U || value_cap != 0U) {
            omc_ccm_set_status_limit(&res);
        }

        free(temp_values);
    }

    if (use_opts->validation_mode != OMC_CCM_VALIDATE_NONE) {
        omc_ccm_run_cross_field_validation(states, state_count, out_issues,
                                           issue_cap, &res);
    }

    free(dng_ifds);
    free(states);
    return res;
}
