// SPDX-License-Identifier: GPL-3.0-or-later
// COSMOS in the browser: the computer's clock and its offset from UTC, for the sky (cosmos/web.c); and RADIO's
// half in the worklet: what the firmware's radio sends (cosmos/radio.c, RD_WEB) goes to the page every few ms, as
// bytes; what the page's MQTT brings goes into it (the page's half: app.js)
globalThis.fm1App = {
  load(p) {
    if (p.ex.web_cosmos_time)
      p.ex.web_cosmos_time(Math.floor(Date.now() / 1000), -new Date().getTimezoneOffset());
    this.q = 0;
  },
  message(p, m) {
    const ex = p.ex;
    if (!ex.web_radio_in) return;
    if (m.rid && m.rid.length === 5)                  // its radio id, the page's
      ex.web_cosmos_radio_id(...[...m.rid].map((c) => c.charCodeAt(0)));
    if (m.clock !== undefined)                        // the time now, ms since 1970 (UTC)
      ex.web_radio_clock(Math.floor(m.clock / 1000) >>> 0, Math.floor(m.clock % 1000));
    if (m.live !== undefined) ex.web_radio_state(m.live ? 1 : 0);
    if (m.rin) {                                      // a message: its topic's bytes, its message's
      const [t, b] = m.rin, buf = new Uint8Array(p.mem.buffer, ex.web_radio_in_buf(), 512);
      if (t.length + b.length > 512) return;
      buf.set(t, 0);
      buf.set(b, t.length);
      ex.web_radio_in(t.length, b.length);
    }
  },
  process(p) {
    if (!p.ex.web_radio_poll || ++this.q < 4) return;   // every 4 quanta: about 12 ms
    this.q = 0;
    const n = p.ex.web_radio_poll();
    if (!n) return;
    const out = new Uint8Array(p.mem.buffer, p.ex.web_radio_out(), n).slice();
    p.ex.web_radio_taken();
    p.port.postMessage({ type: "app", radio: out }, [out.buffer]);
  },
};
