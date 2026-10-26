/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS: a drone synthesizer for the M-VAVE FM-1, played by the sky (docs/DESIGN.md).
 * Stage 2: the sky computed (dsp/astro.c) for a city, a date and a time: the sun and the moon play the sound, the
 * screen is the sky with the readings fading over it, a planet can be chosen while it is on the screen. The weather
 * is on the knobs or drifts by itself (random). WiFi is built in (its heap holds the delay and the reverb, as it
 * will when it fetches the weather) but stays off until stage 3. */
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
