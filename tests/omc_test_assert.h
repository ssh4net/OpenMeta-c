#ifndef OMC_TEST_ASSERT_H
#define OMC_TEST_ASSERT_H

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define OMC_TEST_ASSERT_UNUSED __attribute__((unused))
#else
#define OMC_TEST_ASSERT_UNUSED
#endif

static int OMC_TEST_ASSERT_UNUSED omc_test_check_failures = 0;

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail(const char* kind, const char* file, int line,
              const char* detail, int fatal)
{
    fprintf(stderr, "%s:%d: %s failed: %s\n", file, line, kind, detail);
    fflush(stderr);
    if (fatal) {
        exit(1);
    }
    omc_test_check_failures += 1;
}

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail_expr(const char* kind, const char* expr,
                   const char* file, int line, int fatal)
{
    omc_test_fail(kind, file, line, expr, fatal);
}

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail_ull_eq(const char* kind, const char* actual_expr,
                     unsigned long long actual_value,
                     const char* expected_expr,
                     unsigned long long expected_value,
                     const char* file, int line, int fatal)
{
    fprintf(stderr,
            "%s:%d: %s failed: %s == %s "
            "(actual=%llu expected=%llu)\n",
            file, line, kind, actual_expr, expected_expr,
            actual_value, expected_value);
    fflush(stderr);
    if (fatal) {
        exit(1);
    }
    omc_test_check_failures += 1;
}

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail_ll_eq(const char* kind, const char* actual_expr,
                    long long actual_value, const char* expected_expr,
                    long long expected_value,
                    const char* file, int line, int fatal)
{
    fprintf(stderr,
            "%s:%d: %s failed: %s == %s "
            "(actual=%lld expected=%lld)\n",
            file, line, kind, actual_expr, expected_expr,
            actual_value, expected_value);
    fflush(stderr);
    if (fatal) {
        exit(1);
    }
    omc_test_check_failures += 1;
}

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail_size_eq(const char* kind, const char* actual_expr,
                      size_t actual_value, const char* expected_expr,
                      size_t expected_value,
                      const char* file, int line, int fatal)
{
    fprintf(stderr,
            "%s:%d: %s failed: %s == %s "
            "(actual=%lu expected=%lu)\n",
            file, line, kind, actual_expr, expected_expr,
            (unsigned long)actual_value, (unsigned long)expected_value);
    fflush(stderr);
    if (fatal) {
        exit(1);
    }
    omc_test_check_failures += 1;
}

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail_mem_eq(const char* kind, const char* actual_expr,
                     const void* actual_data, size_t actual_size,
                     const char* expected_expr, const void* expected_data,
                     size_t expected_size,
                     const char* file, int line, int fatal)
{
    const unsigned char* actual_bytes;
    const unsigned char* expected_bytes;
    size_t diff_off;

    actual_bytes = (const unsigned char*)actual_data;
    expected_bytes = (const unsigned char*)expected_data;
    if (actual_size != expected_size) {
        fprintf(stderr,
                "%s:%d: %s failed: size(%s) == size(%s) "
                "(actual=%lu expected=%lu)\n",
                file, line, kind, actual_expr, expected_expr,
                (unsigned long)actual_size, (unsigned long)expected_size);
        fflush(stderr);
        if (fatal) {
            exit(1);
        }
        omc_test_check_failures += 1;
        return;
    }

    diff_off = 0U;
    while (diff_off < actual_size
           && actual_bytes[diff_off] == expected_bytes[diff_off]) {
        diff_off += 1U;
    }
    if (diff_off == actual_size) {
        fprintf(stderr,
                "%s:%d: %s failed: %s == %s "
                "(bytes differ, size=%lu)\n",
                file, line, kind, actual_expr, expected_expr,
                (unsigned long)actual_size);
    } else {
        fprintf(stderr,
                "%s:%d: %s failed: %s == %s "
                "(diff_off=%lu actual=0x%02X expected=0x%02X size=%lu)\n",
                file, line, kind, actual_expr, expected_expr,
                (unsigned long)diff_off,
                (unsigned)actual_bytes[diff_off],
                (unsigned)expected_bytes[diff_off],
                (unsigned long)actual_size);
    }
    fflush(stderr);
    if (fatal) {
        exit(1);
    }
    omc_test_check_failures += 1;
}

static void OMC_TEST_ASSERT_UNUSED
omc_test_fail_mem_contains(const char* kind, const char* haystack_expr,
                           size_t haystack_size, const char* needle_expr,
                           size_t needle_size,
                           const char* file, int line, int fatal)
{
    fprintf(stderr,
            "%s:%d: %s failed: %s contains %s "
            "(haystack_size=%lu needle_size=%lu)\n",
            file, line, kind, haystack_expr, needle_expr,
            (unsigned long)haystack_size, (unsigned long)needle_size);
    fflush(stderr);
    if (fatal) {
        exit(1);
    }
    omc_test_check_failures += 1;
}

#define omc_test_finish() \
    ((omc_test_check_failures != 0) \
         ? (fprintf(stderr, "test finished with %d check failure(s)\n", \
                    omc_test_check_failures), \
            fflush(stderr), 1) \
         : 0)

#define OMC_TEST_REQUIRE(expr) \
    ((expr) ? (void)0 \
            : omc_test_fail_expr("REQUIRE", #expr, __FILE__, __LINE__, 1))

#define OMC_TEST_CHECK(expr) \
    ((expr) ? (void)0 \
            : omc_test_fail_expr("CHECK", #expr, __FILE__, __LINE__, 0))

#define OMC_TEST_REQUIRE_U64_EQ(actual, expected) \
    do { \
        unsigned long long omc_test_actual_; \
        unsigned long long omc_test_expected_; \
        omc_test_actual_ = (unsigned long long)(actual); \
        omc_test_expected_ = (unsigned long long)(expected); \
        if (omc_test_actual_ != omc_test_expected_) { \
            omc_test_fail_ull_eq("REQUIRE", #actual, omc_test_actual_, \
                                 #expected, omc_test_expected_, \
                                 __FILE__, __LINE__, 1); \
        } \
    } while (0)

#define OMC_TEST_CHECK_U64_EQ(actual, expected) \
    do { \
        unsigned long long omc_test_actual_; \
        unsigned long long omc_test_expected_; \
        omc_test_actual_ = (unsigned long long)(actual); \
        omc_test_expected_ = (unsigned long long)(expected); \
        if (omc_test_actual_ != omc_test_expected_) { \
            omc_test_fail_ull_eq("CHECK", #actual, omc_test_actual_, \
                                 #expected, omc_test_expected_, \
                                 __FILE__, __LINE__, 0); \
        } \
    } while (0)

#define OMC_TEST_REQUIRE_S64_EQ(actual, expected) \
    do { \
        long long omc_test_actual_; \
        long long omc_test_expected_; \
        omc_test_actual_ = (long long)(actual); \
        omc_test_expected_ = (long long)(expected); \
        if (omc_test_actual_ != omc_test_expected_) { \
            omc_test_fail_ll_eq("REQUIRE", #actual, omc_test_actual_, \
                                #expected, omc_test_expected_, \
                                __FILE__, __LINE__, 1); \
        } \
    } while (0)

#define OMC_TEST_CHECK_S64_EQ(actual, expected) \
    do { \
        long long omc_test_actual_; \
        long long omc_test_expected_; \
        omc_test_actual_ = (long long)(actual); \
        omc_test_expected_ = (long long)(expected); \
        if (omc_test_actual_ != omc_test_expected_) { \
            omc_test_fail_ll_eq("CHECK", #actual, omc_test_actual_, \
                                #expected, omc_test_expected_, \
                                __FILE__, __LINE__, 0); \
        } \
    } while (0)

#define OMC_TEST_REQUIRE_SIZE_EQ(actual, expected) \
    do { \
        size_t omc_test_actual_; \
        size_t omc_test_expected_; \
        omc_test_actual_ = (size_t)(actual); \
        omc_test_expected_ = (size_t)(expected); \
        if (omc_test_actual_ != omc_test_expected_) { \
            omc_test_fail_size_eq("REQUIRE", #actual, omc_test_actual_, \
                                  #expected, omc_test_expected_, \
                                  __FILE__, __LINE__, 1); \
        } \
    } while (0)

#define OMC_TEST_CHECK_SIZE_EQ(actual, expected) \
    do { \
        size_t omc_test_actual_; \
        size_t omc_test_expected_; \
        omc_test_actual_ = (size_t)(actual); \
        omc_test_expected_ = (size_t)(expected); \
        if (omc_test_actual_ != omc_test_expected_) { \
            omc_test_fail_size_eq("CHECK", #actual, omc_test_actual_, \
                                  #expected, omc_test_expected_, \
                                  __FILE__, __LINE__, 0); \
        } \
    } while (0)

#define OMC_TEST_REQUIRE_MEM_EQ(actual, actual_size, expected, expected_size) \
    do { \
        const void* omc_test_actual_; \
        const void* omc_test_expected_; \
        size_t omc_test_actual_size_; \
        size_t omc_test_expected_size_; \
        omc_test_actual_ = (const void*)(actual); \
        omc_test_expected_ = (const void*)(expected); \
        omc_test_actual_size_ = (size_t)(actual_size); \
        omc_test_expected_size_ = (size_t)(expected_size); \
        if (omc_test_actual_size_ != omc_test_expected_size_ \
            || (omc_test_actual_size_ != 0U \
                && memcmp(omc_test_actual_, omc_test_expected_, \
                          omc_test_actual_size_) != 0)) { \
            omc_test_fail_mem_eq("REQUIRE", #actual, omc_test_actual_, \
                                 omc_test_actual_size_, #expected, \
                                 omc_test_expected_, omc_test_expected_size_, \
                                 __FILE__, __LINE__, 1); \
        } \
    } while (0)

#define OMC_TEST_CHECK_MEM_EQ(actual, actual_size, expected, expected_size) \
    do { \
        const void* omc_test_actual_; \
        const void* omc_test_expected_; \
        size_t omc_test_actual_size_; \
        size_t omc_test_expected_size_; \
        omc_test_actual_ = (const void*)(actual); \
        omc_test_expected_ = (const void*)(expected); \
        omc_test_actual_size_ = (size_t)(actual_size); \
        omc_test_expected_size_ = (size_t)(expected_size); \
        if (omc_test_actual_size_ != omc_test_expected_size_ \
            || (omc_test_actual_size_ != 0U \
                && memcmp(omc_test_actual_, omc_test_expected_, \
                          omc_test_actual_size_) != 0)) { \
            omc_test_fail_mem_eq("CHECK", #actual, omc_test_actual_, \
                                 omc_test_actual_size_, #expected, \
                                 omc_test_expected_, omc_test_expected_size_, \
                                 __FILE__, __LINE__, 0); \
        } \
    } while (0)

#define OMC_TEST_REQUIRE_MEM_CONTAINS(haystack, haystack_size, needle, needle_size) \
    do { \
        const unsigned char* omc_test_haystack_; \
        const unsigned char* omc_test_needle_; \
        size_t omc_test_haystack_size_; \
        size_t omc_test_needle_size_; \
        size_t omc_test_off_; \
        int omc_test_found_; \
        omc_test_haystack_ = (const unsigned char*)(haystack); \
        omc_test_needle_ = (const unsigned char*)(needle); \
        omc_test_haystack_size_ = (size_t)(haystack_size); \
        omc_test_needle_size_ = (size_t)(needle_size); \
        omc_test_found_ = (omc_test_needle_size_ == 0U); \
        for (omc_test_off_ = 0U; \
             omc_test_off_ + omc_test_needle_size_ <= omc_test_haystack_size_; \
             ++omc_test_off_) { \
            if (memcmp(omc_test_haystack_ + omc_test_off_, omc_test_needle_, \
                       omc_test_needle_size_) == 0) { \
                omc_test_found_ = 1; \
                break; \
            } \
        } \
        if (!omc_test_found_) { \
            omc_test_fail_mem_contains("REQUIRE", #haystack, \
                                       omc_test_haystack_size_, #needle, \
                                       omc_test_needle_size_, \
                                       __FILE__, __LINE__, 1); \
        } \
    } while (0)

#define OMC_TEST_CHECK_MEM_CONTAINS(haystack, haystack_size, needle, needle_size) \
    do { \
        const unsigned char* omc_test_haystack_; \
        const unsigned char* omc_test_needle_; \
        size_t omc_test_haystack_size_; \
        size_t omc_test_needle_size_; \
        size_t omc_test_off_; \
        int omc_test_found_; \
        omc_test_haystack_ = (const unsigned char*)(haystack); \
        omc_test_needle_ = (const unsigned char*)(needle); \
        omc_test_haystack_size_ = (size_t)(haystack_size); \
        omc_test_needle_size_ = (size_t)(needle_size); \
        omc_test_found_ = (omc_test_needle_size_ == 0U); \
        for (omc_test_off_ = 0U; \
             omc_test_off_ + omc_test_needle_size_ <= omc_test_haystack_size_; \
             ++omc_test_off_) { \
            if (memcmp(omc_test_haystack_ + omc_test_off_, omc_test_needle_, \
                       omc_test_needle_size_) == 0) { \
                omc_test_found_ = 1; \
                break; \
            } \
        } \
        if (!omc_test_found_) { \
            omc_test_fail_mem_contains("CHECK", #haystack, \
                                       omc_test_haystack_size_, #needle, \
                                       omc_test_needle_size_, \
                                       __FILE__, __LINE__, 0); \
        } \
    } while (0)

#ifdef assert
#undef assert
#endif

#define assert(expr) OMC_TEST_REQUIRE(expr)

#endif
