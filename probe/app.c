/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS PROBE (app.h): three screens, white Terminus text on black.
 *   MAIN      the WiFi, the data, the heap. PLAY fetches again (and every 15 minutes), SEL the networks,
 *             REC WiFi on or off.
 *   NETWORKS  the networks around. SELECT picks, SAVE joins (a kept or open one at once, else the password),
 *             FX looks again, SEL back.
 *   PASSWORD  SELECT turns through the characters, ALGO jumps a group (abc ABC 123 #+=), PLAY adds, HOME
 *             deletes, SAVE joins, SEL back.
 * After every fetch, while the network rests, it measures the heap: the most the fetch took, the biggest block
 * left, and whether COSMOS's delay and reverb (44 KB + 37 KB) fit beside WiFi.
 * Part of the unity build (device/unity.c), after the platform: gfx.c is in scope. */

#define C_TXT C_WHITE
#define C_MID RGB(150, 150, 150)
#define C_LOW RGB(80, 80, 80)
#define ROW 13                           /* Terminus 12 (FONT_XS) and a pixel */
#define REFETCH_MS (15u * 60u * 1000u)
#define DELAY_BYTES (44u * 1024u)        /* COSMOS's delay: stereo, 0.5 s, int16 at 22.05 kHz */
#define REVERB_BYTES (37u * 1024u)       /* and its reverb, int16 */

extern uint32_t shim_heap_used, shim_heap_peak;
extern const uint32_t shim_heap_size;
extern void *malloc(unsigned long n);
extern void free(void *p);

static const char CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                            " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
static const uint8_t GROUP[] = {0, 26, 52, 62};   /* abc ABC 123 #+= */
#define NCHARS (sizeof CHARS - 1u)

static const char *const STATE[] = {"OFF", "STARTING", "ON", "JOINING", "ADDRESS", "CONNECTED", "FAILED"};
enum { S_MAIN, S_NETS, S_PASS };

static struct {
    int screen;
    uint32_t btn;
    int pick;                            /* the network picked in the scan's list */
    char ssid[33];                       /* the one being joined */
    char pass[64];
    int plen, ch;                        /* the password's length, the character SELECT is on */
    char msg[2][32];
    uint32_t msg_until;
    uint8_t redraw, fetched, measured;
    uint32_t last_fetch;
    uint32_t biggest, used_rest;         /* after a fetch: the biggest block, the heap in use */
    int test_ok;                         /* -1 not yet, 0 failed, 1 the delay and reverb fit */
} ui = {.test_ok = -1};

void app_init(void) {}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}

/* for BearSSL's random generator: no hardware generator is set up, so the clocks and MASTER, mixed (BearSSL
 * hashes it). Weak by a security standard; fine for weather numbers */
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
static char *put_fix(char *d, int32_t v, int places)   /* v / 10^places, with its decimals */
{
    int32_t div = places == 2 ? 100 : 10;
    d = put_int(d, v / div);
    *d++ = '.';
    if (places == 2 && v % div < 10)
        *d++ = '0';
    return put_int(d, v % div);
}
static char *put_kb(char *d, uint32_t bytes) { return put(put_int(d, (int32_t)(bytes / 1024u)), " KB"); }

/* ---- the heap, measured from the main loop while the network rests */
static uint32_t biggest_block(void)
{
    uint32_t lo = 0, hi = shim_heap_size;
    while (hi - lo > 512u) {
        uint32_t mid = (lo + hi) / 2u;
        void *p = malloc(mid);
        if (p) {
            free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}
static void measure(void)
{
    void *d = malloc(DELAY_BYTES), *r = malloc(REVERB_BYTES);
    ui.test_ok = d && r;
    free(r);
    free(d);
    ui.biggest = biggest_block();
    ui.used_rest = shim_heap_used;
    ui.measured = 1;
    ui.redraw = 1;
}

/* ---- input */
static void join(const char *ssid, const char *pass)
{
    if (plat_wifi_join(ssid, pass) == 0) {
        app_say("JOINING", ssid);
        ui.fetched = 0;
        ui.screen = S_MAIN;
    } else {
        app_say("CAN'T JOIN", ssid);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn;
    int32_t e = plat_enc(EN_SELECT), g = plat_enc(EN_ALGO);
    ui.btn = btn;
    if (e || g || down)
        ui.redraw = 1;
    if (ui.screen == S_MAIN) {
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
        if (down >> B_PLAY & 1u) {
            if (plat_wifi_state() != WIFI_CONNECTED)
                app_say("NOT CONNECTED", "");
            else if (net_fetch() == 0) {
                ui.fetched = 1;
                ui.measured = 0;
                ui.last_fetch = plat_ms();
            }
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
    const net_res_t *r = net_result();
    char t[48], v[24], *q;
    uint32_t ip = plat_wifi_ip();
    int st = plat_wifi_state(), i;
    row(0, "COSMOS PROBE", C_TXT, net_busy() ? "FETCHING" : "", C_MID);
    row(1, RULE, C_LOW, 0, 0);
    q = put(t, "WIFI     ");
    put(q, st >= 0 && st <= WIFI_FAILED ? STATE[st] : "?");
    v[0] = 0;
    if (st == WIFI_CONNECTED)
        put_int(put(v, "SIG "), plat_wifi_rssi());
    else if (st == WIFI_FAILED)
        copy(v, plat_wifi_why(), sizeof v);
    row(2, t, st == WIFI_CONNECTED ? C_TXT : C_MID, v, C_MID);
    q = put(t, "NETWORK  ");
    put(q, plat_wifi_ssid()[0] ? plat_wifi_ssid() : "-");
    row(3, t, C_TXT, 0, 0);
    q = put(t, "ADDRESS  ");
    if (ip)
        for (i = 0; i < 4; i++) {
            q = put_int(q, (int32_t)(ip >> (8 * i) & 255u));
            if (i < 3)
                q = put(q, ".");
        }
    else
        put(q, "-");
    row(4, t, C_TXT, 0, 0);
    row(5, RULE, C_LOW, 0, 0);
    q = put(t, "CLOUDS   ");
    if (r->cloud >= 0)
        put(put_int(q, r->cloud), " %");
    else
        put(q, "-");
    row(6, t, C_TXT, "OPEN-METEO", C_LOW);
    q = put(t, "PRESS    ");
    if (r->press10 >= 0)
        put(put_fix(q, r->press10, 1), " HPA");
    else
        put(q, "-");
    row(7, t, C_TXT, 0, 0);
    q = put(t, "KP       ");
    if (r->kp100 >= 0)
        put_fix(q, r->kp100, 2);
    else
        put(q, "-");
    row(8, t, C_TXT, r->kp_time, C_LOW);
    q = put(t, "TIME     ");
    put(q, r->date[0] ? r->date + (r->date[3] == ',' ? 5 : 0) : "-");   /* "26 Oct 2026 16:41:02 GMT" */
    row(9, t, C_TXT, 0, 0);
    q = put(t, "FETCH    ");
    q = put_int(put(put_int(q, r->done), " OF 2  TLS "),
                (int32_t)(r->tls_ms[0] > r->tls_ms[1] ? r->tls_ms[0] : r->tls_ms[1]));
    put(q, " MS");
    row(10, ui.fetched ? t : "FETCH    -", C_TXT, 0, 0);
    row(11, r->why[0] ? r->why : "", C_MID, 0, 0);
    row(12, RULE, C_LOW, 0, 0);
    put_kb(put(put_kb(put(t, "HEAP     "), shim_heap_size), "  USED "), shim_heap_used);
    row(13, t, C_TXT, 0, 0);
    if (ui.measured) {
        put_kb(put(t, "FETCH    PEAK "), r->peak);
        row(14, t, C_TXT, 0, 0);
        put_kb(put(put_kb(put(t, "REST     USED "), ui.used_rest), "  BIG "), ui.biggest);
        row(15, t, C_TXT, 0, 0);
        row(16, ui.test_ok ? "TEST     DELAY+REVERB 81 KB: FIT" : "TEST     DELAY+REVERB 81 KB: NO ROOM",
            ui.test_ok ? C_TXT : C_MID, 0, 0);
    } else {
        row(14, "FETCH    PEAK -", C_MID, 0, 0);
        row(15, "REST     -", C_MID, 0, 0);
        row(16, "TEST     -", C_MID, 0, 0);
    }
    row(17, plat_ms() < ui.msg_until ? ui.msg[0] : "PLAY FETCH  SEL WIFI  REC ON/OFF",
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
    plat_wifi_on(1);                         /* on at start: it joins the networks kept */
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_draw;
    static int last_screen = -1;
    uint32_t now = plat_ms();
    input();
    if (plat_wifi_state() == WIFI_CONNECTED && !net_busy() &&
        (!ui.fetched || now - ui.last_fetch > REFETCH_MS)) {   /* at once when connected, then every 15 minutes */
        if (net_fetch() == 0) {
            ui.fetched = 1;
            ui.measured = 0;
            ui.last_fetch = now;
        }
    }
    if (ui.fetched && !ui.measured && !net_busy())
        measure();
    if (now - last_draw > 500u || (ui.msg_until && now >= ui.msg_until)) {
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
/* the host simulator has no lwIP (net.c is the device's): the fetch answers at once */
static net_res_t host_res = {2, 72, 10123, 233, "2026-10-26T15:00", "Mon, 26 Oct 2026 16:41:02 GMT", "", {900, 700}, 90000};
uint32_t shim_heap_used = 120000, shim_heap_peak = 150000;
const uint32_t shim_heap_size = 278528;
int net_fetch(void) { return 0; }
int net_busy(void) { return 0; }
const net_res_t *net_result(void) { return &host_res; }
#endif
