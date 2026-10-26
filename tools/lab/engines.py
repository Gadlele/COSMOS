# SPDX-License-Identifier: GPL-3.0-or-later
# COSMOS sound lab: twenty proposals. Each renders 40 s of COSMOS playing: the harmony, a pad or instrument, the
# star sequencer (the real sky over Rome tonight; a star's place across the screen is its place in the stereo).
import math
import numpy as np
from lab import *

DUR = 40.0
rng = np.random.default_rng(11)


# ------------------------------------------------------------------ shared helpers
def chord_layer(h, dur, note_fn, octave=1, att=1.0, rel=2.5, spread=0.6, n=4, step=2, gain=0.2, overlap=0.0):
    """each chord's notes rendered by note_fn(freq, nsamples, index, chord_index) -> mono, enveloped, panned"""
    N = int(dur * SR)
    out = np.zeros((N + int(12 * SR), 2))
    for ci, (t0, t1, d) in enumerate(h.chords(dur)):
        held = (t1 - t0) + overlap
        ln = int((held + rel) * SR)
        for i, f in enumerate(h.chord_hz(d, octave, n, step)):
            x = note_fn(f, ln, i, ci)
            x = x * adsr(ln, att, 1.0, 1.0, rel, held)
            p = (i / max(1, n - 1) * 2 - 1) * spread
            place(out, pan(x, p) * gain, t0)
    return out[:N]


def star_freq(h, deg, octave=2):
    return h.root * 2 ** octave * 2 ** (semis(h.mode, deg) / 12)


def star_layer(h, dur, ev, voice_fn, length=3.0, octave=2, gain=0.15, width=0.8, quant=None, every=1):
    """the star sequencer: voice_fn(freq, vel, nsamples, event) -> mono"""
    N = int(dur * SR)
    out = np.zeros((N + int(length * SR) + SR, 2))
    for k, e in enumerate(ev):
        if k % every:
            continue
        t, deg, vel, x, y, mag = e
        if quant:
            t = round(t / quant) * quant
        f = star_freq(h, deg, octave)
        s = voice_fn(f, vel, int(length * SR), e)
        place(out, pan(s, (x / 240 * 2 - 1) * width) * gain * vel, t)
    return out[:N]


def mixdown(*layers):
    n = min(len(l) for l in layers)
    return sum(l[:n] for l in layers)


def bell_fm(f, n, ratio=3.5, index=3.0, tau=2.5, itau=0.6):
    t = tt(n)
    return np.sin(2 * np.pi * f * t + index * np.exp(-t / itau) * np.sin(2 * np.pi * f * ratio * t)) * np.exp(-t / tau)


def pitch_shift_oct(x, semis=12, grain=0.08):
    """a granular pitch shift: two taps whose delay sweeps down through a grain, half a grain apart (the shimmer)"""
    n = len(x)
    r = 2 ** (semis / 12)
    G = grain * SR
    i = np.arange(n)
    out = np.zeros(n)
    for off in (0.0, 0.5):
        ph = ((r - 1) * i / G + off) % 1.0
        idx = np.clip(i - G * (1 - ph), 0, n - 1)
        lo = idx.astype(int)
        fr = idx - lo
        hi = np.minimum(lo + 1, n - 1)
        out += (x[lo] * (1 - fr) + x[hi] * fr) * np.sin(np.pi * ph) ** 2
    return out


# ------------------------------------------------------------------ 1 NEBULA: supersaw clouds
def nebula():
    h = Harmony(110, "lydian")
    ev = star_events(DUR)
    def note(f, n, i, ci):
        x = np.zeros(n)
        for k, c in enumerate(np.linspace(-22, 22, 7)):
            x += saw(f * 2 ** (c / 1200), n, rng.random()) * (1.0 if k == 3 else 0.7)
        t = tt(n)
        fc = 500 + 3500 * (0.5 - 0.5 * np.cos(np.pi * np.clip(t / 8, 0, 1)))   # the light rises through each chord
        return lp(x, fc, 0.9) * 0.18
    pad = chord_layer(h, DUR, note, att=2.5, rel=4, spread=0.9, gain=0.5)
    stars = star_layer(h, DUR, ev, lambda f, v, n, e: sine(f, n) * perc(n, 0.004, 0.9) + sine(f * 2, n) * perc(n, 0.002, 0.25) * 0.3, gain=0.28)
    st = mixdown(chorus(pad, 4, 0.18), pingpong(stars, 0.42, 0.55, 0.45), cosmos_drone(h, DUR, 0.10))
    return reverb(st, 7.0, 0.3, 1.3, 0.45)


# ------------------------------------------------------------------ 2 PULSAR: a pad gated by a pulsar's beat
def pulsar():
    h = Harmony(98, "aeolian")
    ev = star_events(DUR)
    bpm = 104
    step = 60 / bpm / 4
    def note(f, n, i, ci):
        x = saw(f * 0.997, n) + saw(f * 1.003, n, 0.3) + pulse(f * 0.5, n, 0.3) * 0.5
        return lp(x, 1800, 1.2) * 0.25
    pad = chord_layer(h, DUR, note, att=0.05, rel=0.3, spread=0.7, gain=0.55)
    n = len(pad)
    t = tt(n)
    pattern = np.array([1, .3, .7, .3, 1, .3, .8, .5, 1, .3, .7, .3, 1, .6, .4, .9])
    s_idx = (t / step).astype(int)
    ph = (t / step) % 1
    gate = pattern[s_idx % 16] * np.exp(-ph * 3.2) * np.minimum(ph * step / 0.004, 1.0)   # 4 ms in: no click
    pad = pad * gate[:, None]
    # the pulse itself: a soft low thump on each beat
    thump = np.zeros(n)
    beat = 60 / bpm
    for k in range(int(DUR / beat)):
        m = int(0.6 * SR)
        tk = tt(m)
        place(thump, np.sin(2 * np.pi * (45 + 60 * np.exp(-tk / 0.03)) * tk) * np.exp(-tk / 0.22), k * beat)
    stars = star_layer(h, DUR, ev, lambda f, v, m, e: lp(saw(f, m) + saw(f * 1.005, m), 2500 * np.exp(-tt(m) / 0.2) + 300, 2.0) * perc(m, 0.002, 0.25),
                       length=1.0, gain=0.22, quant=step)
    st = mixdown(pad, pan(thump, 0) * 0.5, pingpong(stars, step * 3, 0.5, 0.4), cosmos_drone(h, DUR, 0.07))
    return reverb(st, 3.5, 0.4, 1.0, 0.3)


# ------------------------------------------------------------------ 3 AURORA: a shimmer of octaves over the pad
def aurora():
    h = Harmony(110, "ionian", prog=(0, 3, 5, 4))
    ev = star_events(DUR)
    def note(f, n, i, ci):
        return (sine(f, n) + 0.5 * tri(f * 1.002, n) + 0.25 * sine(f * 2.001, n)) * 0.3
    pad = chord_layer(h, DUR, note, att=3, rel=5, spread=0.8, gain=0.45)
    stars = star_layer(h, DUR, ev, lambda f, v, n, e: bell_fm(f, n, 2.0, 1.2, 1.8, 0.3), gain=0.25)
    dry = mixdown(pad, stars, cosmos_drone(h, DUR, 0.08))
    w1 = reverb(dry, 6, 0.25, 1.2, 1.0)[:, :] - dry * 0.5
    sh = np.stack([pitch_shift_oct(w1[:, 0]), pitch_shift_oct(w1[:, 1])], 1)
    w2 = reverb(sh * 0.6, 8, 0.2, 1.4, 1.0)
    sh2 = np.stack([pitch_shift_oct(w2[:, 0]), pitch_shift_oct(w2[:, 1])], 1)
    w3 = reverb(sh2 * 0.4, 9, 0.2, 1.5, 1.0)
    return dry * 0.8 + w1 * 0.35 + w2 * 0.3 + w3 * 0.22


# ------------------------------------------------------------------ 4 RINGS OF SATURN: strings that ring in sympathy
def saturn_rings():
    h = Harmony(110, "dorian")
    ev = star_events(DUR)
    N = int(DUR * SR)
    # a bowed glass drone: noise through resonators on the chord's tones and their octaves
    pad = np.zeros((N, 2))
    noise = rng.standard_normal(N) * 0.5
    for t0, t1, d in h.chords(DUR):
        a, b = int(t0 * SR), min(N, int((t1 + 3) * SR))
        seg = noise[a:b] * adsr(b - a, 2.0, 1, 1, 3.0, t1 - t0)
        for i, f in enumerate(h.chord_hz(d, 1)):
            for mul, g in ((1, 1), (2, 0.5), (3, 0.25)):
                r = resonator(seg, f * mul, 0.9997)
                place(pad, pan(r * g * 0.9, (i - 1.5) / 2), t0)
    # the stars: struck strings, each also waking the chord's strings
    def voice(f, v, n, e):
        exc = rng.standard_normal(n) * np.exp(-tt(n) / 0.004)
        x = resonator(exc, f, 0.9999) * 3 + resonator(exc, f * 2.01, 0.9997) * 1.5 + resonator(exc, f * 3.02, 0.9993)
        return x
    stars = star_layer(h, DUR, ev, voice, length=5, gain=0.5)
    st = mixdown(pad * 0.6, stars, cosmos_drone(h, DUR, 0.08))
    return reverb(st, 5, 0.4, 1.1, 0.35)


# ------------------------------------------------------------------ 5 SOLAR WIND: wind tuned to the chord
def solar_wind():
    h = Harmony(110, "mixolydian")
    ev = star_events(DUR)
    N = int(DUR * SR)
    t = tt(N)
    gust = 0.55 + 0.45 * np.sin(2 * np.pi * 0.07 * t) * np.sin(2 * np.pi * 0.023 * t + 1)
    pink = lp(rng.standard_normal(N), 4000) + 0.3 * rng.standard_normal(N)
    st = np.zeros((N, 2))
    for t0, t1, d in h.chords(DUR):
        a, b = int(t0 * SR), min(N, int((t1 + 3) * SR))
        e = adsr(b - a, 2.5, 1, 1, 3, t1 - t0)
        for i, f in enumerate(h.chord_hz(d, 1) + h.chord_hz(d, 2)[:2]):
            q = 40 + 30 * gust[a:b]
            x = svf(pink[a:b].copy(), np.full(b - a, f), 60.0, 1)
            place(st, pan(x * e * 0.5, math.sin(i * 1.7) * 0.8), t0)
    st *= gust[:, None]
    air = np.stack([bp(pink, 2500 + 1500 * gust, 0.7), bp(np.roll(pink, 999), 2600 + 1400 * gust, 0.7)], 1) * 0.05 * gust[:, None]
    def whistle(f, v, n, e):
        nz = rng.standard_normal(n)
        return svf(nz, np.full(n, f * 2), 120.0, 1) * adsr(n, 0.15, 1, 1, 0.8, 0.2) * 0.8
    stars = star_layer(h, DUR, ev, whistle, length=1.5, gain=0.5, every=2)
    st = mixdown(st, air, stars, cosmos_drone(h, DUR, 0.09))
    return reverb(st, 6, 0.3, 1.3, 0.4)


# ------------------------------------------------------------------ 6 GRAVITY: a black hole's pull
def gravity():
    h = Harmony(55, "phrygian", prog=(0, 5, 1, 4))
    ev = star_events(DUR)
    N = int(DUR * SR)
    def note(f, n, i, ci):
        t = tt(n)
        bend = 2 ** (-2 * np.exp(-t / 0.9) / 12)          # each chord falls into place
        x = saw(f * bend * 0.996, n) + saw(f * bend * 1.004, n, .5) + 0.6 * saw(f * bend * 2.003, n, .2)
        wob = 380 + 260 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.16 * t))
        return np.tanh(lp(x, wob, 1.6) * 1.8) * 0.25
    pad = chord_layer(h, DUR, note, octave=1, att=1.2, rel=3, spread=0.6, gain=0.5)
    sub = np.zeros(N)
    for t0, t1, d in h.chords(DUR):
        n = int((t1 - t0 + 1) * SR)
        place(sub, sine(h.hz(d, 0), n) * adsr(n, 0.8, 1, 1, 1, t1 - t0), t0)
    # swells sucked in backwards before each chord
    rev = np.zeros((N + 4 * SR, 2))
    for t0, t1, d in h.chords(DUR)[1:]:
        n = int(2.5 * SR)
        x = sum(fm(f, 1.0, 1.5, n) for f in h.chord_hz(d, 2)) * np.linspace(0, 1, n) ** 3 * 0.2
        place(rev, pan(x, 0) , t0 - 2.5)
    stars = star_layer(h, DUR, ev, lambda f, v, n, e: bell_fm(f * 0.5, n, 1.414, 2.0, 2.0, 0.4), gain=0.18, every=2)
    st = mixdown(pad, pan(sub, 0) * 0.35, reverb(rev[:N], 6, 0.3, 1.2, 0.7), stars)
    return reverb(st, 5, 0.45, 1.3, 0.3)


# ------------------------------------------------------------------ 7 QUASAR: the chord as a cloud of grains
def quasar():
    h = Harmony(110, "dorian")
    ev = star_events(DUR)
    N = int(DUR * SR)
    st = np.zeros((N + SR, 2))
    chords = h.chords(DUR)
    G = 2600
    for t0, t1, d in chords:
        fs = h.chord_hz(d, 1) + h.chord_hz(d, 2)
        for k in range(int((t1 - t0) * 70)):
            tg = t0 + rng.random() * (t1 - t0)
            f = fs[rng.integers(len(fs))] * 2 ** (rng.normal(0, 4) / 1200)
            g = int(G * (0.5 + rng.random()))
            x = sine(f, g, rng.random()) * np.hanning(g)
            place(st, pan(x, rng.uniform(-1, 1)) * 0.05, tg)
    # each star: a burst of bright grains around its note
    for t, deg, vel, x, y, mag in ev:
        f = star_freq(h, deg, 2)
        for k in range(12):
            g = 1500
            tg = t + rng.random() * 0.25
            gr = sine(f * (2 if rng.random() < 0.3 else 1), g) * np.hanning(g)
            place(st, pan(gr, (x / 120 - 1) * 0.9 + rng.normal(0, 0.1)) * 0.06 * vel, tg)
    st = st[:N]
    st = mixdown(st, cosmos_drone(h, DUR, 0.09))
    return reverb(pingpong(st, 0.31, 0.4, 0.3), 5, 0.3, 1.2, 0.4)


# ------------------------------------------------------------------ 8 ORGANO COSMICO: a cathedral organ
def organo():
    h = Harmony(73.42, "ionian", prog=(0, 3, 5, 4))
    ev = star_events(DUR)
    stops = [(0.5, 0.5), (1, 1.0), (2, 0.7), (3, 0.35), (4, 0.4), (6, 0.18), (8, 0.15), (12, 0.06)]
    def note(f, n, i, ci):
        t = tt(n)
        x = sum(a * np.sin(2 * np.pi * f * m * (1 + rng.normal(0, 0.0006)) * t + rng.random() * 6) for m, a in stops)
        chiff = bp(rng.standard_normal(n), f * 4, 3) * np.exp(-t / 0.05) * 0.5
        return (x + chiff) * 0.12
    pad = chord_layer(h, DUR, note, octave=1, att=0.08, rel=0.6, spread=0.7, gain=0.55)
    pedal = chord_layer(h, DUR, lambda f, n, i, ci: sum(a * np.sin(2 * np.pi * f * m * tt(n)) for m, a in ((0.5, 1), (1, .6), (2, .3))) * 0.2,
                        octave=0, att=0.1, rel=0.8, n=1, spread=0, gain=0.5)
    flute = star_layer(h, DUR, ev, lambda f, v, n, e: (sine(f, n) + 0.2 * sine(f * 2, n) + 0.05 * bp(rng.standard_normal(n), f, 4)) * adsr(n, 0.04, 1, 1, 0.3, 0.35),
                       length=1.0, gain=0.2)
    st = mixdown(chorus(pad, 1.5, 0.1, 10, 0.3), pedal, flute)
    return reverb(st, 9, 0.35, 1.8, 0.5, predelay=0.05)


# ------------------------------------------------------------------ 9 VOYAGER CHOIR: voices that change their vowel
VOWELS = {"a": [(800, 1), (1150, .5), (2900, .25)], "o": [(450, 1), (800, .4), (2830, .1)],
          "u": [(325, 1), (700, .25), (2700, .05)], "e": [(400, 1), (1600, .35), (2700, .2)]}


def voyager():
    h = Harmony(110, "aeolian")
    ev = star_events(DUR)
    order = ["a", "o", "u", "e"]
    def note(f, n, i, ci):
        t = tt(n)
        x = np.zeros(n)
        for v in range(3):    # an ensemble: three singers a note
            vib = 1 + 0.005 * np.sin(2 * np.pi * (5 + v * 0.4) * t + v) + rng.normal(0, 0.002)
            x += saw(f * vib, n, rng.random())
        a, b = VOWELS[order[ci % 4]], VOWELS[order[(ci + 1) % 4]]
        m = np.clip(t / (n / SR), 0, 1)
        y = np.zeros(n)
        for (fa, ga), (fb, gb) in zip(a, b):
            y += svf(x, fa + (fb - fa) * m, 8.0, 1) * (ga + (gb - ga) * m)
        return y * 0.25 + bp(rng.standard_normal(n), 2500, 1) * 0.01
    pad = chord_layer(h, DUR, note, att=1.6, rel=3, spread=0.8, gain=0.6)
    stars = star_layer(h, DUR, ev, lambda f, v, n, e: bell_fm(f, n, 4.0, 1.5, 1.2, 0.2) * 0.8 + sine(f * 0.5, n) * perc(n, 0.01, 0.8) * 0.3, gain=0.2)
    st = mixdown(chorus(pad, 3, 0.25), stars, cosmos_drone(h, DUR, 0.07))
    return reverb(st, 7, 0.35, 1.5, 0.45)


# ------------------------------------------------------------------ 10 KALIMBA DELLE STELLE
def kalimba():
    h = Harmony(130.81, "ionian", prog=(0, 5, 3, 4))
    ev = star_events(DUR)
    def tine(f, v, n, e):
        t = tt(n)
        x = (np.sin(2 * np.pi * f * t) * np.exp(-t / 1.6) + 0.35 * np.sin(2 * np.pi * f * 5.4 * t) * np.exp(-t / 0.15)
             + 0.15 * np.sin(2 * np.pi * f * 9.1 * t) * np.exp(-t / 0.06))
        x = x + resonator(x, 420, 0.995) * 0.6     # the wooden box
        return x * np.minimum(t / 0.002, 1)
    stars = star_layer(h, DUR, ev, tine, length=3, octave=1, gain=0.5)
    pad = chord_layer(h, DUR, lambda f, n, i, ci: (fm(f, 1, 0.8 * np.exp(-tt(n) / 3) + 0.2, n) + 0.3 * sine(f * 2, n)) * 0.2,
                      att=1.5, rel=3, spread=0.6, gain=0.35)
    st = mixdown(stars, pad, cosmos_drone(h, DUR, 0.06))
    return reverb(pingpong(st, 0.36, 0.3, 0.2), 3.5, 0.45, 1.0, 0.3)


# ------------------------------------------------------------------ 11 ARPA DI ORIONE: harp strings
def orion_harp():
    h = Harmony(110, "lydian", prog=(0, 4, 5, 3))
    ev = star_events(DUR)
    def string(f, v, n, e):
        x = karplus(n, SR / f, 0.9993, 0.45, int(e[3] * 7 + e[4]))
        x = x + 0.5 * karplus(n, SR / (f * 2.0), 0.999, 0.5, int(e[4] * 3))
        return lp(x, 5000) * np.minimum(tt(n) / 0.001, 1)
    stars = star_layer(h, DUR, ev, string, length=5, octave=1, gain=0.55)
    pad = chord_layer(h, DUR, lambda f, n, i, ci: lp(saw(f * 0.998, n) + saw(f * 1.002, n, 0.4), 1400) * 0.12,
                      att=2.5, rel=4, spread=0.9, gain=0.45)
    st = mixdown(stars, chorus(pad, 3, 0.2), cosmos_drone(h, DUR, 0.06))
    return reverb(st, 5.5, 0.35, 1.3, 0.38)


# ------------------------------------------------------------------ 12 TAPE MEMORY: a worn cassette of the sky
def tape_memory():
    h = Harmony(98, "dorian")
    ev = star_events(DUR)
    N = int(DUR * SR)
    t = tt(N + 12 * SR)
    wow = 2 ** ((18 * np.sin(2 * np.pi * 0.5 * t) + 6 * np.sin(2 * np.pi * 6.3 * t) * 0.3) / 1200)
    def note(f, n, i, ci):
        w = wow[:n]
        return lp(saw(f * w * 0.995, n) + saw(f * w * 1.006, n, .3) + 0.5 * tri(f * w * 0.5, n), 1300, 0.8) * 0.2
    pad = chord_layer(h, DUR, note, att=0.6, rel=2.5, spread=0.5, gain=0.5)
    def musicbox(f, v, n, e):
        w = wow[:n]
        tn = tt(n)
        return np.sin(2 * np.pi * np.cumsum(f * w) / SR + 1.2 * np.exp(-tn / 0.15) * np.sin(2 * np.pi * np.cumsum(f * 5 * w) / SR)) * np.exp(-tn / 1.2)
    stars = star_layer(h, DUR, ev, musicbox, length=3, octave=2, gain=0.22)
    st = mixdown(pad, stars, cosmos_drone(h, DUR, 0.06))
    st = np.tanh(st * 2.2) / 2.2                       # the tape's saturation
    hiss = np.stack([hp(rng.standard_normal(N), 3000), hp(rng.standard_normal(N), 3000)], 1) * 0.004
    st = np.stack([lp(st[:, 0], 6000), lp(st[:, 1], 6000)], 1) + hiss
    return reverb(st, 2.8, 0.5, 0.9, 0.25)


# ------------------------------------------------------------------ 13 EVENT HORIZON: everything played backwards
def event_horizon():
    h = Harmony(110, "phrygian")
    ev = star_events(DUR)
    N = int(DUR * SR)
    st = np.zeros((N + 6 * SR, 2))
    for t0, t1, d in h.chords(DUR):
        n = int(4 * SR)
        x = np.zeros((n, 2))
        for i, f in enumerate(h.chord_hz(d, 1)):
            x += pan(bell_fm(f, n, 1.0, 2.5, 1.5, 0.8) + saw(f, n) * 0.15 * np.exp(-tt(n) / 1.0), (i - 1.5) / 2)
        wet = reverb(np.vstack([x, np.zeros((3 * SR, 2))]), 4, 0.3, 1.2, 1.0)
        rev = wet[::-1] * np.linspace(0, 1, len(wet))[:, None] ** 2     # the tail first, rising into the chord
        place(st, rev * 0.5, t0 - len(rev) / SR + 1.5)
        hold = np.zeros((int((t1 - t0) * SR), 2))
        for i, f in enumerate(h.chord_hz(d, 1)):
            hold += pan(fm(f, 1, 0.5, len(hold)) * adsr(len(hold), 0.01, 3, 0.45, 2, t1 - t0 - 2), (i - 1.5) / 2) * 0.2
        place(st, hold, t0 + 1.5)
    # the stars: reversed bells, swelling to their strike
    def rbell(f, v, n, e):
        return bell_fm(f, n, 3.5, 2.0, 0.5, 0.2)[::-1] * np.linspace(0, 1, n) ** 2
    stars = star_layer(h, DUR, ev, rbell, length=1.2, gain=0.3, every=2)
    st = mixdown(st[:N], stars, cosmos_drone(h, DUR, 0.08))
    return reverb(st, 4, 0.35, 1.2, 0.3)


# ------------------------------------------------------------------ 14 FONDO COSMICO: a spectrum that breathes
def cosmic_background():
    h = Harmony(55, "lydian")
    ev = star_events(DUR)
    N = int(DUR * SR)
    t = tt(N)
    st = np.zeros((N, 2))
    chords = h.chords(DUR)
    for k in range(1, 48):           # the root's harmonics, louder where the chord has a note
        f = 55 * k
        if f > 9000:
            break
        amp = np.zeros(N)
        for t0, t1, d in chords:
            pcs = [semis(h.mode, d + 2 * i) % 12 for i in range(4)]
            pc = round(12 * math.log2(k)) % 12
            near = abs(12 * math.log2(k) - round(12 * math.log2(k))) < 0.2
            target = (1.0 if (pc in pcs and near) else 0.08) / k ** 0.7
            a, b = int(t0 * SR), min(N, int(t1 * SR))
            amp[a:b] = target
        amp = lp(amp, 0.25)          # each partial glides to its new weight
        drift = 0.6 + 0.4 * np.sin(2 * np.pi * (0.02 + 0.013 * k) * t + k)
        x = np.sin(2 * np.pi * f * (1 + 0.0003 * math.sin(k)) * t + k) * amp * drift
        st += pan(x, math.sin(k * 2.3) * 0.8) * 0.5
    def glint(f, v, n, e):
        return sine(f, n) * adsr(n, 0.3, 1, 1, 1.5, 0.3)
    stars = star_layer(h, DUR, ev, glint, length=2.5, gain=0.12)
    st = mixdown(st, stars)
    return reverb(st, 6, 0.25, 1.2, 0.4)


# ------------------------------------------------------------------ 15 STARGATE: a Berlin-school sequence from the stars
def stargate():
    h = Harmony(82.41, "aeolian", prog=(0, 5, 3, 6))
    ev = star_events(DUR)
    bpm = 112
    step = 60 / bpm / 4
    N = int(DUR * SR)
    t = tt(N)
    # the sequence: each sixteenth plays the star nearest in time, an octave down when it is low on the screen
    seq = np.zeros((N + SR, 2))
    times = np.array([e[0] for e in ev])
    cut_lfo = 0.5 + 0.5 * np.sin(2 * np.pi * t / 16)
    for k in range(int(DUR / step)):
        ts = k * step
        j = np.argmin(np.abs(times - ts))
        tt0, deg, vel, x, y, mag = ev[j]
        f = star_freq(h, deg % 7, 1)
        m = int(step * 1.6 * SR)
        tm = tt(m)
        env = np.exp(-tm / 0.12) * np.minimum(tm / 0.002, 1.0)
        osc = saw(f, m) + saw(f * 1.006, m, .4) * 0.7 + pulse(f * 0.5, m) * 0.5
        fc = 200 + (900 + 3500 * cut_lfo[min(int(ts * SR), N - 1)]) * np.exp(-tm / 0.08) * (0.5 + vel)
        place(seq, pan(np.tanh(svf(osc, fc, 4.0, 0) * 1.5) * env * (1.0 if k % 4 == 0 else 0.7), math.sin(k * 0.7) * 0.3) * 0.22, ts)
    pad = chord_layer(h, DUR, lambda f, n, i, ci: lp(saw(f * 0.997, n) + saw(f * 1.003, n, .5), 1500) * 0.15,
                      att=2, rel=3, spread=0.9, gain=0.4)
    st = mixdown(seq[:N], chorus(pad, 4, 0.2), cosmos_drone(h, DUR, 0.06))
    return reverb(pingpong(st, step * 3, 0.45, 0.3), 4, 0.4, 1.2, 0.3)


# ------------------------------------------------------------------ 16 GROTTA DI CRISTALLO: glass in a cave
def crystal_cave():
    h = Harmony(110, "lydian", prog=(0, 2, 5, 4))
    ev = star_events(DUR)
    def note(f, n, i, ci):
        t = tt(n)
        return (fm(f, 1.41, 0.9 + 0.5 * np.sin(2 * np.pi * 0.11 * t + i), n) + 0.4 * fm(f * 2, 3.5, 0.4, n)) * 0.18
    pad = chord_layer(h, DUR, note, att=2.5, rel=4, spread=0.9, gain=0.45)
    def glass(f, v, n, e):
        t = tt(n)
        a = np.minimum(t / 0.0015, 1.0)
        return (np.sin(2 * np.pi * f * t + 1.4 * np.exp(-t / 0.08) * np.sin(2 * np.pi * f * 3.53 * t)) * np.exp(-t / 1.4)
                + 0.3 * np.sin(2 * np.pi * f * 2.76 * t) * np.exp(-t / 0.5)) * a
    stars = star_layer(h, DUR, ev, glass, length=3, gain=0.2)
    drips = pingpong(stars, 0.53, 0.6, 0.6, 2500)
    st = mixdown(pad, drips, cosmos_drone(h, DUR, 0.07))
    return reverb(st, 6.5, 0.3, 1.6, 0.45, predelay=0.04)


# ------------------------------------------------------------------ 17 SUPERNOVA: brass that swells like a star dying
def supernova():
    h = Harmony(87.31, "aeolian", prog=(0, 5, 2, 6))
    ev = star_events(DUR)
    def note(f, n, i, ci):
        t = tt(n)
        bend = 2 ** (-0.6 * np.exp(-t / 0.25) / 12)
        x = saw(f * bend * 0.996, n) + saw(f * bend * 1.004, n, .5)
        fc = 300 + 3200 * np.clip(t / 3.5, 0, 1) ** 1.5 * (0.85 + 0.15 * np.sin(2 * np.pi * 5.5 * t))
        return np.tanh(svf(x, fc, 1.4, 0) * 1.3) * 0.22
    pad = chord_layer(h, DUR, note, att=1.2, rel=3.5, spread=0.8, gain=0.55)
    def cs_bell(f, v, n, e):
        t = tt(n)
        return lp(pulse(f, n, 0.3) + sine(f * 2, n), 3000 * np.exp(-t / 0.3) + 400) * np.exp(-t / 1.1) * 0.6
    stars = star_layer(h, DUR, ev, cs_bell, length=3, gain=0.2, every=2)
    st = mixdown(pad, pingpong(stars, 0.45, 0.45, 0.35), cosmos_drone(h, DUR, 0.07))
    return reverb(st, 7.5, 0.35, 1.6, 0.42)


# ------------------------------------------------------------------ 18 CAMPANE DELLA LUNA: wind chimes
def moon_chimes():
    h = Harmony(110, "ionian", prog=(0, 3, 0, 4))
    ev = star_events(DUR)
    modes = [(1, 1, 3.5), (2.76, .5, 2.0), (5.40, .25, 1.0), (8.93, .12, .5)]
    def chime(f, v, n, e):
        t = tt(n)
        return sum(a * np.sin(2 * np.pi * f * m * t) * np.exp(-t / tau) for m, a, tau in modes) * np.minimum(t / 0.001, 1)
    N = int(DUR * SR)
    st = np.zeros((N + 6 * SR, 2))
    for t, deg, vel, x, y, mag in ev:     # each star a chime, and in a gust its neighbours swing too
        f = star_freq(h, deg, 2)
        for k in range(1 + int(rng.random() < 0.35) * 2):
            ff = star_freq(h, deg + k * 2, 2)
            place(st, pan(chime(ff, vel, int(6 * SR), None), (x / 120 - 1) * 0.9) * 0.12 * vel * (1 if k == 0 else 0.5), t + k * rng.uniform(0.05, 0.2))
    st = mixdown(st[:N], cosmos_drone(h, DUR, 0.12, index=0.3))
    return reverb(st, 5, 0.3, 1.3, 0.35)


# ------------------------------------------------------------------ 19 ARCHI GALATTICI: a string machine with a phaser
def galactic_strings():
    h = Harmony(110, "dorian", prog=(0, 3, 5, 4))
    ev = star_events(DUR)
    def note(f, n, i, ci):
        x = saw(f, n) + saw(f * 2, n, .2) * 0.6 + pulse(f * 0.5, n, 0.5) * 0.25
        return lp(x, 2600, 0.7) * 0.15
    pad = chord_layer(h, DUR, note, att=0.8, rel=2.5, spread=0.7, gain=0.5)
    # the ensemble: three chorus lines a third of a turn apart
    ens = pad.copy()
    n = len(pad)
    i = np.arange(n)
    for k in range(3):
        d = (8 + 3 * np.sin(2 * np.pi * 0.6 * i / SR + k * 2.094) + 0.5 * np.sin(2 * np.pi * 6 * i / SR + k)) * SR / 1000
        p = np.clip(i - d, 0, n - 1).astype(int)
        ens += pad[p] * 0.5
    # a phaser: four all-pass stages, their pole sweeping
    from scipy.signal import lfilter
    ph = ens.copy()
    sweep = 0.5 + 0.4 * np.sin(2 * np.pi * 0.08 * i / SR)
    for ch in range(2):
        y = ens[:, ch].copy()
        B = 1024
        for s in range(4):
            out = np.empty(n)
            zi = np.zeros(1)
            for a0 in range(0, n, B):
                a = -sweep[a0]
                seg, zi = lfilter([a, 1], [1, a], y[a0:a0 + B], zi=zi)
                out[a0:a0 + B] = seg
            y = out
        ph[:, ch] = (ens[:, ch] + y) * 0.5
    def synth(f, v, n, e):
        return lp(pulse(f, n, 0.25), 2200 * np.exp(-tt(n) / 0.25) + 500, 1.5) * perc(n, 0.005, 0.5)
    stars = star_layer(h, DUR, ev, synth, length=2, gain=0.2)
    st = mixdown(ph, pingpong(stars, 0.375, 0.5, 0.4), cosmos_drone(h, DUR, 0.06))
    return reverb(st, 4.5, 0.4, 1.2, 0.33)


# ------------------------------------------------------------------ 20 CAMPO PROFONDO: folded sines, a gate that pings
def deep_field():
    h = Harmony(110, "mixolydian", prog=(0, 6, 3, 4))
    ev = star_events(DUR)
    def fold(x, k):
        return np.sin(x * k)
    def note(f, n, i, ci):
        t = tt(n)
        depth = 1.2 + 1.8 * (0.5 - 0.5 * np.cos(2 * np.pi * t / 9 + i))
        return fold(sine(f, n) + 0.3 * sine(f * 1.5, n), depth) * 0.18
    pad = chord_layer(h, DUR, note, att=1.5, rel=3, spread=0.8, gain=0.5)
    def lpg(f, v, n, e):          # a low-pass gate struck: the "bongo"
        t = tt(n)
        env = np.exp(-t / 0.35)
        x = fold(sine(f, n) + 0.5 * sine(f * 2.01, n), 1 + 3 * env)
        return lp(x, 200 + 6000 * env ** 2) * env
    stars = star_layer(h, DUR, ev, lpg, length=2, gain=0.35)
    st = mixdown(pad, pingpong(stars, 0.28, 0.35, 0.3), cosmos_drone(h, DUR, 0.07))
    return reverb(st, 4.5, 0.4, 1.1, 0.33)


ENGINES = [
    ("01-nebula", nebula), ("02-pulsar", pulsar), ("03-aurora", aurora), ("04-anelli-di-saturno", saturn_rings),
    ("05-vento-solare", solar_wind), ("06-gravita", gravity), ("07-quasar", quasar), ("08-organo-cosmico", organo),
    ("09-voyager", voyager), ("10-kalimba-delle-stelle", kalimba), ("11-arpa-di-orione", orion_harp),
    ("12-memoria-su-nastro", tape_memory), ("13-orizzonte-degli-eventi", event_horizon),
    ("14-fondo-cosmico", cosmic_background), ("15-stargate", stargate), ("16-grotta-di-cristallo", crystal_cave),
    ("17-supernova", supernova), ("18-campane-della-luna", moon_chimes), ("19-archi-galattici", galactic_strings),
    ("20-campo-profondo", deep_field)]

if __name__ == "__main__":
    import sys, time, subprocess
    out = sys.argv[1]
    only = sys.argv[2:]
    for name, fn in ENGINES:
        if only and not any(o in name for o in only):
            continue
        t0 = time.time()
        st = master(fn(), DUR)
        wav = f"{out}/{name}.wav"
        write(wav, st)
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", wav, "-b:a", "160k", wav[:-4] + ".mp3"], check=True)
        rms = float(np.sqrt((st ** 2).mean()))
        print(f"{name}: {time.time() - t0:.1f} s, rms {rms:.3f}", flush=True)
