#include "omc_crc32.h"

omc_u32
omc_crc32_update(omc_u32 crc, const omc_u8 *bytes, omc_size size)
{
    static const omc_u32 table[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU, 0x76DC4190U, 0x6B6B51F4U,
        0x4DB26158U, 0x5005713CU, 0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU};
    omc_size i;
    for (i = 0U; i < size; ++i) {
        crc ^= bytes[i];
        crc = (crc >> 4U) ^ table[crc & 15U];
        crc = (crc >> 4U) ^ table[crc & 15U];
    }
    return crc;
}
