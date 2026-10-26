/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's sky (astro.h).
 *   the sun: the Astronomical Almanac's low-precision formulas (about 0.01 degree)
 *   the moon: its mean longitude, the six largest terms of its orbit and its parallax (a few tenths of a degree),
 *     its phase from its elongation
 *   the planets: JPL's approximate Keplerian elements, J2000 and their rates a century (Standish), as seen from the
 *     Earth-Moon barycentre: about half a degree, checked against PyEphem
 * Large angles are kept small before they meet a float: whole days and the seconds in the day apart, every rate
 * applied to a time near J2000, every angle wrapped before its sine. */
#include <stdint.h>
#include "astro.h"
#include "fastmath.h"

#define RAD 0.0174532925199433f
#define OBLIQ (23.43928f * RAD)

static float wrap360(float a) { return a - 360.0f * fm_floorf(a / 360.0f); }

/* atan for |x| <= 1, a minimax polynomial (error ~1e-5 rad), and the rest from it */
static float atan01(float x)
{
    float z = x * x;
    return x * (0.99997726f + z * (-0.33262347f + z * (0.19354346f + z * (-0.11643287f + z * (0.05265332f
                + z * -0.01172120f)))));
}
static float atan2_(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), r;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    r = ay <= ax ? atan01(ay / ax) : FM_PI * 0.5f - atan01(ax / ay);
    if (x < 0.0f)
        r = FM_PI - r;
    return y < 0.0f ? -r : r;
}
static float asin_(float x)
{
    x = fm_clampf(x, -1.0f, 1.0f);
    return atan2_(x, fm_sqrtf(1.0f - x * x));
}

/* ecliptic to equatorial */
static void ecl2eq(float lon, float lat, float *ra, float *dec)
{
    float sl = fm_sinf(lon), cl = fm_cosf(lon), sb = fm_sinf(lat), cb = fm_cosf(lat), se = fm_sinf(OBLIQ),
          ce = fm_cosf(OBLIQ);
    *ra = atan2_(sl * cb * ce - sb * se, cl * cb);
    *dec = asin_(sb * ce + cb * se * sl);
}

altaz_t astro_altaz(float ra, float dec, float lst, float lat)
{
    altaz_t p;
    float h = lst - ra, sh, ch, sd = fm_sinf(dec), cd = fm_cosf(dec), sp = fm_sinf(lat), cp = fm_cosf(lat);
    h -= FM_TWO_PI * fm_floorf(h / FM_TWO_PI);
    sh = fm_sinf(h);
    ch = fm_cosf(h);
    p.alt = asin_(sp * sd + cp * cd * ch);
    p.az = atan2_(sh, ch * sp - (sd / cd) * cp) + FM_PI;   /* from north through east */
    return p;
}

int astro_project(altaz_t p, int32_t *x, int32_t *y)
{
    float r = 170.0f * fm_tanf((FM_PI * 0.5f - p.alt) * 0.5f);
    float fx = 120.0f - r * fm_sinf(p.az), fy = 120.0f - r * fm_cosf(p.az);
    if (p.alt < 0.0f || fx < 0.0f || fy < 0.0f || fx >= 240.0f || fy >= 240.0f)
        return 0;
    *x = (int32_t)fx;
    *y = (int32_t)fy;
    return 1;
}

/* the planets: a, e, I, L, long. of perihelion, long. of the node, then their rates a century */
static const float ELEM[6][12] = {
    {0.38709927f, 0.20563593f, 7.00497902f, 252.25032350f, 77.45779628f, 48.33076593f,
     0.00000037f, 0.00001906f, -0.00594749f, 149472.67411175f, 0.16047689f, -0.12534081f},     /* Mercury */
    {0.72333566f, 0.00677672f, 3.39467605f, 181.97909950f, 131.60246718f, 76.67984255f,
     0.00000390f, -0.00004107f, -0.00078890f, 58517.81538729f, 0.00268329f, -0.27769418f},     /* Venus */
    {1.52371034f, 0.09339410f, 1.84969142f, -4.55343205f, -23.94362959f, 49.55953891f,
     0.00001847f, 0.00007882f, -0.00813131f, 19140.30268499f, 0.44441088f, -0.29257343f},      /* Mars */
    {5.20288700f, 0.04838624f, 1.30439695f, 34.39644051f, 14.72847983f, 100.47390909f,
     -0.00011607f, -0.00013253f, -0.00183714f, 3034.74612775f, 0.21252668f, 0.20469106f},      /* Jupiter */
    {9.53667594f, 0.05386179f, 2.48599187f, 49.95424423f, 92.59887831f, 113.66242448f,
     -0.00125060f, -0.00050991f, 0.00193609f, 1222.49362201f, -0.41897216f, -0.28867794f},     /* Saturn */
    {1.00000261f, 0.01671123f, -0.00001531f, 100.46457166f, 102.93768193f, 0.0f,
     0.00000562f, -0.00004392f, -0.01294668f, 35999.37244981f, 0.32327364f, 0.0f},             /* Earth-Moon */
};
static const float MAG0[5] = {-0.2f, -4.1f, 0.0f, -2.2f, 0.7f};   /* typical magnitudes; Mars from its distance */

static void helio(int i, float T, float out[3])
{
    const float *el = ELEM[i];
    float a = el[0] + el[6] * T, e = el[1] + el[7] * T, I = (el[2] + el[8] * T) * RAD;
    float L = wrap360(el[3] + el[9] * T), w = el[4] + el[10] * T, O = el[5] + el[11] * T;
    float M = wrap360(L - w), E, xp, yp, om, co, so, cO, sO, cI, sI;
    int k;
    if (M > 180.0f)
        M -= 360.0f;
    M *= RAD;
    E = M + e * fm_sinf(M);
    for (k = 0; k < 6; k++)
        E -= (E - e * fm_sinf(E) - M) / (1.0f - e * fm_cosf(E));
    xp = a * (fm_cosf(E) - e);
    yp = a * fm_sqrtf(1.0f - e * e) * fm_sinf(E);
    om = wrap360(w - O) * RAD;
    O = wrap360(O) * RAD;
    co = fm_cosf(om);
    so = fm_sinf(om);
    cO = fm_cosf(O);
    sO = fm_sinf(O);
    cI = fm_cosf(I);
    sI = fm_sinf(I);
    out[0] = (co * cO - so * sO * cI) * xp + (-so * cO - co * sO * cI) * yp;
    out[1] = (co * sO + so * cO * cI) * xp + (-so * sO + co * cO * cI) * yp;
    out[2] = so * sI * xp + co * sI * yp;
}

void astro_now(int32_t t, float lat_deg, float lon_deg, sky_now_t *s)
{
    int32_t day = t / 86400, sec = t % 86400;
    float d, T, g, q, L, slon, sra, sdec, lat = lat_deg * RAD, Lm, Mm, Fm, mlon, mlat, mra, mdec, el, earth[3];
    int i;
    if (sec < 0) {
        sec += 86400;
        day--;
    }
    d = (float)day + (float)sec / 86400.0f;
    T = d / 36525.0f;
    /* local sidereal time: 360.98564736629 d = 360 (whole turns) x day + the rest, kept small */
    s->lst = wrap360(280.46061837f + 0.98564736629f * (float)day + 360.98564736629f * (float)sec / 86400.0f
                     + lon_deg) * RAD;
    /* the sun */
    g = wrap360(357.529f + 0.98560028f * d) * RAD;
    q = wrap360(280.459f + 0.98564736f * d);
    L = wrap360(q + 1.915f * fm_sinf(g) + 0.020f * fm_sinf(2.0f * g));
    slon = L * RAD;
    ecl2eq(slon, 0.0f, &sra, &sdec);
    s->sun_alt = astro_altaz(sra, sdec, s->lst, lat).alt / RAD;
    /* the moon */
    Lm = wrap360(218.316f + 13.176396f * d);
    Mm = wrap360(134.963f + 13.064993f * d) * RAD;
    Fm = wrap360(93.272f + 13.229350f * d) * RAD;
    {   /* the equation of the centre, the evection, the variation, the annual equation; the latitude's main term */
        float D = wrap360(297.850f + 12.190749f * d) * RAD;
        mlon = wrap360(Lm + 6.289f * fm_sinf(Mm) - 1.274f * fm_sinf(Mm - 2.0f * D) + 0.658f * fm_sinf(2.0f * D)
                       - 0.186f * fm_sinf(g) - 0.059f * fm_sinf(2.0f * Mm - 2.0f * D)
                       - 0.057f * fm_sinf(Mm - 2.0f * D + g) + 0.053f * fm_sinf(Mm + 2.0f * D));
        mlat = (5.128f * fm_sinf(Fm) - 0.173f * fm_sinf(Fm - 2.0f * D)) * RAD;
    }
    ecl2eq(mlon * RAD, mlat, &mra, &mdec);
    s->moon_pos = astro_altaz(mra, mdec, s->lst, lat);
    s->moon_pos.alt -= 0.95f * RAD * fm_cosf(s->moon_pos.alt);   /* its parallax: seen from here, not the centre */
    el = wrap360(mlon - L);
    s->moon = el / 360.0f;
    s->moon_lit = (1.0f - fm_cosf(el * RAD)) * 0.5f;
    /* the planets, from the Earth */
    helio(5, T, earth);
    for (i = 0; i < 5; i++) {
        float p[3], x, y, z, ye, ze, r, delta, ra, dec;
        helio(i, T, p);
        x = p[0] - earth[0];
        y = p[1] - earth[1];
        z = p[2] - earth[2];
        ye = y * fm_cosf(OBLIQ) - z * fm_sinf(OBLIQ);
        ze = y * fm_sinf(OBLIQ) + z * fm_cosf(OBLIQ);
        ra = atan2_(ye, x);
        dec = atan2_(ze, fm_sqrtf(x * x + ye * ye));
        s->planet[i] = astro_altaz(ra, dec, s->lst, lat);
        r = fm_sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        delta = fm_sqrtf(x * x + y * y + z * z);
        s->planet_mag[i] = i == 2 ? -1.5f + 5.0f * fm_log2f(r * delta) * 0.30103f : MAG0[i];
    }
}

/* ---- civil time (Howard Hinnant's algorithms) */
int32_t days_from_civil(int32_t y, int32_t m, int32_t d)
{
    int32_t era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void civil_from_days(int32_t z, int32_t *y, int32_t *m, int32_t *d)
{
    int32_t era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}
int32_t weekday(int32_t days) { return days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6; }

/* the day of the n-th (1..) Sunday of a month, or of the last (n = 0) */
static int32_t sunday(int32_t y, int32_t m, int32_t n)
{
    int32_t first = days_from_civil(y, m, 1), w = weekday(first), d;
    if (n > 0)
        return first + (7 - w) % 7 + 7 * (n - 1);
    d = days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - 1;
    return d - weekday(d);
}

int32_t utc_offset(uint32_t u, int32_t utc4, int32_t rule)
{
    int32_t std = utc4 * 900, y, mo, dd, on = 0;
    uint32_t a, b, ls = u + (uint32_t)std;                 /* local standard time */
    civil_from_days((int32_t)(ls / 86400u), &y, &mo, &dd);
    switch (rule) {
    case DST_EU:                                           /* 01:00 UTC, last Sundays of March and October */
        a = (uint32_t)sunday(y, 3, 0) * 86400u + 3600u;
        b = (uint32_t)sunday(y, 10, 0) * 86400u + 3600u;
        on = u >= a && u < b;
        break;
    case DST_US:                                           /* 02:00 local, 2nd Sunday of March to 1st of November */
        a = (uint32_t)sunday(y, 3, 2) * 86400u + 7200u;
        b = (uint32_t)sunday(y, 11, 1) * 86400u + 3600u;   /* 02:00 summer time is 01:00 standard */
        on = ls >= a && ls < b;
        break;
    case DST_AU:                                           /* 02:00 standard, 1st Sunday of October to 1st of April */
        a = (uint32_t)sunday(y, 4, 1) * 86400u + 7200u;
        b = (uint32_t)sunday(y, 10, 1) * 86400u + 7200u;
        on = ls < a || ls >= b;
        break;
    case DST_NZ:                                           /* last Sunday of September to 1st Sunday of April */
        a = (uint32_t)sunday(y, 4, 1) * 86400u + 7200u;
        b = (uint32_t)sunday(y, 9, 0) * 86400u + 7200u;
        on = ls < a || ls >= b;
        break;
    default:
        break;
    }
    return std + (on ? 3600 : 0);
}
