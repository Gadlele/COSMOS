// SPDX-License-Identifier: GPL-3.0-or-later
// COSMOS in the browser, RADIO's half in the page: the firmware's radio (cosmos/radio.c, RD_WEB, in the worklet)
// talks to the same public broker as the FM-1s, over MQTT on a WebSocket. Its lines: "C <id>" connect (a will
// emptying here/<id>), "D" disconnect, "P <retain> <topic> <message>" publish. What comes goes back to it, message
// by message; the clock is the site's (its Date header, the second's edge found), as the test player's.
const qs = new URLSearchParams(location.search);
const BROKER = qs.get("broker") || "wss://broker.hivemq.com:8884/mqtt";
const enc = new TextEncoder(), dec = new TextDecoder();
let node = null, mq = null, offset = 0, want = "";

// ---- the radio id: the browser's own, kept ("E" and four)
const ID_CHARS = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
let ID = "";
try { ID = localStorage.getItem("cosmos-emu-radio") || ""; } catch (e) {}
if (!/^[A-Z2-9]{5}$/.test(ID)) {
  ID = "E" + Array.from({ length: 4 }, () => ID_CHARS[Math.floor(Math.random() * ID_CHARS.length)]).join("");
  try { localStorage.setItem("cosmos-emu-radio", ID); } catch (e) {}
}

// ---- the time: the site's Date header, its second's edge found (the FM-1s keep NTP time)
async function syncClock() {
  try {
    let prev = null;
    for (let k = 0; k < 14; k++) {
      const t0 = Date.now();
      const r = await fetch(location.pathname + "?t=" + t0, { method: "HEAD", cache: "no-store" });
      const t1 = Date.now(), d = r.headers.get("Date");
      if (!d) return;
      const s = Date.parse(d);
      if (prev !== null && s !== prev) { offset = s - (t0 + t1) / 2; return; }
      prev = s;
      offset = s + 500 - (t0 + t1) / 2;
      await new Promise((f) => setTimeout(f, 90));
    }
  } catch (e) {}
}
function sendClock() { if (node) node.port.postMessage({ type: "app", rid: ID, clock: Date.now() + offset }); }

// ---- a small MQTT 3.1.1 client: CONNECT (with a will), SUBSCRIBE, PUBLISH at QoS 0, PINGREQ
function mqttStr(s) { const b = typeof s === "string" ? enc.encode(s) : s; return [b.length >> 8, b.length & 255, ...b]; }
function mqttPacket(type, body) {
  const len = []; let n = body.length;
  do { let d = n & 127; n >>= 7; if (n) d |= 128; len.push(d); } while (n);
  return new Uint8Array([type, ...len, ...body]);
}
class Mqtt {
  constructor(url, id, will, onlive, onmsg) {
    Object.assign(this, { url, id, will, onlive, onmsg, ok: false, closed: false, buf: new Uint8Array(0) });
    this.open();
  }
  open() {
    const ws = this.ws = new WebSocket(this.url, "mqtt");
    ws.binaryType = "arraybuffer";
    ws.onopen = () => {
      const body = [...mqttStr("MQTT"), 4, 0x02 | 0x04 | 0x20, 0, 30, ...mqttStr(this.id), ...mqttStr(this.will), 0, 0];
      ws.send(mqttPacket(0x10, body));
    };
    ws.onmessage = (ev) => {
      const a = new Uint8Array(ev.data), b = new Uint8Array(this.buf.length + a.length);
      b.set(this.buf); b.set(a, this.buf.length);
      let i = 0;
      for (;;) {                                       // whole packets
        let len = 0, mul = 1, h = i + 1, whole = false;
        while (h < b.length && h < i + 5) { len += (b[h] & 127) * mul; mul *= 128; if (!(b[h++] & 128)) { whole = true; break; } }
        if (!whole || h + len > b.length) break;
        this.packet(b[i], b.subarray(h, h + len));
        i = h + len;
      }
      this.buf = b.slice(i);
    };
    ws.onclose = () => {
      const was = this.ok;
      this.ok = false; clearInterval(this.ping);
      if (this.closed) return;
      if (was) this.onlive(false);
      this.retry = setTimeout(() => this.open(), 3000);
    };
    clearInterval(this.ping);
    this.ping = setInterval(() => { if (this.ok) ws.send(new Uint8Array([0xC0, 0])); }, 15000);
  }
  packet(type, p) {
    if (type >> 4 === 2) {                             // CONNACK
      if (p[1] !== 0) return;
      this.ok = true;
      this.ws.send(mqttPacket(0x82, [0, 1, ...mqttStr(T + "here/+"), 0, ...mqttStr(T + "play/+"), 0]));
      this.onlive(true);
    } else if (type >> 4 === 3) {                      // PUBLISH (QoS 0)
      const tl = p[0] << 8 | p[1];
      this.onmsg(p.slice(2, 2 + tl), p.slice(2 + tl));
    }
  }
  publish(topic, msg, retain) {
    if (this.ok) this.ws.send(mqttPacket(retain ? 0x31 : 0x30, [...mqttStr(topic), ...enc.encode(msg)]));
  }
  close() {
    this.closed = true; clearTimeout(this.retry); clearInterval(this.ping);
    try { this.ws.send(new Uint8Array([0xE0, 0])); this.ws.close(); } catch (e) {}
  }
}
const T = "cosmos-radio/v3/";

// ---- the firmware's lines
function line(l) {
  if (l[0] === "C") {
    want = l.slice(2).trim();
    if (mq) mq.close();
    sendClock();
    mq = new Mqtt(BROKER, "cosmos-emu-" + want + "-" + Math.floor(Math.random() * 65536), T + "here/" + want,
      (live) => node.port.postMessage({ type: "app", live }),
      (t, b) => node.port.postMessage({ type: "app", rin: [t, b] }, [t.buffer, b.buffer]));
  } else if (l[0] === "D") {
    want = "";
    if (mq) { const m = mq; mq = null; setTimeout(() => m.close(), 150); }   // (the presence emptied first)
  } else if (l[0] === "P" && mq) {
    const a = l.indexOf(" ", 2), b = l.indexOf(" ", a + 1);
    if (a < 0) return;
    const topic = b < 0 ? l.slice(a + 1) : l.slice(a + 1, b), msg = b < 0 ? "" : l.slice(b + 1);
    mq.publish(topic, msg, l[2] === "1");
  }
}

export function attach(p) {
  node = p.node;
  for (const ms of [300, 1000, 2500]) setTimeout(sendClock, ms);   // (the id too: once the firmware is loaded)
  syncClock().then(sendClock);
  setInterval(sendClock, 10000);
  window.addEventListener("pagehide", () => { if (mq) mq.close(); });
}
export function message(m) {
  if (!m.radio) return;
  for (const l of dec.decode(m.radio).split("\n")) if (l) line(l);
}
