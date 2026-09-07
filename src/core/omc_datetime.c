#include "omc_datetime_internal.h"
#include <string.h>

static int
omc_decimal_digits(const omc_u8 *p, omc_size n)
{
    omc_size i;
    int v;
    v = 0;
    for (i = 0U; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

static int
omc_date_valid(int year, int month, int day)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int limit;
    if (year < 1 || month < 1 || month > 12 || day < 1)
        return 0;
    limit = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
        limit++;
    return day <= limit;
}

static int
omc_time_valid(const omc_u8 *p, int spaced)
{
    int h;
    int m;
    int s;
    h = omc_decimal_digits(p, 2U);
    m = omc_decimal_digits(p + (spaced ? 3U : 2U), 2U);
    s = omc_decimal_digits(p + (spaced ? 6U : 4U), 2U);
    return h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s <= 60;
}

int
omc_datetime_parse(const omc_u8 *p, omc_size n, omc_datetime *out)
{
    omc_size i;
    omc_size first;
    int h;
    int m;
    memset(out, 0, sizeof(*out));
    if (p == NULL || n < 10U || p[4] != '-' || p[7] != '-' ||
        !omc_date_valid(omc_decimal_digits(p, 4U), omc_decimal_digits(p + 5U, 2U),
                        omc_decimal_digits(p + 8U, 2U)))
        return 0;
    memcpy(out->date, p, 10U);
    if (n == 10U)
        return 1;
    if (n < 19U || p[10] != 'T' || p[13] != ':' || p[16] != ':' ||
        !omc_time_valid(p + 11U, 1))
        return 0;
    memcpy(out->time, p + 11U, 8U);
    i = 19U;
    if (i < n && p[i] == '.') {
        first = ++i;
        while (i < n && p[i] >= '0' && p[i] <= '9')
            i++;
        if (i == first || i - first > 9U)
            return 0;
        memcpy(out->fraction, p + first, i - first);
    }
    if (i == n)
        return 1;
    if (p[i] == 'Z') {
        memcpy(out->offset, "+00:00", 6U);
        return i + 1U == n;
    }
    if (n - i != 6U || (p[i] != '+' && p[i] != '-') || p[i + 3U] != ':')
        return 0;
    h = omc_decimal_digits(p + i + 1U, 2U);
    m = omc_decimal_digits(p + i + 4U, 2U);
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return 0;
    memcpy(out->offset, p + i, 6U);
    return 1;
}

int
omc_iptc_project_datetime(const omc_store *store, omc_u16 dataset, char *out,
                          omc_size *size, omc_entry_id *date_entry)
{
    const omc_entry *e;
    omc_const_bytes b;
    omc_size i;
    int have_date;
    int h;
    int m;
    have_date = 0;
    *size = 0U;
    *date_entry = OMC_INVALID_ENTRY_ID;
    for (i = 0U; i < store->entry_count; ++i) {
        e = &store->entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET || e->key.u.iptc_dataset.record != 2U ||
            e->key.u.iptc_dataset.dataset != dataset ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            (e->value.kind != OMC_VAL_TEXT && e->value.kind != OMC_VAL_BYTES))
            continue;
        b = omc_arena_view(&store->arena, e->value.u.ref);
        if (b.size != 8U || !omc_date_valid(omc_decimal_digits(b.data, 4U),
                                            omc_decimal_digits(b.data + 4U, 2U),
                                            omc_decimal_digits(b.data + 6U, 2U)))
            continue;
        memcpy(out, b.data, 4U);
        out[4] = '-';
        memcpy(out + 5U, b.data + 4U, 2U);
        out[7] = '-';
        memcpy(out + 8U, b.data + 6U, 2U);
        out[10] = 0;
        *size = 10U;
        *date_entry = (omc_entry_id)i;
        have_date = 1;
        break;
    }
    if (!have_date)
        return 0;
    for (i = 0U; i < store->entry_count; ++i) {
        e = &store->entries[i];
        if (e->key.kind != OMC_KEY_IPTC_DATASET || e->key.u.iptc_dataset.record != 2U ||
            e->key.u.iptc_dataset.dataset != (dataset == 55U ? 60U : 63U) ||
            (e->flags & OMC_ENTRY_FLAG_DELETED) != 0U ||
            (e->value.kind != OMC_VAL_TEXT && e->value.kind != OMC_VAL_BYTES))
            continue;
        b = omc_arena_view(&store->arena, e->value.u.ref);
        if ((b.size != 6U && b.size != 11U) || !omc_time_valid(b.data, 0))
            continue;
        if (b.size == 11U) {
            h = omc_decimal_digits(b.data + 7U, 2U);
            m = omc_decimal_digits(b.data + 9U, 2U);
            if ((b.data[6] != '+' && b.data[6] != '-') || h < 0 || h > 23 || m < 0 ||
                m > 59)
                continue;
        }
        out[10] = 'T';
        memcpy(out + 11U, b.data, 2U);
        out[13] = ':';
        memcpy(out + 14U, b.data + 2U, 2U);
        out[16] = ':';
        memcpy(out + 17U, b.data + 4U, 2U);
        out[19] = 0;
        *size = 19U;
        if (b.size == 11U) {
            memcpy(out + 19U, b.data + 6U, 3U);
            out[22] = ':';
            memcpy(out + 23U, b.data + 9U, 2U);
            out[25] = 0;
            *size = 25U;
        }
        break;
    }
    return 1;
}
