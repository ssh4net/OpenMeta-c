#include "omc/omc_validate.h"

#include "omc_test_assert.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#define OMC_TEST_VALIDATE_TEMP_PATH_CAP 256U

static void
build_temp_pattern(char* out, omc_size out_cap)
{
    const char* tmp_dir;
    omc_size dir_len;
    omc_size pos;

    OMC_TEST_REQUIRE(out != (char*)0);

#if defined(_WIN32)
    tmp_dir = getenv("TEMP");
    if (tmp_dir == (const char*)0 || tmp_dir[0] == '\0') {
        tmp_dir = getenv("TMP");
    }
    if (tmp_dir == (const char*)0 || tmp_dir[0] == '\0') {
        tmp_dir = ".";
    }
#else
    tmp_dir = getenv("TMPDIR");
    if (tmp_dir == (const char*)0 || tmp_dir[0] == '\0') {
        tmp_dir = "/tmp";
    }
#endif

    dir_len = (omc_size)strlen(tmp_dir);
    OMC_TEST_REQUIRE(dir_len + sizeof("/omc_validate_XXXXXX") <= out_cap);

    memcpy(out, tmp_dir, dir_len);
    pos = dir_len;
#if defined(_WIN32)
    if (pos != 0U && out[pos - 1U] != '\\' && out[pos - 1U] != '/') {
        out[pos++] = '\\';
    }
#else
    if (pos != 0U && out[pos - 1U] != '/') {
        out[pos++] = '/';
    }
#endif
    memcpy(out + pos, "omc_validate_XXXXXX", sizeof("omc_validate_XXXXXX"));
}

static void
build_temp_path(char* out, omc_size out_cap, const char* ext)
{
    unsigned attempt;
    omc_size ext_len;

    OMC_TEST_REQUIRE(out != (char*)0);
    OMC_TEST_REQUIRE(ext != (const char*)0);

    ext_len = (omc_size)strlen(ext);
    for (attempt = 0U; attempt < 64U; ++attempt) {
        int fd;
        char pattern[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
        omc_size base_len;

        build_temp_pattern(pattern, sizeof(pattern));
#if defined(_WIN32)
        OMC_TEST_REQUIRE(_mktemp_s(pattern, sizeof(pattern)) == 0);
        fd = _open(pattern, _O_CREAT | _O_EXCL | _O_BINARY | _O_RDWR,
                   _S_IREAD | _S_IWRITE);
        OMC_TEST_REQUIRE(fd >= 0);
        OMC_TEST_REQUIRE(_close(fd) == 0);
#else
        fd = mkstemp(pattern);
        OMC_TEST_REQUIRE(fd >= 0);
        OMC_TEST_REQUIRE(close(fd) == 0);
#endif
        OMC_TEST_REQUIRE(remove(pattern) == 0);

        base_len = (omc_size)strlen(pattern);
        OMC_TEST_REQUIRE(base_len + ext_len + 1U <= out_cap);
        memcpy(out, pattern, base_len);
        memcpy(out + base_len, ext, ext_len + 1U);
        return;
    }

    OMC_TEST_REQUIRE(0);
}

static int
write_bytes_file(const char* path, const omc_u8* bytes, omc_size size)
{
    FILE* fp;

    fp = fopen(path, "wb");
    if (fp == (FILE*)0) {
        return 0;
    }
    if (size != 0U
        && fwrite(bytes, 1U, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}

static int
write_text_file(const char* path, const char* text)
{
    return write_bytes_file(path, (const omc_u8*)text, (omc_size)strlen(text));
}

static int
seek_file(FILE* fp, omc_u64 offset, int whence)
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
write_sparse_file(const char* path, omc_u64 size)
{
    FILE* fp;
    static const omc_u8 k_zero = 0U;

    fp = fopen(path, "wb");
    if (fp == (FILE*)0) {
        return 0;
    }
    if (size == 0U) {
        return fclose(fp) == 0;
    }
    if (!seek_file(fp, size - 1U, SEEK_SET)) {
        fclose(fp);
        return 0;
    }
    if (fwrite(&k_zero, 1U, 1U, fp) != 1U) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}

static int
write_jpeg_with_com_segments(const char* path, omc_u32 count)
{
    FILE* fp;
    omc_u32 i;
    static const omc_u8 k_soi[2] = { 0xFFU, 0xD8U };
    static const omc_u8 k_com[4] = { 0xFFU, 0xFEU, 0x00U, 0x02U };
    static const omc_u8 k_eoi[2] = { 0xFFU, 0xD9U };

    fp = fopen(path, "wb");
    if (fp == (FILE*)0) {
        return 0;
    }
    if (fwrite(k_soi, 1U, sizeof(k_soi), fp) != sizeof(k_soi)) {
        fclose(fp);
        return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (fwrite(k_com, 1U, sizeof(k_com), fp) != sizeof(k_com)) {
            fclose(fp);
            return 0;
        }
    }
    if (fwrite(k_eoi, 1U, sizeof(k_eoi), fp) != sizeof(k_eoi)) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}

static int
write_minimal_dng_with_calibration_only(const char* path)
{
    static const omc_u8 k_dng[] = {
        0x49U, 0x49U, 0x2AU, 0x00U,
        0x08U, 0x00U, 0x00U, 0x00U,
        0x02U, 0x00U,
        0x12U, 0xC6U, 0x01U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x04U, 0x00U, 0x00U,
        0x5AU, 0xC6U, 0x03U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
        0x15U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U
    };

    return write_bytes_file(path, k_dng, sizeof(k_dng));
}

static int
has_issue(const omc_validate_issue* issues, omc_u32 count,
          omc_validate_issue_category category, omc_validate_issue_code code)
{
    omc_u32 i;

    for (i = 0U; i < count; ++i) {
        if (issues[i].category == category && issues[i].code == code) {
            return 1;
        }
    }
    return 0;
}

static void
test_validate_returns_open_failed_for_missing_path(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[8];
    omc_validate_res res;

    build_temp_path(path, sizeof(path), ".jpg");
    (void)remove(path);

    res = omc_validate_file(path, issues, 8U, (const omc_validate_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_OPEN_FAILED);
    OMC_TEST_CHECK(res.failed != 0);
    OMC_TEST_CHECK(res.error_count >= 1U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_FILE,
                             OMC_VALIDATE_CODE_OPEN_FAILED));
}

static void
test_validate_enforces_max_file_bytes(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[8];
    omc_validate_opts opts;
    omc_validate_res res;
    static const omc_u8 k_jpeg[4] = { 0xFFU, 0xD8U, 0xFFU, 0xD9U };

    build_temp_path(path, sizeof(path), ".jpg");
    OMC_TEST_REQUIRE(write_bytes_file(path, k_jpeg, sizeof(k_jpeg)));

    omc_validate_opts_init(&opts);
    opts.max_file_bytes = 3U;
    res = omc_validate_file(path, issues, 8U, &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_TOO_LARGE);
    OMC_TEST_CHECK(res.failed != 0);
    OMC_TEST_CHECK_U64_EQ(res.file_size, 4U);
    OMC_TEST_CHECK(res.error_count >= 1U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_FILE,
                             OMC_VALIDATE_CODE_TOO_LARGE));

    OMC_TEST_REQUIRE(remove(path) == 0);
}

static void
test_validate_reports_malformed_exif_as_error(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[8];
    omc_validate_res res;
    static const omc_u8 k_jpeg_with_truncated_exif[] = {
        0xFFU, 0xD8U, 0xFFU, 0xE1U, 0x00U, 0x11U, 0x45U, 0x78U,
        0x69U, 0x66U, 0x00U, 0x00U, 0x49U, 0x49U, 0x2AU, 0x00U,
        0x08U, 0x00U, 0x00U, 0x00U, 0x01U, 0xFFU, 0xD9U
    };

    build_temp_path(path, sizeof(path), ".jpg");
    OMC_TEST_REQUIRE(write_bytes_file(path, k_jpeg_with_truncated_exif,
                                      sizeof(k_jpeg_with_truncated_exif)));

    res = omc_validate_file(path, issues, 8U, (const omc_validate_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_OK);
    OMC_TEST_CHECK_U64_EQ(res.read.exif.status, OMC_EXIF_MALFORMED);
    OMC_TEST_CHECK(res.failed != 0);
    OMC_TEST_CHECK(res.error_count >= 1U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_EXIF,
                             OMC_VALIDATE_CODE_MALFORMED));

    OMC_TEST_REQUIRE(remove(path) == 0);
}

static void
test_validate_reports_large_sparse_file_size(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[8];
    omc_validate_opts opts;
    omc_validate_res res;
    omc_u64 sparse_size;

    build_temp_path(path, sizeof(path), ".bin");
    sparse_size = ((omc_u64)1U << 31) + (omc_u64)16U;
    OMC_TEST_REQUIRE(write_sparse_file(path, sparse_size));

    omc_validate_opts_init(&opts);
    opts.max_file_bytes = 1U;
    res = omc_validate_file(path, issues, 8U, &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_TOO_LARGE);
    OMC_TEST_CHECK(res.failed != 0);
    OMC_TEST_CHECK_U64_EQ(res.file_size, sparse_size);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_FILE,
                             OMC_VALIDATE_CODE_TOO_LARGE));

    OMC_TEST_REQUIRE(remove(path) == 0);
}

static void
test_validate_warnings_as_errors_promotes_failure(void)
{
    char jpeg_path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    char sidecar_path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue loose_issues[16];
    omc_validate_issue strict_issues[16];
    omc_validate_opts loose;
    omc_validate_opts strict;
    omc_validate_res loose_res;
    omc_validate_res strict_res;
    omc_size path_len;
    static const omc_u8 k_jpeg[4] = { 0xFFU, 0xD8U, 0xFFU, 0xD9U };

    build_temp_path(jpeg_path, sizeof(jpeg_path), ".jpg");
    OMC_TEST_REQUIRE(write_bytes_file(jpeg_path, k_jpeg, sizeof(k_jpeg)));

    path_len = (omc_size)strlen(jpeg_path);
    OMC_TEST_REQUIRE(path_len >= 4U);
    OMC_TEST_REQUIRE(path_len + 1U <= sizeof(sidecar_path));
    memcpy(sidecar_path, jpeg_path, path_len + 1U);
    memcpy(sidecar_path + path_len - 4U, ".xmp", sizeof(".xmp"));
    OMC_TEST_REQUIRE(write_text_file(
        sidecar_path,
        "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
        "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"));

    omc_validate_opts_init(&loose);
    loose.include_xmp_sidecar = 1;
    loose_res = omc_validate_file(jpeg_path, loose_issues, 16U, &loose);
    OMC_TEST_REQUIRE_U64_EQ(loose_res.status, OMC_VALIDATE_OK);
    OMC_TEST_CHECK(loose_res.failed == 0);
    OMC_TEST_CHECK_U64_EQ(loose_res.error_count, 0U);
    OMC_TEST_CHECK(loose_res.warning_count >= 1U);
    OMC_TEST_CHECK_U64_EQ(loose_res.read.xmp.status, OMC_XMP_TRUNCATED);
    OMC_TEST_CHECK(has_issue(loose_issues, loose_res.issues_written,
                             OMC_VALIDATE_ISSUE_XMP,
                             OMC_VALIDATE_CODE_OUTPUT_TRUNCATED));
    OMC_TEST_CHECK(has_issue(
        loose_issues, loose_res.issues_written, OMC_VALIDATE_ISSUE_XMP,
        OMC_VALIDATE_CODE_INVALID_OR_MALFORMED_XML_TEXT));

    strict = loose;
    strict.warnings_as_errors = 1;
    strict_res = omc_validate_file(jpeg_path, strict_issues, 16U, &strict);
    OMC_TEST_REQUIRE_U64_EQ(strict_res.status, OMC_VALIDATE_OK);
    OMC_TEST_CHECK(strict_res.failed != 0);
    OMC_TEST_CHECK_U64_EQ(strict_res.error_count, 0U);
    OMC_TEST_CHECK(strict_res.warning_count >= 1U);

    OMC_TEST_REQUIRE(remove(sidecar_path) == 0);
    OMC_TEST_REQUIRE(remove(jpeg_path) == 0);
}

static void
test_validate_rejects_excessive_scan_scratch_growth(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[8];
    omc_validate_res res;

    build_temp_path(path, sizeof(path), ".jpg");
    OMC_TEST_REQUIRE(write_jpeg_with_com_segments(path, 131073U));

    res = omc_validate_file(path, issues, 8U, (const omc_validate_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_READ_FAILED);
    OMC_TEST_CHECK(res.failed != 0);
    OMC_TEST_CHECK(res.error_count >= 1U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_SCAN,
                             OMC_VALIDATE_CODE_SCRATCH_LIMIT_EXCEEDED));

    OMC_TEST_REQUIRE(remove(path) == 0);
}

static void
test_validate_reports_ccm_issue_for_dng_color_tags(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[16];
    omc_validate_res res;

    build_temp_path(path, sizeof(path), ".dng");
    OMC_TEST_REQUIRE(write_minimal_dng_with_calibration_only(path));

    res = omc_validate_file(path, issues, 16U, (const omc_validate_opts*)0);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_OK);
    OMC_TEST_CHECK_U64_EQ(res.read.exif.status, OMC_EXIF_OK);
    OMC_TEST_CHECK_U64_EQ(res.ccm.status, OMC_CCM_QUERY_OK);
    OMC_TEST_CHECK_U64_EQ(res.ccm_fields, 1U);
    OMC_TEST_CHECK_U64_EQ(res.ccm.fields_needed, 1U);
    OMC_TEST_CHECK(res.warning_count >= 1U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_CCM,
                             OMC_VALIDATE_CODE_CCM_MISSING_COMPANION_TAG));

    OMC_TEST_REQUIRE(remove(path) == 0);
}

static void
test_validate_warnings_as_errors_promotes_ccm_failure(void)
{
    char path[OMC_TEST_VALIDATE_TEMP_PATH_CAP];
    omc_validate_issue issues[16];
    omc_validate_opts opts;
    omc_validate_res res;

    build_temp_path(path, sizeof(path), ".dng");
    OMC_TEST_REQUIRE(write_minimal_dng_with_calibration_only(path));

    omc_validate_opts_init(&opts);
    opts.warnings_as_errors = 1;
    res = omc_validate_file(path, issues, 16U, &opts);
    OMC_TEST_REQUIRE_U64_EQ(res.status, OMC_VALIDATE_OK);
    OMC_TEST_CHECK(res.failed != 0);
    OMC_TEST_CHECK(res.warning_count >= 1U);
    OMC_TEST_CHECK(has_issue(issues, res.issues_written,
                             OMC_VALIDATE_ISSUE_CCM,
                             OMC_VALIDATE_CODE_CCM_MISSING_COMPANION_TAG));

    OMC_TEST_REQUIRE(remove(path) == 0);
}

int
main(void)
{
    test_validate_returns_open_failed_for_missing_path();
    test_validate_enforces_max_file_bytes();
    test_validate_reports_malformed_exif_as_error();
    test_validate_reports_large_sparse_file_size();
    test_validate_warnings_as_errors_promotes_failure();
    test_validate_rejects_excessive_scan_scratch_growth();
    test_validate_reports_ccm_issue_for_dng_color_tags();
    test_validate_warnings_as_errors_promotes_ccm_failure();
    return omc_test_finish();
}
