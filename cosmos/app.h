/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS: a drone synthesizer for the M-VAVE FM-1, played by the sky (docs/DESIGN.md).
 * The sky computed (dsp/astro.c) for the player's place and time: the sun and the moon play the sound, the screen is
 * the sky with the readings over it, a planet can be chosen while it is up. The weather comes over WiFi every half
 * hour (net.c: Open-Meteo, NOAA's Kp, the time from the servers), or, offline, drifts by itself. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "COSMOS"
#define APP_SPLASH_RGB RGB(255, 255, 255)
#define APP_SPLASH                       /* app_splash: black at boot; the stars gather into the name in the first frames */
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 48u)       /* a strip at a time: WiFi's buffers need the pool (gfx.c) */
#define APP_WEB                          /* the browser emulator: the page's own exports (web.c) */

/* the store: COSMOS's settings, the networks kept (OBJ_WIFI, the platform's), the radio's address and calibration
 * (OBJ_BLE), the panel calibration (the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);

/* net.c (built apart, with lwIP's headers): the data, fetched in a task of its own */
typedef struct {
    int done;                            /* requests that answered, of 2 */
    int32_t cloud;                       /* cloud cover, %; -1 none */
    int32_t press10;                     /* sea-level pressure, tenths of hPa; -1 none */
    int32_t trend10;                     /* its change over the last 3 hours, tenths of hPa */
    int32_t utc_s;                       /* the place's offset from UTC now, s (summer time in it); NET_NONE_S none */
    int32_t kp100;                       /* the latest Kp, hundredths; -1 none */
    int32_t wcode;                       /* the weather's kind now, WMO code (0 clear .. 99 thunderstorm); -1 none */
    char date[40];                       /* the last server's Date header: the time */
    char why[32];                        /* what went wrong, or "" */
} net_res_t;
#define NET_NONE_S (-999999)
int net_fetch(int32_t lat100, int32_t lon100);   /* 0 = started; -1 = one is running */
int net_busy(void);
const net_res_t *net_result(void);
/* RADIO COSMOS (radio.c, in net.c): the shared sequence, the clock, who is tuned in; a task of its own while on */
enum { RADIO_OFF, RADIO_NO_WIFI, RADIO_CONNECTING, RADIO_LIVE, RADIO_RETRY };
int radio_on(int on, const char *id, const char *where);   /* on with this FM-1's id and place, or off; 0 = done */
int radio_state(void);                   /* RADIO_* */
const char *radio_why(void);             /* what went wrong last, or "" */
int radio_clock(uint32_t *sec, uint32_t *ms);   /* the time now (unix s and ms): 1 = synced */
int radio_slot(uint32_t slot, uint8_t *notes);  /* a 10 s slot's chord (unix s / 10): up to 3 MIDI notes; 0 the sky's */
void radio_counts(int *fm1, int *web);   /* the FM-1s tuned in (this one too), the web page's visitors */
void cosmos_entropy(uint8_t *p, unsigned n);   /* app.c: for the TLS random generator */
