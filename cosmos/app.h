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
#define APP_SPLASH                       /* app_splash: the name on black (stage 4: the stars gather into it) */
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 48u)       /* a strip at a time: WiFi's buffers need the pool (gfx.c) */

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
    char date[40];                       /* the last server's Date header: the time */
    char why[32];                        /* what went wrong, or "" */
} net_res_t;
#define NET_NONE_S (-999999)
int net_fetch(int32_t lat100, int32_t lon100);   /* 0 = started; -1 = one is running */
int net_busy(void);
const net_res_t *net_result(void);
void cosmos_entropy(uint8_t *p, unsigned n);   /* app.c: for the TLS random generator */
