/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* TILT: a test firmware for COSMOS's phone remote. One plain voice (two saws and a sub, a resonant low-pass); the
 * phone's tilt opens and closes the filter over the internet (MQTT, remote.c; the page at /COSMOS/remote/). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TILT"
#define APP_SPLASH_RGB RGB(255, 255, 255)
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 48u)       /* a strip at a time: WiFi's buffers need the pool (gfx.c) */
#define APP_BIG_FONT FONT_M              /* the splash's name: 20 KB less than FONT_L */

/* the store: the networks kept (OBJ_WIFI, the platform's), the radio's address and calibration (OBJ_BLE), the
 * panel calibration (the platform's); as COSMOS PROBE's, no settings of its own (COSMOS's stay as they were) */
enum { OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xDE000u, 0xDA000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* net.c's remote (built apart, with lwIP's headers): the phone's filter over MQTT, in a task of its own while on */
enum { RM_OFF, RM_NO_WIFI, RM_CONNECTING, RM_LIVE, RM_RETRY };
int net_remote(int on, const char *code);   /* on with the pairing code, or off; 0 = done */
int net_remote_state(void);              /* RM_* */
uint32_t net_remote_count(void);         /* the messages so far */
const char *net_remote_why(void);        /* what went wrong last, or "" */
uint32_t net_remote_get(int32_t *open, int32_t *res);   /* the last message's numbers (0..1000), its age in ms (~0u:
                                                         * none yet) */
