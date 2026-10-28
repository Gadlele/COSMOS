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
 *          K4 Kp, ALGO the moon's cycle; the pressure's trend follows the way K3 was turned
 *   PRESET the drone's level, always (0 .. 100 %, in steps of 5)
 *   SEL    the next planet up (in EDIT any of the seven classical ones, the Sun and the Moon among them), or none
 *   LFO    the sky alone, and back      ARP the ARP screen, the arpeggio on; again, off and back
 *   FX     RADIO COSMOS (radio.c, with WiFi): the chords from the sequence written together on the web page, at the
 *          same moment on every FM-1 tuned in; an empty slot the sky's own; the rest (pad, stars, space) this sky's
 *   GLO    the settings      PLAY the sky stopped or playing (stopping saves)      OCT- / OCT+ the octave
 *   MASTER the volume. The buttons that do something glow, the ones in use bright; the keys glow on the scale,
 *   bright on its root. Saving silences the sound for a moment: only stopping (PLAY) and leaving GLO save.
 * THE HARMONY. Always a chord, coloured by the pressure (the triad and its octave, the seventh, the suspended ninth):
 * the drone holds its root and fifth, sliding to the next in 1.5 s; the pad on top plays the four notes an octave up
 * and attacks each chord anew.
 * THE SOUND. The pad is the weather's (its kind: the network's code, or guessed from the clouds and the pressure), by
 * day or by night (choose_sound: PAD_DAY, PAD_NIGHT); the star voice is the moon's phase; the rhythm is Kp's (from 4
 * STARGATE, from 5 PULSAR). The settings' PAD and STARS can fix them. The mode is the sun's height's (Phrygian at night .. Lydian at noon). Four chords go round, one every 10 s:
 * with no planet on the last key's note; a chosen planet brings its note (its period raised by octaves into hearing:
 * Cousto's cosmic octave, exact under TUNING COSMIC, the nearest tempered note under EQUAL; under NATURAL a natural
 * note each, the spheres' classical order from the Moon's C to Saturn's B) and four chords of its own on its scale;
 * a key then plays the chord of the scale on its note, held and for 10 s after, and the four come back. With the sky stopped, a key still sounds its chord, held and for 10 s after.
 * THE SETTINGS (GLO): latitude, longitude, the offset from UTC, summer time, the date and the time (local), the
 * pad (AUTO: the weather's), the star voice (AUTO: the moon's), the tuning. SELECT picks a row, ALGO changes it a step, PRESET a big step, GLO or HOME
 * saves and goes back. Without WiFi (stage 3) the clock runs from the time set here.
 * Part of the unity build (device/unity.c), after the platform: gfx.c is in scope. */
#include "fastmath.h"
#include "dsp/engine.h"
#include "dsp/astro.h"
#include "sky_data.h"
#include "splash_data.h"

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
    uint8_t summer, manual, inst;        /* summer time; EDIT (else HOME: nature); the pad: 0 or 1 the weather's
                                          * (AUTO), 2..16 one fixed (NEBULA .. STRINGS) */
    uint8_t octave, tuning;              /* tuning 0 COSMIC, 1 EQUAL, 2 NATURAL */
    uint8_t pc[2];                       /* with no planet, the chord's root: a pitch class ([1] unused) */
    int8_t trend;
    uint8_t arp, offline, stars;         /* the arpeggiator on; WiFi off (0: on, fetching the weather); the star voice:
                                          * 0 the moon's (AUTO), 1..8 one fixed, 9 the pad's own (PAD, 0.20) */
    float clouds, press, kp, sun, moon;  /* the sky by hand (EDIT) */
    uint8_t drone, spare[3];             /* (0.19) the drone's level, 0..20 (twentieths); settings saved before it load
                                          * without it */
} set;
#define STARS_PAD (NVOICE + 1)         /* set.stars: the pad's own voice */
#define SET_OLD_SIZE (sizeof set - 4u)   /* the settings before 0.19 */

static sky_t sky = {.sun = 20.0f, .clouds = 30.0f, .press = 1015.0f, .trend = 0, .kp = 1.67f, .moon = 0.4f,
                    .hz = {110.0f, 164.81f}, .linked = 1, .planet = PL_NONE, .on = 1};
/* nature's weather, apart from the sky's: it goes on while EDIT has the knobs, and HOME gives it back at once */
static struct {
    float clouds, press, kp;
    int32_t trend;
    int32_t wcode;                       /* the network's weather code (WMO), -1 none */
} nat = {30.0f, 1015.0f, 1.67f, 0, -1};

static sky_now_t now_sky;
#ifdef PLAT_WEB
static uint32_t web_unix;                /* in the browser: the computer's clock and offset from UTC (web.c) */
static int32_t web_utc_min;
#endif                /* the last astro_now */
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
    int screen;                          /* 0 the sky, 1 the settings, 2 WiFi (ENV), 3 a network's password */
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
    /* the weather from the network */
    uint32_t net_try, net_at;            /* plat_ms of the last fetch asked, of the last that brought the weather (0 none) */
    int fetching, was_connected;
    float wx_cloud, wx_press, wx_kp;     /* what it brought: HOME glides there */
    int32_t wx_trend, wx_code;
    int pick;                            /* WiFi: the network picked in the list */
    char ssid[33], pass[64];             /* the one being joined, its password as typed */
    int plen, ch;                        /* the password's length, the character SELECT is on */
    uint32_t splash_t0;                  /* the splash running since its first frame (plat_ms) */
    int splash_on;                       /* the splash: 1 still to start, 2 running, 0 over */
    uint32_t splash_btn;                 /* the buttons held at its start: only a new press cuts it */
    /* RADIO COSMOS (FX): the chords from the shared sequence, everything else this sky's */
    int radio;                           /* tuned in */
    int r_task;                          /* its task asked for (radio_on): once WiFi is connected, not to take the
                                          * heap while it joins */
    char radio_id[6];                    /* this FM-1's: from its chip */
    uint32_t r_slot;                     /* the 10 s slot playing (unix s / 10); 0 none yet */
    uint8_t r_cur[3], r_next[3];         /* its chord, and the next one's, locked when this one began */
    int r_cur_n, r_next_n;               /* their notes; 0: the sky's own chord */
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

/* the weather's kind: the network's code (WMO), or, offline and in EDIT, guessed from the clouds and the pressure */
enum { W_CLEAR, W_PARTLY, W_OVERCAST, W_FOG, W_DRIZZLE, W_RAIN, W_SNOW, W_STORM, NWX };
static const char *const W_NAME[NWX] = {"CLEAR", "PARTLY", "OVERCAST", "FOG", "DRIZZLE", "RAIN", "SNOW", "STORM"};
/* its pad by day and by night (pads.c's order) */
static const int8_t PAD_DAY[NWX] = {0 /* NEBULA */, 14 /* STRINGS */, 6 /* ORGAN */, 10 /* BACKGROUND */,
                                    8 /* TAPE */, 12 /* CRYSTAL */, 3 /* SOLAR WIND */, 13 /* SUPERNOVA */};
static const int8_t PAD_NIGHT[NWX] = {1 /* AURORA */, 5 /* QUASAR */, 7 /* VOYAGER */, 9 /* HORIZON */,
                                      11 /* DEEP FIELD */, 2 /* RINGS */, 3 /* SOLAR WIND */, 4 /* GRAVITY */};
static int32_t weather_of_code(int32_t c)
{
    if (c <= 1)
        return W_CLEAR;
    if (c == 2)
        return W_PARTLY;
    if (c == 3)
        return W_OVERCAST;
    if (c == 45 || c == 48)
        return W_FOG;
    if (c >= 51 && c <= 57)
        return W_DRIZZLE;
    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82))
        return W_RAIN;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86)
        return W_SNOW;
    if (c >= 95)
        return W_STORM;
    return W_OVERCAST;
}
static int32_t weather_guess(float clouds, float press)
{
    if (clouds > 70.0f && press < 995.0f)
        return W_STORM;
    if (clouds > 70.0f && press < 1004.0f)
        return W_RAIN;
    if (clouds > 80.0f && press < 1009.0f)
        return W_DRIZZLE;
    if (clouds > 90.0f && press > 1020.0f)
        return W_FOG;                    /* still, damp, high pressure: the fogs of an anticyclone */
    if (clouds < 20.0f)
        return W_CLEAR;
    if (clouds < 55.0f)
        return W_PARTLY;
    return W_OVERCAST;
}
static int32_t weather_now(void)
{
    static int32_t prev = W_PARTLY;
    int32_t a, b, c;
    if (!set.manual && nat.wcode >= 0)
        return prev = weather_of_code(nat.wcode);
    a = weather_guess(sky.clouds, sky.press);   /* near an edge, the one there stays */
    b = weather_guess(sky.clouds - 3.0f, sky.press - 1.5f);
    c = weather_guess(sky.clouds + 3.0f, sky.press + 1.5f);
    if (prev != b && prev != c)
        prev = a;
    return prev;
}

/* the pad (the weather's, by day or by night), the star voice (the moon's), the rhythm (Kp's): or the settings' */
static void choose_sound(void)
{
    static int day = 1;
    int32_t w = weather_now(), ph;
    day = day ? sky.sun > -7.0f : sky.sun > -5.0f;   /* civil twilight, with a degree either way */
    sky.engine = set.inst >= 2 && set.inst < 2 + ENG_N ? set.inst - 2 : (day ? PAD_DAY[w] : PAD_NIGHT[w]);
    ph = (int32_t)((sky.moon - fm_floorf(sky.moon)) * 8.0f + 0.5f) & 7;   /* new, crescent, quarter, gibbous, full... */
    sky.voice = set.stars >= 1 && set.stars <= NVOICE ? set.stars - 1 : set.stars == STARS_PAD ? pad_voice(sky.engine) : ph;
    if (sky.rhythm == R_PULSAR)          /* Kp: from 4 the sequence, from 5 the pulse; three tenths back to leave */
        sky.rhythm = sky.kp >= 4.7f ? R_PULSAR : sky.kp >= 3.7f ? R_STARGATE : R_NONE;
    else if (sky.rhythm == R_STARGATE)
        sky.rhythm = sky.kp >= 5.0f ? R_PULSAR : sky.kp >= 3.7f ? R_STARGATE : R_NONE;
    else
        sky.rhythm = sky.kp >= 5.0f ? R_PULSAR : sky.kp >= 4.0f ? R_STARGATE : R_NONE;
}

/* what the drones play: a chord, on the last key's note with no planet; else the planet's four in turn, or a key's
 * chord of its scale */
/* RADIO: the slot's chord, the same notes everywhere; the drone on its lowest, the pad an octave over it; the mode
 * nearest the sun's that holds it, for the stars (the sky's own still for the sound) */
static void radio_chord(void)
{
    int32_t r[3], n = 0, i, j, m, best = -1;
    int32_t lo = ui.r_cur[0], m0 = mode_of_sun(sky.sun);
    float root, flo = 65.406f * fm_exp2f((float)(set.octave - 2));
    for (i = 1; i < ui.r_cur_n; i++)
        if (ui.r_cur[i] < lo)
            lo = ui.r_cur[i];
    for (i = 0; i < ui.r_cur_n; i++) {   /* each note above the lowest, in the octave, once, in order */
        int32_t v = (ui.r_cur[i] - lo) % 12;
        for (j = 0; j < n && r[j] != v; j++)
            ;
        if (j == n)
            r[n++] = v;
    }
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && r[j] < r[j - 1]; j--) {
            int32_t t = r[j];
            r[j] = r[j - 1];
            r[j - 1] = t;
        }
    for (m = 0; m < 12 && best < 0; m++) {   /* m0, m0 - 1, m0 + 1, m0 - 2... */
        int32_t k = m0 + (m & 1 ? -(m + 1) / 2 : m / 2), ok = 1;
        if (k < 0 || k > 5)
            continue;
        for (i = 0; i < n && ok; i++) {
            ok = 0;
            for (j = 0; j < 7; j++)
                ok |= MODES[k][j] == r[i];
        }
        if (ok)
            best = k;
    }
    ui.mode = best >= 0 ? best : m0;
    root = 440.0f * fm_exp2f((float)(lo - 69) / 12.0f);
    while (root >= 2.0f * flo)
        root *= 0.5f;
    while (root < flo)
        root *= 2.0f;
    sky.hz[0] = sky.hz[1] = root;
    sky.pr[0] = sky.pr[1] = 2.0f;        /* the drone's partner: the fifth when the chord has it, else the octave */
    for (i = 0; i < n; i++)
        if (r[i] == 7)
            sky.pr[0] = sky.pr[1] = 1.5f;
    {
        int changed = 0;
        for (i = 0; i < NCHORD; i++) {   /* its notes, then the lowest an octave up; 0: left out */
            float f = i < n ? root * 2.0f * ratio(r[i]) : i == n ? root * 4.0f : 0.0f;
            if (fm_fabsf(f - sky.notes[i]) > f * 0.001f + 0.001f)
                changed = 1;
            sky.notes[i] = f;
        }
        if (changed)
            sky.chord_n++;
    }
    choose_sound();
    sky.root_hz = root;
    for (i = 0; i < 7; i++)
        sky.scale[i] = MODES[ui.mode][i];
}

static void harmony(void)
{
    uint32_t now = plat_ms();
    int32_t p = sky.planet, i, d = 0;
    float root;
    ui.mode = mode_of_sun(sky.sun);
    root = p == PL_NONE ? 440.0f * fm_exp2f((float)(12 * (set.octave + 1) + set.pc[0] - 69) / 12.0f) : root_hz(p);
    ui.chord = (int32_t)((now - ui.chords_t0) / CHORD_MS % 4u);   /* with a planet or not, the four go round */
    if (ui.radio && ui.r_slot) {
        ui.chord = (int32_t)(ui.r_slot % 4u);   /* RADIO: on the shared clock's slots */
        if (ui.r_cur_n) {
            radio_chord();
            return;
        }
    }
    d = now < ui.key_until && p != PL_NONE ? ui.key_deg : PROG[p][ui.chord];
    sky.hz[0] = root * ratio(semis(d));  /* the drone: the root, its partner the fifth */
    sky.pr[0] = ratio(semis(d + 4) - semis(d));
    sky.hz[1] = sky.hz[0];
    sky.pr[1] = sky.pr[0];
    {   /* the pad: the chord on the degree an octave over the drone, coloured by the pressure (high and steady: the
         * triad and its octave; falling: the seventh; deep down: suspended, the ninth); a new one attacked */
        static const int8_t COLOUR[3][NCHORD] = {{0, 2, 4, 7}, {0, 2, 4, 6}, {0, 3, 4, 8}};
        float t = tension(&sky);
        int32_t c = t < 0.3f ? 0 : t < 1.3f ? 1 : 2;
        int changed = 0;
        for (i = 0; i < NCHORD; i++) {
            float f = root * 2.0f * ratio(semis(d + COLOUR[c][i]));
            if (fm_fabsf(f - sky.notes[i]) > f * 0.001f)
                changed = 1;
            sky.notes[i] = f;
        }
        if (changed)
            sky.chord_n++;
    }
    choose_sound();
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
    set.inst = 1;
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
    set.drone = 20;
}
static void save(void)
{
    set.unix_saved = unix_now();
    set.clouds = sky.clouds;
    set.press = sky.press;
    set.kp = sky.kp;
    set.trend = (int8_t)sky.trend;
    set.sun = sky.sun;
    set.moon = sky.moon;
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    ui.last_save = plat_ms();
    ui.dirty = 0;
}

void app_init(void)
{
    int n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    if (n == (int)SET_OLD_SIZE)          /* saved by 0.18 or before: the drone at its full level */
        set.drone = 20;
    if ((n != (int)sizeof set && n != (int)SET_OLD_SIZE) || set.magic != SET_MAGIC
        || set.lat100 < -9000 || set.lat100 > 9000 || set.lon100 < -18000 || set.lon100 > 18000 || set.octave < 1
        || set.octave > 4 || set.pc[0] > 11 || set.pc[1] > 11 || set.tuning > 2 || set.drone > 20)
        defaults();
    clock_set(set.unix_saved);           /* the clock goes on from where it was saved */
#ifdef PLAT_WEB
    if (web_unix) {                      /* in the browser: the visitor's time now */
        clock_set(web_unix);
        set.utc4 = (int8_t)(web_utc_min / 15);
        set.summer = 0;
    }
#endif
    sky.clouds = nat.clouds = set.clouds;   /* nature starts from where it was (the network brings it up to date) */
    sky.press = nat.press = set.press;
    sky.kp = nat.kp = set.kp;
    sky.trend = nat.trend = set.trend;
    sky.sun = set.sun;
    sky.moon = set.moon;
    plat_cpu2_set(1);                    /* the second core, for the pads and the stars (at power-on or never) */
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

static uint16_t grey(int32_t g);

/* ---------------------------------------------------------------- the splash --- */
/* scattered stars gather into the word COSMOS (splash_data.h: Michroma, a star every other pixel), hold, and fade
 * into the sky: 4.4 s, any button or key cuts it short. Each star's start, delay, brightness and twinkle come from a
 * hash of its number (no memory for them); a few dim stars twinkle behind */
#define SPLASH_MS 4400u
#define SPLASH_SKY 45u
static uint32_t hash(uint32_t i)
{
    i ^= i >> 16;
    i *= 0x7FEB352Du;
    i ^= i >> 15;
    i *= 0x846CA68Bu;
    return i ^ (i >> 16);
}
static float h01(uint32_t i, uint32_t k) { return (float)(hash(i * 8u + k + 0x5EEDu) & 0xFFFFu) / 65536.0f; }
static float ease(float t)
{
    t = fm_clampf(t, 0.0f, 1.0f);
    if (t < 0.5f)
        return 4.0f * t * t * t;
    t = 2.0f - 2.0f * t;
    return 1.0f - t * t * t * 0.5f;
}

static void splash_frame(float t)
{
    float in = fm_minf(1.0f, t / 0.6f), out = 1.0f - fm_clampf((t - 3.6f) / 0.6f, 0.0f, 1.0f);
    int32_t s;
    uint32_t i;
    for (s = 0; s < 5; s++) {
        int32_t y0 = s * 48;
        cv_begin(240, 48, C_BLACK);
        for (i = 0; i < SPLASH_SKY; i++) {   /* the sky behind, dim, twinkling */
            int32_t x = (int32_t)(h01(i + 1000u, 0) * 240.0f), y = (int32_t)(h01(i + 1000u, 1) * 240.0f);
            float g = (35.0f + 55.0f * h01(i + 1000u, 2)) * (0.75f + 0.25f * fm_sinf(t * 5.0f + 6.3f * h01(i + 1000u, 3)));
            if (y >= y0 && y < y0 + 48)
                cv_pset(x, y - y0, grey((int32_t)(g * in * out)));
        }
        for (i = 0; i < NSPLASH; i++) {     /* the word's stars: 0.6 s scattered, then each glides in over 1.4 s */
            float k = ease((t - 0.6f - 0.45f * h01(i, 2)) / 1.4f), g0 = 50.0f + 100.0f * h01(i, 3), g;
            float x = h01(i, 0) * 240.0f, y = h01(i, 1) * 240.0f;
            int32_t xi, yi;
            x += ((float)SPLASH_XY[i][0] - x) * k;
            y += ((float)SPLASH_XY[i][1] - y) * k;
            yi = (int32_t)(y + 0.5f);
            if (yi < y0 || yi >= y0 + 48)
                continue;
            xi = (int32_t)(x + 0.5f);
            g = (g0 + (235.0f - g0) * k) * (0.85f + 0.15f * fm_sinf(t * (7.0f - 4.0f * k) + 6.3f * h01(i, 4)));
            cv_pset(xi, yi - y0, grey((int32_t)(g * in * out)));
        }
        cv_blit(0, (uint32_t)y0);
    }
}

void app_splash(void)                    /* at boot: black; the stars start in app_frame */
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
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
    nat.press = fm_clampf(nat.press - (nat.press - 1013.0f) / 60.0f * hours + 2.2f * sq * gauss(), 965.0f, 1045.0f);
    nat.clouds = fm_clampf(nat.clouds - (nat.clouds - 45.0f) / 8.0f * hours + 16.0f * sq * gauss(), 0.0f, 100.0f);
    nat.kp = fm_clampf(nat.kp - (nat.kp - 1.7f) / 7.0f * hours + 0.5f * sq * gauss(), 0.0f, 9.0f);
    if ((float)(rnd() & 0xFFFFFFu) / 16777216.0f < hours / 250.0f)   /* a storm, now and then */
        nat.kp = 5.0f + (float)(rnd() & 0xFFFFu) / 65536.0f * 3.5f;
    since += hours;
    if (since >= 3.0f) {                 /* the trend: the last three hours */
        float dp = nat.press - press_then;
        nat.trend = dp < -1.0f ? -1 : dp > 1.0f ? 1 : 0;
        press_then = nat.press;
        since = 0.0f;
    }
}

/* HOME: the sky's weather is nature's */
static void nature_to_sky(void)
{
    sky.clouds = nat.clouds;
    sky.press = nat.press;
    sky.kp = nat.kp;
    sky.trend = nat.trend;
}

/* ------------------------------------------------------------------ the network --- */
#define FETCH_MS (30u * 60u * 1000u)     /* the weather every half hour */
#define RETRY_MS (5u * 60u * 1000u)      /* a fetch that failed: again in 5 minutes */
#define WX_FRESH_MS (3u * 3600u * 1000u) /* older than this, the weather drifts by itself again */

static const char CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                            " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
static const uint8_t GROUP[] = {0, 26, 52, 62};   /* abc ABC 123 #+= */
#define NCHARS (sizeof CHARS - 1u)
static const char *const WIFI_STATE[] = {"OFF", "STARTING", "ON", "JOINING", "ADDRESS", "CONNECTED", "FAILED"};

/* for BearSSL's random generator: no hardware generator is set up, so the clocks and MASTER, mixed (BearSSL hashes
 * it). Weak by a security standard; fine for weather numbers */
void cosmos_entropy(uint8_t *p, unsigned n)
{
    static uint32_t carry = 0x9E3779B9u;
    uint32_t h = carry ^ plat_ms();
    unsigned i, k;
    for (i = 0; i < n; i++) {
        for (k = 0; k < 8u; k++) {
            h ^= plat_ticks24() + plat_master() * 0x27D4EB2Fu;
            h = (h ^ (h >> 15)) * 0x2C1B3C6Du;
            h ^= h >> 12;
        }
        p[i] = (uint8_t)(h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24));
    }
    carry = h;
}

/* "Mon, 26 Oct 2026 16:41:02 GMT" -> Unix time; 0 if it does not read */
static uint32_t http_date(const char *d)
{
    static const char MON[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int32_t v[6] = {0, 0, 0, 0, 0, 0}, k = 0, m = 0, i;
    while (*d && *d != ',')
        d++;
    if (!*d)
        return 0;
    d++;
    while (*d == ' ')
        d++;
    for (; *d >= '0' && *d <= '9'; d++)
        v[0] = v[0] * 10 + (*d - '0');   /* the day */
    while (*d == ' ')
        d++;
    for (i = 0; i < 12; i++)
        if (d[0] == MON[3 * i] && d[1] == MON[3 * i + 1] && d[2] == MON[3 * i + 2])
            m = i + 1;
    if (!m)
        return 0;
    d += 3;
    for (k = 2; k < 6 && *d; k++) {      /* the year, the hours, the minutes, the seconds */
        while (*d == ' ' || *d == ':')
            d++;
        for (; *d >= '0' && *d <= '9'; d++)
            v[k] = v[k] * 10 + (*d - '0');
    }
    if (v[2] < 2024 || v[0] < 1 || v[0] > 31)
        return 0;
    return (uint32_t)days_from_civil(v[2], m, v[0]) * 86400u + (uint32_t)(v[3] * 3600 + v[4] * 60 + v[5]);
}

/* the network's side of a frame: fetch when connected and due; take what came */
static void net_tick(uint32_t now)
{
    int conn = plat_wifi_state() == WIFI_CONNECTED;
    if (conn && !ui.was_connected)
        ui.net_try = 0;                  /* just connected: at once */
    ui.was_connected = conn;
    if (ui.fetching) {
        const net_res_t *r;
        if (net_busy())
            return;
        ui.fetching = 0;
        r = net_result();
        if (r->press10 >= 0 && r->cloud >= 0) {
            ui.wx_cloud = (float)r->cloud;
            ui.wx_press = (float)r->press10 / 10.0f;
            ui.wx_trend = r->trend10 <= -10 ? -1 : r->trend10 >= 10 ? 1 : 0;   /* a hPa in 3 hours */
            ui.wx_code = r->wcode;
            if (r->kp100 >= 0)
                ui.wx_kp = (float)r->kp100 / 100.0f;
            else if (!ui.net_at)
                ui.wx_kp = sky.kp;
            ui.net_at = now ? now : 1u;
        }
        if (r->utc_s != NET_NONE_S && r->utc_s > -14 * 3600 && r->utc_s < 15 * 3600) {
            set.utc4 = (int8_t)(r->utc_s / 900);   /* the place's offset now: summer time is in it */
            set.summer = 0;
        }
        {
            uint32_t u = http_date(r->date);
            if (u)
                clock_set(u);
        }
        if (!r->done)
            ui.net_try = now - FETCH_MS + RETRY_MS;   /* nothing came: again soon */
        return;
    }
    if (conn && (!ui.net_try || now - ui.net_try >= FETCH_MS) && net_fetch(set.lat100, set.lon100) == 0) {
        ui.fetching = 1;
        ui.net_try = now ? now : 1u;
    }
}

/* the minutes since the weather came, for the readings; -1 none (or too old) */
static int32_t weather_age_min(void)
{
    uint32_t now = plat_ms();
    return ui.net_at && now - ui.net_at < WX_FRESH_MS ? (int32_t)((now - ui.net_at) / 60000u) : -1;
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
/* ---- RADIO COSMOS */
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
#define C_RADIO RGB(150, 190, 255)

/* this FM-1's id: its chip's ID hashed (FNV-1a) into 5 characters, the same every time */
static void radio_make_id(void)
{
    static const char ID_CHARS[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    const uint8_t *u = plat_chip_uid();
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 16; i++)
        h = (h ^ (u ? u[i] : 0u)) * 16777619u;
    for (i = 0; i < 5; i++) {
        ui.radio_id[i] = ID_CHARS[h % (sizeof ID_CHARS - 1u)];
        h = h / (sizeof ID_CHARS - 1u) ^ (h << 27);
    }
    ui.radio_id[5] = 0;
}
static char *dec2(char *d, int32_t v)    /* hundredths as a decimal: "41.90", "-3.70" */
{
    if (v < 0) {
        *d++ = '-';
        v = -v;
    }
    d = put_int(d, v / 100);
    *d++ = '.';
    *d++ = (char)('0' + v / 10 % 10);
    *d++ = (char)('0' + v % 10);
    *d = 0;
    return d;
}
static void radio_set(int on)
{
    if (on && set.offline) {
        app_say("RADIO", "NEEDS WIFI (ENV)");
        return;
    }
    if (on && !ui.radio_id[0])
        radio_make_id();
#ifdef PLAT_WEB
    if (on) {
        app_say("RADIO", "NOT HERE");
        return;
    }
#endif
    if (!on && ui.r_task) {
        radio_on(0, 0, 0);
        ui.r_task = 0;
    }
    ui.radio = on;
    ui.r_slot = 0;
    ui.r_cur_n = ui.r_next_n = 0;
    app_say("RADIO COSMOS", on ? "TUNED IN" : "OFF");
}
/* each frame: the slot on the shared clock; when one begins, its chord is the one locked, and the next one locks */
static void radio_tick(void)
{
    uint32_t sec, ms, slot;
    if (!ui.radio)
        return;
    if (set.offline) {
        radio_set(0);
        return;
    }
    if (!ui.r_task && plat_wifi_state() == WIFI_CONNECTED) {   /* its task, once connected */
        char where[24];
        dec2(put(dec2(where, set.lat100), " "), set.lon100);
        ui.r_task = radio_on(1, ui.radio_id, where) == 0;
    }
    if (!radio_clock(&sec, &ms))
        return;
    slot = sec / 10u;
    if (slot == ui.r_slot)
        return;
    if (ui.r_slot && slot == ui.r_slot + 1u) {
        ui.r_cur_n = ui.r_next_n;
        ui.r_cur[0] = ui.r_next[0];
        ui.r_cur[1] = ui.r_next[1];
        ui.r_cur[2] = ui.r_next[2];
    } else {                             /* tuned in just now (or a jump): what the broker has for it */
        ui.r_cur_n = radio_slot(slot, ui.r_cur);
    }
    ui.r_next_n = radio_slot(slot + 1u, ui.r_next);
    ui.r_slot = slot;
}
/* "A-C-E", or "SKY" */
static char *chord_text(char *d, const uint8_t *n, int c)
{
    int i;
    if (!c)
        return put(d, "SKY");
    for (i = 0; i < c; i++)
        d = put(i ? put(d, "-") : d, NOTE_NAME[n[i] % 12u]);
    return d;
}

static void readings(char lines[NLINES][32], uint16_t col[NLINES])
{
    sky_map_t m;
    char l[32], r[32], v[20], *q;
    int32_t fil, y, mo, d, h, mi, i;
    sky_map(&sky, &m);
    for (i = 0; i < NLINES; i++)
        col[i] = C_TXT;
    line2(lines[0], sky.on ? "COSMOS" : "COSMOS  (OFF)", engine_name(sky.engine));   /* the pad playing */
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
    field(l, W_NAME[weather_now()], v, 12);   /* the weather's kind, the cloud cover */
    put_int(v, round_f(m.verb * 100.0f));
    field(r, "REVERB", v, 12);
    line2(lines[3], l, r);
    q = put_int(v, round_f(sky.press));
    put(q, sky.trend < 0 ? " v" : sky.trend > 0 ? " ^" : " =");
    field(l, "PRESS", v, 12);
    {   /* the chord's colour */
        float t = tension(&sky);
        field(r, "CHORD", ui.radio && ui.r_cur_n ? "RADIO" : t < 0.3f ? "TRIAD" : t < 1.3f ? "7TH" : "SUS 9", 12);
    }
    line2(lines[4], l, r);
    q = put_int(v, round_f(sky.kp * 10.0f) / 10);
    q = put(q, ".");
    put_int(q, round_f(sky.kp * 10.0f) % 10);
    field(l, "KP", v, 12);
    if (sky.rhythm != R_NONE) {          /* the Sun stirred: the rhythms */
        field(r, "", sky.rhythm == R_PULSAR ? "PULSAR" : "STARGATE", 12);
    } else {
        put_int(v, round_f(m.drift * 99.0f));
        field(r, "DRIFT", v, 12);
    }
    line2(lines[5], l, r);
    if (sky.rhythm != R_NONE)
        col[5] = C_STORM;
    field(l, "MOON", moon_name(sky.moon), 12);
    field(r, "", voice_name(sky.voice), 12);   /* the stars' voice */
    line2(lines[6], l, r);
    copy(lines[7], "----------------------------", 32);
    col[7] = C_LOW;
    if (sky.planet != PL_NONE) {         /* the planet; the mode and the chord */
        put(l, planet_name(sky.planet));
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
    if (ui.radio) {                      /* RADIO: who is tuned in; the chord now and the next */
        int st = ui.r_task ? radio_state() : RADIO_NO_WIFI, f = 0, w = 0;
        uint32_t s0, ms0;
        if (st == RADIO_LIVE && radio_clock(&s0, &ms0)) {
            radio_counts(&f, &w);
            put(put_int(put(put_int(put(lines[8], "RADIO  "), f), " FM-1  "), w), " WEB");
        } else {
            copy(lines[8], st == RADIO_LIVE ? "RADIO: SETTING THE CLOCK" : st == RADIO_NO_WIFI ? "RADIO: WAITING FOR WIFI"
                         : st == RADIO_RETRY && radio_why()[0] ? radio_why() : "RADIO: TUNING IN", 32);
        }
        col[8] = C_RADIO;
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
    if (ui.radio && ui.r_slot) {
        chord_text(put(chord_text(put(lines[10], "NOW "), ui.r_cur, ui.r_cur_n), "  NEXT "), ui.r_next, ui.r_next_n);
    } else if (set.manual) {
        copy(lines[10], "EDIT: THE SKY BY HAND", 32);
    } else {
        int32_t age = weather_age_min();
        if (age >= 0)
            put(put_int(put(lines[10], "HOME: WEATHER "), age), " MIN AGO");
        else
            copy(lines[10], set.offline ? "OFFLINE: RANDOM WEATHER" : "HOME: RANDOM WEATHER (ENV)", 32);
    }
    col[10] = ui.radio && ui.r_slot ? C_RADIO : set.manual ? C_MID : C_LOW;
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
#define NROWS 9
static void settings_frame(void)
{
    static const char *const LABEL[NROWS] = {"LATITUDE", "LONGITUDE", "UTC", "SUMMER TIME", "DATE", "TIME",
                                             "PAD", "STARS", "TUNING"};
    char t[32], v[24], l[24], *q;
    int32_t y, mo, d, h, mi, i;
    local_time(unix_now(), &y, &mo, &d, &h, &mi);
    for (i = 0; i < 14; i++) {
        uint16_t c = C_TXT;
        cv_begin(240, 16, C_BLACK);
        t[0] = 0;
        if (i == 0) {
            copy(t, "SETTINGS", 32);
        } else if (i == 1 || i == 11) {
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
                copy(v, set.inst >= 2 && set.inst < 2 + ENG_N ? engine_name(set.inst - 2) : "AUTO", 24);
                break;
            case 7:
                copy(v, set.stars >= 1 && set.stars <= NVOICE ? voice_name(set.stars - 1)
                        : set.stars == STARS_PAD ? "PAD" : "AUTO", 24);
                break;
            default:
                copy(v, set.tuning == 0 ? "COSMIC" : set.tuning == 1 ? "EQUAL" : "NATURAL", 24);
                break;
            }
            copy(l, r == ui.row ? "> " : "  ", 24);
            put(l + 2, LABEL[r]);
            line2(t, l, v);
        } else if (i == 12) {            /* (test builds) the load of each part, % of a block's time */
            uint32_t lm, lp, ls;
            eng_load(&lm, &lp, &ls);
            q = put(put_int(put(t, "DRONE+FX "), (int32_t)lm), " PAD ");
            put(put_int(put(put_int(q, (int32_t)lp), " STARS "), (int32_t)ls), "");
            c = C_MID;
        } else if (i == 13) {            /* the load; one core: why (the second's give-up code); the glitches */
            q = put(t, plat_cpu2_on() ? "CPU2 " : "CPU ");
            q = put(put_int(q, (int32_t)plat_cpu_pct()), "%");
            if (!plat_cpu2_on()) {       /* G: the second core's give-up code; F: the boots that failed (under 30 s) */
                q = put_int(put(q, plat_cpu2_barred() ? " B" : " G"), plat_cpu2_gave_up());
#ifndef PLAT_HOST
                q = put_int(put(q, " F"), (int32_t)bootguard.failed);
#endif
            }
            q = put_int(put(q, " XR "), (int32_t)(plat_xruns() > 9999u ? 9999u : plat_xruns()));
            put(put_int(put(q, " H"), (int32_t)((shim_heap_size - shim_heap_used) / 1024u)), "K");
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
    case 6:                              /* AUTO (the weather's), or one of the pads */
        {
            int32_t k = (set.inst < 2 ? 0 : set.inst - 1) + (ENG_N + 1) + (step > 0 ? 1 : -1);
            k %= ENG_N + 1;
            set.inst = (uint8_t)(k ? k + 1 : 1);
        }
        break;
    case 7:                              /* AUTO (the moon's), one of the star voices, or PAD (the pad's own) */
        set.stars = (uint8_t)((set.stars + (NVOICE + 2) + (step > 0 ? 1 : -1)) % (NVOICE + 2));
        break;
    default:
        set.tuning = (uint8_t)((set.tuning + 3 + (step > 0 ? 1 : -1)) % 3);
        break;
    }
    ui.dirty = 1;
    ui.last_sky = 0;                     /* the sky again, at once */
}

/* ------------------------------------------------------------------ WiFi (ENV) --- */
/* a row of the WiFi pages: 14 rows of 16 px, the text left, another right-aligned */
static void wrow(int i, const char *t, uint16_t c, const char *right, uint16_t rc)
{
    cv_begin(240, 16, C_BLACK);
    cv_text(8, 0, &FONT_S, t, c);
    if (right && right[0])
        cv_text(232 - text_w(&FONT_S, right), 0, &FONT_S, right, rc);
    cv_blit(0, (uint32_t)(8 + i * 16));
}
#define RULE "----------------------------"

static void wifi_page(void)
{
    char t[40], v[24], *q;
    int st = plat_wifi_state(), i;
    int32_t age = weather_age_min();
    const net_res_t *r = net_result();
    wrow(0, "WIFI", C_TXT, st >= 0 && st <= WIFI_FAILED ? WIFI_STATE[st] : "?", st == WIFI_CONNECTED ? C_TXT : C_MID);
    v[0] = 0;
    if (st == WIFI_CONNECTED)
        put_int(put(v, "SIG "), plat_wifi_rssi());
    else if (st == WIFI_FAILED)
        copy(v, plat_wifi_why(), sizeof v);
    wrow(1, plat_wifi_ssid()[0] ? plat_wifi_ssid() : "NO NETWORK", C_MID, v, C_LOW);
    if (ui.fetching)
        copy(t, "WEATHER: FETCHING...", sizeof t);
    else if (age >= 0)
        put(put_int(put(t, "WEATHER: "), age), " MIN AGO");
    else
        copy(t, r->why[0] ? r->why : "WEATHER: NOT YET", sizeof t);
    wrow(2, t, C_MID, 0, 0);
    wrow(3, RULE, C_LOW, 0, 0);
    for (i = 0; i < 7; i++) {            /* the networks around, the strongest first */
        char ssid[33];
        int rssi = 0, sec = 0, have = plat_wifi_found(i, ssid, &rssi, &sec);
        t[0] = v[0] = 0;
        if (have) {
            q = put(t, i == ui.pick ? "> " : "  ");
            copy(q, ssid, 21);
            if (plat_wifi_kept(ssid))
                put(v, "SAVED");
            else
                put(put_int(v, rssi), sec ? " LOCK" : "");
        } else if (i == 0) {
            copy(t, st == WIFI_OFF ? "  WIFI IS OFF" : "  LOOKING...", sizeof t);
        }
        wrow(4 + i, t, i == ui.pick ? C_TXT : C_MID, v, C_LOW);
    }
    wrow(11, RULE, C_LOW, 0, 0);
    if (plat_ms() < ui.msg_until) {
        wrow(12, ui.msg[0], C_TXT, ui.msg[1], C_MID);
        wrow(13, "", C_TXT, 0, 0);
    } else {
        wrow(12, "SELECT PICK  SAVE JOIN", C_LOW, 0, 0);
        wrow(13, set.offline ? "SEQ LOOK  REC WIFI ON" : "SEQ LOOK  REC OFF  PLAY NOW", C_LOW, 0, 0);
    }
}

static void pass_page(void)
{
    char t[48];
    int k;
    wrow(0, "PASSWORD FOR", C_MID, 0, 0);
    wrow(1, ui.ssid, C_TXT, 0, 0);
    wrow(2, RULE, C_LOW, 0, 0);
    put(put(t, ui.plen > 24 ? ui.pass + ui.plen - 24 : ui.pass), "_");   /* the end of it, when long */
    wrow(3, t, C_TXT, 0, 0);
    put(put_int(t, ui.plen), " CHARACTERS");
    wrow(4, t, C_LOW, 0, 0);
    cv_begin(240, 32, C_BLACK);          /* the characters around the one SELECT is on, the middle one big */
    for (k = -6; k <= 6; k++) {
        char c[3] = {CHARS[((ui.ch + k) % (int)NCHARS + (int)NCHARS) % (int)NCHARS], 0, 0};
        int32_t x = 120 + k * 17;
        if (c[0] == ' ')
            c[0] = 'S', c[1] = 'P';
        if (k == 0) {
            int32_t w = text_w(&FONT_M, c) + 6;
            cv_rect(x - w / 2, 2, w, 26, C_WHITE);
            cv_text(x - text_w(&FONT_M, c) / 2, 3, &FONT_M, c, C_BLACK);
        } else {
            cv_text(x - text_w(&FONT_S, c) / 2, 8, &FONT_S, c, k == -1 || k == 1 ? C_MID : C_LOW);
        }
    }
    cv_blit(0, 8 + 5 * 16);
    wrow(7, "", C_TXT, 0, 0);
    wrow(8, "SELECT CHARACTER", C_MID, 0, 0);
    wrow(9, "ALGO   abc ABC 123 #+=", C_MID, 0, 0);
    wrow(10, "PLAY   ADD   HOME DELETE", C_MID, 0, 0);
    wrow(11, "SAVE   JOIN  SEL BACK", C_MID, 0, 0);
    wrow(12, RULE, C_LOW, 0, 0);
    wrow(13, plat_ms() < ui.msg_until ? ui.msg[0] : "", C_TXT, plat_ms() < ui.msg_until ? ui.msg[1] : 0, C_MID);
}

static void net_join(const char *ssid, const char *pass)
{
    if (plat_wifi_join(ssid, pass) == 0) {
        app_say("JOINING", ssid);
        ui.screen = 2;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    } else {
        app_say("CAN'T JOIN", ssid);
    }
}

/* the WiFi pages' input: 1 = taken */
static int wifi_page_input(uint32_t down, int32_t es, int32_t ea)
{
    if (ui.screen == 2) {
        if (es) {
            ui.pick += es > 0 ? 1 : -1;
            if (ui.pick < 0)
                ui.pick = 0;
            while (ui.pick > 0 && !plat_wifi_found(ui.pick, 0, 0, 0))
                ui.pick--;
        }
        if (down >> B_SEQ & 1u && plat_wifi_scan() != 0)   /* (FX is RADIO's, on every page) */
            app_say("WIFI IS OFF", "REC TURNS IT ON");
        if (down >> B_REC & 1u) {
            set.offline = !set.offline;
            plat_wifi_on(!set.offline);
            if (!set.offline)
                plat_wifi_scan();
            ui.dirty = 1;
            app_say("WIFI", set.offline ? "OFF" : "ON");
        }
        if (down >> B_PLAY & 1u) {
            if (plat_wifi_state() != WIFI_CONNECTED)
                app_say("NOT CONNECTED", "");
            else
                ui.net_try = 0;          /* now */
        }
        if (down >> B_SAVE & 1u) {
            int sec = 0;
            if (plat_wifi_found(ui.pick, ui.ssid, 0, &sec)) {
                if (!sec || plat_wifi_kept(ui.ssid)) {
                    net_join(ui.ssid, 0);
                } else {
                    ui.screen = 3;
                    ui.plen = 0;
                    ui.pass[0] = 0;
                    lcd_fill(0, 0, 240, 240, C_BLACK);
                }
            }
        }
        return 1;
    }
    if (ui.screen == 3) {
        ui.ch = (int)(((int32_t)ui.ch + es) % (int32_t)NCHARS + (int32_t)NCHARS) % (int32_t)NCHARS;
        if (ea) {                        /* to the next (or the previous) group's first */
            int k, cur = 0;
            for (k = 0; k < 4; k++)
                if (ui.ch >= GROUP[k])
                    cur = k;
            ui.ch = GROUP[((cur + (ea > 0 ? 1 : -1)) % 4 + 4) % 4];
        }
        if ((down >> B_PLAY & 1u) && ui.plen + 1 < (int)sizeof ui.pass) {
            ui.pass[ui.plen++] = CHARS[ui.ch];
            ui.pass[ui.plen] = 0;
        }
        if ((down >> B_HOME & 1u) && ui.plen > 0)
            ui.pass[--ui.plen] = 0;
        if (down >> B_SEL & 1u) {
            ui.screen = 2;
            lcd_fill(0, 0, 240, 240, C_BLACK);
        }
        if ((down >> B_SAVE & 1u) && ui.plen >= 8)
            net_join(ui.ssid, ui.pass);
        else if (down >> B_SAVE & 1u)
            app_say("TOO SHORT", "WPA2: 8 OR MORE");
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ the arpeggiator --- */
/* a line sweeps the square left to right in 16 s; each star brighter than magnitude 3 it crosses plays a note in
 * the moon's voice: its height a degree of the scale now (two octaves, the root two octaves over the drones' at the
 * bottom), its brightness its loudness, its place across the screen its place in the stereo. Hidden stars play softly.
 * With the full moon's chimes a gust swings the neighbours too. Under STARGATE (Kp 4) a sixteenth at a time instead:
 * the star nearest the line, in the sequence's voice. Under PULSAR the engine holds each note to its grid */
#define SWEEP_MS 16000u
#define SEQ_STEP_MS 134u                 /* STARGATE's sixteenths: 112 BPM */
#define ARP_MAG10 30
static void arp_tick(void)
{
    static uint32_t seq_step;
    uint32_t now = plat_ms(), x = now % SWEEP_MS * 240u / SWEEP_MS, from = ui.sweep_x;
    int32_t i, best[2] = {-1, -1};
    ui.sweep_x = x;
    if (!set.arp || !sky.on || ui.splash_on)
        return;
    if (sky.rhythm == R_STARGATE) {      /* the sequence: each sixteenth (112 BPM) the star nearest the line */
        uint32_t k = now / SEQ_STEP_MS;
        int32_t near = -1, nd = 999;
        if (k == seq_step)
            return;
        seq_step = k;
        for (i = 0; i < (int32_t)NSTARS; i++) {
            int32_t dd = (int32_t)star_x[i] - (int32_t)x;
            if (star_y[i] == 255 || STARS[i].mag10 > ARP_MAG10)
                continue;
            dd = dd < 0 ? -dd : dd;
            dd = dd > 120 ? 240 - dd : dd;
            if (dd < nd) {
                nd = dd;
                near = i;
            }
        }
        if (near >= 0) {
            int32_t deg = (239 - star_y[near]) * 15 / 240 % 7;
            eng_note(sky.root_hz * 4.0f * ratio(semis(deg)), k % 4u ? 0.18f : 0.26f,
                     ((float)star_x[near] / 120.0f - 1.0f) * 0.3f, V_SEQ, 0);
            star_flash[near] = 3;
        }
        return;
    }
    if (x == from)
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
        eng_note(sky.root_hz * 4.0f * ratio(semis(deg)), i == sky.planet ? 0.28f : 0.22f,
                 ((float)px / 120.0f - 1.0f) * 0.8f, -1, 0);
        pl_flash[i] = 16;
    }
    for (i = 0; i < 2; i++)
        if (best[i] >= 0) {
            int32_t st = best[i], deg = (239 - star_y[st]) * 15 / 240;
            float amp = 0.08f + (float)(ARP_MAG10 - STARS[st].mag10) * 0.003f;
            if (star_dim[st >> 3] >> (st & 7) & 1u)   /* a hidden one: softly */
                amp *= 0.45f;
            {
                float pan = ((float)star_x[st] / 120.0f - 1.0f) * 0.8f;   /* where it is on the screen */
                eng_note(sky.root_hz * 4.0f * ratio(semis(deg)), amp, pan, -1, 0);
                if (sky.voice == V_CHIMES && sky.rhythm == R_NONE && (rnd() & 0xFFu) < 90u) {
                    eng_note(sky.root_hz * 4.0f * ratio(semis(deg + 2)), amp * 0.5f, pan, -1, 50u + rnd() % 150u);
                    eng_note(sky.root_hz * 4.0f * ratio(semis(deg + 4)), amp * 0.4f, pan, -1, 150u + rnd() % 200u);
                }                        /* a gust: the chimes beside it swing too */
            }
            star_flash[st] = 5;
        }
}

/* the lights: the buttons that do something glow, the ones in use are bright; the keys of the scale glow, the root's
 * are bright. Set only when they change */
static void lights(void)
{
    uint32_t bb = 0, db, bk = 0, dk = 0, k;
    int32_t root_pc = ((int32_t)(69.0f + 12.0f * fm_log2f(sky.root_hz / 440.0f) + 0.5f) % 12 + 12) % 12;
    uint32_t nav = 1u << B_HOME | 1u << B_EDIT | 1u << B_GLO | 1u << B_ENV | 1u << B_LFO | 1u << B_ARP | 1u << B_FX
                   | 1u << B_SEL;        /* the page buttons: they work everywhere */
    if (ui.screen == 1) {
        db = nav | 1u << B_PLAY | 1u << B_OCTDN | 1u << B_OCTUP;
        bb = 1u << B_GLO | (ui.radio ? 1u << B_FX : 0u);
    } else if (ui.screen == 2) {
        db = nav | 1u << B_SAVE | 1u << B_SEQ | 1u << B_REC | 1u << B_PLAY;
        bb = 1u << B_ENV | (set.offline ? 0u : 1u << B_REC) | (ui.radio ? 1u << B_FX : 0u);
    } else if (ui.screen == 3) {
        db = nav | 1u << B_SAVE | 1u << B_PLAY;
        bb = 1u << B_ENV | (ui.radio ? 1u << B_FX : 0u);
    } else {
        db = 1u << B_HOME | 1u << B_EDIT | 1u << B_SEL | 1u << B_GLO | 1u << B_LFO | 1u << B_ARP | 1u << B_PLAY | 1u << B_FX
             | 1u << B_OCTDN | 1u << B_OCTUP | 1u << B_ENV;
        bb |= set.manual ? 1u << B_EDIT : 1u << B_HOME;
        if (ui.view_sky)
            bb |= 1u << B_LFO;
        if (weather_age_min() >= 0)
            bb |= 1u << B_ENV;           /* the weather is the network's */
        if (set.arp)
            bb |= 1u << B_ARP;
        if (ui.radio)
            bb |= 1u << B_FX;
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
    if ((ui.screen == 1 || ui.screen == 2) && sc != ui.screen && ui.dirty) {
        save();
        ui.net_try = 0;                  /* the place may have changed: the weather again */
    }
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
    if (down || (kdown && !ui.view_sky) || e1 || e2 || e3 || e4 || ea || ep || es)   /* a new touch (a held button is
                                          * not one; on the sky alone, LFO or ARP, the keys only play) */
        ui.touched = now;
    if (down >> B_ENV & 1u) {            /* WiFi, and back */
        to_screen(ui.screen >= 2 ? 0 : 2);
        if (ui.screen == 2) {
            ui.pick = 0;
            plat_wifi_scan();
        }
        return;
    }
    {   /* the page buttons work from any page: on WiFi's, they leave it and do what they do (the password's HOME and
         * SEL are its DELETE and BACK) */
        uint32_t nav = 1u << B_HOME | 1u << B_EDIT | 1u << B_GLO | 1u << B_LFO | 1u << B_ARP | 1u << B_FX | 1u << B_SEL;
        if (ui.screen == 3)
            nav &= ~(1u << B_HOME | 1u << B_SEL);
        if (ui.screen >= 2 && (down & nav)) {
            to_screen(0);
            es = ea = 0;
        } else if (ui.screen >= 2 && wifi_page_input(down, es, ea)) {
            return;
        } else if (ui.screen == 1 && (down & (1u << B_LFO | 1u << B_ARP | 1u << B_FX | 1u << B_SEL))) {
            to_screen(0);                /* the settings: the sky's buttons go back to it (saving) and act */
            es = ea = ep = 0;
        }
    }
    if (down >> B_GLO & 1u) {            /* the settings, and back */
        to_screen(ui.screen == 1 ? 0 : 1);
        return;
    }
    if (down >> B_HOME & 1u) {           /* back to nature, the readings */
        to_screen(0);
        set.manual = 0;
        nature_to_sky();                 /* the weather as it is, at once (the sun and the moon follow: last_sky) */
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
        if (!(down & (1u << B_PLAY | 1u << B_OCTDN | 1u << B_OCTUP)))
            return;                      /* (PLAY and the octave work here too) */
        es = ea = ep = e1 = e2 = e3 = e4 = 0;
        kdown = keys = 0;
    }
    if (ep) {                            /* PRESETS: the drone's level, always */
        char v[8];
        int32_t d = (int32_t)set.drone + (ep > 0 ? 1 : -1);
        set.drone = (uint8_t)(d < 0 ? 0 : d > 20 ? 20 : d);
        put(put_int(v, (int32_t)set.drone * 5), "%");
        app_say("DRONE", v);
        ui.dirty = 1;
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
        if (e3) sky.trend = e3 > 0 ? 1 : -1;   /* the trend: the way the pressure was turned */
        if (e1 || e2 || e3 || e4 || ea) {
            ui.dirty = 1;
            ui.last_sky = 0;             /* what shows follows at once */
        }
    } else if (e1 || e2 || e3 || e4 || ea) {
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
            app_say(p == PL_NONE ? "NO PLANET" : planet_name(p), p == PL_NONE ? "" : "ITS SCALE");
        }
    }
    if (down >> B_FX & 1u && ui.screen == 0)   /* RADIO COSMOS, and off */
        radio_set(!ui.radio);
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
    for (k = 0; k < NKEYS && !ui.radio; k++)   /* (RADIO: the chords are the radio's) */
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
    ui.splash_on = 1;                    /* the stars gather first; WiFi starts after them (its start holds the
                                          * loop and the panel's scan for a second) */
    ui.touched = plat_ms();              /* the readings show at the start */
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    if (ui.splash_on) {                  /* the splash: timed from its first frame; a new press cuts it */
        uint32_t b = plat_buttons();
        if (ui.splash_on == 1) {         /* its first frame: the clock starts now (exactly now: the time since is
                                          * unsigned, and a start even 1 ms ahead would read as 49 days gone) */
            ui.splash_on = 2;
            ui.splash_t0 = now;
            ui.splash_btn = b;
            ui.last_frame = now - 50u;
        }
        ui.splash_btn &= b;              /* a held button counts once let go and pressed again */
        if (now - ui.splash_t0 >= SPLASH_MS || (now - ui.splash_t0 > 400u && (b & ~ui.splash_btn))) {
            ui.splash_on = 0;
            ui.touched = now;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            if (!set.offline)
                plat_wifi_on(1);         /* it joins the networks kept; the weather follows (net_tick) */
        }
    }
    input();
    if (ui.last_sky == 0 || now - ui.last_sky > 2000u) {   /* the sky every 2 s: the stars move slowly */
        ui.last_sky = now ? now : 1u;
        sky_update();
    }
    net_tick(now);
    if (now - ui.last_weather >= 1000u) {   /* nature's weather: the network's, else random at its real pace */
        float hours = (float)(now - ui.last_weather) / 3600000.0f;
        if (ui.net_at && now - ui.net_at < WX_FRESH_MS) {   /* glide to what came, over a few minutes */
            float k = 1.0f - fm_expf(-hours * 60.0f / 4.0f);
            nat.clouds += (ui.wx_cloud - nat.clouds) * k;
            nat.press += (ui.wx_press - nat.press) * k;
            nat.kp += (ui.wx_kp - nat.kp) * k;
            nat.trend = ui.wx_trend;
            nat.wcode = ui.wx_code;
        } else {
            weather_tick(hours);
            nat.wcode = -1;              /* the weather's kind from the clouds and the pressure */
        }
        if (!set.manual)
            nature_to_sky();
        ui.last_weather = now;
    }
    radio_tick();
    harmony();                           /* the chords move on their own: every frame */
    sky.drone_on[0] = !ui.splash_on && (sky.on || now < ui.key_until);   /* after the splash; stopped, a key still
                                                                         * plays */
    sky.drone_on[1] = sky.drone_on[0];
    sky.drone_level = (float)set.drone / 20.0f;
    eng_sky(&sky);
    arp_tick();
    lights();
    {   /* the readings: always, but in the sky alone (LFO): there in fast after a touch, held, out slowly */
        float to = !ui.view_sky || now - ui.touched < FADE_HOLD_MS ? 1.0f : 0.0f;
        ui.alpha += (to - ui.alpha) * (to > ui.alpha ? 0.5f : 0.09f);
        if (ui.alpha < 0.01f && to == 0.0f)
            ui.alpha = 0.0f;
    }
    if (ui.splash_on) {                  /* the splash, 20 frames a second */
        if (now - ui.last_frame >= 50u) {
            ui.last_frame = now;
            splash_frame((float)(now - ui.splash_t0) / 1000.0f);
        }
        return;
    }
    if (now - ui.last_frame < (ui.alpha > 0.0f && ui.alpha < 1.0f ? 60u : 120u))   /* ~16 frames a second fading */
        return;
    ui.last_frame = now;
    if (ui.screen == 0)
        sky_frame();
    else if (ui.screen == 1)
        settings_frame();
    else if (ui.screen == 2)
        wifi_page();
    else
        pass_page();
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
/* the host simulator has no lwIP (net.c is the device's): the fetch answers at once; in the browser nothing comes
 * (the weather is the simulated one, the time the computer's) */
#ifdef PLAT_WEB
static net_res_t host_res = {0, -1, -1, 0, NET_NONE_S, -1, -1, "", ""};
#else
static net_res_t host_res = {2, 85, 10032, -14, 3600, 233, 61, "Mon, 26 Oct 2026 21:41:02 GMT", ""};
#endif
int net_fetch(int32_t lat100, int32_t lon100)
{
    (void)lat100;
    (void)lon100;
    return 0;
}
int net_busy(void) { return 0; }
const net_res_t *net_result(void) { return &host_res; }
/* RADIO: on the computer a broadcast going round A-C-E, F-A-C, the sky, G-B-D, three FM-1s and two visitors; in the
 * browser none */
static int host_radio;
int radio_on(int on, const char *id, const char *where)
{
    (void)id, (void)where;
#ifdef PLAT_WEB
    return on ? -1 : 0;
#else
    host_radio = on;
    return 0;
#endif
}
int radio_state(void) { return host_radio ? RADIO_LIVE : RADIO_OFF; }
const char *radio_why(void) { return ""; }
int radio_clock(uint32_t *sec, uint32_t *ms)
{
    uint32_t t = plat_ms() - ui.clock_ms;
    *sec = ui.clock_unix + t / 1000u;
    *ms = t % 1000u;
    return host_radio;
}
int radio_slot(uint32_t slot, uint8_t *notes)
{
    static const uint8_t DEMO[4][3] = {{57, 60, 64}, {53, 57, 60}, {0, 0, 0}, {55, 59, 62}};
    int i;
    if (!DEMO[slot % 4u][0])
        return 0;
    for (i = 0; i < 3; i++)
        notes[i] = DEMO[slot % 4u][i];
    return 3;
}
void radio_counts(int *fm1, int *web)
{
    *fm1 = 3;
    *web = 2;
}
#endif
