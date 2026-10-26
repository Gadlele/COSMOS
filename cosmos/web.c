/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS in the browser (fm1-base's emulator, web/emu/emu_web.c includes this: app.h APP_WEB): the page's own exports.
 * The computer's clock and its offset from UTC, given before the boot: the sky is the visitor's now. */
__attribute__((used, visibility("default"))) void web_cosmos_time(uint32_t unix, int32_t utc_min)
{
    web_unix = unix;
    web_utc_min = utc_min;
}
