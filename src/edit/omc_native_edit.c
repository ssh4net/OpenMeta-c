#include "omc_native_edit_internal.h"
#include "../core/omc_value_internal.h"

#include <string.h>

static const omc_u16 ifd0_tags[OMC_NATIVE_IFD0_REMOVAL_CAPACITY] = {
    0x010EU, 0x010FU, 0x0110U, 0x0131U, 0x0132U, 0x013BU, 0x8298U
};
static const omc_u16 exif_tags[OMC_NATIVE_EXIF_REMOVAL_CAPACITY] = {
    0x829AU, 0x829DU, 0x8822U, 0x8827U, 0x8828U, 0x8830U, 0x8831U, 0x8832U,
    0x8833U, 0x8834U, 0x8835U, 0x9000U, 0x9003U, 0x9004U, 0x9010U,
    0x9011U, 0x9012U, 0x9201U, 0x9202U, 0x9203U, 0x9204U, 0x9205U, 0x9206U,
    0x9207U, 0x9209U, 0x920AU, 0x9214U, 0x9286U, 0x9287U, 0x9290U, 0x9291U,
    0x9292U, 0x9400U, 0x9401U, 0x9402U, 0x9403U, 0x9404U, 0x9405U,
    0xA000U, 0xA004U, 0xA20BU, 0xA20CU, 0xA20EU, 0xA20FU, 0xA210U,
    0xA214U, 0xA215U, 0xA217U, 0xA300U, 0xA301U, 0xA302U, 0xA401U, 0xA402U,
    0xA403U, 0xA404U, 0xA405U, 0xA406U, 0xA407U, 0xA408U, 0xA409U, 0xA40AU,
    0xA40BU, 0xA40CU, 0xA40DU, 0xA40EU, 0xA40FU, 0xA410U, 0xA411U, 0xA412U,
    0xA420U, 0xA430U, 0xA432U, 0xA433U, 0xA434U, 0xA435U, 0xA436U,
    0xA437U, 0xA438U, 0xA439U, 0xA43AU, 0xA43BU, 0xA43CU,
    0xA460U, 0xA461U, 0xA462U
};

static unsigned
native_slot(const omc_u16* tags, unsigned count, omc_u16 tag)
{
    unsigned i;
    for (i = 0U; i < count; ++i)
        if (tags[i] == tag) return i;
    return count;
}

int
omc_native_edit_removal_allowed(unsigned directory, omc_u16 tag)
{
    if (directory == 0U)
        return native_slot(ifd0_tags, OMC_NATIVE_IFD0_REMOVAL_CAPACITY, tag)
               < OMC_NATIVE_IFD0_REMOVAL_CAPACITY;
    if (directory == 1U)
        return native_slot(exif_tags, OMC_NATIVE_EXIF_REMOVAL_CAPACITY, tag)
               < OMC_NATIVE_EXIF_REMOVAL_CAPACITY;
    return directory == 2U && tag >= 1U && tag <= 31U;
}

void
omc_native_edit_intent_init(omc_native_edit_intent* intent)
{
    if (intent != NULL) memset(intent, 0, sizeof(*intent));
}

omc_status
omc_native_edit_intent_validate(const omc_native_edit_intent* intent)
{
    const omc_u16* lists[3];
    omc_u32 counts[3];
    unsigned d;
    omc_u32 i;
    if (intent == NULL ||
        intent->ifd0_removal_count > OMC_NATIVE_IFD0_REMOVAL_CAPACITY ||
        intent->exif_removal_count > OMC_NATIVE_EXIF_REMOVAL_CAPACITY ||
        intent->gps_removal_count > OMC_NATIVE_GPS_REMOVAL_CAPACITY ||
        (intent->merge_exif != 0 && intent->merge_exif != 1) ||
        (intent->merge_gps != 0 && intent->merge_gps != 1) ||
        (intent->clear_exif != 0 && intent->clear_exif != 1) ||
        (intent->clear_gps != 0 && intent->clear_gps != 1) ||
        (intent->clear_exif && (intent->merge_exif || intent->exif_removal_count)) ||
        (intent->clear_gps && (intent->merge_gps || intent->gps_removal_count)))
        return OMC_STATUS_INVALID_ARGUMENT;
    lists[0] = intent->ifd0_removals;
    lists[1] = intent->exif_removals;
    lists[2] = intent->gps_removals;
    counts[0] = intent->ifd0_removal_count;
    counts[1] = intent->exif_removal_count;
    counts[2] = intent->gps_removal_count;
    for (d = 0U; d < 3U; ++d)
        for (i = 0U; i < counts[d]; ++i)
            if (!omc_native_edit_removal_allowed(d, lists[d][i]) ||
                (i != 0U && lists[d][i - 1U] >= lists[d][i]))
                return OMC_STATUS_INVALID_ARGUMENT;
    return OMC_STATUS_OK;
}

int
omc_native_edit_intent_present(const omc_native_edit_intent* intent)
{
    return intent != NULL && (intent->ifd0_removal_count != 0U ||
        intent->exif_removal_count != 0U || intent->gps_removal_count != 0U ||
        intent->merge_exif || intent->merge_gps ||
        intent->clear_exif || intent->clear_gps);
}

omc_status
omc_native_edit_intent_collect(const omc_store* source,
                               omc_native_edit_intent* intent)
{
    static const char* const names[3] = {"ifd0", "exififd", "gpsifd"};
    unsigned char live[3][OMC_NATIVE_EXIF_REMOVAL_CAPACITY];
    unsigned char deleted[3][OMC_NATIVE_EXIF_REMOVAL_CAPACITY];
    int clear[2], live_pointer[2];
    omc_native_edit_intent result;
    omc_size i;
    unsigned d, slot;
    omc_u16 tag;
    omc_const_bytes name;
    const omc_entry* entry;
    omc_status status;
    if (source == NULL || intent == NULL || !omc_store_shape_valid(source))
        return OMC_STATUS_INVALID_ARGUMENT;
    if (source->entry_count > 200000U) return OMC_STATUS_OVERFLOW;
    omc_native_edit_intent_init(&result);
    memset(live, 0, sizeof(live));
    memset(deleted, 0, sizeof(deleted));
    memset(clear, 0, sizeof(clear));
    memset(live_pointer, 0, sizeof(live_pointer));
    for (i = 0U; i < source->entry_count; ++i) {
        entry = &source->entries[i];
        if (entry->key.kind != OMC_KEY_EXIF_TAG) continue;
        if (!omc_ref_valid(&source->arena, entry->key.u.exif_tag.ifd))
            return OMC_STATUS_INVALID_ARGUMENT;
        name = omc_arena_view(&source->arena, entry->key.u.exif_tag.ifd);
        for (d = 0U; d < 3U; ++d)
            if (name.size == strlen(names[d]) &&
                memcmp(name.data, names[d], name.size) == 0) break;
        if (d == 3U) continue;
        tag = entry->key.u.exif_tag.tag;
        if (d == 0U && (tag == 0x8769U || tag == 0x8825U)) {
            slot = tag == 0x8769U ? 0U : 1U;
            if (!(entry->flags & OMC_ENTRY_FLAG_DELETED)) live_pointer[slot] = 1;
            else if (entry->flags & OMC_ENTRY_FLAG_DIRTY) clear[slot] = 1;
        }
        if (entry->flags & OMC_ENTRY_FLAG_DIRTY) {
            if ((d == 0U && tag == 0x0132U) ||
                (d == 1U && (tag == 0xA001U ||
                             omc_native_edit_removal_allowed(d, tag))))
                result.merge_exif = 1;
            if (d == 2U && tag >= 1U && tag <= 31U) result.merge_gps = 1;
        }
        if (d == 0U) slot = native_slot(ifd0_tags, OMC_NATIVE_IFD0_REMOVAL_CAPACITY, tag);
        else if (d == 1U) slot = native_slot(exif_tags, OMC_NATIVE_EXIF_REMOVAL_CAPACITY, tag);
        else slot = tag >= 1U && tag <= 31U ? (unsigned)tag - 1U : OMC_NATIVE_EXIF_REMOVAL_CAPACITY;
        if ((d == 0U && slot == OMC_NATIVE_IFD0_REMOVAL_CAPACITY) ||
            slot >= OMC_NATIVE_EXIF_REMOVAL_CAPACITY) continue;
        if (entry->flags & OMC_ENTRY_FLAG_DELETED) {
            if (entry->flags & OMC_ENTRY_FLAG_DIRTY) deleted[d][slot] = 1U;
        } else live[d][slot] = 1U;
    }
    for (slot = 0U; slot < OMC_NATIVE_IFD0_REMOVAL_CAPACITY; ++slot)
        if (deleted[0][slot] && !live[0][slot])
            result.ifd0_removals[result.ifd0_removal_count++] = ifd0_tags[slot];
    for (slot = 0U; slot < OMC_NATIVE_EXIF_REMOVAL_CAPACITY; ++slot)
        if (deleted[1][slot] && !live[1][slot])
            result.exif_removals[result.exif_removal_count++] = exif_tags[slot];
    for (slot = 0U; slot < OMC_NATIVE_GPS_REMOVAL_CAPACITY; ++slot)
        if (deleted[2][slot] && !live[2][slot])
            result.gps_removals[result.gps_removal_count++] = (omc_u16)(slot + 1U);
    result.clear_exif = clear[0] && !live_pointer[0];
    result.clear_gps = clear[1] && !live_pointer[1];
    status = omc_native_edit_intent_validate(&result);
    if (status == OMC_STATUS_OK) *intent = result;
    return status;
}
