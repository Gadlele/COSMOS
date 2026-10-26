# SPDX-License-Identifier: GPL-3.0-or-later
# COSMOS sound lab: the shared parts of every proposal.
# - the harmony as COSMOS plays it (a seventh chord of the scale every 10 s, the mode from the sun)
# - the star sequencer from the real sky over Rome tonight (the 500 brightest stars, the firmware's projection)
# - DSP building blocks (numba), effects, mastering
import math, os, wave
import numpy as np
from numba import njit

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
STARS_CSV = os.path.join(os.path.dirname(HERE), "..", "cosmos", "data", "stars.csv")

MODES = {  # semitones of the seven degrees
    "phrygian": [0, 1, 3, 5, 7, 8, 10], "aeolian": [0, 2, 3, 5, 7, 8, 10], "dorian": [0, 2, 3, 5, 7, 9, 10],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10], "ionian": [0, 2, 4, 5, 7, 9, 11], "lydian": [0, 2, 4, 6, 7, 9, 11]}


def semis(mode, d):
    m = MODES[mode]
    return m[d % 7] + 12 * (d // 7)


class Harmony:
    """four chords of 10 s, as COSMOS's harmony(): degrees of the scale, sevenths"""

    def __init__(self, root_hz=110.0, mode="dorian", prog=(0, 5, 3, 4), chord_s=10.0):
        self.root, self.mode, self.prog, self.chord_s = root_hz, mode, prog, chord_s

    def chords(self, dur):
        out, t, k = [], 0.0, 0
        while t < dur:
            d = self.prog[k % len(self.prog)]
            out.append((t, min(dur, t + self.chord_s), d))
            t += self.chord_s
            k += 1
        return out

    def hz(self, d, octave=0):
        return self.root * 2 ** octave * 2 ** (semis(self.mode, d) / 12)

    def chord_hz(self, d, octave=1, n=4, step=2):
        return [self.hz(d + step * i, octave) for i in range(n)]


# ------------------------------------------------------------------ the sky tonight
def star_events(dur, sweep_s=16.0, lat=41.9, lon=12.5, unix=None, maglim=3.0):
    """(time, degree 0..14, velocity 0..1, x, y, mag) for each star crossed by the sweep, as the firmware's arp_tick"""
    if unix is None:
        unix = 1793048400  # 2026-10-26 22:00 CET (21:00 UTC)
    jd = unix / 86400 + 2440587.5
    gmst = (280.46061837 + 360.98564736629 * (jd - 2451545.0)) % 360
    lst = math.radians((gmst + lon) % 360)
    la = math.radians(lat)
    pts = []
    for line in open(STARS_CSV):
        if line.startswith("#") or not line.strip():
            continue
        hr, ra, dec, mag = line.split(",")
        ra, dec, mag = math.radians(float(ra)), math.radians(float(dec)), float(mag)
        if mag > maglim:
            continue
        h = lst - ra
        alt = math.asin(math.sin(la) * math.sin(dec) + math.cos(la) * math.cos(dec) * math.cos(h))
        az = math.atan2(math.sin(h), math.cos(h) * math.sin(la) - math.tan(dec) * math.cos(la)) + math.pi
        if alt < 0:
            continue
        r = 170 * math.tan((math.pi / 2 - alt) / 2)
        x, y = 120 - r * math.sin(az), 120 - r * math.cos(az)
        if 0 <= x < 240 and 0 <= y < 240:
            pts.append((x, y, mag))
    ev = []
    k = 0
    while k * sweep_s < dur:
        for x, y, mag in pts:
            t = k * sweep_s + x / 240 * sweep_s
            if t < dur:
                deg = int((239 - y) * 15 / 240)
                vel = min(1.0, max(0.25, (3.2 - mag) / 4.0))
                ev.append((t, deg, vel, x, y, mag))
        k += 1
    ev.sort()
    return ev


# ------------------------------------------------------------------ building blocks
def tt(n):
    return np.arange(n) / SR


@njit(cache=True)
def _saw_blep(ph, dt):
    out = np.empty(ph.size)
    for i in range(ph.size):
        t = ph[i]
        x = 2.0 * t - 1.0
        d = dt[i]
        if t < d:
            u = t / d
            x -= u + u - u * u - 1.0
        elif t > 1.0 - d:
            u = (t - 1.0) / d
            x -= u * u + u + u + 1.0
        out[i] = x
    return out


def phase(freq, n, ph0=0.0):
    """freq: scalar or array (Hz) -> phase 0..1, its step"""
    f = np.broadcast_to(np.asarray(freq, dtype=float), (n,))
    dt = f / SR
    ph = (ph0 + np.cumsum(dt)) % 1.0
    return ph, dt


def saw(freq, n, ph0=0.0):
    ph, dt = phase(freq, n, ph0)
    return _saw_blep(ph, dt)


def pulse(freq, n, width=0.5, ph0=0.0):
    ph, dt = phase(freq, n, ph0)
    w = np.broadcast_to(np.asarray(width, dtype=float), (n,))
    a = _saw_blep(ph, dt)
    b = _saw_blep((ph + 1.0 - w) % 1.0, dt)
    return (a - b) * 0.5 - (2 * w - 1) * 0.0


def sine(freq, n, ph0=0.0):
    ph, _ = phase(freq, n, ph0)
    return np.sin(2 * np.pi * ph)


def tri(freq, n, ph0=0.0):
    ph, _ = phase(freq, n, ph0)
    return np.abs(4 * ph - 2) - 1


def fm(fc, ratio, index, n, fb=0.0, ph0=0.0):
    """two-operator FM: index scalar or array"""
    pm, _ = phase(np.asarray(fc) * ratio, n, ph0 * 0.37)
    pc, _ = phase(fc, n, ph0)
    return np.sin(2 * np.pi * pc + np.asarray(index) * np.sin(2 * np.pi * pm))


def adsr(n, a, d, s, r, hold):
    """attack, decay, sustain level, release (s), held for hold s; length n"""
    t = tt(n)
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    rel = np.clip(1 - (t - hold) / max(r, 1e-4), 0, 1)
    e = np.where(t < hold, e, e[min(int(hold * SR), n - 1)] * rel)
    return e


def perc(n, a, tau):
    t = tt(n)
    return np.minimum(t / max(a, 1e-4), 1.0) * np.exp(-t / tau)


@njit(cache=True)
def svf(x, fc, q, mode):
    """Simper's SVF, time-varying: fc array (Hz), q scalar; mode 0 lp 1 bp 2 hp"""
    y = np.empty(x.size)
    s1 = 0.0
    s2 = 0.0
    k = 1.0 / q
    for i in range(x.size):
        g = math.tan(math.pi * min(fc[i], 20000.0) / 44100.0)
        a1 = 1.0 / (1.0 + g * (g + k))
        a2 = g * a1
        a3 = g * a2
        v3 = x[i] - s2
        v1 = a1 * s1 + a2 * v3
        v2 = s2 + a2 * s1 + a3 * v3
        s1 = 2.0 * v1 - s1
        s2 = 2.0 * v2 - s2
        if mode == 0:
            y[i] = v2
        elif mode == 1:
            y[i] = v1
        else:
            y[i] = x[i] - k * v1 - v2
    return y


def lp(x, fc, q=0.707):
    return svf(x, np.broadcast_to(np.asarray(fc, dtype=float), (x.size,)).copy(), q, 0)


def bp(x, fc, q=2.0):
    return svf(x, np.broadcast_to(np.asarray(fc, dtype=float), (x.size,)).copy(), q, 1)


def hp(x, fc, q=0.707):
    return svf(x, np.broadcast_to(np.asarray(fc, dtype=float), (x.size,)).copy(), q, 2)


@njit(cache=True)
def karplus(n, period, decay, bright, seed):
    """a plucked string: a noise burst in a delay line, a one-pole low-pass in the loop"""
    p = max(2, int(period))
    frac = period - p
    buf = np.zeros(p + 2)
    np.random.seed(seed)
    for i in range(p + 2):
        buf[i] = np.random.uniform(-1.0, 1.0)
    y = np.empty(n)
    idx = 0
    last = 0.0
    for i in range(n):
        a = buf[idx % (p + 2)]
        b = buf[(idx + 1) % (p + 2)]
        v = a + (b - a) * frac
        last = last + bright * (v - last)
        out = last * decay
        y[i] = a
        buf[idx % (p + 2)] = out
        idx += 1
    return y


@njit(cache=True)
def resonator(x, f, r):
    """a two-pole resonator at f Hz, pole radius r"""
    y = np.empty(x.size)
    c = 2.0 * r * math.cos(2.0 * math.pi * f / 44100.0)
    r2 = r * r
    y1 = 0.0
    y2 = 0.0
    for i in range(x.size):
        v = x[i] + c * y1 - r2 * y2
        y[i] = v
        y2 = y1
        y1 = v
    return y * (1.0 - r)


@njit(cache=True)
def _fdn(xl, xr, lens, g, damp, mod):
    n = xl.size
    L = lens.size
    maxl = 0
    for j in range(L):
        maxl = max(maxl, lens[j])
    bufs = np.zeros((L, maxl + 64))
    w = np.zeros(L, dtype=np.int64)
    lpst = np.zeros(L)
    ol = np.empty(n)
    orr = np.empty(n)
    o = np.zeros(L)
    for i in range(n):
        for j in range(L):
            m = lens[j] + mod * math.sin(i * (0.00003 + 0.000011 * j) + j)
            rp = w[j] - m
            ri = int(math.floor(rp))
            fr = rp - ri
            sz = bufs.shape[1]
            a = bufs[j, ri % sz]
            b = bufs[j, (ri + 1) % sz]
            v = a + (b - a) * fr
            lpst[j] = lpst[j] + damp * (v - lpst[j])
            o[j] = lpst[j]
        # Householder mix
        s = 0.0
        for j in range(L):
            s += o[j]
        s *= 2.0 / L
        ol[i] = 0.0
        orr[i] = 0.0
        for j in range(L):
            fb = (o[j] - s) * g
            inp = xl[i] if (j % 2 == 0) else xr[i]
            sz = bufs.shape[1]
            bufs[j, w[j] % sz] = inp + fb
            w[j] += 1
            if j % 2 == 0:
                ol[i] += o[j]
            else:
                orr[i] += o[j]
    return ol, orr


def reverb(st, decay_s=4.0, damp=0.35, size=1.0, wet=0.35, mod=6.0, predelay=0.02):
    """an 8-line FDN; decay_s: the RT60"""
    lens = (np.array([1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116]) * 2.3 * size).astype(np.int64)
    mean = lens.mean() / SR
    g = 10 ** (-3 * mean / decay_s)
    pd = int(predelay * SR)
    xl = np.concatenate([np.zeros(pd), st[:, 0]])[: len(st)] * 0.3
    xr = np.concatenate([np.zeros(pd), st[:, 1]])[: len(st)] * 0.3
    ol, orr = _fdn(xl, xr, lens, g, damp, mod)
    return np.stack([st[:, 0] * (1 - wet * 0.5) + ol * wet, st[:, 1] * (1 - wet * 0.5) + orr * wet], 1)


def pingpong(st, time_s=0.375, fb=0.45, wet=0.3, damp=3500.0):
    n, D = len(st), int(time_s * SR)
    out = np.zeros((n, 2))
    inl, inr = st[:, 0].copy(), st[:, 1].copy()
    yl, yr = np.zeros(n), np.zeros(n)
    for k in range(0, n, D):  # y[n] = x[n-D] crossed + fb * y[n-D] crossed
        e = min(n, k + D)
        if k >= D:
            yl[k:e] = inr[k - D:e - D] * 0 + (inl[k - D:e - D] + fb * yr[k - D:e - D])
            yr[k:e] = (inr[k - D:e - D] + fb * yl[k - D:e - D])
    yl, yr = lp(yl, damp), lp(yr, damp)
    out[:, 0] = st[:, 0] + yl * wet
    out[:, 1] = st[:, 1] + yr * wet
    return out


def chorus(st, depth_ms=3.0, rate=0.3, center_ms=12.0, mix=0.5):
    n = len(st)
    i = np.arange(n)
    out = st.copy()
    for ch, off in ((0, 0.0), (1, np.pi / 2)):
        d = (center_ms + depth_ms * np.sin(2 * np.pi * rate * i / SR + off)) * SR / 1000
        p = np.clip(i - d, 0, n - 1)
        lo = p.astype(int)
        fr = p - lo
        hi = np.minimum(lo + 1, n - 1)
        src = st[:, 1 - ch]
        out[:, ch] = st[:, ch] * (1 - mix * 0.5) + (src[lo] * (1 - fr) + src[hi] * fr) * mix
    return out


def pan(x, p):
    """p -1 left .. 1 right, equal power"""
    a = (np.asarray(p) + 1) * np.pi / 4
    return np.stack([x * np.cos(a), x * np.sin(a)], 1)


def place(buf, x, t):
    i = int(round(t * SR))
    if i < 0:                       # starts before the buffer: what falls inside
        x = x[-i:]
        i = 0
    if i >= len(buf) or len(x) == 0:
        return
    j = min(len(buf), i + len(x))
    buf[i:j] += x[: j - i]


def master(st, dur, peak=0.89, drive=1.0):
    st = st[: int(dur * SR)]
    st = st - st.mean(0)
    n = len(st)
    t = tt(n)
    fade = np.clip(t / 1.5, 0, 1) * np.clip((dur - t) / 3.0, 0, 1)
    st = st * fade[:, None]
    st = st / (np.sqrt((st ** 2).mean()) + 1e-9) * 0.2 * drive     # the same loudness for each: 0.2 RMS
    return peak * np.tanh(st / peak)                                  # peaks folded softly under the ceiling


def write(path, st):
    w = wave.open(path, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((np.clip(st, -1, 1) * 32767).astype(np.int16).tobytes())
    w.close()


# ------------------------------------------------------------------ COSMOS's drone (soft FM, root and fifth)
def cosmos_drone(h, dur, level=0.12, glide=1.5, index=0.6, fifth=True):
    n = int(dur * SR)
    f = np.zeros(n)
    for t0, t1, d in h.chords(dur):
        f[int(t0 * SR):] = h.hz(d)
    a = np.exp(-1 / (glide * SR))
    from scipy.signal import lfilter
    f = lfilter([1 - a], [1, -a], f, zi=[f[0] * a])[0]
    out = np.zeros(n)
    for mul, lv in ((1.0, 1.0), (1.5, 0.6 if fifth else 0.0)):
        for c in (-6, 6):
            fr = f * mul * 2 ** (c / 2400)
            out += fm(fr, 1.0, index, n, ph0=np.random.rand()) * lv
        out += fm(f * mul * 2, 1.0, index * 0.6, n) * lv * 0.4
    out += sine(f * 0.5, n) * 0.6
    breath = 0.85 + 0.15 * np.sin(2 * np.pi * 0.05 * tt(n))
    return pan(lp(out, 900) * level * breath, 0.0)
