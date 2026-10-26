/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's sky: where the sun, the moon, the planets and the stars are, for a place and a time, to a fraction of a
 * degree (plenty on 240 pixels), in single-precision floats with no libm.
 * Time is seconds since J2000.0 (2000-01-01 12:00 UTC) in an int32: good from 1932 to 2068. */
#pragma once
#include <stdint.h>

#define J2000_UNIX 946728000             /* J2000.0 as a Unix time */

typedef struct {
    float alt, az;                       /* radians: height above the horizon; azimuth from north through east */
} altaz_t;

typedef struct {
    float sun_alt;                       /* the sun's height, degrees */
    altaz_t sun_pos;
    float moon;                          /* the moon's cycle: 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter */
    float moon_lit;                      /* its lit fraction, 0..1 */
    altaz_t moon_pos;
    altaz_t planet[5];                   /* Mercury, Venus, Mars, Jupiter, Saturn */
    float planet_mag[5];
    float lst;                           /* local sidereal time, radians: for the stars */
} sky_now_t;

void astro_now(int32_t t, float lat_deg, float lon_deg, sky_now_t *s);
altaz_t astro_altaz(float ra, float dec, float lst, float lat);   /* radians in, radians out */

/* the screen's projection: stereographic, zenith at the centre, north up, east on the left (looking up); the
 * square's corners touch the horizon. 0 = off the square */
int astro_project(altaz_t p, int32_t *x, int32_t *y);

/* civil time: days from 1970-01-01 for a date, and back; the weekday (0 Sunday) */
int32_t days_from_civil(int32_t y, int32_t m, int32_t d);
void civil_from_days(int32_t z, int32_t *y, int32_t *m, int32_t *d);
int32_t weekday(int32_t days);
