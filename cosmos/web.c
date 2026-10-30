/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS in the browser (fm1-base's emulator, web/emu/emu_web.c includes this: app.h APP_WEB): the page's own exports.
 * The computer's clock and its offset from UTC, given before the boot: the sky is the visitor's now. */
__attribute__((used, visibility("default"))) void web_cosmos_time(uint32_t unix, int32_t utc_min)
{
    web_unix = unix;
    web_utc_min = utc_min;
}
/* RADIO's id in the browser: the page's own (5 characters, kept in the browser), not the chip's */
__attribute__((used, visibility("default"))) void web_cosmos_radio_id(int a, int b, int c, int d, int e)
{
    ui.radio_id[0] = (char)a;
    ui.radio_id[1] = (char)b;
    ui.radio_id[2] = (char)c;
    ui.radio_id[3] = (char)d;
    ui.radio_id[4] = (char)e;
    ui.radio_id[5] = 0;
}
/* RADIO in the browser (radio.c's RD_WEB): its half in the worklet calls these (cosmos/web/emu/app_worklet.js) */
__attribute__((used, visibility("default"))) int web_radio_poll(void) { return rdw_poll(); }
__attribute__((used, visibility("default"))) char *web_radio_out(void) { return rdw_out(); }
__attribute__((used, visibility("default"))) void web_radio_taken(void) { rdw_taken(); }
__attribute__((used, visibility("default"))) void web_radio_state(int live) { rdw_state(live); }
__attribute__((used, visibility("default"))) uint8_t *web_radio_in_buf(void) { return rdw_in_buf(); }
__attribute__((used, visibility("default"))) void web_radio_in(int tn, int mn) { rdw_in(tn, mn); }
__attribute__((used, visibility("default"))) void web_radio_clock(uint32_t sec, uint32_t ms) { rdw_clock(sec, ms); }
