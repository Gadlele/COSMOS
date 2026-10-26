/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's engine (engine.h), in single-precision floats (the FM-1's FPU) at 44.1 kHz.
 *
 * Each drone is a small instrument of its own, on its own note:
 *   main oscillator ─┐ (the moon's four characters, blended; a planet may soften it toward a sine)
 *   partner ─────────┼─ drive ─ its own resonant filter ─ its breath ─┐   the partner an interval above (the pressure's,
 *   sub ─────────────┤                                                │   or an octave), a few cents off: it beats
 *   pink noise ──────┘                                                │
 *          drone A a little left, drone B a little right, ring-modulated in a storm ◄──────────┘
 *   + the planet's rhythmic voice
 *   ─┬─ dry ────────────────────────────────┬─ warmth (a soft drive over the sound and its echoes) ─ out
 *    ├─ delay: 0.4 s ping-pong, tape wow ───┤
 *    └─ reverb: diffused, four long lines ──┘
 * Nothing is ever still: each drone's pitch, cutoff, level and partner wander on their own (more with Kp), and each
 * breathes at its own slow rate. Every parameter glides to the sky's target once a block (5.8 ms) at the pace of its
 * source. The delay and the reverb live in int16 lines on the WiFi heap. */
#include <stdint.h>
#include "engine.h"
#include "fastmath.h"
#include "waves.h"

extern void *malloc(unsigned long n);

#define SR 44100.0f
#define TO32 (4294967296.0f / SR)
#define DLY_N 17640u                     /* 0.4 s */
#define WOW 24.0f                        /* the tape's wow: the delay's read wanders this many samples */
static const uint32_t AP_LEN[4] = {142u, 107u, 379u, 277u};      /* the reverb's input diffusion */
static const uint32_t RV_LEN[4] = {2999u, 3571u, 4111u, 4651u};  /* its lines: 68 to 105 ms, prime */
#define AP_N (142u + 107u + 379u + 277u)
#define RV_N (2999u + 3571u + 4111u + 4651u)

/* ------------------------------------------------------------------ the planets --- */
typedef struct {
    float sine;                          /* the oscillators softened toward a pure sine, 0..1 */
    float pr_mul;                        /* the partner's ratio times this... */
    float pr_fixed;                      /* ...or this one, inharmonic (0: none) */
    float partner, sub, drive, cutoff, q, noise, verb;   /* multipliers (verb: added) */
} engine_t;
static const engine_t ENGINES[PL_N] = {
    /*            sine  pr_mul pr_fixed partner sub  drive cutoff q    noise verb */
    [PL_NONE] = {0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f},
    [PL_SUN] = {0.1f, 1.0f, 0.0f, 1.0f, 0.6f, 1.0f, 1.5f, 0.8f, 0.5f, 0.10f},       /* radiant, open */
    [PL_MOON] = {0.6f, 1.0f, 0.0f, 0.7f, 0.7f, 0.6f, 0.9f, 1.4f, 0.4f, 0.20f},      /* silver, singing */
    [PL_MERCURY] = {0.3f, 2.0f, 0.0f, 0.8f, 0.2f, 0.7f, 1.7f, 0.8f, 0.5f, 0.05f},   /* thin and bright */
    [PL_VENUS] = {0.85f, 2.0f, 0.0f, 0.55f, 0.8f, 0.55f, 1.3f, 0.5f, 0.3f, 0.15f},  /* sweet, glass */
    [PL_MARS] = {0.0f, 1.0f, 0.0f, 1.2f, 1.0f, 2.6f, 1.15f, 1.3f, 2.0f, -0.05f},    /* raw, driven */
    [PL_JUPITER] = {0.4f, 1.0f, 0.0f, 0.7f, 1.9f, 0.9f, 0.6f, 0.6f, 0.6f, 0.05f},   /* deep and warm */
    [PL_SATURN] = {0.2f, 1.0f, 2.76f, 0.9f, 0.8f, 0.8f, 1.1f, 1.6f, 0.6f, 0.1f},    /* metallic, ringed */
};
static const char *const PL_NAME[PL_N] = {"", "SUN", "MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"};
static const char *const PL_VOICE[PL_N] = {"", "FLARE", "RIPPLE", "GLINT", "BELLS", "PULSE", "SWELL", "RINGS"};
/* each voice's pulse: 2.2 s times the square root of its period in years: the Earth's year for the Sun, the
 * synodic month for the Moon, each planet's orbit (Moon 0.6 s, Mercury 1.1 .. Saturn 11.9) */
static const float PL_PERIOD[PL_N] = {0.0f, 2.2f, 0.63f, 1.08f, 1.73f, 3.02f, 7.58f, 11.94f};

const char *planet_name(int32_t p) { return p > 0 && p < PL_N ? PL_NAME[p] : ""; }
const char *planet_voice(int32_t p) { return p > 0 && p < PL_N ? PL_VOICE[p] : ""; }

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

float tension(const sky_t *s) { return (1013.0f - s->press) / 22.0f - 0.6f * (float)s->trend; }

void sky_map(const sky_t *s, sky_map_t *m)
{
    float t = tension(s);
    float c = fm_clampf(s->clouds / 100.0f, 0.0f, 1.0f), x = s->moon - fm_floorf(s->moon);
    int32_t k = s->linked ? (int32_t)(fm_clampf((t + 1.4f) / 4.0f, 0.0f, 1.0f) * (float)(NINT - 1) + 0.5f) : 0;
    float h = s->sun;
    m->hz[0] = s->hz[0];
    m->hz[1] = s->hz[1];
    m->interval = INTERVALS[k].semi;
    m->interval_name = INTERVALS[k].name;
    m->pr[0] = s->pr[0] > 0.0f ? s->pr[0] : fm_exp2f((float)m->interval / 12.0f);   /* a chord's, or the pressure's */
    m->pr[1] = s->pr[1] > 0.0f ? s->pr[1] : fm_exp2f((float)m->interval / 12.0f);
    m->beat = 2.5f + (s->linked ? fm_maxf(0.0f, t) * 6.0f : 0.0f);   /* a slow beat, faster in tension */
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
static volatile int32_t target_on, target_don[2], target_planet, target_chord;   /* chord: the partners are a chord's tones */
static volatile int8_t target_scale[7];
static volatile float target_root;
static int16_t *dly, *ap_line[4], *rv_line[4];
static uint32_t dly_i, ap_i[4], rv_i[4];

typedef struct {                         /* a drone */
    float hz;                            /* its pitch, gliding */
    uint32_t ph, php, phs;               /* main, partner, sub */
    float s1, s2;                        /* its filter (Simper's state-variable filter) */
    float dp, dc, dl, dd;                /* its wanders: pitch, cutoff, level, the partner's detune (-1..1) */
    float breath, breath_rate;           /* its slow swell: phase 0..1, Hz */
    float a1, a2, a3;                    /* the filter's coefficients for the block */
    float inc, incp, incs, lvl;
} drone_t;

#define NPING 12
typedef struct {                         /* a struck partial of a planet's voice */
    uint32_t ph, inc, mph, minc;         /* its phase, and its modulator's (an FM bell) */
    float amp, dec, idx, idec;           /* its level and its decay a sample; the modulation index and its decay */
    int32_t wait;                        /* samples before it starts */
} ping_t;

static struct {                          /* where the render is: each glides to the target */
    float pr[2], beat, cutoff, q, sub, morph, verb, delay, feedback, noise, dry, drift, storm, level, gate[2];
    engine_t eng;                        /* the planet's engine, gliding */
    drone_t d[2];
    uint32_t phR, phW;                   /* the ring's oscillator; the tape's wow */
    float p0, p1, p2;                    /* pink noise (Paul Kellet's economy filter) */
    float dlpL, dlpR, rvlp[4];
    ping_t ping[NPING];
    int32_t planet, until, step;         /* the voice playing, samples to its next pulse, its pulses so far */
    uint32_t phJ;                        /* Jupiter's swell: its oscillator */
    float jamp, jlp;
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
    int i;
    sky_map(s, &m);
    target.hz[0] = m.hz[0];
    target.hz[1] = m.hz[1];
    target.pr[0] = m.pr[0];
    target.pr[1] = m.pr[1];
    target_chord = s->pr[0] > 0.0f;
    for (i = 0; i < 7; i++)
        target_scale[i] = s->scale[i];
    target_root = s->root_hz;
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
    target_planet = s->planet > 0 && s->planet < PL_N ? s->planet : PL_NONE;
    target_on = s->on;
    target_don[0] = s->drone_on[0];
    target_don[1] = s->drone_on[1];
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

/* ---- the planets' voices: struck partials (a sine, or an FM bell), and Jupiter's swell */
static void strike(float hz, float amp, float tau, float mod_ratio, float index, float itau, int32_t wait)
{
    int i, quiet = 0;
    for (i = 1; i < NPING; i++)          /* a free one, or the quietest */
        if (e.ping[i].amp < e.ping[quiet].amp)
            quiet = i;
    e.ping[quiet].ph = 0;
    e.ping[quiet].mph = 0;
    e.ping[quiet].inc = (uint32_t)(hz * TO32);
    e.ping[quiet].minc = (uint32_t)(hz * mod_ratio * TO32);
    e.ping[quiet].amp = amp;
    e.ping[quiet].dec = fm_expf(-1.0f / (tau * SR));
    e.ping[quiet].idx = index;
    e.ping[quiet].idec = itau > 0.0f ? fm_expf(-1.0f / (itau * SR)) : 1.0f;
    e.ping[quiet].wait = wait;
}

static float semi(int32_t s) { return fm_exp2f((float)s / 12.0f); }

/* a degree of the planet's scale, any octave: its frequency over the root */
static float degree(int32_t d)
{
    int32_t o = d >= 0 ? d / 7 : (d - 6) / 7;
    return fm_exp2f((float)o + (float)target_scale[d - o * 7] / 12.0f);
}

static void pulse(int32_t planet, int32_t k, float base)
{
    switch (planet) {
    case PL_SUN:                         /* FLARE: a soft, bright chord of the scale flaring high and fading */
        strike(base * 4.0f, 0.05f, 1.6f, 2.0f, 0.6f, 0.4f, 0);
        strike(base * 4.0f * degree(2), 0.04f, 1.6f, 2.0f, 0.6f, 0.4f, (int32_t)(0.03f * SR));
        strike(base * 4.0f * degree(4), 0.04f, 1.6f, 2.0f, 0.6f, 0.4f, (int32_t)(0.06f * SR));
        break;
    case PL_MOON: {                      /* RIPPLE: tiny glassy drops, wandering the scale like water */
        static int32_t at = 0;
        at += (int32_t)(e.rnd % 3u) - 1;
        at = at < 0 ? 0 : at > 9 ? 9 : at;
        strike(base * 8.0f * degree(at), 0.035f, 0.22f, 0.0f, 0.0f, 0.0f, 0);
        break;
    }
    case PL_MERCURY:                     /* GLINT: quick high sparks up the chord */
        strike(base * 8.0f * degree((k & 3) * 2), 0.10f, 0.12f, 0.0f, 0.0f, 0.0f, 0);
        strike(base * 16.0f * degree((k & 3) * 2) * 1.005f, 0.04f, 0.07f, 0.0f, 0.0f, 0.0f, 0);
        break;
    case PL_VENUS:                       /* BELLS: FM bells walking the scale as a seven-pointed star */
        strike(base * 4.0f * degree((k * 3) % 7), 0.16f, 1.1f, 3.5f, 2.2f, 0.12f, 0);
        break;
    case PL_MARS:                        /* PULSE: a low double beat, a heartbeat */
        strike(base * 0.5f, 0.55f, 0.09f, 0.0f, 0.0f, 0.0f, 0);
        strike(base * 0.5f, 0.35f, 0.09f, 0.0f, 0.0f, 0.0f, (int32_t)(0.19f * SR));
        break;
    case PL_SATURN: {                    /* RINGS: an inharmonic chime with a long tail */
        float f = base * 2.0f * degree(4);
        strike(f, 0.10f, 3.0f, 0.0f, 0.0f, 0.0f, 0);
        strike(f * 2.76f, 0.055f, 2.0f, 0.0f, 0.0f, 0.0f, 0);
        strike(f * 5.40f, 0.035f, 1.3f, 0.0f, 0.0f, 0.0f, 0);
        strike(f * 8.93f, 0.02f, 0.8f, 0.0f, 0.0f, 0.0f, 0);
        break;
    }
    default:
        break;
    }
}

/* the voices' block: the next pulses, Jupiter's swell */
static void voices_block(uint32_t n)
{
    int32_t p = target_on ? target_planet : PL_NONE;
    if (p != e.planet) {                 /* a new planet: its first pulse soon */
        e.planet = p;
        e.until = (int32_t)(0.3f * SR);
        e.step = 0;
    }
    if (e.planet == PL_JUPITER) {        /* SWELL: up for the first half of its period, then down */
        float per = PL_PERIOD[PL_JUPITER] * SR, at = per - (float)e.until;
        e.jamp = glide(e.jamp, at < per * 0.45f ? 0.30f : 0.06f, 0.004f);
    } else {
        e.jamp = glide(e.jamp, 0.0f, 0.01f);
    }
    if (e.planet == PL_NONE)
        return;
    e.until -= (int32_t)n;
    if (e.until <= 0) {
        if (e.planet != PL_JUPITER)
            pulse(e.planet, e.step, target_root > 0.0f ? target_root : e.d[0].hz);
        e.step++;
        e.until += (int32_t)(PL_PERIOD[e.planet] * SR);
    }
}

/* ---- a drone's block: its wanders, its pitch, its filter */
static void drone_block(drone_t *d, float hz, float pratio, float cutoff, uint32_t frames)
{
    float sp = 0.02f + 0.12f * e.drift, depth = e.drift, pr;
    d->hz = glide(d->hz, hz, 0.003f);
    d->dp = fm_clampf(d->dp + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dc = fm_clampf(d->dc + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dl = fm_clampf(d->dl + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dd = fm_clampf(d->dd + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    hz = d->hz * fm_exp2f(d->dp * (3.0f + 30.0f * depth * depth) / 1200.0f);
    /* a chord's tone; else the planet's partner (Saturn's inharmonic one), or the pressure's interval */
    pr = target_chord ? pratio : e.eng.pr_fixed > 0.0f ? e.eng.pr_fixed : pratio * e.eng.pr_mul;
    d->inc = hz * TO32;
    d->incp = hz * pr * fm_exp2f((e.beat + d->dd * 4.0f * depth) / 1200.0f) * TO32;
    d->incs = hz * 0.5f * TO32;
    d->lvl = 1.0f + d->dl * 0.18f * depth;
    d->breath += d->breath_rate * (float)frames / SR;
    if (d->breath >= 1.0f)
        d->breath -= 1.0f;
    cutoff *= e.eng.cutoff * fm_exp2f(d->dc * 0.9f * depth);   /* up to most of an octave either way */
    {
        float g = fm_tanf(FM_PI * fm_clampf(cutoff, 40.0f, 16000.0f) / SR), k = 1.0f / (e.q * e.eng.q);
        d->a1 = 1.0f / (1.0f + g * (g + k));
        d->a2 = g * d->a1;
        d->a3 = g * d->a2;
    }
}

/* a drone's sample: its oscillators and noise, driven into its filter, breathing */
static inline float drone_tick(drone_t *d, int32_t m0, int32_t m1, float w1, float sn, uint32_t pm, float nz,
                               float drv, float breath, float plv, float slv)
{
    float w0 = (1.0f - w1) * (1.0f - sn), x, v1, v2, v3;
    uint32_t ph, pp;
    w1 *= 1.0f - sn;
    d->ph += (uint32_t)d->inc;
    d->php += (uint32_t)d->incp;
    d->phs += (uint32_t)d->incs;
    ph = d->ph + pm;
    pp = d->php + pm;
    x = (wt(WAVES[m0], ph) * w0 + wt(WAVES[m1], ph) * w1 + wt(SINE, ph) * sn) * 0.62f;
    x += (wt(WAVES[m0], pp) * w0 + wt(WAVES[m1], pp) * w1 + wt(SINE, pp) * sn) * plv;
    x += wt(SINE, d->phs) * slv + nz;
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
    float incR, incW, fmd, ring, dry_amp, to0, to1, k0, k1, drv, brA, brB, decay, plv, slv, nzl, sn, verb;
    int32_t m0, m1;
    float mw;
    if (!dly) {                          /* no lines (no heap): silence */
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    if (!e.started) {                    /* the first block: start where the sky is */
        e.d[0].hz = target.hz[0];
        e.d[1].hz = target.hz[1];
        e.pr[0] = target.pr[0];
        e.pr[1] = target.pr[1];
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
        e.eng = ENGINES[target_planet];
        e.started = 1;
    }
    /* a block's glides (5.8 ms): the interval ~4 s, the filters ~1 s, the moon ~6 s, the space ~2 s */
    e.pr[0] = glide(e.pr[0], target.pr[0], 0.004f);   /* the partners follow the chords over ~1.5 s */
    e.pr[1] = glide(e.pr[1], target.pr[1], 0.004f);
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
    {   /* the planet's engine, over ~2 s (an inharmonic partner jumps: it has no in-between) */
        const engine_t *t = &ENGINES[target_planet];
        float k = 0.003f;
        e.eng.sine = glide(e.eng.sine, t->sine, k);
        e.eng.pr_mul = glide(e.eng.pr_mul, t->pr_mul, k);
        e.eng.pr_fixed = t->pr_fixed;
        e.eng.partner = glide(e.eng.partner, t->partner, k);
        e.eng.sub = glide(e.eng.sub, t->sub, k);
        e.eng.drive = glide(e.eng.drive, t->drive, k);
        e.eng.cutoff = glide(e.eng.cutoff, t->cutoff, k);
        e.eng.q = glide(e.eng.q, t->q, k);
        e.eng.noise = glide(e.eng.noise, t->noise, k);
        e.eng.verb = glide(e.eng.verb, t->verb, k);
    }
    drone_block(&e.d[0], target.hz[0], e.pr[0], e.cutoff, n);
    drone_block(&e.d[1], target.hz[1], e.pr[1], e.cutoff * 1.12f, n);
    voices_block(n);
    incR = (e.d[0].hz * 2.71f + 3.0f) * TO32;
    incW = 0.37f * TO32;
    /* the moon's character: two neighbouring tables and how far between */
    m0 = (int32_t)e.morph & 3;
    m1 = (m0 + 1) & 3;
    mw = e.morph - fm_floorf(e.morph);
    mw = mw * mw * (3.0f - 2.0f * mw);
    sn = e.eng.sine;
    plv = 0.30f * e.eng.partner;
    slv = e.sub * e.eng.sub;
    nzl = e.noise * e.eng.noise;
    verb = fm_clampf(e.verb + e.eng.verb, 0.0f, 0.95f);
    fmd = e.storm * e.storm * 0.35f;     /* B's phase pushed by A: up to a third of a cycle */
    ring = 0.9f * e.storm;
    dry_amp = 1.0f - 0.6f * e.storm;
    drv = (1.1f + 0.5f * (e.sub - 0.05f) / 0.45f + 1.6f * e.storm) * e.eng.drive;   /* heavier at full moon */
    decay = 0.80f + 0.13f * fm_clampf((verb - 0.10f) / 0.70f, 0.0f, 1.0f);       /* the clouds lengthen it */
    /* each drone breathes: up to a quarter quieter at the bottom of its swell */
    brA = 1.0f - 0.22f * (0.5f - 0.5f * fm_cosf(FM_TWO_PI * e.d[0].breath));
    brB = 1.0f - 0.22f * (0.5f - 0.5f * fm_cosf(FM_TWO_PI * e.d[1].breath));
    to0 = target_don[0] ? 1.0f : 0.0f;  /* each drone fades in over ~0.6 s, out over ~0.3 s */
    to1 = target_don[1] ? 1.0f : 0.0f;
    k0 = to0 > e.gate[0] ? 0.00004f : 0.00008f;
    k1 = to1 > e.gate[1] ? 0.00004f : 0.00008f;
    gain *= 8300000.0f * 0.3f;
    for (i = 0; i < n; i++) {
        float white, nzA, nzB, a, b, l, r, ro, dL, dR, rin, o[4], y[4], outL, outR, pos, fr, v = 0.0f;
        uint32_t j, pm, ri, rj;
        e.gate[0] += (to0 - e.gate[0]) * k0;
        e.gate[1] += (to1 - e.gate[1]) * k1;
        e.level += ((target_on ? 1.0f : 0.0f) - e.level) * 0.00008f;   /* the planet's voice */
        /* pink noise, a little different in each drone */
        white = (float)(int32_t)xs() * (1.0f / 2147483648.0f);
        e.p0 = 0.99765f * e.p0 + white * 0.0990460f;
        e.p1 = 0.96300f * e.p1 + white * 0.2965164f;
        e.p2 = 0.57000f * e.p2 + white * 1.0526913f;
        nzA = (e.p0 + e.p1 + e.p2 + white * 0.1848f) * 0.18f * nzl;
        nzB = (e.p0 - e.p1 + e.p2) * 0.18f * nzl;
        a = drone_tick(&e.d[0], m0, m1, mw, sn, 0u, nzA, drv, brA, plv, slv);
        pm = (uint32_t)(int32_t)(a * fmd * 4294967296.0f);   /* the storm: A bends B */
        b = drone_tick(&e.d[1], m0, m1, mw, sn, pm, nzB, drv, brB, plv, slv);
        /* A a little left, B a little right */
        a *= e.gate[0];
        b *= e.gate[1];
        l = a * 0.80f + b * 0.40f;
        r = a * 0.40f + b * 0.80f;
        e.phR += (uint32_t)incR;
        ro = wt(SINE, e.phR);
        l = l * dry_amp + l * ro * ring;
        r = r * dry_amp + r * ro * ring;
        /* the planet's voice: its struck partials, Jupiter's swell (a low, filtered saw) */
        for (j = 0; j < NPING; j++) {
            ping_t *p = &e.ping[j];
            if (p->amp < 0.0005f)
                continue;
            if (p->wait > 0) {
                p->wait--;
                continue;
            }
            p->ph += p->inc;
            if (p->minc) {
                p->mph += p->minc;
                v += wt(SINE, p->ph + (uint32_t)(int32_t)(wt(SINE, p->mph) * p->idx * 683565275.0f)) * p->amp;
                p->idx *= p->idec;
            } else {
                v += wt(SINE, p->ph) * p->amp;
            }
            p->amp *= p->dec;
        }
        if (e.jamp > 0.001f) {
            e.phJ += (uint32_t)(e.d[0].hz * 0.25f * TO32);
            e.jlp += (wt(WAVES[2], e.phJ) - e.jlp) * 0.04f;
            v += e.jlp * e.jamp;
        }
        l += v * e.level;
        r += v * e.level;
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
            float z = (float)ap_line[j][ap_i[j]] * (1.0f / 32768.0f), w = rin + 0.6f * z;
            ap_line[j][ap_i[j]] = (int16_t)(fm_clampf(w, -1.0f, 1.0f) * 32767.0f);
            rin = z - 0.6f * w;
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
        outL += (o[0] + o[2]) * verb;
        outR += (o[1] + o[3]) * verb;
        outL = outL / (1.0f + fm_fabsf(outL) * 0.4f);
        outR = outR / (1.0f + fm_fabsf(outR) * 0.4f);
        lr[2u * i] = (int32_t)(outL * gain);
        lr[2u * i + 1u] = (int32_t)(outR * gain);
    }
}
