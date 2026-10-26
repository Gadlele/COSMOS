/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS (app.h): the sky computed, drawn, and playing.
 *
 * THE SKY (the main screen): the sun, the moon, the planets and the stars where they are above the chosen place now,
 * on the whole square. The sun, the moon and the clouds decide what shows (a full moon or a daylight sky hides the
 * faint stars); Kp makes the stars twinkle. The readings stay over it in white Terminus; LFO leaves the sky alone
 * (a touch brings the readings back for two seconds), ARP the sky with the arpeggiator's line.
 * THE ARPEGGIATOR: a line sweeps the square in 16 s; the bright stars it crosses pluck a note of the scale by their
 * height, louder the brighter; the planets it crosses too, louder still. On the ARP screen what plays lights up.
 * THE BUTTONS
 *   HOME   back to nature: the sun and the moon as they are, the weather as it comes (fetched with WiFi, stage 3;
 *          until then drifting by itself)
 *   EDIT   everything by hand, the readings held on the screen: K1 the sun's height, K2 the clouds, K3 the pressure,
 *          K4 Kp, ALGO the moon's cycle, PRESET the pressure's trend
 *   SEL    the next planet up (in EDIT any of the seven classical ones, the Sun and the Moon among them), or none
 *   LFO    the sky alone, and back      ARP the ARP screen, the arpeggio on; again, off and back
 *   GLO    the settings      PLAY the sky stopped or playing (stopping saves)      OCT- / OCT+ the octave
 *   MASTER the volume. The buttons that do something glow, the ones in use bright; the keys glow on the scale,
 *   bright on its root. Saving silences the sound for a moment: only stopping (PLAY) and leaving GLO save.
 * THE HARMONY. The drones always play a chord: drone 1 its root, its partner the fifth, drone 2 the third (a fourth,
 * suspended, when the pressure is deep down), its partner coloured by the pressure (the octave high and steady, the
 * seventh as it falls, the ninth deep down), in the mode the sun's height gives (Phrygian at night .. Lydian at
 * noon). With no planet the chord is on the last key's note. A chosen planet brings its note (its period raised by
 * octaves into hearing: Cousto's cosmic octave, exact under TUNING COSMIC, the nearest tempered note under EQUAL;
 * under NATURAL a natural note each, the spheres' classical order from the Moon's C to Saturn's B) and four chords of
 * its own on its scale, one every 10 s; a key then plays the chord of the scale on its note, held and for 10 s after,
 * and the four come back. With the sky stopped, a key still sounds its chord, held and for 10 s after.
 * THE SETTINGS (GLO): latitude, longitude, the offset from UTC, summer time, the date and the time (local), the
 * pressure in the harmony, the tuning. SELECT picks a row, ALGO changes it a step, PRESET a big step, GLO or HOME
 * saves and goes back. Without WiFi (stage 3) the clock runs from the time set here.
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
#define DEFAULT_UNIX 1793037600u         /* 2026-10-26 18:00 UTC: until a time is set */
#define SET_MAGIC 0x434F5334u            /* "COS4" */
#define FADE_HOLD_MS 2300u
#define CHORD_MS 10000u                  /* a chord every 10 s */
#define KEY_HOLD_MS 10000u               /* a key's note stays 10 s after it is let go */

/* ---------------------------------------------------------------- the state --- */
static struct {                          /* kept in the store (OBJ_SETTINGS) */
    uint32_t magic;
    uint32_t unix_saved;                 /* the clock when it was saved */
    int16_t lat100, lon100;              /* the place: hundredths of a degree, north and east positive */
    int8_t utc4;                         /* the offset from UTC, quarter hours */
    uint8_t summer, manual, linked;      /* summer time; EDIT (else HOME: nature); the pressure in the harmony */
    uint8_t octave, tuning;              /* tuning 0 COSMIC, 1 EQUAL, 2 NATURAL */
    uint8_t pc[2];                       /* with no planet, the chord's root: a pitch class ([1] unused) */
    int8_t trend;
    uint8_t arp, pad[2];                 /* the arpeggiator on */
    float clouds, press, kp, sun, moon;  /* the sky by hand (EDIT) */
} set;

static sky_t sky = {.sun = 20.0f, .clouds = 30.0f, .press = 1015.0f, .trend = 0, .kp = 1.67f, .moon = 0.4f,
                    .hz = {110.0f, 164.81f}, .linked = 1, .planet = PL_NONE, .on = 1};
static sky_now_t now_sky;                /* the last astro_now */
static uint8_t star_x[NSTARS], star_y[NSTARS];   /* the stars on the screen (y 255: not up, or off the square) */
static int16_t pl_x[PL_N], pl_y[PL_N];   /* each planet on the square, by PL_* (x -1: below the horizon or off it) */
static uint8_t pl_drawn[PL_N];           /* bright enough to show through the sky now */
static uint8_t pl_up[PL_N];              /* can be chosen from nature: up, on the square, the sky dark enough for it */
static uint8_t star_flash[NSTARS];       /* a star the arpeggiator just played: frames it stays bright */
static uint8_t pl_flash[PL_N];           /* a planet it just played: frames its glow lasts */
static uint8_t star_dim[(NSTARS + 7) / 8];   /* a star the sky would hide (day, clouds, the moon): drawn faint */
#define STAR_FLOOR 3.0f                  /* the stars down to this magnitude always show, faint when hidden */
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
    uint32_t chords_t0;                  /* when the chosen planet's chords began */
    int32_t chord;                       /* the chord playing, 0..3 */
    int32_t mode;                        /* the scale's mode, 0 Phrygian .. 5 Lydian */
    int32_t key_deg;                     /* a key's chord: its scale degree */
    uint32_t key_until;                  /* until when (plat_ms); a held key keeps pushing it on */
    int view_sky;                        /* LFO: the sky alone, the readings only on a touch */
    uint32_t lit[4];                     /* the lights last set: bright and dim buttons, bright and dim keys */
    uint32_t sweep_x;                    /* the arpeggiator's line, the last column it reached */
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
static int32_t utc_offset_s(void) { return (int32_t)set.utc4 * 900 + (set.summer ? 3600 : 0); }

/* ------------------------------------------------------------- the harmony --- */
/* each planet's note: its period raised by octaves into hearing (Hans Cousto's cosmic octave): the Earth's year for
 * the Sun, the synodic month for the Moon, each planet's orbit; and the nearest tempered note (MIDI) */
static const float COSMIC_HZ[PL_N] = {0.0f, 126.22f, 210.42f, 141.27f, 221.23f, 144.72f, 183.58f, 147.85f};
static const int8_t EQUAL_NOTE[PL_N] = {0, 47, 56, 49, 57, 50, 54, 50};   /* B2 G#3 C#3 A3 D3 F#3 D3 */
/* a natural note each, in the spheres' classical order outward: Moon C, Mercury D, Venus E, Sun F, Mars G, Jupiter A,
 * Saturn B */
static const int8_t NATURAL_NOTE[PL_N] = {0, 53, 48, 50, 52, 55, 57, 59};
/* the modes from dark to bright, as the sun climbs */
static const int8_t MODES[6][7] = {
    {0, 1, 3, 5, 7, 8, 10},              /* Phrygian: deep night */
    {0, 2, 3, 5, 7, 8, 10},              /* Aeolian: night */
    {0, 2, 3, 5, 7, 9, 10},              /* Dorian: dawn and dusk */
    {0, 2, 4, 5, 7, 9, 10},              /* Mixolydian: a low sun */
    {0, 2, 4, 5, 7, 9, 11},              /* Ionian: day */
    {0, 2, 4, 6, 7, 9, 11},              /* Lydian: a high sun */
};
static const char *const MODE_NAME[6] = {"PHR", "AEO", "DOR", "MIX", "ION", "LYD"};
/* each planet's four chords, and with none (the first): scale degrees, 0 the root */
static const int8_t PROG[PL_N][4] = {
    {0, 5, 3, 4}, {0, 4, 5, 3}, {0, 5, 2, 3}, {0, 1, 3, 4}, {0, 3, 5, 4}, {0, 6, 5, 4}, {0, 3, 1, 4}, {0, 2, 6, 5}};
static const char *const ROMAN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};

static int32_t mode_of_sun(float h)
{
    return h < -18.0f ? 0 : h < -6.0f ? 1 : h < 0.0f ? 2 : h < 12.0f ? 3 : h < 30.0f ? 4 : 5;
}
/* a scale degree (any, negative too) in semitones above the root */
static int32_t semis(int32_t d)
{
    int32_t o = d >= 0 ? d / 7 : (d - 6) / 7;
    return 12 * o + MODES[ui.mode][d - 7 * o];
}
static float ratio(int32_t s) { return fm_exp2f((float)s / 12.0f); }

/* the planet's root, in the drones' octave (C of the octave up to the next C) */
static float root_hz(int32_t p)
{
    float f = set.tuning == 0 ? COSMIC_HZ[p]
              : 440.0f * fm_exp2f((float)((set.tuning == 1 ? EQUAL_NOTE[p] : NATURAL_NOTE[p]) - 69) / 12.0f);
    float lo = 65.406f * fm_exp2f((float)(set.octave - 2));
    while (f >= 2.0f * lo)
        f *= 0.5f;
    while (f < lo)
        f *= 2.0f;
    return f;
}

/* the nearest scale degree to a key (MIDI), within the octave above the root */
static int32_t key_degree(int32_t note, float root)
{
    float rel = (float)note - (69.0f + 12.0f * fm_log2f(root / 440.0f));
    int32_t pc = ((int32_t)(rel + 0.5f + 1200.0f)) % 12, best = 0, bd = 99, d;
    for (d = 0; d < 7; d++) {
        int32_t dist = pc - MODES[ui.mode][d];
        if (dist < 0)
            dist = -dist;
        if (dist > 6)
            dist = 12 - dist;
        if (dist < bd) {
            bd = dist;
            best = d;
        }
    }
    return best;
}

/* what the drones play: a chord, on the last key's note with no planet; else the planet's four in turn, or a key's
 * chord of its scale */
static void harmony(void)
{
    uint32_t now = plat_ms();
    int32_t p = sky.planet, i, a, b, top, d = 0;
    float root, t = sky.linked ? tension(&sky) : -1.0f;
    ui.mode = mode_of_sun(sky.sun);
    root = p == PL_NONE ? 440.0f * fm_exp2f((float)(12 * (set.octave + 1) + set.pc[0] - 69) / 12.0f) : root_hz(p);
    ui.chord = (int32_t)((now - ui.chords_t0) / CHORD_MS % 4u);   /* with a planet or not, the four go round */
    d = now < ui.key_until && p != PL_NONE ? ui.key_deg : PROG[p][ui.chord];
    a = d;
    b = t >= 1.3f ? d + 3 : d + 2;       /* deep low pressure: a suspension */
    /* drone 2's partner: the octave of the root high and steady, the seventh as it falls, the ninth deep down */
    top = t < 0.3f ? a + 7 : t < 1.3f ? b + 4 : b + 5;
    while (semis(top) <= semis(b))
        top += 7;
    sky.hz[0] = root * ratio(semis(a));
    sky.hz[1] = root * ratio(semis(b));
    sky.pr[0] = ratio(semis(a + 4) - semis(a));   /* drone 1's partner: the fifth */
    sky.pr[1] = ratio(semis(top) - semis(b));
    sky.root_hz = root;
    for (i = 0; i < 7; i++)
        sky.scale[i] = MODES[ui.mode][i];
}

static void defaults(void)
{
    set.magic = SET_MAGIC;
    set.unix_saved = DEFAULT_UNIX;
    set.lat100 = 4190;                   /* 41.9 N, 12.5 E */
    set.lon100 = 1250;
    set.utc4 = 4;
    set.summer = 0;
    set.manual = 0;
    set.arp = 1;
    set.linked = 1;
    set.octave = 2;
    set.tuning = 0;
    set.pc[0] = 9;
    set.pc[1] = 4;
    set.trend = 0;
    set.clouds = 30.0f;
    set.press = 1015.0f;
    set.kp = 1.67f;
    set.sun = 20.0f;
    set.moon = 0.4f;
}
static void save(void)
{
    set.unix_saved = unix_now();
    set.clouds = sky.clouds;
    set.press = sky.press;
    set.kp = sky.kp;
    set.trend = (int8_t)sky.trend;
    set.linked = (uint8_t)sky.linked;
    set.sun = sky.sun;
    set.moon = sky.moon;
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    ui.last_save = plat_ms();
    ui.dirty = 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.lat100 < -9000 || set.lat100 > 9000 || set.lon100 < -18000 || set.lon100 > 18000 || set.octave < 1
        || set.octave > 4 || set.pc[0] > 11 || set.pc[1] > 11 || set.tuning > 2)
        defaults();
    clock_set(set.unix_saved);           /* the clock goes on from where it was saved */
    sky.clouds = set.clouds;
    sky.press = set.press;
    sky.kp = set.kp;
    sky.trend = set.trend;
    sky.linked = set.linked;
    sky.sun = set.sun;
    sky.moon = set.moon;
    eng_ok = eng_init() == 0;            /* the delay and the reverb, before WiFi takes the heap */
    harmony();
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
/* a body on the square: drawn if the sky lets it through; chosen from nature if up and the sky dark enough for it
 * (the clouds hide it from the eye, not from the choice) */
static void place(altaz_t p, int32_t pl, float mag, int edge, float dark_below)
{
    int32_t x, y;
    pl_x[pl] = -1;
    pl_drawn[pl] = pl_up[pl] = 0;
    if (astro_project(p, &x, &y) && x >= edge && y >= edge && x < 240 - edge && y < 240 - edge) {
        pl_x[pl] = (int16_t)x;
        pl_y[pl] = (int16_t)y;
        pl_drawn[pl] = mag <= limit + (sky.sun < 0.0f ? 2.5f : 0.0f);
        pl_up[pl] = now_sky.sun_alt < dark_below;
    }
}

/* where everything is now; the sun and the moon to the engine; which planets are seen */
static void sky_update(void)
{
    float lat = (float)set.lat100 / 100.0f, lon = (float)set.lon100 / 100.0f;
    int32_t i, x, y;
    astro_now((int32_t)(unix_now() - (uint32_t)J2000_UNIX), lat, lon, &now_sky);
    if (!set.manual) {                  /* nature; EDIT leaves them in the hands */
        sky.sun = now_sky.sun_alt;
        sky.moon = now_sky.moon;
    }
    /* the faintest star the sky lets through: daylight, twilight, the moon if it is up, the clouds */
    limit = 4.6f;
    if (sky.sun > -6.0f)
        limit = -5.0f;
    else if (sky.sun > -12.0f)
        limit = 1.0f;
    else if (sky.sun > -18.0f)
        limit = 3.0f;
    if (now_sky.moon_pos.alt > 0.0f)
        limit -= 0.8f * (1.0f - fm_cosf(FM_TWO_PI * sky.moon)) * 0.5f;
    limit -= sky.clouds / 100.0f * 2.5f;   /* clouds dim the sky; the brightest still show */
    for (i = 0; i < (int32_t)NSTARS; i++) {
        altaz_t p = astro_altaz((float)STARS[i].ra * (FM_TWO_PI / 65536.0f), (float)STARS[i].dec * 0.000174532925f,
                                now_sky.lst, lat * 0.0174532925f);
        float mag = (float)STARS[i].mag10 / 10.0f;
        if (mag <= fm_maxf(limit, STAR_FLOOR) && astro_project(p, &x, &y)) {
            star_x[i] = (uint8_t)x;
            star_y[i] = (uint8_t)y;
            if (mag > limit)
                star_dim[i >> 3] |= (uint8_t)(1u << (i & 7));
            else
                star_dim[i >> 3] &= (uint8_t)~(1u << (i & 7));
        } else {
            star_y[i] = 255;
        }
    }
    /* the planets from nightfall (Venus and Jupiter in the twilight already), the Moon and the Sun whenever up */
    for (i = 0; i < 5; i++)
        place(now_sky.planet[i], PL_MERCURY + i, now_sky.planet_mag[i], 2,
              PL_MERCURY + i == PL_VENUS || PL_MERCURY + i == PL_JUPITER ? -2.0f : -5.0f);
    place(now_sky.sun_pos, PL_SUN, -26.0f, 6, 90.0f);
    place(now_sky.moon_pos, PL_MOON, sky.sun < 8.0f ? -12.0f : 99.0f, 5, 90.0f);
    /* from nature, a chosen planet that sets takes its engine and its chords along (EDIT keeps it) */
    if (!set.manual && sky.planet != PL_NONE && !pl_up[sky.planet]) {
        app_say(planet_name(sky.planet), "GONE");
        sky.planet = PL_NONE;
    }
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
static char *put_dec1(char *d, int32_t v100)   /* hundredths as one decimal, unsigned: 4190 -> "41.9" */
{
    int32_t a = v100 < 0 ? -v100 : v100, t = (a + 5) / 10;
    d = put_int(d, t / 10);
    *d++ = '.';
    *d++ = (char)('0' + t % 10);
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

/* the place as "41.9N 12.5E" */
static void place_text(char *d)
{
    d = put_dec1(d, set.lat100);
    d = put(d, set.lat100 >= 0 ? "N " : "S ");
    d = put_dec1(d, set.lon100);
    put(d, set.lon100 >= 0 ? "E" : "W");
}
/* the local time: the date, the hour, the minute */
static void local_time(uint32_t u, int32_t *y, int32_t *mo, int32_t *d, int32_t *h, int32_t *mi)
{
    uint32_t l = u + (uint32_t)utc_offset_s();
    civil_from_days((int32_t)(l / 86400u), y, mo, d);
    *h = (int32_t)(l % 86400u / 3600u);
    *mi = (int32_t)(l % 3600u / 60u);
}
/* a pitch as its nearest note's name: "A2", "C#3" */
static char *note_name(char *d, float hz)
{
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t n = round_f(69.0f + 12.0f * fm_log2f(hz / 440.0f));
    d = put(d, NOTE[(n % 12 + 12) % 12]);
    return put_int(d, n / 12 - 1);
}

#define NLINES 11
#define TY0 32
/* the readings, line by line, with their colours */
static void readings(char lines[NLINES][32], uint16_t col[NLINES])
{
    sky_map_t m;
    char l[32], r[32], v[20], *q;
    int32_t fil, y, mo, d, h, mi, i;
    sky_map(&sky, &m);
    for (i = 0; i < NLINES; i++)
        col[i] = C_TXT;
    q = note_name(v, sky.hz[0]);
    q = put(q, " ");
    note_name(q, sky.hz[1]);
    line2(lines[0], sky.on ? "COSMOS" : "COSMOS  (OFF)", v);
    copy(lines[1], "----------------------------", 32);
    col[1] = C_LOW;
    fil = round_f(fm_log2f(m.cutoff / 450.0f) / fm_log2f(7000.0f / 450.0f) * 99.0f);
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
    {   /* the chord's colour */
        float t = sky.linked ? tension(&sky) : -1.0f;
        field(r, "CHORD", t < 0.3f ? "TRIAD" : t < 1.3f ? "7TH" : "SUS 9", 12);
    }
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
    if (sky.planet != PL_NONE) {         /* the planet, its voice; the mode and the chord */
        q = put(l, planet_name(sky.planet));
        put(put(q, " "), planet_voice(sky.planet));
        q = put(v, MODE_NAME[ui.mode]);
        put(put(q, " "), ROMAN[PROG[sky.planet][ui.chord] % 7]);
        line2(lines[8], l, v);
    } else {
        int seen = 0;
        for (i = 1; i < PL_N; i++)
            seen += pl_up[i];
        put(put_int(v, seen), seen == 1 ? " PLANET IN SIGHT" : " PLANETS IN SIGHT");
        copy(lines[8], seen ? v : "NO PLANET IN SIGHT", 32);
        col[8] = C_MID;
    }
    if (plat_ms() < ui.msg_until) {
        line2(lines[9], ui.msg[0], ui.msg[1]);
    } else {
        local_time(unix_now(), &y, &mo, &d, &h, &mi);
        place_text(l);
        q = put2(v, h);
        q = put(q, ":");
        put2(q, mi);
        line2(lines[9], l, v);
        col[9] = C_MID;
    }
    copy(lines[10], set.manual ? "EDIT: THE SKY BY HAND" : "HOME: NATURE, RANDOM WEATHER", 32);
    col[10] = set.manual ? C_MID : C_LOW;
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
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c, int32_t y0)
{
    int32_t dx, dy;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + 1)
                cv_pset(cx + dx, cy + dy - y0, c);
}

/* one frame of the sky, in five strips of 48 rows: stars, planets, the moon, the sun, then the readings */
static void sky_frame(void)
{
    char lines[NLINES][32];
    uint16_t col[NLINES];
    int32_t s, i, j, tw = (int32_t)(6.0f + 7.0f * sky.kp), arp_view = ui.view_sky && set.arp;
    if (ui.alpha > 0.01f)
        readings(lines, col);
    for (s = 0; s < 5; s++) {
        int32_t y0 = s * 48;
        cv_begin(240, 48, C_BLACK);
        if (arp_view && sky.on)          /* the ARP screen: the arpeggiator's line, faint, under the stars */
            for (i = 0; i < 48; i++)
                cv_pset((int32_t)ui.sweep_x, i, grey(28));
        for (i = 0; i < (int32_t)NSTARS; i++) {   /* the stars twinkle, more with Kp; one just played flashes */
            int32_t y = star_y[i], g;
            if (y == 255 || y < y0 || y >= y0 + 48)
                continue;
            if (star_flash[i] && arp_view) {
                cv_pset(star_x[i], y - y0, C_WHITE);
                cv_pset(star_x[i] - 1, y - y0, grey(120));
                cv_pset(star_x[i] + 1, y - y0, grey(120));
                cv_pset(star_x[i], y - 1 - y0, grey(120));
                cv_pset(star_x[i], y + 1 - y0, grey(120));
                continue;
            }
            if (star_dim[i >> 3] >> (i & 7) & 1u) {   /* behind the day, the clouds or the moon: faint, steady */
                cv_pset(star_x[i], y - y0, grey(46));
                continue;
            }
            g = 161 - (int32_t)((float)STARS[i].mag10 * 2.6f) + (int32_t)(rnd() % (uint32_t)(2 * tw + 1)) - tw;
            cv_pset(star_x[i], y - y0, grey(g < 40 ? 40 : g > 215 ? 215 : g));
        }
        if (arp_view)                    /* the ARP screen: a planet just played glows, fading */
            for (i = PL_SUN; i < PL_N; i++)
                if (pl_flash[i] && pl_x[i] >= 0 && pl_y[i] >= y0 - 12 && pl_y[i] < y0 + 60)
                    disc(pl_x[i], pl_y[i], i == PL_SUN ? 10 : i == PL_MOON ? 7 : 4, grey(30 + 10 * pl_flash[i]), y0);
        for (i = PL_MERCURY; i <= PL_SATURN; i++) {   /* the planets hold steady */
            int32_t x = pl_x[i], y = pl_y[i], g = 205 - (int32_t)((now_sky.planet_mag[i - PL_MERCURY] + 1.5f) * 18.0f);
            uint16_t c;
            if (x < 0 || !pl_drawn[i] || y < y0 - 8 || y >= y0 + 56)
                continue;
            c = pl_flash[i] && arp_view ? C_WHITE : grey(g < 120 ? 120 : g > 220 ? 220 : g);
            cv_pset(x, y - y0, c);
            if (i == PL_VENUS || i == PL_JUPITER) {   /* 2 x 2 */
                cv_pset(x + 1, y - y0, c);
                cv_pset(x, y + 1 - y0, c);
                cv_pset(x + 1, y + 1 - y0, c);
            }
            if (i == PL_SATURN) {        /* its rings */
                cv_pset(x - 1, y - y0, grey(g / 2));
                cv_pset(x + 1, y - y0, grey(g / 2));
            }
        }
        if (pl_x[PL_MOON] >= 0 && pl_drawn[PL_MOON] && pl_y[PL_MOON] >= y0 - 5 && pl_y[PL_MOON] < y0 + 53) {   /* the moon in its phase */
            float k = (1.0f - fm_cosf(FM_TWO_PI * sky.moon)) * 0.5f;   /* its phase: nature's, or EDIT's */
            int waxing = sky.moon - fm_floorf(sky.moon) < 0.5f, dx, dy;
            for (dy = -4; dy <= 4; dy++)
                for (dx = -4; dx <= 4; dx++) {
                    float half, edge;
                    int lit;
                    if (dx * dx + dy * dy > 17)
                        continue;
                    half = fm_sqrtf((float)(16 - dy * dy > 0 ? 16 - dy * dy : 0));
                    edge = half * (1.0f - 2.0f * k);
                    lit = waxing ? (float)dx > edge : (float)-dx > edge;
                    cv_pset(pl_x[PL_MOON] + dx, pl_y[PL_MOON] + dy - y0, grey(lit ? 210 : 45));
                }
        }
        if (pl_x[PL_SUN] >= 0 && pl_y[PL_SUN] >= y0 - 7 && pl_y[PL_SUN] < y0 + 55) {   /* the sun: a disc, a halo */
            disc(pl_x[PL_SUN], pl_y[PL_SUN], 6, grey(70), y0);
            disc(pl_x[PL_SUN], pl_y[PL_SUN], 4, grey(235), y0);
        }
        if (sky.planet != PL_NONE && pl_x[sky.planet] >= 0) {   /* the chosen one: four corners around it */
            int32_t x = pl_x[sky.planet], y = pl_y[sky.planet], r = sky.planet <= PL_MOON ? 9 : 5;
            for (j = 0; j < 4; j++) {
                int32_t sx = j & 1 ? 1 : -1, sy = j & 2 ? 1 : -1, cx = x + sx * r, cy = y + sy * r;
                cv_pset(cx, cy - y0, grey(150));
                cv_pset(cx - sx, cy - y0, grey(150));
                cv_pset(cx, cy - sy - y0, grey(150));
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
#define NROWS 8
static void settings_frame(void)
{
    static const char *const LABEL[NROWS] = {"LATITUDE", "LONGITUDE", "UTC", "SUMMER TIME", "DATE", "TIME",
                                             "PRESS HARMONY", "TUNING"};
    char t[32], v[24], l[24], *q;
    int32_t y, mo, d, h, mi, i;
    local_time(unix_now(), &y, &mo, &d, &h, &mi);
    for (i = 0; i < 14; i++) {
        uint16_t c = C_TXT;
        cv_begin(240, 16, C_BLACK);
        t[0] = 0;
        if (i == 0) {
            copy(t, "SETTINGS", 32);
        } else if (i == 1 || i == 10) {
            copy(t, "----------------------------", 32);
            c = C_LOW;
        } else if (i >= 2 && i < 2 + NROWS) {
            int r = i - 2;
            switch (r) {
            case 0:
                put(put_dec1(v, set.lat100), set.lat100 >= 0 ? " N" : " S");
                break;
            case 1:
                put(put_dec1(v, set.lon100), set.lon100 >= 0 ? " E" : " W");
                break;
            case 2:
                q = put(v, set.utc4 >= 0 ? "+" : "-");
                q = put_int(q, (set.utc4 < 0 ? -set.utc4 : set.utc4) / 4);
                if (set.utc4 % 4) {
                    q = put(q, ":");
                    put2(q, (set.utc4 < 0 ? -set.utc4 : set.utc4) % 4 * 15);
                }
                break;
            case 3:
                copy(v, set.summer ? "ON" : "OFF", 24);
                break;
            case 4:
                q = put_int(v, y);
                q = put(q, "-");
                q = put2(q, mo);
                q = put(q, "-");
                put2(q, d);
                break;
            case 5:
                q = put2(v, h);
                q = put(q, ":");
                put2(q, mi);
                break;
            case 6:
                copy(v, sky.linked ? "ON" : "OFF", 24);
                break;
            default:
                copy(v, set.tuning == 0 ? "COSMIC" : set.tuning == 1 ? "EQUAL" : "NATURAL", 24);
                break;
            }
            copy(l, r == ui.row ? "> " : "  ", 24);
            put(l + 2, LABEL[r]);
            line2(t, l, v);
        } else if (i == 11) {
            copy(t, "SELECT ROW  ALGO STEP", 32);
            c = C_MID;
        } else if (i == 12) {
            copy(t, "PRESET BIG  GLO BACK", 32);
            c = C_MID;
        } else if (i == 13) {
            int b = plat_battery();
            q = put(t, "CPU ");
            q = put(put_int(q, (int32_t)plat_cpu_pct()), "% HEAP ");
            q = put(put_int(q, (int32_t)((shim_heap_size - shim_heap_used) / 1024u)), "K BAT ");
            if (b == 4)
                put(q, "USB");
            else if (b >= 0)
                put(put_int(q, b * 100 / 3), "%");
            else
                put(q, "-");
            c = C_LOW;
        }
        cv_text(8, 0, &FONT_S, t, c);
        cv_blit(0, (uint32_t)(8 + i * 16));
    }
}

static void settings_change(int32_t step, int big)
{
    uint32_t u = unix_now();
    switch (ui.row) {
    case 0:
        set.lat100 = (int16_t)fm_clampf((float)(set.lat100 + step * (big ? 500 : 50)), -9000.0f, 9000.0f);
        break;
    case 1:
        set.lon100 = (int16_t)(set.lon100 + step * (big ? 500 : 50));
        if (set.lon100 > 18000)
            set.lon100 = (int16_t)(set.lon100 - 36000);
        if (set.lon100 < -18000)
            set.lon100 = (int16_t)(set.lon100 + 36000);
        break;
    case 2:                              /* keeping the local time: the UTC clock moves the other way */
        {
            int32_t before = utc_offset_s();
            set.utc4 = (int8_t)fm_clampf((float)(set.utc4 + step * (big ? 4 : 1)), -48.0f, 56.0f);
            clock_set(u + (uint32_t)(before - utc_offset_s()));
        }
        break;
    case 3:
        {
            int32_t before = utc_offset_s();
            set.summer = !set.summer;
            clock_set(u + (uint32_t)(before - utc_offset_s()));
        }
        break;
    case 4:
        clock_set(u + (uint32_t)(step * (big ? 30 : 1) * 86400));
        break;
    case 5:
        clock_set(u + (uint32_t)(step * (big ? 3600 : 60)));
        break;
    case 6:
        sky.linked = !sky.linked;
        break;
    default:
        set.tuning = (uint8_t)((set.tuning + 3 + (step > 0 ? 1 : -1)) % 3);
        break;
    }
    ui.dirty = 1;
    ui.last_sky = 0;                     /* the sky again, at once */
}

/* ------------------------------------------------------------ the arpeggiator --- */
/* a line sweeps the square left to right in 16 s; each star brighter than magnitude 3 it crosses plays a pluck: its
 * height a degree of the scale now (two octaves, the root two octaves over the drones' at the bottom), its brightness
 * its loudness. Clouds and daylight hide stars, and so thin the notes out */
#define SWEEP_MS 16000u
#define ARP_MAG10 30
static void arp_tick(void)
{
    uint32_t now = plat_ms(), x = now % SWEEP_MS * 240u / SWEEP_MS, from = ui.sweep_x;
    int32_t i, best[2] = {-1, -1};
    ui.sweep_x = x;
    if (!set.arp || !sky.on || x == from)
        return;
    for (i = 0; i < (int32_t)NSTARS; i++) {   /* the two brightest it crossed since the last frame */
        uint32_t sx = star_x[i];
        int crossed = from < x ? sx > from && sx <= x : sx > from || sx <= x;
        if (star_y[i] == 255 || STARS[i].mag10 > ARP_MAG10 || !crossed)
            continue;
        if (best[0] < 0 || STARS[i].mag10 < STARS[best[0]].mag10) {
            best[1] = best[0];
            best[0] = i;
        } else if (best[1] < 0 || STARS[i].mag10 < STARS[best[1]].mag10) {
            best[1] = i;
        }
    }
    for (i = PL_SUN; i < PL_N; i++) {    /* a planet it crosses plays too, louder: the sun only when up */
        int32_t px = pl_x[i], deg;
        int crossed;
        if (px < 0 || (i != PL_SUN && !pl_drawn[i]))
            continue;
        crossed = from < x ? (uint32_t)px > from && (uint32_t)px <= x : (uint32_t)px > from || (uint32_t)px <= x;
        if (!crossed)
            continue;
        deg = (239 - pl_y[i]) * 15 / 240;
        eng_note(sky.root_hz * 4.0f * ratio(semis(deg)), i == sky.planet ? 0.28f : 0.22f);
        pl_flash[i] = 16;
    }
    for (i = 0; i < 2; i++)
        if (best[i] >= 0) {
            int32_t st = best[i], deg = (239 - star_y[st]) * 15 / 240;
            float amp = 0.08f + (float)(ARP_MAG10 - STARS[st].mag10) * 0.003f;
            if (star_dim[st >> 3] >> (st & 7) & 1u)   /* a hidden one: softly */
                amp *= 0.45f;
            eng_note(sky.root_hz * 4.0f * ratio(semis(deg)), amp);
            star_flash[st] = 5;
        }
}

/* the lights: the buttons that do something glow, the ones in use are bright; the keys of the scale glow, the root's
 * are bright. Set only when they change */
static void lights(void)
{
    uint32_t bb = 0, db, bk = 0, dk = 0, k;
    int32_t root_pc = ((int32_t)(69.0f + 12.0f * fm_log2f(sky.root_hz / 440.0f) + 0.5f) % 12 + 12) % 12;
    if (ui.screen == 1) {
        db = 1u << B_GLO | 1u << B_HOME;
        bb = 1u << B_GLO;
    } else {
        db = 1u << B_HOME | 1u << B_EDIT | 1u << B_SEL | 1u << B_GLO | 1u << B_LFO | 1u << B_ARP | 1u << B_PLAY
             | 1u << B_OCTDN | 1u << B_OCTUP;
        bb |= set.manual ? 1u << B_EDIT : 1u << B_HOME;
        if (ui.view_sky)
            bb |= 1u << B_LFO;
        if (set.arp)
            bb |= 1u << B_ARP;
        if (sky.planet != PL_NONE)
            bb |= 1u << B_SEL;
        if (sky.on)
            bb |= LED_PLAY_GREEN;        /* playing: green; stopped: PLAY glows red */
        for (k = 0; k < NKEYS; k++) {
            int32_t rel = (((int32_t)FIRST_KEY_NOTE + (int32_t)k - root_pc) % 12 + 12) % 12, d;
            for (d = 0; d < 7; d++)
                if (MODES[ui.mode][d] == rel)
                    dk |= 1u << k;
            if (rel == 0)
                bk |= 1u << k;
        }
    }
    if (bb != ui.lit[0] || db != ui.lit[1] || bk != ui.lit[2] || dk != ui.lit[3]) {
        plat_lights(bb, bk, db, dk, 1);
        ui.lit[0] = bb;
        ui.lit[1] = db;
        ui.lit[2] = bk;
        ui.lit[3] = dk;
    }
}

/* ------------------------------------------------------------------- input --- */
static void to_screen(int sc)
{
    if (ui.screen == 1 && ui.dirty)
        save();
    ui.screen = sc;
    lcd_fill(0, 0, 240, 240, C_BLACK);
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, keys = plat_keys(), kdown = keys & ~ui.keys, now = plat_ms();
    int32_t e1 = plat_enc(EN_K1), e2 = plat_enc(EN_K2), e3 = plat_enc(EN_K3), e4 = plat_enc(EN_K4);
    int32_t ea = plat_enc(EN_ALGO), ep = plat_enc(EN_PRESET), es = plat_enc(EN_SELECT);
    int k;
    ui.btn = btn;
    ui.keys = keys;
    if (down || kdown || e1 || e2 || e3 || e4 || ea || ep || es)   /* a new touch (a held button is not one) */
        ui.touched = now;
    if (down >> B_GLO & 1u) {            /* the settings, and back */
        to_screen(!ui.screen);
        return;
    }
    if (down >> B_HOME & 1u) {           /* back to nature, the readings */
        to_screen(0);
        set.manual = 0;
        ui.view_sky = 0;
        ui.dirty = 1;
        ui.last_sky = 0;
        app_say("HOME", "NATURE");
        return;
    }
    if (down >> B_EDIT & 1u) {           /* everything by hand, from where nature left it */
        to_screen(0);
        set.manual = 1;
        ui.view_sky = 0;
        ui.dirty = 1;
        app_say("EDIT", "BY HAND");
        return;
    }
    if (down >> B_LFO & 1u && ui.screen == 0) {   /* the sky alone, and back to the readings */
        ui.view_sky = !ui.view_sky;
        ui.touched = ui.view_sky ? 0u : now;
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
    if (set.manual) {                    /* EDIT: the knobs are the sky */
        if (e1) sky.sun = fm_clampf(sky.sun + (float)e1 * 2.0f, -40.0f, 72.0f);
        if (e2) sky.clouds = fm_clampf(sky.clouds + (float)e2 * 3.0f, 0.0f, 100.0f);
        if (e3) sky.press = fm_clampf(sky.press + (float)e3 * 1.0f, 965.0f, 1045.0f);
        if (e4) sky.kp = fm_clampf(sky.kp + (float)e4 * 0.33f, 0.0f, 9.0f);
        if (ea) {
            sky.moon += (float)ea * 0.01f;
            sky.moon -= fm_floorf(sky.moon);
        }
        if (ep) sky.trend = (int32_t)fm_clampf((float)(sky.trend + (ep > 0 ? 1 : -1)), -1.0f, 1.0f);
        if (e1 || e2 || e3 || e4 || ea || ep) {
            ui.dirty = 1;
            ui.last_sky = 0;             /* what shows follows at once */
        }
    } else if (e1 || e2 || e3 || e4 || ea || ep) {
        app_say("NATURE", "EDIT TO CHANGE");
    }
    if (down >> B_SEL & 1u || es) {      /* the next planet up (in EDIT any of the seven), or none */
        int32_t p = sky.planet, n, dir = es < 0 ? -1 : 1;
        for (n = 0; n < PL_N; n++) {
            p = ((p + dir) % PL_N + PL_N) % PL_N;
            if (p == PL_NONE || set.manual || pl_up[p])
                break;
        }
        if (p != sky.planet) {
            sky.planet = p;
            ui.chords_t0 = now;          /* its chords from the first */
            ui.key_until = 0;
            app_say(p == PL_NONE ? "NO PLANET" : planet_name(p), p == PL_NONE ? "" : planet_voice(p));
        }
    }
    if (down >> B_OCTDN & 1u && set.octave > 1) { set.octave--; ui.dirty = 1; }
    if (down >> B_OCTUP & 1u && set.octave < 4) { set.octave++; ui.dirty = 1; }
    if (down >> B_ARP & 1u && ui.screen == 0) {   /* the ARP screen: the sky, the line, what it plays lighting up */
        if (!set.arp || !ui.view_sky) {
            set.arp = 1;
            ui.view_sky = 1;
            ui.touched = 0;
        } else {                         /* on it already: the arpeggio off, back to the readings */
            set.arp = 0;
            ui.view_sky = 0;
            ui.touched = now;
        }
        ui.dirty = 1;
    }
    if (down >> B_PLAY & 1u) {
        sky.on = !sky.on;
        if (!sky.on)                     /* stopped: a quiet moment to save (saving silences the sound) */
            save();
    }
    for (k = 0; k < NKEYS; k++)
        if (keys >> k & 1u) {            /* a key's chord: held, and 10 s after */
            if (sky.planet == PL_NONE) {
                if (kdown >> k & 1u) {   /* its note the new root */
                    set.pc[0] = (uint8_t)((FIRST_KEY_NOTE + k) % 12);
                    ui.dirty = 1;
                }
            } else {                     /* the planet's scale only */
                ui.key_deg = key_degree(FIRST_KEY_NOTE + k, root_hz(sky.planet));
            }
            ui.key_until = now + KEY_HOLD_MS;
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
        if (!set.manual)
            weather_tick((float)(now - ui.last_weather) / 3600000.0f);
        ui.last_weather = now;
    }
    harmony();                           /* the chords move on their own: every frame */
    sky.drone_on[0] = sky.on || now < ui.key_until;   /* stopped, a key still plays */
    sky.drone_on[1] = sky.on || now < ui.key_until;
    eng_sky(&sky);
    arp_tick();
    lights();
    {   /* the readings: always, but in the sky alone (LFO): there in fast after a touch, held, out slowly */
        float to = !ui.view_sky || now - ui.touched < FADE_HOLD_MS ? 1.0f : 0.0f;
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
    {
        uint32_t i;
        for (i = 0; i < NSTARS; i++)     /* the played stars fade back */
            if (star_flash[i])
                star_flash[i]--;
        for (i = 0; i < PL_N; i++)
            if (pl_flash[i])
                pl_flash[i]--;
    }
}

#ifdef PLAT_HOST
uint32_t shim_heap_used = 0;
const uint32_t shim_heap_size = 278528;
#endif
