/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS, stage 2 (app.h): the sky computed, drawn, and playing.
 *
 * THE SKY (the main screen): the stars, the planets and the moon where they are above the chosen city now, on the
 * whole square. The sun, the moon and the clouds decide what shows (a full moon or a daylight sky hides the faint
 * stars); Kp makes the stars twinkle. Touch anything and the readings fade in over the sky in white Terminus,
 * hold, and fade out after two seconds.
 *   the sun's height and the moon's cycle come from the sky; the weather from the knobs (MANUAL) or drifting by
 *   itself (RANDOM): K2 CLOUDS (%)   K3 PRESS (hPa)   K4 KP   PRESET the pressure's trend (turning one: MANUAL)
 *   SELECT a planet on the screen (its engine and its voice)   SEL the pressure in the harmony, or not
 *   keys: the left half (F3..F#4) drone 1's note, the right half (G4..G5) drone 2's   OCT- / OCT+ their octave
 *   PLAY the drones on or off   MASTER the volume   HOME the settings
 * THE SETTINGS: the city, the date and the time (local), the weather. SELECT picks a row, ALGO changes it a step,
 * PRESET a big step, HOME saves and goes back. Without WiFi (stage 3) the clock runs from the time set here.
 * Part of the unity build (device/unity.c), after the platform: gfx.c is in scope. */
#include "fastmath.h"
#include "dsp/engine.h"
#include "dsp/astro.h"
#include "sky_data.h"

extern uint32_t shim_heap_used;
extern const uint32_t shim_heap_size;

#define C_TXT C_WHITE
#define C_MID RGB(150, 150, 150)
#define C_LOW RGB(80, 80, 80)
#define C_STORM RGB(255, 179, 138)
#define FIRST_KEY_NOTE 53                /* key 0 is F3 */
#define SPLIT_KEY 14                     /* keys 0..13 (F3..F#4) drone 1, 14..26 (G4..G5) drone 2 */
#define DEFAULT_UNIX 1793037600u         /* 2026-10-26 18:00 UTC: until a time is set */
#define SET_MAGIC 0x434F5331u            /* "COS1" */
#define FADE_HOLD_MS 2300u

/* ---------------------------------------------------------------- the state --- */
static struct {                          /* kept in the store (OBJ_SETTINGS) */
    uint32_t magic;
    uint32_t unix_saved;                 /* the clock when it was saved */
    uint8_t city, weather, linked, octave;   /* weather: 0 MANUAL, 1 RANDOM */
    uint8_t pc[2];
    int8_t trend, pad;
    float clouds, press, kp;
} set;

static sky_t sky = {.sun = 20.0f, .clouds = 30.0f, .press = 1015.0f, .trend = 0, .kp = 1.67f, .moon = 0.4f,
                    .note = {45, 52}, .linked = 1, .planet = PL_NONE, .on = 1};
static sky_now_t now_sky;                /* the last astro_now */
static uint8_t star_x[NSTARS], star_y[NSTARS];   /* the stars on the screen (y 255: not up, or off the square) */
static int16_t pl_x[5], pl_y[5];         /* the planets on the screen (-1: not seen) */
static int16_t moon_x = -1, moon_y;
static float limit;                      /* the faintest magnitude the sky lets through now */
static volatile int panic;
static int eng_ok;

static struct {
    uint32_t btn, keys;
    uint32_t clock_ms;                   /* plat_ms at clock_unix */
    uint32_t clock_unix;
    uint32_t touched;                    /* plat_ms of the last input: the readings show */
    float alpha;                         /* the readings' opacity, 0..1 */
    uint32_t last_sky, last_weather, last_save, last_frame;
    uint32_t rnd;
    int screen;                          /* 0 the sky, 1 the settings */
    int row;                             /* the settings' row */
    char msg[2][24];
    uint32_t msg_until;
    int dirty;                           /* settings changed: save on leaving the page */
} ui = {.rnd = 0x2545F491u};

static uint32_t unix_now(void) { return ui.clock_unix + (plat_ms() - ui.clock_ms) / 1000u; }
static void clock_set(uint32_t u)
{
    ui.clock_unix = u;
    ui.clock_ms = plat_ms();
}
static uint32_t rnd(void)
{
    ui.rnd ^= ui.rnd << 13;
    ui.rnd ^= ui.rnd >> 17;
    ui.rnd ^= ui.rnd << 5;
    return ui.rnd;
}
static float gauss(void)                 /* roughly normal: the sum of four uniforms */
{
    float s = 0.0f;
    int i;
    for (i = 0; i < 4; i++)
        s += (float)(rnd() & 0xFFFFu) / 65536.0f;
    return (s - 2.0f) * 1.732f;
}

/* each drone's note from its pitch class: drone 1 in the octave, drone 2 the first such note above drone 1 */
static void notes(void)
{
    sky.note[0] = 12 * (set.octave + 1) + set.pc[0];
    sky.note[1] = 12 * (set.octave + 1) + set.pc[1];
    if (sky.note[1] <= sky.note[0])
        sky.note[1] += 12;
}

static void defaults(void)
{
    set.magic = SET_MAGIC;
    set.unix_saved = DEFAULT_UNIX;
    set.city = 0;                        /* ROMA */
    set.weather = 1;
    set.linked = 1;
    set.octave = 2;
    set.pc[0] = 9;
    set.pc[1] = 4;
    set.trend = 0;
    set.clouds = 30.0f;
    set.press = 1015.0f;
    set.kp = 1.67f;
}
static void save(void)
{
    set.unix_saved = unix_now();
    set.clouds = sky.clouds;
    set.press = sky.press;
    set.kp = sky.kp;
    set.trend = (int8_t)sky.trend;
    set.linked = (uint8_t)sky.linked;
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    ui.last_save = plat_ms();
    ui.dirty = 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.city >= NCITIES || set.octave < 1 || set.octave > 4 || set.pc[0] > 11 || set.pc[1] > 11)
        defaults();
    clock_set(set.unix_saved);           /* the clock goes on from where it was saved */
    sky.clouds = set.clouds;
    sky.press = set.press;
    sky.kp = set.kp;
    sky.trend = set.trend;
    sky.linked = set.linked;
    eng_ok = eng_init() == 0;            /* the delay and the reverb, before WiFi takes the heap */
    notes();
    eng_sky(&sky);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    if (panic) {
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    eng_render(lr, n, (float)master_q12 / 4096.0f);
}

void app_panic(void) { panic = 1; }

static void copy(char *d, const char *s, unsigned max)
{
    unsigned i;
    for (i = 0; i + 1u < max && s && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

void app_say(const char *a, const char *b)
{
    copy(ui.msg[0], a, sizeof ui.msg[0]);
    copy(ui.msg[1], b, sizeof ui.msg[1]);
    ui.msg_until = plat_ms() + 1800u;
    ui.touched = plat_ms();
}

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 32, C_BLACK);
    cv_text((240 - text_w(&FONT_L, APP_NAME)) / 2, 0, &FONT_L, APP_NAME, C_WHITE);
    cv_blit(0, 104);
}

/* ------------------------------------------------------------------ the sky --- */
/* where everything is now; the sun and the moon to the engine; which planets are seen */
static void sky_update(void)
{
    const city_t *c = &CITIES[set.city];
    float lat = (float)c->lat100 / 100.0f, lon = (float)c->lon100 / 100.0f;
    int32_t i, x, y;
    astro_now((int32_t)(unix_now() - (uint32_t)J2000_UNIX), lat, lon, &now_sky);
    sky.sun = now_sky.sun_alt;
    sky.moon = now_sky.moon;
    /* the faintest star the sky lets through: daylight, twilight, the moon if it is up, the clouds */
    limit = 4.6f;
    if (sky.sun > -6.0f)
        limit = -5.0f;
    else if (sky.sun > -12.0f)
        limit = 1.0f;
    else if (sky.sun > -18.0f)
        limit = 3.0f;
    if (now_sky.moon_pos.alt > 0.0f)
        limit -= 0.8f * now_sky.moon_lit;
    limit -= sky.clouds / 100.0f * 4.0f;
    for (i = 0; i < (int32_t)NSTARS; i++) {
        altaz_t p = astro_altaz((float)STARS[i].ra * (FM_TWO_PI / 65536.0f), (float)STARS[i].dec * 0.000174532925f,
                                now_sky.lst, lat * 0.0174532925f);
        if ((float)STARS[i].mag10 / 10.0f <= limit && astro_project(p, &x, &y)) {
            star_x[i] = (uint8_t)x;
            star_y[i] = (uint8_t)y;
        } else {
            star_y[i] = 255;
        }
    }
    for (i = 0; i < 5; i++) {            /* a planet is seen while it is drawn: up, on the square, bright enough */
        if (now_sky.planet_mag[i] <= limit + (sky.sun < 0.0f ? 2.5f : 0.0f) && astro_project(now_sky.planet[i], &x, &y)
            && x >= 2 && y >= 2 && x < 238 && y < 238) {
            pl_x[i] = (int16_t)x;
            pl_y[i] = (int16_t)y;
        } else {
            pl_x[i] = -1;
        }
    }
    moon_x = -1;
    if (sky.sun < 8.0f && astro_project(now_sky.moon_pos, &x, &y)) {
        moon_x = (int16_t)x;
        moon_y = (int16_t)y;
    }
    /* a chosen planet that leaves the screen takes its engine and its voice along */
    if (sky.planet != PL_NONE && pl_x[sky.planet - 1] < 0) {
        app_say(planet_name(sky.planet), "GONE");
        sky.planet = PL_NONE;
    }
    eng_sky(&sky);
}

/* the weather drifting by itself, each source at its own pace (offline, and until WiFi brings the real one) */
static void weather_tick(float hours)
{
    static float press_then = 1015.0f, since = 0.0f;
    float sq = fm_sqrtf(hours);
    sky.press = fm_clampf(sky.press - (sky.press - 1013.0f) / 60.0f * hours + 2.2f * sq * gauss(), 965.0f, 1045.0f);
    sky.clouds = fm_clampf(sky.clouds - (sky.clouds - 45.0f) / 8.0f * hours + 16.0f * sq * gauss(), 0.0f, 100.0f);
    sky.kp = fm_clampf(sky.kp - (sky.kp - 1.7f) / 7.0f * hours + 0.5f * sq * gauss(), 0.0f, 9.0f);
    if ((float)(rnd() & 0xFFFFFFu) / 16777216.0f < hours / 250.0f)   /* a storm, now and then */
        sky.kp = 5.0f + (float)(rnd() & 0xFFFFu) / 65536.0f * 3.5f;
    since += hours;
    if (since >= 3.0f) {                 /* the trend: the last three hours */
        float dp = sky.press - press_then;
        sky.trend = dp < -1.0f ? -1 : dp > 1.0f ? 1 : 0;
        press_then = sky.press;
        since = 0.0f;
    }
}

/* ---- text: a line of 28 characters, a source on the left and what it does on the right */
static char *put(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}
static char *put_int(char *d, int32_t v)
{
    char t[12];
    int k = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *d++ = '-';
    do {
        t[k++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (k)
        *d++ = t[--k];
    *d = 0;
    return d;
}
static char *put2(char *d, int32_t v)    /* two digits */
{
    *d++ = (char)('0' + v / 10 % 10);
    *d++ = (char)('0' + v % 10);
    *d = 0;
    return d;
}
static int32_t round_f(float v) { return (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f); }

/* left (16 columns, padded) and right (12, right-aligned) into one line */
static void line2(char *out, const char *left, const char *right)
{
    int i, l = 0, r = 0;
    while (left[l])
        l++;
    while (right[r])
        r++;
    for (i = 0; i < 28; i++)
        out[i] = ' ';
    for (i = 0; i < l && i < 16; i++)
        out[i] = left[i];
    for (i = 0; i < r && i < 12; i++)
        out[28 - r + i] = right[i];
    out[28] = 0;
}
/* a label and a value, the value right-aligned in w columns */
static void field(char *out, const char *label, const char *value, int w)
{
    int i, n = 0, l = 0;
    while (label[l])
        l++;
    while (value[n])
        n++;
    for (i = 0; i < w; i++)
        out[i] = ' ';
    for (i = 0; i < l && i < w; i++)
        out[i] = label[i];
    for (i = 0; i < n && i < w; i++)
        out[w - n + i] = value[i];
    out[w] = 0;
}

/* the local time at the chosen city: hours, minutes; and the date */
static void local_time(uint32_t u, int32_t *y, int32_t *mo, int32_t *d, int32_t *h, int32_t *mi)
{
    const city_t *c = &CITIES[set.city];
    uint32_t l = u + (uint32_t)utc_offset(u, c->utc4, c->dst);
    civil_from_days((int32_t)(l / 86400u), y, mo, d);
    *h = (int32_t)(l % 86400u / 3600u);
    *mi = (int32_t)(l % 3600u / 60u);
}

#define NLINES 11
#define TY0 32
/* the readings, line by line, with their colours */
static void readings(char lines[NLINES][32], uint16_t col[NLINES])
{
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    sky_map_t m;
    char l[32], r[32], v[20], *q;
    int32_t fil, y, mo, d, h, mi, i;
    sky_map(&sky, &m);
    for (i = 0; i < NLINES; i++)
        col[i] = C_TXT;
    q = put(v, NOTE[sky.note[0] % 12]);
    q = put_int(q, sky.note[0] / 12 - 1);
    q = put(put(q, " "), NOTE[sky.note[1] % 12]);
    put_int(q, sky.note[1] / 12 - 1);
    line2(lines[0], sky.on ? "COSMOS" : "COSMOS  (OFF)", v);
    copy(lines[1], "----------------------------", 32);
    col[1] = C_LOW;
    fil = round_f(fm_log2f(m.cutoff / 160.0f) / fm_log2f(7000.0f / 160.0f) * 99.0f);
    q = put(v, sky.sun >= 0.0f ? "+" : "");
    put_int(q, round_f(sky.sun));
    field(l, "SUN", v, 12);
    put_int(v, fil);
    field(r, "FILTER", v, 12);
    line2(lines[2], l, r);
    put(put_int(v, round_f(sky.clouds)), "%");
    field(l, "CLOUDS", v, 12);
    put_int(v, round_f(m.verb * 100.0f));
    field(r, "REVERB", v, 12);
    line2(lines[3], l, r);
    q = put_int(v, round_f(sky.press));
    put(q, sky.trend < 0 ? " v" : sky.trend > 0 ? " ^" : " =");
    field(l, "PRESS", v, 12);
    field(r, "PARTNER", sky.linked ? m.interval_name : "8VA", 12);
    line2(lines[4], l, r);
    q = put_int(v, round_f(sky.kp * 10.0f) / 10);
    q = put(q, ".");
    put_int(q, round_f(sky.kp * 10.0f) % 10);
    field(l, "KP", v, 12);
    if (m.storm > 0.0f)
        put(v, "STORM");
    else
        put_int(v, round_f(m.drift * 99.0f));
    field(r, "DRIFT", v, 12);
    line2(lines[5], l, r);
    if (m.storm > 0.0f)
        col[5] = C_STORM;
    field(l, "MOON", moon_name(sky.moon), 12);
    put_int(v, round_f(m.sub * 198.0f));
    field(r, "SUB", v, 12);
    line2(lines[6], l, r);
    copy(lines[7], "----------------------------", 32);
    col[7] = C_LOW;
    if (sky.planet != PL_NONE) {
        field(l, "PLANET", planet_name(sky.planet), 16);
        line2(lines[8], l, planet_voice(sky.planet));
    } else {
        int seen = 0;
        for (i = 0; i < 5; i++)
            seen += pl_x[i] >= 0;
        put(put_int(v, seen), seen == 1 ? " PLANET IN SIGHT" : " PLANETS IN SIGHT");
        copy(lines[8], seen ? v : "NO PLANET IN SIGHT", 32);
        col[8] = C_MID;
    }
    if (plat_ms() < ui.msg_until) {
        line2(lines[9], ui.msg[0], ui.msg[1]);
    } else {
        local_time(unix_now(), &y, &mo, &d, &h, &mi);
        copy(l, CITIES[set.city].name, 14);
        q = put2(v, h);
        q = put(q, ":");
        put2(q, mi);
        line2(lines[9], l, v);
        col[9] = C_MID;
    }
    copy(lines[10], set.weather ? "RANDOM WEATHER" : "MANUAL WEATHER", 32);
    col[10] = C_LOW;
}

static uint16_t grey(int32_t g)
{
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    return RGB(g, g, g);
}
static uint16_t scale(uint16_t c, float a)  /* a colour at an opacity, over black */
{
    int32_t r = (int32_t)((float)(c >> 11) * a), g = (int32_t)((float)((c >> 5) & 63u) * a),
            b = (int32_t)((float)(c & 31u) * a);
    return (uint16_t)(r << 11 | g << 5 | b);
}

/* one frame of the sky, in five strips of 48 rows: stars, planets, the moon, then the readings at their opacity */
static void sky_frame(void)
{
    char lines[NLINES][32];
    uint16_t col[NLINES];
    int32_t s, i, j, tw = (int32_t)(6.0f + 7.0f * sky.kp);
    if (ui.alpha > 0.01f)
        readings(lines, col);
    for (s = 0; s < 5; s++) {
        int32_t y0 = s * 48;
        cv_begin(240, 48, C_BLACK);
        for (i = 0; i < (int32_t)NSTARS; i++) {   /* the stars twinkle, more with Kp */
            int32_t y = star_y[i], g;
            if (y == 255 || y < y0 || y >= y0 + 48)
                continue;
            g = 200 - (int32_t)((float)STARS[i].mag10 * 2.6f) - 39 + (int32_t)(rnd() % (uint32_t)(2 * tw + 1)) - tw;
            cv_pset(star_x[i], y - y0, grey(g < 40 ? 40 : g > 215 ? 215 : g));
        }
        for (i = 0; i < 5; i++) {        /* the planets hold steady */
            int32_t x = pl_x[i], y = pl_y[i], g = 205 - (int32_t)((now_sky.planet_mag[i] + 1.5f) * 18.0f);
            uint16_t c;
            if (x < 0 || y < y0 - 8 || y >= y0 + 56)
                continue;
            c = grey(g < 120 ? 120 : g > 220 ? 220 : g);
            cv_pset(x, y - y0, c);
            if (i == 1 || i == 3) {      /* Venus and Jupiter: 2 x 2 */
                cv_pset(x + 1, y - y0, c);
                cv_pset(x, y + 1 - y0, c);
                cv_pset(x + 1, y + 1 - y0, c);
            }
            if (i == 4) {                /* Saturn's rings */
                cv_pset(x - 1, y - y0, grey(g / 2));
                cv_pset(x + 1, y - y0, grey(g / 2));
            }
            if (sky.planet == i + 1)     /* chosen: four corners around it */
                for (j = 0; j < 4; j++) {
                    int32_t sx = j & 1 ? 1 : -1, sy = j & 2 ? 1 : -1, cx = x + (sx > 0 ? 5 : -4), cy = y + (sy > 0 ? 5 : -4);
                    cv_pset(cx, cy - y0, grey(150));
                    cv_pset(cx - sx, cy - y0, grey(150));
                    cv_pset(cx, cy - sy - y0, grey(150));
                }
        }
        if (moon_x >= 0 && moon_y >= y0 - 5 && moon_y < y0 + 53) {   /* the moon, its phase drawn */
            float k = now_sky.moon_lit;
            int waxing = now_sky.moon < 0.5f, dx, dy;
            for (dy = -4; dy <= 4; dy++)
                for (dx = -4; dx <= 4; dx++) {
                    float half, edge;
                    int lit;
                    if (dx * dx + dy * dy > 17)
                        continue;
                    half = fm_sqrtf((float)(16 - dy * dy > 0 ? 16 - dy * dy : 0));
                    edge = half * (1.0f - 2.0f * k);
                    lit = waxing ? (float)dx > edge : (float)-dx > edge;
                    cv_pset(moon_x + dx, moon_y + dy - y0, grey(lit ? 210 : 45));
                }
        }
        if (ui.alpha > 0.01f)            /* the readings: a black outline, then the letters at their opacity */
            for (i = 0; i < NLINES; i++) {
                int32_t ty = TY0 + i * 16 - y0;
                if (ty <= -17 || ty >= 49)
                    continue;
                if (ui.alpha > 0.2f) {
                    cv_text(7, ty, &FONT_S, lines[i], C_BLACK);
                    cv_text(9, ty, &FONT_S, lines[i], C_BLACK);
                    cv_text(8, ty - 1, &FONT_S, lines[i], C_BLACK);
                    cv_text(8, ty + 1, &FONT_S, lines[i], C_BLACK);
                }
                cv_text(8, ty, &FONT_S, lines[i], scale(col[i], ui.alpha));
            }
        cv_blit(0, (uint32_t)y0);
    }
}

/* ------------------------------------------------------------- the settings --- */
#define NROWS 4
static void settings_frame(void)
{
    static const char *const LABEL[NROWS] = {"CITY", "DATE", "TIME", "WEATHER"};
    char t[32], v[24], *q;
    int32_t y, mo, d, h, mi, i;
    local_time(unix_now(), &y, &mo, &d, &h, &mi);
    for (i = 0; i < 13; i++) {
        cv_begin(240, 16, C_BLACK);
        t[0] = 0;
        if (i == 0)
            copy(t, "SETTINGS", 32);
        else if (i == 1 || i == 6)
            copy(t, "----------------------------", 32);
        else if (i >= 2 && i < 2 + NROWS) {
            int r = i - 2;
            if (r == 0)
                copy(v, CITIES[set.city].name, 24);
            else if (r == 1) {
                q = put_int(v, y);
                q = put(q, "-");
                q = put2(q, mo);
                q = put(q, "-");
                put2(q, d);
            } else if (r == 2) {
                q = put2(v, h);
                q = put(q, ":");
                put2(q, mi);
            } else {
                copy(v, set.weather ? "RANDOM" : "MANUAL", 24);
            }
            {
                char l[20];
                copy(l, r == ui.row ? "> " : "  ", 20);
                put(l + 2, LABEL[r]);
                line2(t, l, v);
            }
        } else if (i == 7)
            copy(t, "SELECT ROW   ALGO CHANGE", 32);
        else if (i == 8)
            copy(t, "PRESET BIG STEP", 32);
        else if (i == 9)
            copy(t, "HOME SAVE AND BACK", 32);
        else if (i == 11) {
            q = put(t, "CPU ");
            q = put(put_int(q, (int32_t)plat_cpu_pct()), "%  HEAP ");
            put(put_int(q, (int32_t)((shim_heap_size - shim_heap_used) / 1024u)), "K");
        } else if (i == 12) {
            int b = plat_battery();
            q = put(t, "BATTERY ");
            if (b == 4)
                put(q, "USB");
            else if (b >= 0)
                put(put_int(q, b * 100 / 3), "%");
            else
                put(q, "-");
        }
        cv_text(8, 0, &FONT_S, t, i >= 7 && i <= 9 ? C_MID : i >= 11 ? C_LOW : (i == 1 || i == 6) ? C_LOW : C_TXT);
        cv_blit(0, (uint32_t)(16 + i * 16));
    }
}

static void settings_change(int32_t step, int big)
{
    uint32_t u = unix_now();
    switch (ui.row) {
    case 0:
        set.city = (uint8_t)(((int32_t)set.city + step * (big ? 5 : 1)) % (int32_t)NCITIES + (int32_t)NCITIES) % NCITIES;
        break;
    case 1:
        clock_set(u + (uint32_t)(step * (big ? 30 : 1) * 86400));
        break;
    case 2:
        clock_set(u + (uint32_t)(step * (big ? 3600 : 60)));
        break;
    default:
        set.weather = !set.weather;
        break;
    }
    ui.dirty = 1;
    ui.last_sky = 0;                     /* the sky again, at once */
}

/* ------------------------------------------------------------------- input --- */
static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, keys = plat_keys(), kdown = keys & ~ui.keys;
    int32_t e1 = plat_enc(EN_K1), e2 = plat_enc(EN_K2), e3 = plat_enc(EN_K3), e4 = plat_enc(EN_K4);
    int32_t ea = plat_enc(EN_ALGO), ep = plat_enc(EN_PRESET), es = plat_enc(EN_SELECT);
    int changed = 0, k;
    ui.btn = btn;
    ui.keys = keys;
    (void)e1;
    if (btn || keys || e1 || e2 || e3 || e4 || ea || ep || es)
        ui.touched = plat_ms();
    if (down >> B_HOME & 1u) {
        if (ui.screen == 1 && ui.dirty)
            save();
        ui.screen = !ui.screen;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        return;
    }
    if (ui.screen == 1) {
        if (es)
            ui.row = ((ui.row + (es > 0 ? 1 : -1)) % NROWS + NROWS) % NROWS;
        if (ea)
            settings_change(ea > 0 ? 1 : -1, 0);
        if (ep)
            settings_change(ep > 0 ? 1 : -1, 1);
        return;
    }
    /* the weather on the knobs: turning one takes it out of RANDOM */
    if (e2 || e3 || e4 || ep) {
        if (set.weather) {
            set.weather = 0;
            app_say("WEATHER", "MANUAL");
        }
        ui.dirty = 1;
        changed = 1;
    }
    if (e2) sky.clouds = fm_clampf(sky.clouds + (float)e2 * 3.0f, 0.0f, 100.0f);
    if (e3) sky.press = fm_clampf(sky.press + (float)e3 * 1.0f, 965.0f, 1045.0f);
    if (e4) sky.kp = fm_clampf(sky.kp + (float)e4 * 0.33f, 0.0f, 9.0f);
    if (ep) sky.trend = (int32_t)fm_clampf((float)(sky.trend + (ep > 0 ? 1 : -1)), -1.0f, 1.0f);
    if (es) {                            /* a planet in sight, or none: round the ones on the screen */
        int32_t p = sky.planet, n;
        for (n = 0; n < PL_N; n++) {
            p = ((p + (es > 0 ? 1 : -1)) % PL_N + PL_N) % PL_N;
            if (p == PL_NONE || pl_x[p - 1] >= 0)
                break;
        }
        sky.planet = p;
        changed = 1;
    }
    if (down >> B_SEL & 1u) {
        sky.linked = !sky.linked;
        app_say("PRESSURE", sky.linked ? "SETS PARTNER" : "OFF: OCTAVE");
        ui.dirty = 1;
        changed = 1;
    }
    if (down >> B_OCTDN & 1u && set.octave > 1) { set.octave--; ui.dirty = 1; changed = 1; }
    if (down >> B_OCTUP & 1u && set.octave < 4) { set.octave++; ui.dirty = 1; changed = 1; }
    if (down >> B_PLAY & 1u) {
        sky.on = !sky.on;
        changed = 1;
    }
    for (k = 0; k < NKEYS; k++)
        if (kdown >> k & 1u) {           /* the left half moves drone 1, the right half drone 2 */
            set.pc[k >= SPLIT_KEY] = (uint8_t)((FIRST_KEY_NOTE + k) % 12);
            ui.dirty = 1;
            changed = 1;
        }
    if (changed) {
        notes();
        eng_sky(&sky);
    }
}
void app_input_only(void) { input(); }

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.touched = plat_ms();              /* the readings show at the start */
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    if (ui.last_sky == 0 || now - ui.last_sky > 2000u) {   /* the sky every 2 s: the stars move slowly */
        ui.last_sky = now ? now : 1u;
        sky_update();
    }
    if (now - ui.last_weather >= 1000u) {   /* the random weather, at its real pace */
        if (set.weather)
            weather_tick((float)(now - ui.last_weather) / 3600000.0f);
        ui.last_weather = now;
    }
    if (ui.dirty && ui.screen == 0 && now - ui.touched > 10000u)   /* settings: saved once the hands are off */
        save();
    if (now - ui.last_save > 30u * 60u * 1000u)   /* and the clock, every half hour */
        save();
    {   /* the readings: in fast after a touch, held, then out slowly */
        float to = now - ui.touched < FADE_HOLD_MS ? 1.0f : 0.0f;
        ui.alpha += (to - ui.alpha) * (to > ui.alpha ? 0.5f : 0.09f);
        if (ui.alpha < 0.01f && to == 0.0f)
            ui.alpha = 0.0f;
    }
    if (now - ui.last_frame < (ui.alpha > 0.0f && ui.alpha < 1.0f ? 60u : 120u))   /* ~16 frames a second fading */
        return;
    ui.last_frame = now;
    if (ui.screen == 0)
        sky_frame();
    else
        settings_frame();
}

#ifdef PLAT_HOST
uint32_t shim_heap_used = 0;
const uint32_t shim_heap_size = 278528;
#endif
