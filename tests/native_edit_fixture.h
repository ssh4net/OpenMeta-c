#ifndef OMC_TEST_NATIVE_EDIT_FIXTURE_H
#define OMC_TEST_NATIVE_EDIT_FIXTURE_H
#include "omc/omc_base.h"
#include <string.h>

static void native_put(omc_u8* p, omc_u64 v, unsigned n, int little)
{
    unsigned i;
    for (i = 0U; i < n; ++i)
        p[little ? i : n - i - 1U] = (omc_u8)(v >> (8U * i));
}

static omc_u64 native_get(const omc_u8* p, unsigned n, int little)
{
    omc_u64 v;
    unsigned i;
    v = 0U;
    for (i = 0U; i < n; ++i) v |= (omc_u64)p[little ? i : n - i - 1U] << (8U * i);
    return v;
}

static omc_u8* native_entry(omc_u8* file, unsigned dir, unsigned index,
                            int big, int little, omc_u16 tag, omc_u16 type,
                            omc_u32 count, omc_u64 value)
{
    omc_u8* p;
    p = file + dir + (big ? 8U : 2U) + index * (big ? 20U : 12U);
    native_put(p, tag, 2U, little);
    native_put(p + 2U, type, 2U, little);
    native_put(p + 4U, count, big ? 8U : 4U, little);
    native_put(p + (big ? 12U : 8U), value, big ? 8U : 4U, little);
    return p + (big ? 12U : 8U);
}

/* Every out-of-line value has a fixed absolute offset. MakerNote and pixel
 * sentinels must survive edits byte-for-byte at those offsets. */
static void native_fixture(omc_u8 file[1024], int big, int little)
{
    omc_u8* p;
    memset(file, 0, 1024U);
    file[0] = file[1] = (omc_u8)(little ? 'I' : 'M');
    native_put(file + 2U, big ? 43U : 42U, 2U, little);
    if (big) native_put(file + 4U, 8U, 2U, little);
    native_put(file + (big ? 8U : 4U), 16U, big ? 8U : 4U, little);
    native_put(file + 16U, 7U, big ? 8U : 2U, little);
    p = native_entry(file, 16U, 0U, big, little, 0x010FU, 2U, 8U, 704U);
    if (big) memcpy(p, "Camera!", 8U);
    native_entry(file, 16U, 1U, big, little, 0x0111U, 4U, 1U, 0U);
    p = file + 16U + (big ? 8U : 2U) + (big ? 20U : 12U) + (big ? 12U : 8U);
    native_put(p, 976U, 4U, little);
    native_entry(file, 16U, 2U, big, little, 0x0132U, 2U, 20U, 720U);
    native_entry(file, 16U, 3U, big, little, 0x8769U, big ? 18U : 4U, 1U, 256U);
    native_entry(file, 16U, 4U, big, little, 0x8825U, big ? 18U : 4U, 1U, 448U);
    p = native_entry(file, 16U, 5U, big, little, 0xC612U, 1U, 4U, 0U);
    memcpy(p, "\1\6\0\0", 4U);
    p = native_entry(file, 16U, 6U, big, little, 0xC7FFU, 7U, 8U, 960U);
    if (big) memcpy(p, "OPAQUE!!", 8U);
    native_put(file + 256U, 6U, big ? 8U : 2U, little);
    native_entry(file, 256U, 0U, big, little, 0x829AU, 5U, 1U, 800U);
    p = file + 256U + (big ? 8U : 2U) + (big ? 12U : 8U);
    if (big) { native_put(p, 1U, 4U, little); native_put(p + 4U, 125U, 4U, little); }
    p = native_entry(file, 256U, 1U, big, little, 0x9000U, 7U, 4U, 0U);
    memcpy(p, "0232", 4U);
    native_entry(file, 256U, 2U, big, little, 0x9003U, 2U, 20U, 752U);
    p = native_entry(file, 256U, 3U, big, little, 0x927CU, 7U, 8U, 944U);
    if (big) memcpy(p, "NOTE!!!!", 8U);
    p = native_entry(file, 256U, 4U, big, little, 0xA001U, 3U, 1U, 0U);
    native_put(p, 1U, 2U, little);
    native_entry(file, 256U, 5U, big, little, 0xA005U, big ? 18U : 4U, 1U, 640U);
    native_put(file + 448U, 4U, big ? 8U : 2U, little);
    p = native_entry(file, 448U, 0U, big, little, 0U, 1U, 4U, 0U);
    memcpy(p, "\2\3\0\0", 4U);
    p = native_entry(file, 448U, 1U, big, little, 1U, 2U, 2U, 0U);
    memcpy(p, "N", 2U);
    native_entry(file, 448U, 2U, big, little, 2U, 5U, 3U, 832U);
    p = native_entry(file, 448U, 3U, big, little, 18U, 2U, 4U, 0U);
    memcpy(p, "WGS", 4U);
    native_put(file + 640U, 1U, big ? 8U : 2U, little);
    p = native_entry(file, 640U, 0U, big, little, 1U, 2U, 4U, 0U);
    memcpy(p, "R98", 4U);
    memcpy(file + 704U, "Camera!", 8U);
    memcpy(file + 720U, "2020:01:02 03:04:05", 20U);
    memcpy(file + 752U, "2020:01:02 03:04:05", 20U);
    native_put(file + 800U, 1U, 4U, little);
    native_put(file + 804U, 125U, 4U, little);
    native_put(file + 832U, 51U, 4U, little);
    native_put(file + 836U, 1U, 4U, little);
    native_put(file + 844U, 1U, 4U, little);
    native_put(file + 852U, 1U, 4U, little);
    memcpy(file + 944U, "NOTE!!!!", 8U);
    memcpy(file + 960U, "OPAQUE!!", 8U);
    memcpy(file + 976U, "PIXELS!!", 8U);
}

static const omc_u8* native_find(const omc_u8* file, omc_size size,
                                 omc_u64 dir, omc_u16 tag, int big, int little)
{
    omc_u64 count, i, at;
    unsigned cs, es;
    cs = big ? 8U : 2U;
    es = big ? 20U : 12U;
    if (dir == 0U || dir > size || cs > size - dir) return NULL;
    count = native_get(file + (omc_size)dir, cs, little);
    if (count > (size - dir - cs) / es) return NULL;
    for (i = 0U; i < count; ++i) {
        at = dir + cs + i * es;
        if (native_get(file + (omc_size)at, 2U, little) == tag)
            return file + (omc_size)at;
    }
    return NULL;
}
#endif
