/* SPDX-License-Identifier: Apache-2.0
 * Synthetic regressions for the RD6 corpus fixes. No camera data is embedded. */
#ifndef OMC_READ_DECODE_VENDOR_FIXTURES_H
#define OMC_READ_DECODE_VENDOR_FIXTURES_H
#include <string.h>
static void omc_rd6_put(omc_u8 *p, omc_u32 n, unsigned width)
{
    unsigned i;
    for (i = 0U; i < width; ++i)
        p[i] = (omc_u8)(n >> (i * 8U));
}
static void omc_rd6_entry(omc_u8 *p, unsigned tag, unsigned type, omc_u32 count,
                          omc_u32 value)
{
    omc_rd6_put(p, tag, 2U);
    omc_rd6_put(p + 2U, type, 2U);
    omc_rd6_put(p + 4U, count, 4U);
    omc_rd6_put(p + 8U, value, 4U);
}
/* Caller provides at least 512 bytes. kind 0: Canon, 1: Sony, 2/3: Nikon,
 * 4: complete CR3 carrying a CMT3 TIFF directory. */
static omc_size omc_rd6_vendor_fixture(omc_u8 *out, unsigned kind)
{
    omc_u8 *raw;
    unsigned i;
    omc_u32 cj, ck;
    memset(out, 0, 512U);
    if (kind == 0U) {
        omc_rd6_put(out, 3U, 2U);
        omc_rd6_entry(out + 2U, 2U, 3U, 4U, 42U);
        omc_rd6_entry(out + 14U, 0x0dU, 7U, 128U, 50U);
        omc_rd6_entry(out + 26U, 0x4024U, 4U, 18U, 178U);
        omc_rd6_put(out + 42U, 1U, 2U);
        omc_rd6_put(out + 44U, 50U, 2U);
        omc_rd6_put(out + 48U, 6000U, 2U);
        raw = out + 50U;
        memset(raw, 0xff, 128U);
        omc_rd6_put(raw + 16U, 5U, 2U);
        omc_rd6_entry(raw + 18U, 0U, 3U, 0U, 0U);
        omc_rd6_entry(raw + 30U, 0U, 1U, 0U, 0U);
        omc_rd6_entry(raw + 42U, 0U, 7U, 0U, 0U);
        omc_rd6_entry(raw + 54U, 0U, 1U, 9U, 0xffffff00U);
        omc_rd6_entry(raw + 66U, 99U, 3U, 1U, 42U);
        omc_rd6_put(raw + 78U, 0U, 4U);
        raw = out + 178U;
        omc_rd6_put(raw, 72U, 4U);
        omc_rd6_put(raw + 8U, 1U, 4U);
        omc_rd6_put(raw + 12U, 20U, 4U);
        omc_rd6_put(raw + 16U, 1U, 4U);
        omc_rd6_put(raw + 20U, 0x201U, 4U);
        omc_rd6_put(raw + 24U, 1U, 4U);
        omc_rd6_put(raw + 28U, 7U, 4U);
        omc_rd6_put(raw + 32U, 2U, 4U);
        omc_rd6_put(raw + 36U, 36U, 4U);
        omc_rd6_put(raw + 40U, 2U, 4U);
        omc_rd6_put(raw + 44U, 0x402U, 4U);
        omc_rd6_put(raw + 48U, 1U, 4U);
        omc_rd6_put(raw + 52U, 2U, 4U);
        omc_rd6_put(raw + 56U, 0x403U, 4U);
        omc_rd6_put(raw + 60U, 2U, 4U);
        omc_rd6_put(raw + 64U, 300U, 4U);
        omc_rd6_put(raw + 68U, 700U, 4U);
        return 250U;
    }
    if (kind == 1U) {
        memcpy(out, "SONY", 4U);
        omc_rd6_put(out + 4U, 3U, 2U);
        /* Sony offsets use the enclosing TIFF base. The shared wrapper
         * places this note at 50 + sizeof("SONY") + sizeof("ILCE-6700"). */
        omc_rd6_entry(out + 6U, 0x9050U, 7U, 64U, 111U);
        omc_rd6_entry(out + 18U, 0x3000U, 7U, 168U, 175U);
        omc_rd6_entry(out + 30U, 0xb050U, 3U, 1U, 1U);
        /* Cipher byte 1 remains 1 under Sony's substitution; the selected
         * 9050D U32 field therefore decodes to 0x01010101. */
        memset(out + 46U, 1, 64U);
        raw = out + 110U;
        memcpy(raw, "II", 2U);
        omc_rd6_put(raw + 2U, 0x5eU, 2U);
        omc_rd6_put(raw + 0x30U, 2U, 2U);
        omc_rd6_put(raw + 0x32U, 0x25U, 2U);
        for (i = 0U; i < 8U; ++i)
            omc_rd6_put(raw + 0x5eU + (i / 4U) * 0x25U + (i % 4U) * 2U, 10U + i, 2U);
        return 278U;
    }
    if (kind == 2U || kind == 3U) {
        memcpy(out, "Nikon\0\2\0\0\0II\052\0\10\0\0\0", 18U);
        omc_rd6_put(out + 18U, kind == 2U ? 3U : 1U, 2U);
        omc_rd6_entry(out + 20U, 0x91U, 7U, 128U, 50U);
        if (kind == 2U) {
            omc_rd6_entry(out + 32U, 0x1dU, 2U, 2U, '1');
            omc_rd6_entry(out + 44U, 0xa7U, 4U, 1U, 1U);
        }
        raw = out + 60U;
        memcpy(raw, "0213", 4U);
        for (i = 4U; i < 128U; ++i)
            raw[i] = (omc_u8)i;
        if (kind == 2U) {
            /* Serial 1, shutter 1: pinned Nikon cipher seeds BF/BC. */
            cj = 0xbcU;
            ck = 0x60U;
            for (i = 4U; i < 128U; ++i) {
                cj = (cj + 0xbfU * ck) & 255U;
                ck = (ck + 1U) & 255U;
                raw[i] ^= (omc_u8)cj;
            }
        }
        return 188U;
    }
    memcpy(out, "\0\0\0\24ftypcrx \0\0\0\0crx ", 20U);
    memcpy(out + 20U, "\0\0\0\64CMT3II\052\0\10\0\0\0", 16U);
    omc_rd6_put(out + 36U, 2U, 2U);
    omc_rd6_entry(out + 38U, 0x10U, 4U, 1U, 0x80000331U);
    omc_rd6_entry(out + 50U, 2U, 3U, 4U, 38U);
    omc_rd6_put(out + 66U, 1U, 2U);
    omc_rd6_put(out + 68U, 50U, 2U);
    omc_rd6_put(out + 70U, 20U, 2U);
    omc_rd6_put(out + 72U, 30U, 2U);
    /* Box length: 8 header + 46 TIFF bytes. */
    out[23U] = 54U;
    /* Canon CMT boxes live within moov/uuid with the Canon metadata UUID. */
    memmove(out + 52U, out + 20U, 54U);
    memcpy(out + 20U, "\0\0\0\126moov\0\0\0\116uuid", 16U);
    {
        static const omc_u8 uuid[16] = {0x85U, 0xc0U, 0xb6U, 0x87U, 0x82U, 0x0fU,
                                        0x11U, 0xe0U, 0x81U, 0x11U, 0xf4U, 0xceU,
                                        0x46U, 0x2bU, 0x6aU, 0x48U};
        memcpy(out + 36U, uuid, sizeof(uuid));
    }
    return 106U;
}
#endif
