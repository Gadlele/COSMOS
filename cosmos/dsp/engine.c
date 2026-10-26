/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's engine (engine.h), in single-precision floats (the FM-1's FPU) at 44.1 kHz.
 *
 *   drone A ─┐ (wavetables: the moon's four characters, blended)    ┌─ ring (Kp storm) ─┐
 *   drone B ─┤  B phase-modulated by A in a storm                    │                   │
 *   sub    ──┼── left / right (A left, B right) ─────────────────────┴─── dry ───────────┴─ filter (sun) ─┬─ out
 *   noise  ──┘                                                                                          ├─ delay ─┤
 *                                                                                                       └─ reverb ┘
 * The delay is 0.48 s stereo, the reverb four lines mixed by a Hadamard matrix (a feedback delay network), both
 * in int16 lines on the WiFi heap. Every parameter glides to the sky's target once a block (5.8 ms), each at the
 * pace of its source: the moon's character and the pressure's interval over seconds, the filter over one. */
#include <stdint.h>
#include "engine.h"
#include "fastmath.h"
#include "waves.h"

extern void *malloc(unsigned long n);

#define SR 44100.0f
#define DLY_N 21168u                     /* 0.48 s */
#define RV_A 1499u                       /* the reverb's four lines, prime lengths: 34 to 63 ms */
#define RV_B 1867u
#define RV_C 2269u
#define RV_D 2791u
#define RV_N (RV_A + RV_B + RV_C + RV_D)

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
    float h = s->sun, cos2;
    m->base_hz = 440.0f * fm_exp2f((float)(s->note - 69) / 12.0f);
    m->interval = INTERVALS[k].semi;
    m->interval_name = INTERVALS[k].name;
    m->beat = fm_maxf(0.0f, t) * 7.0f;
    /* the sun: 160 Hz at night, 900 at the horizon, 7 kHz at 65 degrees */
    if (h < -18.0f)
        m->cutoff = 160.0f;
    else if (h < 0.0f)
        m->cutoff = 160.0f * fm_powf(900.0f / 160.0f, (h + 18.0f) / 18.0f);
    else
        m->cutoff = 900.0f * fm_powf(7000.0f / 900.0f, fm_clampf(h / 65.0f, 0.0f, 1.0f));
    /* the moon: its lit fraction, its character around the cycle */
    cos2 = fm_cosf(FM_TWO_PI * x);
    m->lit = (1.0f - cos2) * 0.5f;
    m->morph = x * 4.0f;
    m->sub = 0.05f + 0.45f * m->lit;
    m->q = 0.7f + 9.0f * m->lit * m->lit;
    /* the clouds: the space */
    m->verb = 0.08f + 0.75f * c;
    m->delay = 0.04f + 0.32f * c;
    m->feedback = 0.2f + 0.45f * c;
    m->noise = 0.004f + 0.07f * c;
    m->dry = 1.0f - 0.35f * c;
    /* Kp: the drift, and the storm from 5 */
    m->drift = fm_clampf(s->kp / 9.0f, 0.0f, 1.0f);
    m->storm = fm_clampf((s->kp - 4.5f) / 4.0f, 0.0f, 1.0f);
}

/* ------------------------------------------------------------------ the state --- */
static volatile sky_map_t target;        /* the main loop writes, the render reads (word-sized fields) */
static volatile int32_t target_on;
static int16_t *dly, *rv;                /* the delay (L, R interleaved) and the reverb's four lines */
static uint32_t dly_i, rv_i[4];
static const uint32_t RV_LEN[4] = {RV_A, RV_B, RV_C, RV_D};
static int16_t *rv_line[4];

static struct {                          /* where the render is: each glides to the target */
    float base, ratio, beat, cutoff, q, sub, morph, verb, delay, feedback, noise, dry, drift, storm, level;
    float dA, dB;                        /* the drift's random walks, -1..1 */
    uint32_t phA, phB, phS, phR;         /* the oscillators' phases, 32-bit */
    float s1L, s2L, s1R, s2R;            /* the filter's state (Simper's SVF), left and right */
    float nlp, nhp;                      /* the noise's band */
    float dlpL, dlpR;                    /* the delay loop's damping */
    float rvlp[4];                       /* the reverb lines' damping */
    uint32_t rnd;
    int started;
} e = {.rnd = 0x9E3779B9u};

uint32_t eng_bytes(void) { return (2u * DLY_N + RV_N) * 2u; }

int eng_init(void)
{
    uint32_t i;
    dly = malloc(2u * DLY_N * 2u);
    rv = malloc(RV_N * 2u);
    if (!dly || !rv)
        return -1;
    for (i = 0; i < 2u * DLY_N; i++)
        dly[i] = 0;
    for (i = 0; i < RV_N; i++)
        rv[i] = 0;
    rv_line[0] = rv;
    for (i = 1; i < 4u; i++)
        rv_line[i] = rv_line[i - 1] + RV_LEN[i - 1];
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

/* a wavetable at a 32-bit phase, linearly interpolated, -1..1 */
static inline float wt(const int16_t *t, uint32_t ph)
{
    uint32_t i = ph >> (32u - WAVE_BITS), j = (i + 1u) & (WAVE_N - 1u);
    float f = (float)((ph >> (32u - WAVE_BITS - 12u)) & 4095u) * (1.0f / 4096.0f);
    return ((float)t[i] + ((float)t[j] - (float)t[i]) * f) * (1.0f / 32768.0f);
}

static inline float glide(float now, float to, float k) { return now + (to - now) * k; }

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i;
    float incA, incB, incS, incR, g, k, a1, a2, a3, wA, wB, fmd, ring, dry_amp, lvl_to, lvl_k;
    int32_t m0, m1;
    float mw, wdA, wdB;
    if (!dly) {                          /* no lines (no heap): silence */
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    if (!e.started) {                    /* the first block: start where the sky is */
        e.base = target.base_hz;
        e.ratio = fm_exp2f((float)target.interval / 12.0f);
        e.cutoff = target.cutoff;
        e.q = target.q;
        e.morph = target.morph;
        e.sub = target.sub;
        e.verb = target.verb;
        e.delay = target.delay;
        e.feedback = target.feedback;
        e.noise = target.noise;
        e.dry = target.dry;
        e.started = 1;
    }
    /* a block's glides (5.8 ms): pitch ~2 s, the interval ~4 s, the filter ~1 s, the moon ~6 s, the space ~2 s */
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
    /* the drift: each drone's pitch wanders, faster and further with Kp */
    {
        float sp = (0.08f + 0.6f * e.drift) * 0.25f, depth = 2.0f + 38.0f * e.drift * fm_sqrtf(e.drift);
        e.dA = fm_clampf(e.dA + ((float)(xs() & 0xFFFFu) / 65536.0f - 0.5f) * sp, -1.0f, 1.0f) * 0.9995f;
        e.dB = fm_clampf(e.dB + ((float)(xs() & 0xFFFFu) / 65536.0f - 0.5f) * sp, -1.0f, 1.0f) * 0.9995f;
        incA = e.base * fm_exp2f(e.dA * depth / 1200.0f);
        incB = e.base * e.ratio * fm_exp2f((e.dB * depth + e.beat) / 1200.0f);
    }
    incS = e.base * 0.5f;
    incR = e.base * 2.71f + 3.0f;
    {
        const float to32 = 4294967296.0f / SR;
        incA *= to32;
        incB *= to32;
        incS *= to32;
        incR *= to32;
    }
    /* the filter's coefficients for the block (Simper's state-variable filter, low-pass) */
    g = fm_tanf(FM_PI * fm_clampf(e.cutoff, 40.0f, 16000.0f) / SR);
    k = 1.0f / e.q;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    /* the moon's character: two neighbouring tables and how far between */
    m0 = (int32_t)e.morph & 3;
    m1 = (m0 + 1) & 3;
    mw = e.morph - fm_floorf(e.morph);
    mw = mw * mw * (3.0f - 2.0f * mw);
    fmd = e.storm * e.storm * 0.35f;     /* B's phase pushed by A: up to a third of a cycle */
    ring = 0.9f * e.storm;
    dry_amp = 1.0f - 0.6f * e.storm;
    wA = 0.30f;
    wB = 0.26f;
    wdA = 1.0f - mw;
    wdB = mw;
    lvl_to = target_on ? 1.0f : 0.0f;
    lvl_k = target_on ? 0.00004f : 0.00008f;  /* ~0.6 s in, ~0.3 s out */
    gain *= 8300000.0f * 0.8f;
    for (i = 0; i < n; i++) {
        float a, b, sub, nz, l, r, ro, dL, dR, rin, o[4], y[4], fl, fr, outL, outR;
        uint32_t j;
        e.level += (lvl_to - e.level) * lvl_k;
        e.phA += (uint32_t)incA;
        e.phB += (uint32_t)incB;
        e.phS += (uint32_t)incS;
        e.phR += (uint32_t)incR;
        a = wt(WAVES[m0], e.phA) * wdA + wt(WAVES[m1], e.phA) * wdB;
        {
            uint32_t pb = e.phB + (uint32_t)(int32_t)(a * fmd * 4294967296.0f);
            b = wt(WAVES[m0], pb) * wdA + wt(WAVES[m1], pb) * wdB;
        }
        sub = wt(SINE, e.phS) * e.sub;
        nz = (float)(int32_t)xs() * (1.0f / 2147483648.0f);
        e.nlp += (nz - e.nlp) * 0.12f;   /* a band around 900 Hz */
        e.nhp += (e.nlp - e.nhp) * 0.06f;
        nz = (e.nlp - e.nhp) * e.noise * 3.0f;
        /* A left, B right (each ~0.55 off centre), sub and noise in the middle */
        l = (a * wA * 0.85f + b * wB * 0.35f + sub + nz) * e.level;
        r = (a * wA * 0.35f + b * wB * 0.85f + sub + nz) * e.level;
        /* the storm: ring modulation, blended */
        ro = wt(SINE, e.phR);
        l = l * dry_amp + l * ro * ring;
        r = r * dry_amp + r * ro * ring;
        /* the filter */
        {
            float v3 = l - e.s2L, v1 = a1 * e.s1L + a2 * v3, v2 = e.s2L + a2 * e.s1L + a3 * v3;
            e.s1L = 2.0f * v1 - e.s1L;
            e.s2L = 2.0f * v2 - e.s2L;
            fl = v2;
            v3 = r - e.s2R;
            v1 = a1 * e.s1R + a2 * v3;
            v2 = e.s2R + a2 * e.s1R + a3 * v3;
            e.s1R = 2.0f * v1 - e.s1R;
            e.s2R = 2.0f * v2 - e.s2R;
            fr = v2;
        }
        /* the delay: 0.48 s, its loop damped */
        dL = (float)dly[2u * dly_i] * (1.0f / 32768.0f);
        dR = (float)dly[2u * dly_i + 1u] * (1.0f / 32768.0f);
        e.dlpL += (dL - e.dlpL) * 0.35f;
        e.dlpR += (dR - e.dlpR) * 0.35f;
        {
            float wl = fm_clampf(fl + e.dlpR * e.feedback, -1.0f, 1.0f);   /* (crossed: it ping-pongs) */
            float wr = fm_clampf(fr + e.dlpL * e.feedback, -1.0f, 1.0f);
            dly[2u * dly_i] = (int16_t)(wl * 32767.0f);
            dly[2u * dly_i + 1u] = (int16_t)(wr * 32767.0f);
        }
        if (++dly_i == DLY_N)
            dly_i = 0;
        /* the reverb: four lines, a Hadamard mix, each damped */
        rin = (fl + fr) * 0.25f;
        for (j = 0; j < 4u; j++) {
            o[j] = (float)rv_line[j][rv_i[j]] * (1.0f / 32768.0f);
            e.rvlp[j] += (o[j] - e.rvlp[j]) * 0.45f;
        }
        y[0] = 0.5f * (e.rvlp[0] + e.rvlp[1] + e.rvlp[2] + e.rvlp[3]);
        y[1] = 0.5f * (e.rvlp[0] - e.rvlp[1] + e.rvlp[2] - e.rvlp[3]);
        y[2] = 0.5f * (e.rvlp[0] + e.rvlp[1] - e.rvlp[2] - e.rvlp[3]);
        y[3] = 0.5f * (e.rvlp[0] - e.rvlp[1] - e.rvlp[2] + e.rvlp[3]);
        for (j = 0; j < 4u; j++) {
            rv_line[j][rv_i[j]] = (int16_t)(fm_clampf(rin + y[j] * 0.86f, -1.0f, 1.0f) * 32767.0f);
            if (++rv_i[j] == RV_LEN[j])
                rv_i[j] = 0;
        }
        /* out: the dry, the delay, the reverb; a soft limit */
        outL = fl * e.dry + dL * e.delay + (o[0] + o[2]) * e.verb;
        outR = fr * e.dry + dR * e.delay + (o[1] + o[3]) * e.verb;
        outL = outL / (1.0f + fm_fabsf(outL) * 0.5f);
        outR = outR / (1.0f + fm_fabsf(outR) * 0.5f);
        lr[2u * i] = (int32_t)(outL * gain);
        lr[2u * i + 1u] = (int32_t)(outR * gain);
    }
}
