// SPDX-License-Identifier: GPL-3.0-or-later
// COSMOS in the browser: the computer's clock and its offset from UTC, for the sky (cosmos/web.c)
globalThis.fm1App = {
  load(p) {
    if (p.ex.web_cosmos_time)
      p.ex.web_cosmos_time(Math.floor(Date.now() / 1000), -new Date().getTimezoneOffset());
  },
};
