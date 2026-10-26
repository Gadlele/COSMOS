/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS PROBE: a test firmware for COSMOS on fm1-base's WiFi. It joins a network (the password typed on the
 * panel), fetches the cloud cover and pressure (Open-Meteo) and the Kp index (NOAA) over HTTPS, and shows how much
 * of the WiFi heap is left: what COSMOS's delay and reverb can have. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "PROBE"
#define APP_SPLASH_RGB RGB(255, 255, 255)
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 48u)       /* a strip at a time: WiFi's buffers need the pool (gfx.c) */
#define APP_BIG_FONT FONT_M              /* the splash's name: 20 KB less than FONT_L */

/* the store: the networks kept (OBJ_WIFI, the platform's), the radio's address and calibration (OBJ_BLE), the
 * panel calibration (the platform's) */
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

/* net.c (built apart, with lwIP's headers): the data, fetched in a task of its own */
typedef struct {
    int done;                            /* requests that answered, of 2 */
    int32_t cloud;                       /* cloud cover, %; -1 none */
    int32_t press10;                     /* sea-level pressure, tenths of hPa; -1 none */
    int32_t kp100;                       /* the latest Kp, hundredths; -1 none */
    char kp_time[20];                    /* its time tag, "2026-10-26T15:00" */
    char date[40];                       /* the last server's Date header: the time, for the sky */
    char why[32];                        /* what went wrong, or "" */
    uint32_t tls_ms[2];                  /* each TLS handshake, ms */
    uint32_t peak;                       /* the heap's most during the fetch, bytes */
} net_res_t;
int net_fetch(void);                     /* 0 = started; -1 = one is running */
int net_busy(void);
const net_res_t *net_result(void);
void cosmos_entropy(uint8_t *p, unsigned n);   /* app.c: for the TLS random generator */
