#include "omc/omc_validate.h"

#include "omc/omc_store.h"
#include "omc/omc_xmp.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#elif defined(_POSIX_VERSION)
#include <sys/types.h>
#endif

#define OMC_VALIDATE_DEFAULT_BLOCK_CAP 128U
#define OMC_VALIDATE_DEFAULT_IFD_CAP 256U
#define OMC_VALIDATE_DEFAULT_PAYLOAD_CAP ((omc_size)(1024U * 1024U))
#define OMC_VALIDATE_DEFAULT_PAYLOAD_PART_CAP 16384U
#define OMC_VALIDATE_DEFAULT_RETRY_PASSES 8U
#define OMC_VALIDATE_MAX_BLOCKS_HARD_CAP ((omc_u64)(1UL << 17))

typedef enum omc_validate_read_file_status {
    OMC_VALIDATE_FILE_READ_OK = 0,
    OMC_VALIDATE_FILE_READ_OPEN_FAILED = 1,
    OMC_VALIDATE_FILE_READ_TOO_LARGE = 2,
    OMC_VALIDATE_FILE_READ_FAILED = 3
} omc_validate_read_file_status;

static void
omc_validate_init_res(omc_validate_res* res)
{
    if (res == (omc_validate_res*)0) {
        return;
    }

    memset(res, 0, sizeof(*res));
    res->status = OMC_VALIDATE_OK;
}

static void
omc_validate_add_issue(omc_validate_res* res, omc_validate_issue* out_issues,
                       omc_u32 issue_cap,
                       omc_validate_issue_severity severity,
                       omc_validate_issue_category category,
                       omc_validate_issue_code code)
{
    omc_u32 index;

    if (res == (omc_validate_res*)0) {
        return;
    }

    index = res->issues_needed;
    res->issues_needed += 1U;
    if (severity == OMC_VALIDATE_ERROR) {
        res->error_count += 1U;
    } else {
        res->warning_count += 1U;
    }

    if (out_issues != (omc_validate_issue*)0 && index < issue_cap) {
        out_issues[index].severity = severity;
        out_issues[index].category = category;
        out_issues[index].code = code;
        res->issues_written = index + 1U;
    }
}

static int
omc_validate_seek_file(FILE* fp, omc_u64 offset, int whence)
{
    if (fp == (FILE*)0) {
        return 0;
    }

#if defined(_WIN32)
    if (offset > (omc_u64)0x7FFFFFFFFFFFFFFFULL) {
        return 0;
    }
    return _fseeki64(fp, (__int64)offset, whence) == 0;
#elif defined(_POSIX_VERSION)
    if (offset > (omc_u64)LONG_MAX && sizeof(off_t) < sizeof(omc_u64)) {
        return 0;
    }
    return fseeko(fp, (off_t)offset, whence) == 0;
#else
    if (offset > (omc_u64)LONG_MAX) {
        return 0;
    }
    return fseek(fp, (long)offset, whence) == 0;
#endif
}

static int
omc_validate_tell_file(FILE* fp, omc_u64* out_offset)
{
    if (fp == (FILE*)0 || out_offset == (omc_u64*)0) {
        return 0;
    }

#if defined(_WIN32)
    {
        __int64 pos;

        pos = _ftelli64(fp);
        if (pos < 0) {
            return 0;
        }
        *out_offset = (omc_u64)pos;
    }
#elif defined(_POSIX_VERSION)
    {
        off_t pos;

        pos = ftello(fp);
        if (pos < (off_t)0) {
            return 0;
        }
        *out_offset = (omc_u64)pos;
    }
#else
    {
        long pos;

        pos = ftell(fp);
        if (pos < 0L) {
            return 0;
        }
        *out_offset = (omc_u64)pos;
    }
#endif
    return 1;
}

static omc_validate_read_file_status
omc_validate_read_file_bytes(const char* path, omc_u8** out_bytes,
                             omc_size* out_size, omc_u64 max_bytes,
                             omc_u64* out_file_size)
{
    FILE* fp;
    omc_u64 size64;
    omc_u8* bytes;

    if (out_bytes != (omc_u8**)0) {
        *out_bytes = (omc_u8*)0;
    }
    if (out_size != (omc_size*)0) {
        *out_size = 0U;
    }
    if (out_file_size != (omc_u64*)0) {
        *out_file_size = 0U;
    }
    if (path == (const char*)0 || path[0] == '\0') {
        return OMC_VALIDATE_FILE_READ_OPEN_FAILED;
    }

    fp = fopen(path, "rb");
    if (fp == (FILE*)0) {
        return OMC_VALIDATE_FILE_READ_OPEN_FAILED;
    }
    if (!omc_validate_seek_file(fp, 0U, SEEK_END)
        || !omc_validate_tell_file(fp, &size64)) {
        fclose(fp);
        return OMC_VALIDATE_FILE_READ_FAILED;
    }
    if (out_file_size != (omc_u64*)0) {
        *out_file_size = size64;
    }
    if (max_bytes != 0U && size64 > max_bytes) {
        fclose(fp);
        return OMC_VALIDATE_FILE_READ_TOO_LARGE;
    }
    if (size64 > (omc_u64)((omc_size)(~(omc_size)0))) {
        fclose(fp);
        return OMC_VALIDATE_FILE_READ_TOO_LARGE;
    }
    if (!omc_validate_seek_file(fp, 0U, SEEK_SET)) {
        fclose(fp);
        return OMC_VALIDATE_FILE_READ_FAILED;
    }

    bytes = (omc_u8*)0;
    if (size64 != 0U) {
        bytes = (omc_u8*)malloc((size_t)size64);
        if (bytes == (omc_u8*)0) {
            fclose(fp);
            return OMC_VALIDATE_FILE_READ_FAILED;
        }
        if (fread(bytes, 1U, (size_t)size64, fp) != (size_t)size64) {
            free(bytes);
            fclose(fp);
            return OMC_VALIDATE_FILE_READ_FAILED;
        }
    }

    fclose(fp);
    if (out_bytes != (omc_u8**)0) {
        *out_bytes = bytes;
    } else {
        free(bytes);
    }
    if (out_size != (omc_size*)0) {
        *out_size = (omc_size)size64;
    }
    return OMC_VALIDATE_FILE_READ_OK;
}

static omc_u64
omc_validate_block_retry_cap(omc_u32 max_parts)
{
    omc_u64 cap;

    cap = (omc_u64)max_parts;
    if (cap == 0U) {
        cap = (omc_u64)(1U << 14);
    }
    if (cap > (~(omc_u64)0) / 8U) {
        cap = ~(omc_u64)0;
    } else {
        cap *= 8U;
    }
    if (cap < 4096U) {
        cap = 4096U;
    }
    if (cap > OMC_VALIDATE_MAX_BLOCKS_HARD_CAP) {
        cap = OMC_VALIDATE_MAX_BLOCKS_HARD_CAP;
    }
    return cap;
}

static void
omc_validate_merge_xmp_status(omc_xmp_status* out_status, omc_xmp_status in)
{
    if (out_status == (omc_xmp_status*)0) {
        return;
    }
    if (*out_status == OMC_XMP_NOMEM) {
        return;
    }
    if (in == OMC_XMP_NOMEM) {
        *out_status = in;
        return;
    }
    if (*out_status == OMC_XMP_LIMIT) {
        return;
    }
    if (in == OMC_XMP_LIMIT) {
        *out_status = in;
        return;
    }
    if (*out_status == OMC_XMP_MALFORMED) {
        return;
    }
    if (in == OMC_XMP_MALFORMED) {
        *out_status = in;
        return;
    }
    if (*out_status == OMC_XMP_TRUNCATED) {
        return;
    }
    if (in == OMC_XMP_TRUNCATED) {
        *out_status = in;
        return;
    }
    if (*out_status == OMC_XMP_UNSUPPORTED && in == OMC_XMP_OK) {
        *out_status = in;
        return;
    }
}

static void
omc_validate_add_decode_status_issues(omc_validate_res* res,
                                      omc_validate_issue* out_issues,
                                      omc_u32 issue_cap)
{
    if (res == (omc_validate_res*)0) {
        return;
    }

    if (res->read.scan.status == OMC_SCAN_TRUNCATED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_WARNING,
                               OMC_VALIDATE_ISSUE_SCAN,
                               OMC_VALIDATE_CODE_OUTPUT_TRUNCATED);
    } else if (res->read.scan.status == OMC_SCAN_MALFORMED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_SCAN,
                               OMC_VALIDATE_CODE_MALFORMED);
    }

    if (res->read.pay.status == OMC_PAY_TRUNCATED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_WARNING,
                               OMC_VALIDATE_ISSUE_PAYLOAD,
                               OMC_VALIDATE_CODE_OUTPUT_TRUNCATED);
    } else if (res->read.pay.status == OMC_PAY_MALFORMED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR,
                               OMC_VALIDATE_ISSUE_PAYLOAD,
                               OMC_VALIDATE_CODE_MALFORMED);
    } else if (res->read.pay.status == OMC_PAY_LIMIT) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR,
                               OMC_VALIDATE_ISSUE_PAYLOAD,
                               OMC_VALIDATE_CODE_LIMIT_EXCEEDED);
    } else if (res->read.pay.status == OMC_PAY_NOMEM) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR,
                               OMC_VALIDATE_ISSUE_PAYLOAD,
                               OMC_VALIDATE_CODE_NO_MEMORY);
    }

    if (res->read.exif.status == OMC_EXIF_TRUNCATED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_WARNING,
                               OMC_VALIDATE_ISSUE_EXIF,
                               OMC_VALIDATE_CODE_OUTPUT_TRUNCATED);
    } else if (res->read.exif.status == OMC_EXIF_MALFORMED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_EXIF,
                               OMC_VALIDATE_CODE_MALFORMED);
    } else if (res->read.exif.status == OMC_EXIF_LIMIT) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_EXIF,
                               OMC_VALIDATE_CODE_LIMIT_EXCEEDED);
    } else if (res->read.exif.status == OMC_EXIF_NOMEM) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_EXIF,
                               OMC_VALIDATE_CODE_NO_MEMORY);
    }

    if (res->read.xmp.status == OMC_XMP_TRUNCATED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_WARNING, OMC_VALIDATE_ISSUE_XMP,
                               OMC_VALIDATE_CODE_OUTPUT_TRUNCATED);
        omc_validate_add_issue(
            res, out_issues, issue_cap, OMC_VALIDATE_WARNING,
            OMC_VALIDATE_ISSUE_XMP,
            OMC_VALIDATE_CODE_INVALID_OR_MALFORMED_XML_TEXT);
    } else if (res->read.xmp.status == OMC_XMP_MALFORMED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_XMP,
                               OMC_VALIDATE_CODE_MALFORMED);
    } else if (res->read.xmp.status == OMC_XMP_LIMIT) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_XMP,
                               OMC_VALIDATE_CODE_LIMIT_EXCEEDED);
    } else if (res->read.xmp.status == OMC_XMP_NOMEM) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_XMP,
                               OMC_VALIDATE_CODE_NO_MEMORY);
    }

    if (res->read.exr.status == OMC_EXR_MALFORMED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_EXR,
                               OMC_VALIDATE_CODE_MALFORMED);
    } else if (res->read.exr.status == OMC_EXR_LIMIT) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_EXR,
                               OMC_VALIDATE_CODE_LIMIT_EXCEEDED);
    } else if (res->read.exr.status == OMC_EXR_NOMEM) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_EXR,
                               OMC_VALIDATE_CODE_NO_MEMORY);
    }

    if (res->read.jumbf.status == OMC_JUMBF_MALFORMED) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_JUMBF,
                               OMC_VALIDATE_CODE_MALFORMED);
    } else if (res->read.jumbf.status == OMC_JUMBF_LIMIT) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_JUMBF,
                               OMC_VALIDATE_CODE_LIMIT_EXCEEDED);
    } else if (res->read.jumbf.status == OMC_JUMBF_NOMEM) {
        omc_validate_add_issue(res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR, OMC_VALIDATE_ISSUE_JUMBF,
                               OMC_VALIDATE_CODE_NO_MEMORY);
    }
}

static omc_validate_issue_code
omc_validate_map_ccm_issue_code(omc_ccm_issue_code code)
{
    switch (code) {
    case OMC_CCM_ISSUE_DECODE_FAILED:
        return OMC_VALIDATE_CODE_CCM_DECODE_FAILED;
    case OMC_CCM_ISSUE_NON_FINITE_VALUE:
        return OMC_VALIDATE_CODE_CCM_NON_FINITE_VALUE;
    case OMC_CCM_ISSUE_UNEXPECTED_COUNT:
        return OMC_VALIDATE_CODE_CCM_UNEXPECTED_COUNT;
    case OMC_CCM_ISSUE_MATRIX_COUNT_NOT_DIVISIBLE_BY_3:
        return OMC_VALIDATE_CODE_CCM_MATRIX_COUNT_NOT_DIVISIBLE_BY_3;
    case OMC_CCM_ISSUE_NON_POSITIVE_VALUE:
        return OMC_VALIDATE_CODE_CCM_NON_POSITIVE_VALUE;
    case OMC_CCM_ISSUE_AS_SHOT_CONFLICT:
        return OMC_VALIDATE_CODE_CCM_AS_SHOT_CONFLICT;
    case OMC_CCM_ISSUE_MISSING_COMPANION_TAG:
        return OMC_VALIDATE_CODE_CCM_MISSING_COMPANION_TAG;
    case OMC_CCM_ISSUE_TRIPLE_ILLUMINANT_RULE:
        return OMC_VALIDATE_CODE_CCM_TRIPLE_ILLUMINANT_RULE;
    case OMC_CCM_ISSUE_CALIBRATION_SIGNATURE_MISMATCH:
        return OMC_VALIDATE_CODE_CCM_CALIBRATION_SIGNATURE_MISMATCH;
    case OMC_CCM_ISSUE_MISSING_ILLUMINANT_DATA:
        return OMC_VALIDATE_CODE_CCM_MISSING_ILLUMINANT_DATA;
    case OMC_CCM_ISSUE_INVALID_ILLUMINANT_CODE:
        return OMC_VALIDATE_CODE_CCM_INVALID_ILLUMINANT_CODE;
    case OMC_CCM_ISSUE_WHITE_XY_OUT_OF_RANGE:
        return OMC_VALIDATE_CODE_CCM_WHITE_XY_OUT_OF_RANGE;
    }
    return OMC_VALIDATE_CODE_CCM_DECODE_FAILED;
}

static char*
omc_validate_dup_with_suffix(const char* path, omc_size base_len,
                             const char* suffix)
{
    char* out;
    omc_size suffix_len;

    if (path == (const char*)0 || suffix == (const char*)0) {
        return (char*)0;
    }

    suffix_len = (omc_size)strlen(suffix);
    out = (char*)malloc(base_len + suffix_len + 1U);
    if (out == (char*)0) {
        return (char*)0;
    }

    memcpy(out, path, base_len);
    memcpy(out + base_len, suffix, suffix_len + 1U);
    return out;
}

static void
omc_validate_xmp_sidecar_candidates(const char* path, char** out_a,
                                    char** out_b)
{
    omc_size len;
    omc_size sep;
    omc_size dot;
    omc_size i;
    char* a;
    char* b;

    if (out_a != (char**)0) {
        *out_a = (char*)0;
    }
    if (out_b != (char**)0) {
        *out_b = (char*)0;
    }
    if (path == (const char*)0 || path[0] == '\0' || out_a == (char**)0
        || out_b == (char**)0) {
        return;
    }

    len = (omc_size)strlen(path);
    sep = (omc_size)(~(omc_size)0);
    dot = (omc_size)(~(omc_size)0);
    for (i = 0U; i < len; ++i) {
        if (path[i] == '/' || path[i] == '\\') {
            sep = i;
        } else if (path[i] == '.') {
            dot = i;
        }
    }

    b = omc_validate_dup_with_suffix(path, len, ".xmp");
    if (b == (char*)0) {
        return;
    }

    if (dot != (omc_size)(~(omc_size)0)
        && (sep == (omc_size)(~(omc_size)0) || dot > sep)) {
        a = omc_validate_dup_with_suffix(path, dot, ".xmp");
    } else {
        a = omc_validate_dup_with_suffix(path, len, ".xmp");
    }
    if (a == (char*)0) {
        free(b);
        return;
    }

    if (strcmp(a, b) == 0) {
        free(b);
        b = (char*)0;
    }

    *out_a = a;
    *out_b = b;
}

static void
omc_validate_fail_terminal(omc_validate_res* res,
                           omc_validate_issue* out_issues, omc_u32 issue_cap,
                           omc_validate_status status,
                           omc_validate_issue_category category,
                           omc_validate_issue_code code)
{
    if (res == (omc_validate_res*)0) {
        return;
    }

    res->status = status;
    omc_validate_add_issue(res, out_issues, issue_cap, OMC_VALIDATE_ERROR,
                           category, code);
    res->failed = 1;
}

void
omc_validate_opts_init(omc_validate_opts* opts)
{
    if (opts == (omc_validate_opts*)0) {
        return;
    }

    omc_read_opts_init(&opts->read);
    omc_ccm_query_opts_init(&opts->ccm);
    opts->max_file_bytes = 0U;
    opts->include_xmp_sidecar = 0;
    opts->warnings_as_errors = 0;
    opts->max_retry_passes = OMC_VALIDATE_DEFAULT_RETRY_PASSES;
    opts->read.exif.decode_embedded_containers = 1;
    opts->read.xmp.malformed_mode = OMC_XMP_MALFORMED_TRUNCATED;
}

const char*
omc_validate_status_name(omc_validate_status status)
{
    switch (status) {
    case OMC_VALIDATE_OK: return "ok";
    case OMC_VALIDATE_OPEN_FAILED: return "open_failed";
    case OMC_VALIDATE_TOO_LARGE: return "too_large";
    case OMC_VALIDATE_READ_FAILED: return "read_failed";
    }
    return "unknown";
}

const char*
omc_validate_issue_category_name(omc_validate_issue_category category)
{
    switch (category) {
    case OMC_VALIDATE_ISSUE_FILE: return "file";
    case OMC_VALIDATE_ISSUE_SCAN: return "scan";
    case OMC_VALIDATE_ISSUE_PAYLOAD: return "payload";
    case OMC_VALIDATE_ISSUE_EXIF: return "exif";
    case OMC_VALIDATE_ISSUE_XMP: return "xmp";
    case OMC_VALIDATE_ISSUE_EXR: return "exr";
    case OMC_VALIDATE_ISSUE_JUMBF: return "jumbf";
    case OMC_VALIDATE_ISSUE_XMP_SIDECAR: return "xmp_sidecar";
    case OMC_VALIDATE_ISSUE_CCM: return "ccm";
    case OMC_VALIDATE_ISSUE_READ: return "read";
    }
    return "unknown";
}

const char*
omc_validate_issue_code_name(omc_validate_issue_code code)
{
    switch (code) {
    case OMC_VALIDATE_CODE_OPEN_FAILED: return "open_failed";
    case OMC_VALIDATE_CODE_TOO_LARGE: return "too_large";
    case OMC_VALIDATE_CODE_READ_FAILED: return "read_failed";
    case OMC_VALIDATE_CODE_OUTPUT_TRUNCATED: return "output_truncated";
    case OMC_VALIDATE_CODE_MALFORMED: return "malformed";
    case OMC_VALIDATE_CODE_LIMIT_EXCEEDED: return "limit_exceeded";
    case OMC_VALIDATE_CODE_INVALID_OR_MALFORMED_XML_TEXT:
        return "invalid_or_malformed_xml_text";
    case OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED:
        return "scratch_limit_exceeded";
    case OMC_VALIDATE_CODE_RETRY_LIMIT_EXCEEDED:
        return "retry_limit_exceeded";
    case OMC_VALIDATE_CODE_NO_MEMORY: return "no_memory";
    case OMC_VALIDATE_CODE_CCM_DECODE_FAILED: return "decode_failed";
    case OMC_VALIDATE_CODE_CCM_NON_FINITE_VALUE:
        return "non_finite_value";
    case OMC_VALIDATE_CODE_CCM_UNEXPECTED_COUNT:
        return "unexpected_count";
    case OMC_VALIDATE_CODE_CCM_MATRIX_COUNT_NOT_DIVISIBLE_BY_3:
        return "matrix_count_not_divisible_by_3";
    case OMC_VALIDATE_CODE_CCM_NON_POSITIVE_VALUE:
        return "non_positive_value";
    case OMC_VALIDATE_CODE_CCM_AS_SHOT_CONFLICT:
        return "as_shot_conflict";
    case OMC_VALIDATE_CODE_CCM_MISSING_COMPANION_TAG:
        return "missing_companion_tag";
    case OMC_VALIDATE_CODE_CCM_TRIPLE_ILLUMINANT_RULE:
        return "triple_illuminant_rule";
    case OMC_VALIDATE_CODE_CCM_CALIBRATION_SIGNATURE_MISMATCH:
        return "calibration_signature_mismatch";
    case OMC_VALIDATE_CODE_CCM_MISSING_ILLUMINANT_DATA:
        return "missing_illuminant_data";
    case OMC_VALIDATE_CODE_CCM_INVALID_ILLUMINANT_CODE:
        return "invalid_illuminant_code";
    case OMC_VALIDATE_CODE_CCM_WHITE_XY_OUT_OF_RANGE:
        return "white_xy_out_of_range";
    }
    return "unknown";
}

omc_validate_res
omc_validate_file(const char* path, omc_validate_issue* out_issues,
                  omc_u32 issue_cap, const omc_validate_opts* opts)
{
    omc_validate_res res;
    omc_validate_opts local_opts;
    const omc_validate_opts* use_opts;
    omc_u8* file_bytes;
    omc_size file_size;
    omc_validate_read_file_status file_status;
    omc_blk_ref* blocks;
    omc_exif_ifd_ref* ifd_refs;
    omc_u8* payload;
    omc_u32* payload_parts;
    omc_u32 block_cap;
    omc_u32 ifd_cap;
    omc_size payload_cap;
    omc_u32 payload_part_cap;
    omc_u64 max_block_retry;
    omc_u64 max_ifd_retry;
    omc_u64 max_payload_retry;
    omc_u32 retry_cap;
    omc_store store;
    omc_u32 retry_count;
    omc_ccm_issue* ccm_issues;
    omc_size ccm_issue_bytes;
    omc_u32 ccm_issue_index;

    omc_validate_init_res(&res);

    if (opts == (const omc_validate_opts*)0) {
        omc_validate_opts_init(&local_opts);
        use_opts = &local_opts;
    } else {
        local_opts = *opts;
        use_opts = &local_opts;
    }

    if (path == (const char*)0 || path[0] == '\0') {
        omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                   OMC_VALIDATE_OPEN_FAILED,
                                   OMC_VALIDATE_ISSUE_FILE,
                                   OMC_VALIDATE_CODE_OPEN_FAILED);
        return res;
    }

    file_bytes = (omc_u8*)0;
    file_size = 0U;
    file_status = omc_validate_read_file_bytes(path, &file_bytes, &file_size,
                                               use_opts->max_file_bytes,
                                               &res.file_size);
    if (file_status != OMC_VALIDATE_FILE_READ_OK) {
        if (file_status == OMC_VALIDATE_FILE_READ_TOO_LARGE) {
            omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_TOO_LARGE,
                                       OMC_VALIDATE_ISSUE_FILE,
                                       OMC_VALIDATE_CODE_TOO_LARGE);
        } else if (file_status == OMC_VALIDATE_FILE_READ_OPEN_FAILED) {
            omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_OPEN_FAILED,
                                       OMC_VALIDATE_ISSUE_FILE,
                                       OMC_VALIDATE_CODE_OPEN_FAILED);
        } else {
            omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_READ_FAILED,
                                       OMC_VALIDATE_ISSUE_FILE,
                                       OMC_VALIDATE_CODE_READ_FAILED);
        }
        return res;
    }

    block_cap = OMC_VALIDATE_DEFAULT_BLOCK_CAP;
    ifd_cap = OMC_VALIDATE_DEFAULT_IFD_CAP;
    payload_cap = OMC_VALIDATE_DEFAULT_PAYLOAD_CAP;
    payload_part_cap = OMC_VALIDATE_DEFAULT_PAYLOAD_PART_CAP;
    max_block_retry = omc_validate_block_retry_cap(
        use_opts->read.pay.limits.max_parts);
    max_ifd_retry = (omc_u64)use_opts->read.exif.limits.max_ifds;
    max_payload_retry = use_opts->read.pay.limits.max_output_bytes;
    retry_cap = use_opts->max_retry_passes;
    if (retry_cap == 0U) {
        retry_cap = OMC_VALIDATE_DEFAULT_RETRY_PASSES;
    }

    blocks = (omc_blk_ref*)malloc((size_t)block_cap * sizeof(*blocks));
    ifd_refs = (omc_exif_ifd_ref*)malloc((size_t)ifd_cap * sizeof(*ifd_refs));
    payload = (omc_u8*)malloc((size_t)payload_cap);
    payload_parts = (omc_u32*)malloc((size_t)payload_part_cap
                                     * sizeof(*payload_parts));
    if (blocks == (omc_blk_ref*)0 || ifd_refs == (omc_exif_ifd_ref*)0
        || payload == (omc_u8*)0 || payload_parts == (omc_u32*)0) {
        free(file_bytes);
        free(blocks);
        free(ifd_refs);
        free(payload);
        free(payload_parts);
        omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                   OMC_VALIDATE_READ_FAILED,
                                   OMC_VALIDATE_ISSUE_READ,
                                   OMC_VALIDATE_CODE_NO_MEMORY);
        return res;
    }

    omc_store_init(&store);
    retry_count = 0U;
    for (;;) {
        omc_read_res one;

        if (retry_count > retry_cap) {
            omc_store_fini(&store);
            free(file_bytes);
            free(blocks);
            free(ifd_refs);
            free(payload);
            free(payload_parts);
            omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_READ_FAILED,
                                       OMC_VALIDATE_ISSUE_READ,
                                       OMC_VALIDATE_CODE_RETRY_LIMIT_EXCEEDED);
            return res;
        }

        omc_store_reset(&store);
        one = omc_read_simple(file_bytes, file_size, &store, blocks, block_cap,
                              ifd_refs, ifd_cap, payload, payload_cap,
                              payload_parts, payload_part_cap,
                              &use_opts->read);
        res.read = one;

        if (res.read.scan.status == OMC_SCAN_TRUNCATED
            && res.read.scan.needed > block_cap) {
            omc_blk_ref* new_blocks;
            omc_u32 needed_blocks;

            needed_blocks = res.read.scan.needed;
            if ((omc_u64)needed_blocks > max_block_retry) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(
                    &res, out_issues, issue_cap, OMC_VALIDATE_READ_FAILED,
                    OMC_VALIDATE_ISSUE_SCAN,
                    OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED);
                return res;
            }
            new_blocks = (omc_blk_ref*)realloc(
                blocks, (size_t)needed_blocks * sizeof(*blocks));
            if (new_blocks == (omc_blk_ref*)0) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                           OMC_VALIDATE_READ_FAILED,
                                           OMC_VALIDATE_ISSUE_SCAN,
                                           OMC_VALIDATE_CODE_NO_MEMORY);
                return res;
            }
            blocks = new_blocks;
            block_cap = needed_blocks;
            retry_count += 1U;
            continue;
        }

        if (res.read.exif.status == OMC_EXIF_TRUNCATED
            && res.read.exif.ifds_needed > ifd_cap) {
            omc_exif_ifd_ref* new_ifds;
            omc_u32 needed_ifds;

            needed_ifds = res.read.exif.ifds_needed;
            if ((omc_u64)needed_ifds > max_ifd_retry) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(
                    &res, out_issues, issue_cap, OMC_VALIDATE_READ_FAILED,
                    OMC_VALIDATE_ISSUE_EXIF,
                    OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED);
                return res;
            }
            new_ifds = (omc_exif_ifd_ref*)realloc(
                ifd_refs, (size_t)needed_ifds * sizeof(*ifd_refs));
            if (new_ifds == (omc_exif_ifd_ref*)0) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                           OMC_VALIDATE_READ_FAILED,
                                           OMC_VALIDATE_ISSUE_EXIF,
                                           OMC_VALIDATE_CODE_NO_MEMORY);
                return res;
            }
            ifd_refs = new_ifds;
            ifd_cap = needed_ifds;
            retry_count += 1U;
            continue;
        }

        if (res.read.pay.status == OMC_PAY_TRUNCATED
            && res.read.pay.needed > (omc_u64)payload_cap) {
            omc_u8* new_payload;

            if (max_payload_retry != 0U
                && res.read.pay.needed > max_payload_retry) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(
                    &res, out_issues, issue_cap, OMC_VALIDATE_READ_FAILED,
                    OMC_VALIDATE_ISSUE_PAYLOAD,
                    OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED);
                return res;
            }
            if (res.read.pay.needed
                > (omc_u64)((omc_size)(~(omc_size)0))) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(
                    &res, out_issues, issue_cap, OMC_VALIDATE_READ_FAILED,
                    OMC_VALIDATE_ISSUE_PAYLOAD,
                    OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED);
                return res;
            }
            new_payload = (omc_u8*)realloc(payload,
                                           (size_t)res.read.pay.needed);
            if (new_payload == (omc_u8*)0) {
                omc_store_fini(&store);
                free(file_bytes);
                free(blocks);
                free(ifd_refs);
                free(payload);
                free(payload_parts);
                omc_validate_fail_terminal(&res, out_issues, issue_cap,
                                           OMC_VALIDATE_READ_FAILED,
                                           OMC_VALIDATE_ISSUE_PAYLOAD,
                                           OMC_VALIDATE_CODE_NO_MEMORY);
                return res;
            }
            payload = new_payload;
            payload_cap = (omc_size)res.read.pay.needed;
            retry_count += 1U;
            continue;
        }

        break;
    }

    if (use_opts->include_xmp_sidecar) {
        char* sidecar_a;
        char* sidecar_b;
        const char* sidecar_paths[2];
        omc_u32 sidecar_index;

        sidecar_a = (char*)0;
        sidecar_b = (char*)0;
        omc_validate_xmp_sidecar_candidates(path, &sidecar_a, &sidecar_b);
        sidecar_paths[0] = sidecar_a;
        sidecar_paths[1] = sidecar_b;

        for (sidecar_index = 0U; sidecar_index < 2U; ++sidecar_index) {
            omc_u8* xmp_bytes;
            omc_size xmp_size;
            omc_u64 sidecar_size;
            omc_validate_read_file_status sidecar_status;

            if (sidecar_paths[sidecar_index] == (const char*)0
                || sidecar_paths[sidecar_index][0] == '\0') {
                continue;
            }

            xmp_bytes = (omc_u8*)0;
            xmp_size = 0U;
            sidecar_size = 0U;
            sidecar_status = omc_validate_read_file_bytes(
                sidecar_paths[sidecar_index], &xmp_bytes, &xmp_size,
                use_opts->max_file_bytes, &sidecar_size);
            if (sidecar_status == OMC_VALIDATE_FILE_READ_OPEN_FAILED) {
                continue;
            }
            if (sidecar_status == OMC_VALIDATE_FILE_READ_TOO_LARGE) {
                omc_validate_add_issue(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_ERROR,
                                       OMC_VALIDATE_ISSUE_XMP_SIDECAR,
                                       OMC_VALIDATE_CODE_TOO_LARGE);
                continue;
            }
            if (sidecar_status != OMC_VALIDATE_FILE_READ_OK) {
                omc_validate_add_issue(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_ERROR,
                                       OMC_VALIDATE_ISSUE_XMP_SIDECAR,
                                       OMC_VALIDATE_CODE_READ_FAILED);
                continue;
            }

            {
                omc_xmp_res xmp_res;

                xmp_res = omc_xmp_dec(xmp_bytes, xmp_size, &store,
                                      OMC_INVALID_BLOCK_ID,
                                      OMC_ENTRY_FLAG_NONE,
                                      &use_opts->read.xmp);
                omc_validate_merge_xmp_status(&res.read.xmp.status,
                                              xmp_res.status);
                res.read.xmp.entries_decoded += xmp_res.entries_decoded;
            }
            free(xmp_bytes);
        }

        free(sidecar_a);
        free(sidecar_b);
    }

    res.entries = (omc_u32)store.entry_count;
    omc_validate_add_decode_status_issues(&res, out_issues, issue_cap);

    ccm_issues = (omc_ccm_issue*)0;
    ccm_issue_bytes = 0U;
    ccm_issue_index = 0U;
    res.ccm = omc_ccm_collect_fields(&store, (omc_ccm_field*)0, 0U,
                                     (double*)0, 0U, (omc_ccm_issue*)0, 0U,
                                     &use_opts->ccm);
    res.ccm_fields = res.ccm.fields_needed;

    if (res.ccm.status != OMC_CCM_QUERY_NOMEM
        && res.ccm.status != OMC_CCM_QUERY_INVALID_ARGUMENT
        && res.ccm.issues_needed != 0U) {
        if ((omc_size)res.ccm.issues_needed
            > ((omc_size)(~(omc_size)0) / sizeof(*ccm_issues))) {
            omc_validate_add_issue(&res, out_issues, issue_cap,
                                   OMC_VALIDATE_ERROR,
                                   OMC_VALIDATE_ISSUE_CCM,
                                   OMC_VALIDATE_CODE_NO_MEMORY);
        } else {
            ccm_issue_bytes = (omc_size)res.ccm.issues_needed
                              * sizeof(*ccm_issues);
            ccm_issues = (omc_ccm_issue*)malloc((size_t)ccm_issue_bytes);
            if (ccm_issues == (omc_ccm_issue*)0) {
                omc_validate_add_issue(&res, out_issues, issue_cap,
                                       OMC_VALIDATE_ERROR,
                                       OMC_VALIDATE_ISSUE_CCM,
                                       OMC_VALIDATE_CODE_NO_MEMORY);
            } else {
                res.ccm = omc_ccm_collect_fields(
                    &store, (omc_ccm_field*)0, 0U, (double*)0, 0U, ccm_issues,
                    res.ccm.issues_needed, &use_opts->ccm);
                res.ccm_fields = res.ccm.fields_needed;
            }
        }
    }

    if (res.ccm.status == OMC_CCM_QUERY_LIMIT_EXCEEDED) {
        omc_validate_add_issue(&res, out_issues, issue_cap,
                               OMC_VALIDATE_WARNING,
                               OMC_VALIDATE_ISSUE_CCM,
                               OMC_VALIDATE_CODE_LIMIT_EXCEEDED);
    } else if (res.ccm.status == OMC_CCM_QUERY_NOMEM) {
        omc_validate_add_issue(&res, out_issues, issue_cap,
                               OMC_VALIDATE_ERROR,
                               OMC_VALIDATE_ISSUE_CCM,
                               OMC_VALIDATE_CODE_NO_MEMORY);
    }

    if (ccm_issues != (omc_ccm_issue*)0
        && res.ccm.status != OMC_CCM_QUERY_NOMEM
        && res.ccm.status != OMC_CCM_QUERY_INVALID_ARGUMENT) {
        for (ccm_issue_index = 0U; ccm_issue_index < res.ccm.issues_written;
             ++ccm_issue_index) {
            omc_validate_add_issue(
                &res, out_issues, issue_cap,
                ccm_issues[ccm_issue_index].severity == OMC_CCM_ERROR
                    ? OMC_VALIDATE_ERROR
                    : OMC_VALIDATE_WARNING,
                OMC_VALIDATE_ISSUE_CCM,
                omc_validate_map_ccm_issue_code(
                    ccm_issues[ccm_issue_index].code));
        }
    }

    res.failed = (res.error_count != 0U)
                 || (use_opts->warnings_as_errors
                     && res.warning_count != 0U);

    free(ccm_issues);
    omc_store_fini(&store);
    free(file_bytes);
    free(blocks);
    free(ifd_refs);
    free(payload);
    free(payload_parts);
    return res;
}
