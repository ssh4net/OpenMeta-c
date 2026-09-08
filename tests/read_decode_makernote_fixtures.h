/* SPDX-License-Identifier: Apache-2.0
 * Bounded synthetic regressions. Phase One layout follows the pinned C++
 * make_phaseone_makernote_main fixture at f11de3e0. */
#ifndef OMC_READ_DECODE_MAKERNOTE_FIXTURES_H
#define OMC_READ_DECODE_MAKERNOTE_FIXTURES_H
static void omc_rd5_fixture_u32(omc_u8 *out, omc_size off, omc_u32 value)
{
    unsigned i;
    for (i = 0U; i < 4U; ++i)
        out[off + i] = (omc_u8)(value >> (i * 8U));
}
static omc_size omc_rd5_makernote_fixture(omc_u8 *out, unsigned kind)
{
    unsigned i;
    memset(out, 0, 128U);
    if (kind == 0U) {
        memcpy(out, "IIIICwaR", 8U);
        omc_rd5_fixture_u32(out, 8U, 0x20U);
        omc_rd5_fixture_u32(out, 0x20U, 3U);
        for (i = 0U; i < 3U; ++i) {
            omc_size off = 0x28U + i * 16U;
            omc_rd5_fixture_u32(out, off,
                                i == 0U ? 0x100U : (i == 1U ? 0x105U : 0x110U));
            omc_rd5_fixture_u32(out, off + 4U, 4U);
            omc_rd5_fixture_u32(out, off + 8U, i == 2U ? 32U : 4U);
            omc_rd5_fixture_u32(out, off + 12U, i == 0U ? 1U : (i == 1U ? 35U : 0x60U));
        }
        memcpy(out + 0x60U, "IIII\001\000\000\000", 8U);
        omc_rd5_fixture_u32(out, 0x68U, 12U);
        omc_rd5_fixture_u32(out, 0x6CU, 1U);
        omc_rd5_fixture_u32(out, 0x74U, 0x400U);
        omc_rd5_fixture_u32(out, 0x78U, 4U);
        omc_rd5_fixture_u32(out, 0x7CU, 7U);
        return 128U;
    }
    if (kind == 1U) {
        out[0] = 5U;
        for (i = 0U; i < 5U; ++i) {
            omc_size off = 2U + i * 12U;
            out[off] = (omc_u8)(i + 1U);
            out[off + 2U] = i < 3U ? 3U : 2U;
            omc_rd5_fixture_u32(out, off + 4U, i < 3U ? 1U : 8U);
            omc_rd5_fixture_u32(out, off + 8U, i < 3U ? 17U + i : 0x100000U);
        }
        return 66U;
    }
    if (kind == 2U) {
        out[0] = 1U;
        out[2] = 1U;
        out[4] = 3U;
        omc_rd5_fixture_u32(out, 6U, 3U);
        omc_rd5_fixture_u32(out, 10U, 50U);
        out[18] = 3U;
        out[20] = 17U;
        out[22] = 19U;
        return 24U;
    }
    memcpy(out, "SONY", 4U);
    out[4] = 1U;
    out[6] = 1U;
    out[8] = 3U;
    omc_rd5_fixture_u32(out, 10U, 1U);
    omc_rd5_fixture_u32(out, 14U, 42U);
    omc_rd5_fixture_u32(out, 18U, 0xFFFFFF00U);
    return 22U;
}
#endif
