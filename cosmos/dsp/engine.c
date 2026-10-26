/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's engine (engine.h), in single-precision floats (the FM-1's FPU) at 44.1 kHz.
 *
 * Each drone is a small instrument of its own:
 *   main oscillator ─┐ (the moon's four characters, blended)
 *   partner, an octave up, a few cents off: it beats ─┼─ drive ─ its own resonant filter ─ its breath ─┐
 *   sub, an octave down ─┤                                                                             │
 *   pink noise ──────────┘                                                                             │
 *                         drone A a little left, drone B a little right, ring-modulated in a storm ◄───┘
 *   ─┬─ dry ────────────────────────────────┬─ warmth (a soft drive over the sound and its echoes) ─ out
 *    ├─ delay: 0.4 s ping-pong, tape wow ───┤
 *    └─ reverb: diffused, four long lines ──┘
 * Nothing is ever still: each drone's pitch, cutoff, level and partner wander on their own (Drift, more with Kp),
 * and each breathes at its own slow rate. Every parameter glides to the sky's target once a block (5.8 ms) at the
 * pace of its source: the moon's character over seconds, the interval over four, the filters over one. The delay
 * and the reverb live in int16 lines on the WiFi heap. */
#include <stdint.h>
#include "engine.h"
#include "fastmath.h"
#include "waves.h"

extern void *malloc(unsigned long n);

#define SR 44100.0f
#define DLY_N 17640u                     /* 0.4 s */
#define WOW 24.0f                        /* the tape's wow: the delay's read wanders this many samples */
static const uint32_t AP_LEN[4] = {142u, 107u, 379u, 277u};      /* the reverb's input diffusion */
static const uint32_t RV_LEN[4] = {2999u, 3571u, 4111u, 4651u};  /* its lines: 68 to 105 ms, prime */
#define AP_N (142u + 107u + 379u + 277u)
#define RV_N (2999u + 3571u + 4111u + 4651u)

/* ---------------------------------------------------------------- the mappings --- */
static const struct { int8_t semi; char name[3]; } INTERVALS[] = {   /* from consonant to tense */
    {12, "P8"}, {7, "P5"}, {9, "M6"}, {4, "M3"}, {5, "P4"}, {3, "m3"}, {8, "m6"}, {2, "M2"}, {10, "m7"}, {6, "TT"},
    {1, "m2"}};
#define NINT (sizeof INTERVALS / sizeof INTERVALS[0])

const char *moon_name(float moon)
{
    static const char *const N[4] = {"NEW", "WAXING", "FULL", "WANING"};
    float x = moon - fm_floorf(moon);
    return N[(int32_t)(x * 4.0f + 0.5f) & 3];
}

void sky_map(const sky_t *s, sky_map_t *m)
{
    float t = (1013.0f - s->press) / 22.0f - 0.6f * (float)s->trend;   /* tension: -1.4 high and rising .. 2.6 */
    float c = fm_clampf(s->clouds / 100.0f, 0.0f, 1.0f), x = s->moon - fm_floorf(s->moon);
    int32_t k = (int32_t)(fm_clampf((t + 1.4f) / 4.0f, 0.0f, 1.0f) * (float)(NINT - 1) + 0.5f);
    float h = s->sun;
    m->base_hz = 440.0f * fm_exp2f((float)(s->note - 69) / 12.0f);
    m->interval = INTERVALS[k].semi;
    m->interval_name = INTERVALS[k].name;
    m->beat = 2.5f + fm_maxf(0.0f, t) * 6.0f;   /* the partners' detune, cents: a slow beat, faster in tension */
    /* the sun: 160 Hz at night, 900 at the horizon, 7 kHz at 65 degrees */
    if (h < -18.0f)
        m->cutoff = 160.0f;
    else if (h < 0.0f)
        m->cutoff = 160.0f * fm_powf(900.0f / 160.0f, (h + 18.0f) / 18.0f);
    else
        m->cutoff = 900.0f * fm_powf(7000.0f / 900.0f, fm_clampf(h / 65.0f, 0.0f, 1.0f));
    /* the moon: its lit fraction, its character around the cycle; full, the filters sing */
    m->lit = (1.0f - fm_cosf(FM_TWO_PI * x)) * 0.5f;
    m->morph = x * 4.0f;
    m->sub = 0.05f + 0.45f * m->lit;
    m->q = 0.8f + 11.0f * m->lit * m->lit;
    /* the clouds: the space */
    m->verb = 0.10f + 0.70f * c;
    m->delay = 0.05f + 0.30f * c;
    m->feedback = 0.25f + 0.40f * c;
    m->noise = 0.006f + 0.06f * c;
    m->dry = 1.0f - 0.35f * c;
    /* Kp: the drift, and the storm from 5 */
    m->drift = fm_clampf(0.12f + s->kp / 9.0f, 0.0f, 1.0f);
    m->storm = fm_clampf((s->kp - 4.5f) / 4.0f, 0.0f, 1.0f);
}

/* ------------------------------------------------------------------ the state --- */
static volatile sky_map_t target;        /* the main loop writes, the render reads (word-sized fields) */
static volatile int32_t target_on;
static int16_t *dly, *ap_line[4], *rv_line[4];
static uint32_t dly_i, ap_i[4], rv_i[4];

typedef struct {                         /* a drone */
    uint32_t ph, php, phs;               /* main, partner, sub */
    float s1, s2;                        /* its filter (Simper's state-variable filter) */
    float dp, dc, dl, dd;                /* its wanders: pitch, cutoff, level, the partner's detune (-1..1) */
    float breath, breath_rate;           /* its slow swell: phase 0..1, Hz */
    float a1, a2, a3, k;                 /* the filter's coefficients for the block */
    float inc, incp, incs, lvl;
} drone_t;

static struct {                          /* where the render is: each glides to the target */
    float base, ratio, beat, cutoff, q, sub, morph, verb, delay, feedback, noise, dry, drift, storm, level, decay;
    drone_t d[2];
    uint32_t phR, phW;                   /* the ring's oscillator; the tape's wow */
    float p0, p1, p2;                    /* pink noise (Paul Kellet's economy filter) */
    float dlpL, dlpR, rvlp[4];
    uint32_t rnd;
    int started;
} e = {.rnd = 0x9E3779B9u, .d = {{.breath_rate = 0.043f}, {.breath = 0.37f, .breath_rate = 0.031f}}};

uint32_t eng_bytes(void) { return (2u * DLY_N + AP_N + RV_N) * 2u; }

int eng_init(void)
{
    uint32_t i;
    int16_t *p;
    dly = malloc(2u * DLY_N * 2u);
    p = malloc((AP_N + RV_N) * 2u);
    if (!dly || !p)
        return -1;
    for (i = 0; i < 2u * DLY_N; i++)
        dly[i] = 0;
    for (i = 0; i < AP_N + RV_N; i++)
        p[i] = 0;
    for (i = 0; i < 4u; i++) {
        ap_line[i] = p;
        p += AP_LEN[i];
    }
    for (i = 0; i < 4u; i++) {
        rv_line[i] = p;
        p += RV_LEN[i];
    }
    return 0;
}

void eng_sky(const sky_t *s)
{
    sky_map_t m;
    sky_map(s, &m);
    target.base_hz = m.base_hz;
    target.interval = m.interval;
    target.beat = m.beat;
    target.cutoff = m.cutoff;
    target.q = m.q;
    target.sub = m.sub;
    target.morph = m.morph;
    target.verb = m.verb;
    target.delay = m.delay;
    target.feedback = m.feedback;
    target.noise = m.noise;
    target.dry = m.dry;
    target.drift = m.drift;
    target.storm = m.storm;
    target_on = s->on;
}

static inline uint32_t xs(void)
{
    e.rnd ^= e.rnd << 13;
    e.rnd ^= e.rnd >> 17;
    e.rnd ^= e.rnd << 5;
    return e.rnd;
}
static inline float rnd_pm(void) { return (float)(xs() & 0xFFFFu) * (1.0f / 32768.0f) - 1.0f; }   /* -1..1 */

/* a wavetable at a 32-bit phase, linearly interpolated, -1..1 */
static inline float wt(const int16_t *t, uint32_t ph)
{
    uint32_t i = ph >> (32u - WAVE_BITS), j = (i + 1u) & (WAVE_N - 1u);
    float f = (float)((ph >> (32u - WAVE_BITS - 12u)) & 4095u) * (1.0f / 4096.0f);
    return ((float)t[i] + ((float)t[j] - (float)t[i]) * f) * (1.0f / 32768.0f);
}
/* a soft saturation: tanh's shape, cheaper (a Padé approximant), clamped where it turns */
static inline float sat(float x)
{
    x = fm_clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
static inline float glide(float now, float to, float k) { return now + (to - now) * k; }

/* a drone's block: its wanders, its pitch, its filter */
static void drone_block(drone_t *d, float hz, float cutoff, uint32_t frames)
{
    const float to32 = 4294967296.0f / SR;
    float sp = 0.02f + 0.12f * e.drift, depth = e.drift;
    d->dp = fm_clampf(d->dp + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dc = fm_clampf(d->dc + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dl = fm_clampf(d->dl + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dd = fm_clampf(d->dd + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    hz *= fm_exp2f(d->dp * (3.0f + 30.0f * depth * depth) / 1200.0f);
    d->inc = hz * to32;
    d->incp = hz * 2.0f * fm_exp2f((e.beat + d->dd * 4.0f * depth) / 1200.0f) * to32;
    d->incs = hz * 0.5f * to32;
    d->lvl = 1.0f + d->dl * 0.18f * depth;
    d->breath += d->breath_rate * (float)frames / SR;
    if (d->breath >= 1.0f)
        d->breath -= 1.0f;
    cutoff *= fm_exp2f(d->dc * 0.9f * depth);   /* up to most of an octave either way */
    {
        float g = fm_tanf(FM_PI * fm_clampf(cutoff, 40.0f, 16000.0f) / SR);
        d->k = 1.0f / e.q;
        d->a1 = 1.0f / (1.0f + g * (g + d->k));
        d->a2 = g * d->a1;
        d->a3 = g * d->a2;
    }
}

/* a drone's sample: its oscillators and noise, driven into its filter, breathing */
static inline float drone_tick(drone_t *d, int32_t m0, int32_t m1, float w1, uint32_t pm, float nz, float drv,
                               float breath)
{
    float w0 = 1.0f - w1, x, v1, v2, v3;
    uint32_t ph, pp;
    d->ph += (uint32_t)d->inc;
    d->php += (uint32_t)d->incp;
    d->phs += (uint32_t)d->incs;
    ph = d->ph + pm;
    pp = d->php + pm;
    x = (wt(WAVES[m0], ph) * w0 + wt(WAVES[m1], ph) * w1) * 0.62f;
    x += (wt(WAVES[m0], pp) * w0 + wt(WAVES[m1], pp) * w1) * 0.30f;
    x += wt(SINE, d->phs) * e.sub + nz;
    x = sat(x * drv);
    v3 = x - d->s2;
    v1 = d->a1 * d->s1 + d->a2 * v3;
    v2 = d->s2 + d->a2 * d->s1 + d->a3 * v3;
    d->s1 = 2.0f * v1 - d->s1;
    d->s2 = 2.0f * v2 - d->s2;
    return sat(v2) * d->lvl * breath;
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i;
    float incR, incW, fmd, ring, dry_amp, lvl_to, lvl_k, drv, brA, brB, decay;
    int32_t m0, m1;
    float mw;
    if (!dly) {                          /* no lines (no heap): silence */
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    if (!e.started) {                    /* the first block: start where the sky is */
        e.base = target.base_hz;
        e.ratio = fm_exp2f((float)target.interval / 12.0f);
        e.beat = target.beat;
        e.cutoff = target.cutoff;
        e.q = target.q;
        e.morph = target.morph;
        e.sub = target.sub;
        e.verb = target.verb;
        e.delay = target.delay;
        e.feedback = target.feedback;
        e.noise = target.noise;
        e.dry = target.dry;
        e.drift = target.drift;
        e.started = 1;
    }
    /* a block's glides (5.8 ms): pitch ~2 s, the interval ~4 s, the filters ~1 s, the moon ~6 s, the space ~2 s */
    e.base = glide(e.base, target.base_hz, 0.003f);
    e.ratio = glide(e.ratio, fm_exp2f((float)target.interval / 12.0f), 0.0015f);
    e.beat = glide(e.beat, target.beat, 0.003f);
    e.cutoff = glide(e.cutoff, target.cutoff, 0.006f);
    e.q = glide(e.q, target.q, 0.002f);
    {   /* the moon's character goes round its cycle: the short way */
        float d = target.morph - e.morph;
        if (d > 2.0f)
            d -= 4.0f;
        else if (d < -2.0f)
            d += 4.0f;
        e.morph += d * 0.001f;
        if (e.morph < 0.0f)
            e.morph += 4.0f;
        else if (e.morph >= 4.0f)
            e.morph -= 4.0f;
    }
    e.sub = glide(e.sub, target.sub, 0.002f);
    e.verb = glide(e.verb, target.verb, 0.003f);
    e.delay = glide(e.delay, target.delay, 0.003f);
    e.feedback = glide(e.feedback, target.feedback, 0.003f);
    e.noise = glide(e.noise, target.noise, 0.003f);
    e.dry = glide(e.dry, target.dry, 0.003f);
    e.drift = glide(e.drift, target.drift, 0.005f);
    e.storm = glide(e.storm, target.storm, 0.004f);
    drone_block(&e.d[0], e.base, e.cutoff, n);
    drone_block(&e.d[1], e.base * e.ratio, e.cutoff * 1.12f, n);
    incR = (e.base * 2.71f + 3.0f) * (4294967296.0f / SR);
    incW = 0.37f * (4294967296.0f / SR);
    /* the moon's character: two neighbouring tables and how far between */
    m0 = (int32_t)e.morph & 3;
    m1 = (m0 + 1) & 3;
    mw = e.morph - fm_floorf(e.morph);
    mw = mw * mw * (3.0f - 2.0f * mw);
    fmd = e.storm * e.storm * 0.35f;     /* B's phase pushed by A: up to a third of a cycle */
    ring = 0.9f * e.storm;
    dry_amp = 1.0f - 0.6f * e.storm;
    drv = 1.1f + 0.5f * (e.sub - 0.05f) / 0.45f + 1.6f * e.storm;   /* heavier at full moon, raw in a storm */
    decay = 0.80f + 0.13f * (e.verb - 0.10f) / 0.70f;               /* the clouds lengthen the reverb */
    /* each drone breathes: up to a quarter quieter at the bottom of its swell */
    brA = 1.0f - 0.22f * (0.5f - 0.5f * fm_cosf(FM_TWO_PI * e.d[0].breath));
    brB = 1.0f - 0.22f * (0.5f - 0.5f * fm_cosf(FM_TWO_PI * e.d[1].breath));
    lvl_to = target_on ? 1.0f : 0.0f;
    lvl_k = target_on ? 0.00004f : 0.00008f;  /* ~0.6 s in, ~0.3 s out */
    gain *= 8300000.0f * 0.55f;
    for (i = 0; i < n; i++) {
        float white, nzA, nzB, a, b, l, r, ro, dL, dR, rin, o[4], y[4], outL, outR, pos, fr;
        uint32_t j, pm, ri, rj;
        e.level += (lvl_to - e.level) * lvl_k;
        /* pink noise, a little different in each drone */
        white = (float)(int32_t)xs() * (1.0f / 2147483648.0f);
        e.p0 = 0.99765f * e.p0 + white * 0.0990460f;
        e.p1 = 0.96300f * e.p1 + white * 0.2965164f;
        e.p2 = 0.57000f * e.p2 + white * 1.0526913f;
        nzA = (e.p0 + e.p1 + e.p2 + white * 0.1848f) * 0.18f * e.noise;
        nzB = (e.p0 - e.p1 + e.p2) * 0.18f * e.noise;
        a = drone_tick(&e.d[0], m0, m1, mw, 0u, nzA, drv, brA);
        pm = (uint32_t)(int32_t)(a * fmd * 4294967296.0f);   /* the storm: A bends B */
        b = drone_tick(&e.d[1], m0, m1, mw, pm, nzB, drv, brB);
        /* A a little left, B a little right */
        l = (a * 0.80f + b * 0.40f) * e.level;
        r = (a * 0.40f + b * 0.80f) * e.level;
        e.phR += (uint32_t)incR;
        ro = wt(SINE, e.phR);
        l = l * dry_amp + l * ro * ring;
        r = r * dry_amp + r * ro * ring;
        /* the delay: 0.4 s, crossed (ping-pong), its read wandering like tape, its loop damped */
        e.phW += (uint32_t)incW;
        pos = 2.0f + WOW * (1.0f + wt(SINE, e.phW));   /* from the oldest sample: the delay is DLY_N - pos */
        ri = (dly_i + (uint32_t)pos) % DLY_N;
        rj = (ri + 1u) % DLY_N;
        fr = pos - fm_floorf(pos);
        dL = ((float)dly[2u * ri] + ((float)dly[2u * rj] - (float)dly[2u * ri]) * fr) * (1.0f / 32768.0f);
        dR = ((float)dly[2u * ri + 1u] + ((float)dly[2u * rj + 1u] - (float)dly[2u * ri + 1u]) * fr) * (1.0f / 32768.0f);
        e.dlpL += (dL - e.dlpL) * 0.3f;
        e.dlpR += (dR - e.dlpR) * 0.3f;
        dly[2u * dly_i] = (int16_t)(fm_clampf(l + e.dlpR * e.feedback, -1.0f, 1.0f) * 32767.0f);
        dly[2u * dly_i + 1u] = (int16_t)(fm_clampf(r + e.dlpL * e.feedback, -1.0f, 1.0f) * 32767.0f);
        if (++dly_i == DLY_N)
            dly_i = 0;
        /* warmth: the sound and its echoes driven together, softly */
        outL = sat((l * e.dry + dL * e.delay) * 1.15f);
        outR = sat((r * e.dry + dR * e.delay) * 1.15f);
        /* the reverb: four all-passes smear the input, then four long lines mixed by a Hadamard matrix */
        rin = (outL + outR) * 0.3f;
        for (j = 0; j < 4u; j++) {
            float z = (float)ap_line[j][ap_i[j]] * (1.0f / 32768.0f), v = rin + 0.6f * z;
            ap_line[j][ap_i[j]] = (int16_t)(fm_clampf(v, -1.0f, 1.0f) * 32767.0f);
            rin = z - 0.6f * v;
            if (++ap_i[j] == AP_LEN[j])
                ap_i[j] = 0;
        }
        for (j = 0; j < 4u; j++) {
            o[j] = (float)rv_line[j][rv_i[j]] * (1.0f / 32768.0f);
            e.rvlp[j] += (o[j] - e.rvlp[j]) * 0.38f;
        }
        y[0] = 0.5f * (e.rvlp[0] + e.rvlp[1] + e.rvlp[2] + e.rvlp[3]);
        y[1] = 0.5f * (e.rvlp[0] - e.rvlp[1] + e.rvlp[2] - e.rvlp[3]);
        y[2] = 0.5f * (e.rvlp[0] + e.rvlp[1] - e.rvlp[2] - e.rvlp[3]);
        y[3] = 0.5f * (e.rvlp[0] - e.rvlp[1] - e.rvlp[2] + e.rvlp[3]);
        for (j = 0; j < 4u; j++) {
            rv_line[j][rv_i[j]] = (int16_t)(fm_clampf(rin + y[j] * decay, -1.0f, 1.0f) * 32767.0f);
            if (++rv_i[j] == RV_LEN[j])
                rv_i[j] = 0;
        }
        outL += (o[0] + o[2]) * e.verb;
        outR += (o[1] + o[3]) * e.verb;
        outL = outL / (1.0f + fm_fabsf(outL) * 0.4f);
        outR = outR / (1.0f + fm_fabsf(outR) * 0.4f);
        lr[2u * i] = (int32_t)(outL * gain);
        lr[2u * i + 1u] = (int32_t)(outR * gain);
    }
}
