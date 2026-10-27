/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* TILT (app.h): a test of COSMOS's phone remote, on one plain voice. White Terminus text on black.
 *   MAIN      the WiFi, the remote (its code: type it on the phone, at gadlele.github.io/COSMOS/remote/), the
 *             filter. The keys play the note, SELECT turns the filter by hand (the phone's, while it talks), PLAY
 *             the sound on and off, SEL the networks, REC WiFi on or off.
 *   NETWORKS  the networks around. SELECT picks, SAVE joins (a kept or open one at once, else the password),
 *             FX looks again, SEL back.
 *   PASSWORD  SELECT turns through the characters, ALGO jumps a group (abc ABC 123 #+=), PLAY adds, HOME
 *             deletes, SAVE joins, SEL back.
 * The voice: two saws a little apart and a sine an octave under, into two state-variable low-passes (the second
 * resonant), 60 Hz .. 15 kHz. The phone silent 10 s, the filter goes back to the knob's over a second.
 * Part of the unity build (device/unity.c), after the platform: gfx.c is in scope. */
#include "fastmath.h"

#define C_TXT C_WHITE
#define C_MID RGB(150, 150, 150)
#define C_LOW RGB(80, 80, 80)
#define C_BAR RGB(207, 216, 255)
#define ROW 13                           /* Terminus 12 (FONT_XS) and a pixel */
#define SR 44100.0f
#define RM_HOLD_MS 10000u                /* the phone silent this long: the knob's filter again */

static const char CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                            " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
static const uint8_t GROUP[] = {0, 26, 52, 62};   /* abc ABC 123 #+= */
#define NCHARS (sizeof CHARS - 1u)
static const char CODE_CHARS[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   /* none to mistake for another */

static const char *const STATE[] = {"OFF", "STARTING", "ON", "JOINING", "ADDRESS", "CONNECTED", "FAILED"};
enum { S_MAIN, S_NETS, S_PASS };

static struct {
    int screen;
    uint32_t btn, keys;
    int pick;                            /* the network picked in the scan's list */
    char ssid[33];                       /* the one being joined */
    char pass[64];
    int plen, ch;                        /* the password's length, the character SELECT is on */
    char msg[2][32];
    uint32_t msg_until;
    uint8_t redraw;
    char code[6];                        /* the pairing code: from the chip's own ID, the same every time */
    int knob;                            /* the filter by hand, 0..1000 */
    float open, res;                     /* the filter now (the phone's or the knob's), 0..1 */
    int phone;                           /* the phone has it */
    uint32_t count_at, count_n, rate;    /* messages a second */
} ui = {.knob = 600, .open = 0.6f};

/* ---- the voice: the main loop sets these, the audio interrupt reads them */
static volatile float v_hz = 55.0f, v_open = 0.6f, v_res = 0.0f;
static volatile int v_on = 1;
static float ph[3], hz = 55.0f, gate, cut_at = 0.6f, res_at, lp_a[3], lp_b[3], lp1[2], lp2[2];

static void svf_set(float *a, float fc, float k)
{
    float g = fm_tanf(FM_PI * fm_clampf(fc, 30.0f, 16000.0f) / SR);
    a[0] = 1.0f / (1.0f + g * (g + k));
    a[1] = g * a[0];
    a[2] = g * a[1];
}
static inline float svf(float *st, const float *a, float x)
{
    float v3 = x - st[1], v1 = a[0] * st[0] + a[1] * v3, v2 = st[1] + a[1] * st[0] + a[2] * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    return v2;
}
/* a saw, its step rounded (polyBLEP); ph 0..1 */
static inline float saw(float t, float dt)
{
    float x = 2.0f * t - 1.0f;
    if (t < dt) {
        t /= dt;
        x -= t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        x -= t * t + t + t + 1.0f;
    }
    return x;
}

void app_init(void) {}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = (float)master_q12 / 4096.0f * 8388607.0f * 0.5f, to = v_on ? 1.0f : 0.0f;
    for (i = 0; i < n; i++) {
        float d0, d1, d2, x;
        if ((i & 31u) == 0) {            /* every 32 samples: the glides, the filters (~10 ms to settle) */
            cut_at += (v_open - cut_at) * 0.08f;
            res_at += (v_res - res_at) * 0.08f;
            hz += (v_hz - hz) * 0.05f;
            svf_set(lp_a, 60.0f * fm_exp2f(8.0f * cut_at), 1.41f);
            svf_set(lp_b, 60.0f * fm_exp2f(8.0f * cut_at), 1.41f - 1.3f * res_at);
        }
        gate += (to - gate) * 0.0005f;
        d0 = hz * 1.0035f / SR;
        d1 = hz * 0.9965f / SR;
        d2 = hz * 0.5f / SR;
        ph[0] += d0;
        ph[1] += d1;
        ph[2] += d2;
        ph[0] -= (float)(int)ph[0];
        ph[1] -= (float)(int)ph[1];
        ph[2] -= (float)(int)ph[2];
        x = 0.3f * saw(ph[0], d0) + 0.3f * saw(ph[1], d1) + 0.35f * fm_sinf(FM_TWO_PI * ph[2]);
        x = svf(lp2, lp_b, svf(lp1, lp_a, x)) * gate;
        x = x / (1.0f + fm_fabsf(x));    /* a resonant peak stays in */
        lr[2u * i] = lr[2u * i + 1u] = (int32_t)(x * gain);
    }
}
void app_panic(void) { v_on = 0; }

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

/* the pairing code: the chip's ID hashed (FNV-1a) into 5 of CODE_CHARS */
static void make_code(void)
{
    const uint8_t *u = plat_chip_uid();
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 16; i++)
        h = (h ^ (u ? u[i] : 0u)) * 16777619u;
    for (i = 0; i < 5; i++) {
        ui.code[i] = CODE_CHARS[h % (sizeof CODE_CHARS - 1u)];
        h = h / (sizeof CODE_CHARS - 1u) ^ (h << 27);
    }
    ui.code[5] = 0;
}

/* ---- input */
static void join(const char *ssid, const char *pass)
{
    if (plat_wifi_join(ssid, pass) == 0) {
        app_say("JOINING", ssid);
        ui.screen = S_MAIN;
    } else {
        app_say("CAN'T JOIN", ssid);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, keys = plat_keys(), kdown = keys & ~ui.keys;
    int32_t e = plat_enc(EN_SELECT), g = plat_enc(EN_ALGO);
    ui.btn = btn;
    ui.keys = keys;
    if (e || g || down || kdown)
        ui.redraw = 1;
    if (ui.screen == S_MAIN) {
        uint32_t k;
        for (k = 0; k < NKEYS; k++)      /* the newest key: its note, two octaves down (F1 .. G3) */
            if (kdown >> k & 1u)
                v_hz = 43.65f * fm_exp2f((float)k / 12.0f);
        if (e) {
            ui.knob += (int)e * 20;
            ui.knob = ui.knob < 0 ? 0 : ui.knob > 1000 ? 1000 : ui.knob;
        }
        if (down >> B_PLAY & 1u) {
            v_on = !v_on;
            app_say("SOUND", v_on ? "ON" : "OFF");
        }
        if (down >> B_SEL & 1u) {
            ui.screen = S_NETS;
            ui.pick = 0;
            if (plat_wifi_scan() != 0)
                app_say("WIFI IS OFF", "REC TURNS IT ON");
        }
        if (down >> B_REC & 1u) {
            int on = plat_wifi_state() == WIFI_OFF;
            plat_wifi_on(on);
            app_say("WIFI", on ? "ON" : "OFF");
        }
    } else if (ui.screen == S_NETS) {
        if (e) {
            ui.pick += e;
            if (ui.pick < 0)
                ui.pick = 0;
            while (ui.pick > 0 && !plat_wifi_found(ui.pick, 0, 0, 0))
                ui.pick--;
        }
        if (down >> B_FX & 1u)
            plat_wifi_scan();
        if (down >> B_SEL & 1u)
            ui.screen = S_MAIN;
        if (down >> B_SAVE & 1u) {
            int sec = 0;
            if (plat_wifi_found(ui.pick, ui.ssid, 0, &sec)) {
                if (!sec || plat_wifi_kept(ui.ssid)) {
                    join(ui.ssid, 0);
                } else {
                    ui.screen = S_PASS;
                    ui.plen = 0;
                    ui.pass[0] = 0;
                }
            }
        }
    } else {
        ui.ch = (int)(((int32_t)ui.ch + e) % (int32_t)NCHARS + (int32_t)NCHARS) % (int32_t)NCHARS;
        if (g) {                                   /* to the next (or the previous) group's first */
            int k, cur = 0;
            for (k = 0; k < 4; k++)
                if (ui.ch >= GROUP[k])
                    cur = k;
            ui.ch = GROUP[((cur + (g > 0 ? 1 : -1)) % 4 + 4) % 4];
        }
        if ((down >> B_PLAY & 1u) && ui.plen + 1 < (int)sizeof ui.pass) {
            ui.pass[ui.plen++] = CHARS[ui.ch];
            ui.pass[ui.plen] = 0;
        }
        if ((down >> B_HOME & 1u) && ui.plen > 0)
            ui.pass[--ui.plen] = 0;
        if (down >> B_SEL & 1u)
            ui.screen = S_NETS;
        if ((down >> B_SAVE & 1u) && ui.plen >= 8)
            join(ui.ssid, ui.pass);
        else if (down >> B_SAVE & 1u)
            app_say("TOO SHORT", "WPA2: 8 OR MORE");
    }
}
void app_input_only(void) { input(); }

/* ---- the filter: the phone's while it talks, else back to the knob's over a second */
static void filter_tick(uint32_t now)
{
    int32_t o = 0, r = 0;
    uint32_t age = net_remote_get(&o, &r), c = net_remote_count();
    ui.phone = age < RM_HOLD_MS;
    if (ui.phone) {
        ui.open = (float)o / 1000.0f;
        ui.res = (float)r / 1000.0f;
    } else {
        ui.open += ((float)ui.knob / 1000.0f - ui.open) * 0.06f;
        ui.res -= ui.res * 0.06f;
    }
    v_open = ui.open;
    v_res = ui.res;
    if (now - ui.count_at >= 1000u) {    /* messages in the last second */
        ui.rate = c - ui.count_n;
        ui.count_n = c;
        ui.count_at = now;
    }
}

/* ---- drawing: a row of text in a strip of its own, a second text right-aligned */
static void row(int i, const char *t, uint16_t c, const char *right, uint16_t rc)
{
    cv_begin(240, ROW, C_BLACK);
    cv_text(6, 0, &FONT_XS, t, c);
    if (right)
        cv_text(234 - text_w(&FONT_XS, right), 0, &FONT_XS, right, rc);
    cv_blit(0, (uint32_t)(4 + i * ROW));
}
#define RULE "--------------------------------------"

static void draw_main(void)
{
    char t[48], v[32];
    int st = plat_wifi_state(), rs = net_remote_state();
    int32_t age_o = 0;
    uint32_t age = net_remote_get(&age_o, 0);
    row(0, "TILT", C_TXT, "PHONE FILTER TEST", C_MID);
    row(1, RULE, C_LOW, 0, 0);
    v[0] = 0;
    if (st == WIFI_CONNECTED)
        put_int(put(v, "SIG "), plat_wifi_rssi());
    else if (st == WIFI_FAILED)
        copy(v, plat_wifi_why(), sizeof v);
    put(put(t, "WIFI     "), st >= 0 && st <= WIFI_FAILED ? STATE[st] : "?");
    row(2, t, st == WIFI_CONNECTED ? C_TXT : C_MID, v, C_MID);
    put(put(t, "NETWORK  "), plat_wifi_ssid()[0] ? plat_wifi_ssid() : "-");
    row(3, t, C_TXT, 0, 0);
    if (rs == RM_LIVE)
        copy(v, ui.phone ? "PHONE LIVE" : "READY", sizeof v);
    else if (rs == RM_RETRY && net_remote_why()[0])
        copy(v, net_remote_why(), sizeof v);
    else
        copy(v, rs == RM_NO_WIFI ? "WAITING FOR WIFI" : rs == RM_CONNECTING ? "CONNECTING" : "OFF", sizeof v);
    row(4, "REMOTE", C_TXT, v, rs == RM_LIVE ? C_TXT : C_MID);
    row(5, RULE, C_LOW, 0, 0);
    cv_begin(240, 2 * ROW, C_BLACK);     /* the code, big */
    cv_text(6, 4, &FONT_XS, "CODE", C_MID);
    cv_text(234 - text_w(&FONT_M, ui.code), 0, &FONT_M, ui.code, C_TXT);
    cv_blit(0, 4 + 6 * ROW);
    row(8, "gadlele.github.io/COSMOS/remote", C_LOW, 0, 0);
    put(put_int(put(t, "MESSAGES "), (int32_t)ui.rate), " A SECOND");
    if (age != ~0u)
        put(put_int(v, (int32_t)(age > 99999u ? 99999u : age)), " MS AGO");
    else
        copy(v, "NONE YET", sizeof v);
    row(9, t, C_MID, v, C_LOW);
    row(10, RULE, C_LOW, 0, 0);
    {   /* the filter: a bar, its percent, whose it is */
        int32_t w = (int32_t)(ui.open * 228.0f + 0.5f);
        cv_begin(240, 3 * ROW, C_BLACK);
        put(put_int(put(t, "FILTER "), (int32_t)(ui.open * 100.0f + 0.5f)), "%");
        cv_text(6, 0, &FONT_XS, t, C_TXT);
        cv_text(234 - text_w(&FONT_XS, ui.phone ? "PHONE" : "KNOB"), 0, &FONT_XS, ui.phone ? "PHONE" : "KNOB",
                ui.phone ? C_TXT : C_MID);
        cv_rect(6, ROW + 2, 228, 18, C_LOW);
        if (w > 0)
            cv_rect(6, ROW + 2, w, 18, C_BAR);
        cv_blit(0, 4 + 11 * ROW);
    }
    put(put_int(put(t, "RESONANCE "), (int32_t)(ui.res * 100.0f + 0.5f)), "%");
    row(14, t, C_MID, v_on ? "SOUND ON" : "SOUND OFF", v_on ? C_TXT : C_MID);
    row(15, RULE, C_LOW, 0, 0);
    row(16, "KEYS NOTE  SELECT FILTER  PLAY SOUND", C_LOW, 0, 0);
    row(17, plat_ms() < ui.msg_until ? ui.msg[0] : "SEL WIFI  REC WIFI ON/OFF",
        plat_ms() < ui.msg_until ? C_TXT : C_LOW, plat_ms() < ui.msg_until ? ui.msg[1] : 0, C_MID);
}

static void draw_nets(void)
{
    int i;
    row(0, "WIFI NETWORKS", C_TXT, plat_wifi_state() == WIFI_OFF ? "WIFI OFF" : "", C_MID);
    row(1, RULE, C_LOW, 0, 0);
    for (i = 0; i < 14; i++) {
        char ssid[33], t[48], r[24];
        int rssi = 0, sec = 0, have = plat_wifi_found(i, ssid, &rssi, &sec);
        t[0] = r[0] = 0;
        if (have) {
            put(put(t, i == ui.pick ? "> " : "  "), ssid);
            put(put(put_int(r, rssi), sec ? " LOCK" : ""), plat_wifi_kept(ssid) ? " SAVED" : "");
        } else if (i == 0) {
            put(t, "  LOOKING...");
        }
        row(2 + i, t, i == ui.pick ? C_TXT : C_MID, have ? r : 0, C_LOW);
    }
    row(16, RULE, C_LOW, 0, 0);
    row(17, plat_ms() < ui.msg_until ? ui.msg[0] : "SELECT PICK SAVE JOIN FX LOOK SEL BACK",
        plat_ms() < ui.msg_until ? C_TXT : C_LOW, plat_ms() < ui.msg_until ? ui.msg[1] : 0, C_MID);
}

static void draw_pass(void)
{
    char t[48];
    int i, k;
    row(0, "PASSWORD FOR", C_MID, 0, 0);
    row(1, ui.ssid, C_TXT, 0, 0);
    row(2, RULE, C_LOW, 0, 0);
    put(put(t, ui.plen > 36 ? ui.pass + ui.plen - 36 : ui.pass), "_");   /* the end of it, when long */
    row(3, t, C_TXT, 0, 0);
    put(put_int(t, ui.plen), " CHARACTERS");
    row(4, t, C_LOW, 0, 0);
    row(5, "", C_TXT, 0, 0);
    /* the characters around the one SELECT is on, the middle one big */
    cv_begin(240, 26, C_BLACK);
    for (k = -6; k <= 6; k++) {
        char c[3] = {CHARS[((ui.ch + k) % (int)NCHARS + (int)NCHARS) % (int)NCHARS], 0, 0};
        int32_t x = 120 + k * 17;
        if (c[0] == ' ')
            c[0] = 'S', c[1] = 'P';
        if (k == 0) {
            int32_t w = text_w(&FONT_M, c) + 6;
            cv_rect(x - w / 2, 0, w, 26, C_WHITE);
            cv_text(x - text_w(&FONT_M, c) / 2, 1, &FONT_M, c, C_BLACK);
        } else {
            cv_text(x - text_w(&FONT_S, c) / 2, 5, &FONT_S, c, k == -1 || k == 1 ? C_MID : C_LOW);
        }
    }
    cv_blit(0, 4 + 6 * ROW);
    for (i = 8; i < 10; i++)
        row(i, "", C_TXT, 0, 0);
    row(10, "SELECT   CHARACTER", C_MID, 0, 0);
    row(11, "ALGO     abc ABC 123 #+=", C_MID, 0, 0);
    row(12, "PLAY     ADD", C_MID, 0, 0);
    row(13, "HOME     DELETE", C_MID, 0, 0);
    row(14, "SAVE     JOIN", C_MID, 0, 0);
    row(15, "SEL      BACK", C_MID, 0, 0);
    row(16, RULE, C_LOW, 0, 0);
    row(17, plat_ms() < ui.msg_until ? ui.msg[0] : "", C_TXT, plat_ms() < ui.msg_until ? ui.msg[1] : 0, C_MID);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    make_code();
    plat_wifi_on(1);                         /* on at start: it joins the networks kept */
    net_remote(1, ui.code);                  /* the remote's task waits for it */
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_draw;
    static int last_screen = -1;
    uint32_t now = plat_ms();
    input();
    filter_tick(now);
    if (now - last_draw > (ui.screen == S_MAIN ? 100u : 500u) || (ui.msg_until && now >= ui.msg_until)) {
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.screen != last_screen) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        last_screen = ui.screen;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        if (ui.screen == S_MAIN)
            draw_main();
        else if (ui.screen == S_NETS)
            draw_nets();
        else
            draw_pass();
    }
}

#ifdef PLAT_HOST
/* the host simulator has no lwIP (net.c is the device's): a phone that sweeps the filter shut and open every 8 s */
static int host_rm;
int net_remote(int on, const char *code)
{
    (void)code;
    host_rm = on;
    return 0;
}
int net_remote_state(void) { return host_rm ? RM_LIVE : RM_OFF; }
uint32_t net_remote_count(void) { return plat_ms() / 50u; }
const char *net_remote_why(void) { return ""; }
uint32_t net_remote_get(int32_t *open, int32_t *res)
{
    uint32_t t = plat_ms() % 8000u;
    if (open)
        *open = (int32_t)(t < 4000u ? 1000u - t / 4u : (t - 4000u) / 4u);
    if (res)
        *res = 500;
    return host_rm && plat_ms() > 3000u ? 0u : ~0u;
}
#endif
