/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS, stage 1 (app.h): the engine, the sky on the knobs.
 *   K1 SUN (degrees)   K2 CLOUDS (%)   K3 PRESS (hPa)   K4 KP   ALGO MOON (around its cycle)   PRESET the pressure's
 *   trend   SELECT the planet (all of them until the sky is computed; then only those on the screen)   SEL the
 *   pressure sets the partners' interval, or not   keys: the left half (F3..F#4) drone 1's note, the right half
 *   (G4..G5) drone 2's   OCT- / OCT+ their octave   PLAY the drones on or off   MASTER the volume
 * One screen, Terminus white on black: the sources on the left, what they do to the sound on the right, the planet,
 * and at the bottom what the chip spends (the audio's share of its time, the heap left) and the battery.
 * Part of the unity build (device/unity.c), after the platform: gfx.c is in scope. */
#include "fastmath.h"
#include "dsp/engine.h"

extern uint32_t shim_heap_used;
extern const uint32_t shim_heap_size;

#define C_TXT C_WHITE
#define C_MID RGB(150, 150, 150)
#define C_LOW RGB(80, 80, 80)
#define C_STORM RGB(255, 179, 138)
#define FIRST_KEY_NOTE 53                /* key 0 is F3 */
#define SPLIT_KEY 14                     /* keys 0..13 (F3..F#4) drone 1, 14..26 (G4..G5) drone 2 */

static sky_t sky = {.sun = 20.0f, .clouds = 30.0f, .press = 1015.0f, .trend = 0, .kp = 1.67f, .moon = 0.4f,
                    .note = {45, 52}, .linked = 1, .planet = PL_NONE, .on = 1};
static volatile int panic;
static int eng_ok;

static struct {
    uint32_t btn, keys;
    int32_t octave;                      /* the drones' octave: 1..4 (A2: 2) */
    int32_t pc[2];                       /* each drone's pitch class: 0 C .. 11 B */
    char msg[2][24];
    uint32_t msg_until;
    uint8_t redraw;
} ui = {.octave = 2, .pc = {9, 4}};

/* each drone's note from its pitch class: drone 1 in the octave, drone 2 the first such note above drone 1 */
static void notes(void)
{
    sky.note[0] = 12 * (ui.octave + 1) + ui.pc[0];
    sky.note[1] = 12 * (ui.octave + 1) + ui.pc[1];
    if (sky.note[1] <= sky.note[0])
        sky.note[1] += 12;
}

void app_init(void)
{
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
    ui.msg_until = plat_ms() + 1500u;
    ui.redraw = 1;
}

void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 32, C_BLACK);
    cv_text((240 - text_w(&FONT_L, APP_NAME)) / 2, 0, &FONT_L, APP_NAME, C_WHITE);
    cv_blit(0, 104);
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

#define Y0 32
static void row(int i, const char *t, uint16_t c)
{
    cv_begin(240, 16, C_BLACK);
    cv_text(8, 0, &FONT_S, t, c);
    cv_blit(0, (uint32_t)(Y0 + i * 16));
}

/* the bottom line: what the chip spends, and the battery drawn as one (four bars, or USB) */
static void bottom(void)
{
    char t[32], v[16], *q;
    int b = plat_battery(), i;
    cv_begin(240, 16, C_BLACK);
    if (plat_ms() < ui.msg_until) {
        line2(t, ui.msg[0], ui.msg[1]);
        cv_text(8, 0, &FONT_S, t, C_TXT);
    } else if (!eng_ok) {
        cv_text(8, 0, &FONT_S, "NO ROOM FOR DELAY AND REVERB", C_MID);
    } else {
        q = put(t, "CPU ");
        q = put(put_int(q, (int32_t)plat_cpu_pct()), "%  HEAP ");
        put(put_int(q, (int32_t)((shim_heap_size - shim_heap_used) / 1024u)), "K");
        cv_text(8, 0, &FONT_S, t, C_MID);
        if (b == 4) {
            cv_text(232 - text_w(&FONT_S, "USB"), 0, &FONT_S, "USB", C_MID);
        } else if (b >= 0) {             /* a battery: its outline, its tip, a bar for each quarter */
            uint16_t c = b == 0 ? C_STORM : C_MID;
            cv_rect(203, 3, 24, 1, c);
            cv_rect(203, 12, 24, 1, c);
            cv_rect(203, 3, 1, 10, c);
            cv_rect(226, 3, 1, 10, c);
            cv_rect(227, 6, 2, 4, c);
            for (i = 0; i < 3; i++)
                if (i < b)
                    cv_rect(205 + i * 7, 5, 6, 6, c);
            (void)v;
        }
    }
    cv_blit(0, Y0 + 10 * 16);
}

static void draw(void)
{
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    sky_map_t m;
    char l[32], r[32], v[16], t[32], *q;
    int32_t fil;
    sky_map(&sky, &m);
    q = put(put(v, NOTE[sky.note[0] % 12]), "");
    q = put_int(q, sky.note[0] / 12 - 1);
    q = put(put(q, " "), NOTE[sky.note[1] % 12]);
    put_int(q, sky.note[1] / 12 - 1);
    line2(t, sky.on ? "COSMOS" : "COSMOS  (OFF)", v);
    row(0, t, C_TXT);
    row(1, "----------------------------", C_LOW);
    /* the filter as 0..99 between 160 Hz and 7 kHz */
    fil = round_f(fm_log2f(m.cutoff / 160.0f) / fm_log2f(7000.0f / 160.0f) * 99.0f);
    q = put(v, sky.sun >= 0.0f ? "+" : "");
    put_int(q, round_f(sky.sun));
    field(l, "SUN", v, 12);
    put_int(v, fil);
    field(r, "FILTER", v, 12);
    line2(t, l, r);
    row(2, t, C_TXT);
    put(put_int(v, round_f(sky.clouds)), "%");
    field(l, "CLOUDS", v, 12);
    put_int(v, round_f(m.verb * 100.0f));
    field(r, "REVERB", v, 12);
    line2(t, l, r);
    row(3, t, C_TXT);
    q = put_int(v, round_f(sky.press));
    put(q, sky.trend < 0 ? " v" : sky.trend > 0 ? " ^" : " =");
    field(l, "PRESS", v, 12);
    field(r, "PARTNER", sky.linked ? m.interval_name : "8VA", 12);
    line2(t, l, r);
    row(4, t, sky.linked ? C_TXT : C_MID);
    q = put_int(v, round_f(sky.kp * 10.0f) / 10);
    q = put(q, ".");
    put_int(q, round_f(sky.kp * 10.0f) % 10);
    field(l, "KP", v, 12);
    if (m.storm > 0.0f)
        put(v, "STORM");
    else
        put_int(v, round_f(m.drift * 99.0f));
    field(r, "DRIFT", v, 12);
    line2(t, l, r);
    row(5, t, m.storm > 0.0f ? C_STORM : C_TXT);
    field(l, "MOON", moon_name(sky.moon), 12);
    put_int(v, round_f(m.sub * 198.0f));
    field(r, "SUB", v, 12);
    line2(t, l, r);
    row(6, t, C_TXT);
    row(7, "----------------------------", C_LOW);
    if (sky.planet != PL_NONE) {
        field(l, "PLANET", planet_name(sky.planet), 16);
        line2(t, l, planet_voice(sky.planet));
        row(8, t, C_TXT);
    } else {
        row(8, "PLANET       (SELECT TURNS)", C_LOW);
    }
    row(9, "----------------------------", C_LOW);
    bottom();
}

/* ---- input */
static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, keys = plat_keys(), kdown = keys & ~ui.keys;
    int32_t e1 = plat_enc(EN_K1), e2 = plat_enc(EN_K2), e3 = plat_enc(EN_K3), e4 = plat_enc(EN_K4);
    int32_t ea = plat_enc(EN_ALGO), ep = plat_enc(EN_PRESET), es = plat_enc(EN_SELECT);
    int changed = 0, k;
    ui.btn = btn;
    ui.keys = keys;
    if (e1) { sky.sun = fm_clampf(sky.sun + (float)e1 * 2.0f, -40.0f, 72.0f); changed = 1; }
    if (e2) { sky.clouds = fm_clampf(sky.clouds + (float)e2 * 3.0f, 0.0f, 100.0f); changed = 1; }
    if (e3) { sky.press = fm_clampf(sky.press + (float)e3 * 1.0f, 965.0f, 1045.0f); changed = 1; }
    if (e4) { sky.kp = fm_clampf(sky.kp + (float)e4 * 0.33f, 0.0f, 9.0f); changed = 1; }
    if (ea) {
        sky.moon += (float)ea * 0.01f;
        sky.moon -= fm_floorf(sky.moon);
        changed = 1;
    }
    if (ep) {
        sky.trend = (int32_t)fm_clampf((float)(sky.trend + (ep > 0 ? 1 : -1)), -1.0f, 1.0f);
        changed = 1;
    }
    if (es) {                            /* the planet: none, then Mercury .. Saturn, round */
        sky.planet = ((sky.planet + (es > 0 ? 1 : -1)) % PL_N + PL_N) % PL_N;
        changed = 1;
    }
    if (down >> B_SEL & 1u) {
        sky.linked = !sky.linked;
        app_say("PRESSURE", sky.linked ? "SETS PARTNER" : "OFF: OCTAVE");
        changed = 1;
    }
    if (down >> B_OCTDN & 1u && ui.octave > 1) { ui.octave--; changed = 1; }
    if (down >> B_OCTUP & 1u && ui.octave < 4) { ui.octave++; changed = 1; }
    if (down >> B_PLAY & 1u) {
        sky.on = !sky.on;
        changed = 1;
    }
    for (k = 0; k < NKEYS; k++)
        if (kdown >> k & 1u) {           /* the left half moves drone 1, the right half drone 2 */
            ui.pc[k >= SPLIT_KEY] = (FIRST_KEY_NOTE + k) % 12;
            changed = 1;
        }
    if (changed) {
        notes();
        eng_sky(&sky);
        ui.redraw = 1;
    }
}
void app_input_only(void) { input(); }

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_draw;
    uint32_t now = plat_ms();
    input();
    if (now - last_draw > 1000u || (ui.msg_until && now >= ui.msg_until)) {
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
uint32_t shim_heap_used = 0;
const uint32_t shim_heap_size = 278528;
#endif
