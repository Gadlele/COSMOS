/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's engine (engine.h), in single-precision floats (the FM-1's FPU) at 44.1 kHz.
 *
 * One drone underneath, a soft FM pad on the chord's root and its fifth:
 *   root: three FM pairs ───┐ each note three pairs of sines, a carrier and a light modulator: two a few cents
 *   fifth: three more ──────┼─ its gentle filter ─ its breath ─┐   either side of the note, one an octave up; the
 *   sub, pink noise ────────┘                                  │   index low (the moon's light raises it)
 *                                                ring-modulated in a storm
 *   + a pad on top, the whole chord, each chord attacked anew: one of fifteen (pads.c), the weather's by day or by
 *     night (sky_t.engine), gated by PULSAR in a geomagnetic storm (stars.c)
 *   + the star voices: one of eight, the moon's (sky_t.voice), or the rhythms' (stars.c), or the pad's own
 *   ─ a slow stereo chorus
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

/* ------------------------------------------------------------------ the drone --- */
/* each note three two-operator pairs, two a few cents either side of the note and one an octave up */
typedef struct {
    float index;                         /* the modulators' depth, radians (the moon's light scales it) */
    float mratio;                        /* each modulator over its carrier */
    float oratio, olevel;                /* the third pair: an octave up, its level */
    float detune;                        /* the two unison pairs apart, cents */
    float wow;                           /* the pitch wandering slowly, cents */
    float chorus;                        /* the chorus's depth, 0..1 */
    float partner, sub, cutoff, q, noise, verb;   /* multipliers (verb: added) */
} engine_t;
static const engine_t DRONE = {0.9f, 1.0f, 2.0f, 0.45f, 6.0f, 3.0f, 0.6f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
static const char *const PL_NAME[PL_N] = {"", "SUN", "MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"};

const char *planet_name(int32_t p) { return p > 0 && p < PL_N ? PL_NAME[p] : ""; }

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
    /* the sun: 450 Hz at night (dark, but the chord still heard), 1.2 kHz at the horizon, 7 kHz at 65 degrees */
    if (h < -18.0f)
        m->cutoff = 450.0f;
    else if (h < 0.0f)
        m->cutoff = 450.0f * fm_powf(1200.0f / 450.0f, (h + 18.0f) / 18.0f);
    else
        m->cutoff = 1200.0f * fm_powf(7000.0f / 1200.0f, fm_clampf(h / 65.0f, 0.0f, 1.0f));
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
static volatile int32_t target_on, target_don[2], target_engine, target_chord;   /* chord: the partners are a chord's tones */
static volatile int32_t target_voice, target_rhythm;
static volatile float target_dlevel = 1.0f;   /* the drone's level */   /* the star voice; 0 none, 1 STARGATE, 2 PULSAR */
static volatile float target_notes[NCHORD];   /* the instrument's chord, Hz (0: none) */
static volatile uint32_t target_chord_n;      /* a new chord: it changes */
static int16_t *dly, *ap_line[4], *rv_line[4], *ch_line;
static uint32_t dly_i, ap_i[4], rv_i[4], ch_i;
#define CH_N 1024u                       /* the chorus's line, stereo (23 ms) */
#define CH_C 480.0f                      /* its delay: 11 ms... */
#define CH_D 150.0f                      /* ...3.4 ms either way */
#define SH_N 2048u                       /* the shimmer's line (AURORA): 46 ms */
#define SH_G 1764.0f                     /* its grain: 40 ms */
static int16_t *sh_line;
static uint32_t sh_i;
#ifndef GLIDE_S
#define GLIDE_S 1.5f                     /* a new chord: the notes slide to it over about this long, s */
#endif

typedef struct {                         /* a drone */
    float hz;                            /* its pitch, gliding */
    uint32_t c[2][3], m[2][3], phs;      /* the carriers and the modulators: the tone's three pairs, the partner's; the sub */
    float ci[2][3], mi[2][3];            /* their steps */
    float wow, wow_rate;                 /* the pitch's slow wander: a phase 0..1, Hz */
    float s1, s2;                        /* its filter (Simper's state-variable filter) */
    float dp, dc, dl, dd;                /* its wanders: pitch, cutoff, level, the partner's detune (-1..1) */
    float breath, breath_rate;           /* its slow swell: phase 0..1, Hz */
    float a1, a2, a3;                    /* the filter's coefficients for the block */
    float incs, lvl;
} drone_t;

static struct {                          /* where the render is: each glides to the target */
    float pr[2], beat, cutoff, q, sub, verb, delay, feedback, noise, dry, drift, storm, level, gate[2];
    float chr, vadd, shim, shph, lit, dlevel;    /* the pad's chorus and reverb, AURORA's shimmer and its grain phase */
    engine_t eng;                        /* the drone's */
    drone_t d[2];
    uint32_t phR, phW, phC;              /* the ring's oscillator; the tape's wow; the chorus's sway */
    float p0, p1, p2;                    /* pink noise (Paul Kellet's economy filter) */
    float dlpL, dlpR, rvlp[4];
    uint32_t rnd;
    int started;
} e = {.rnd = 0x9E3779B9u, .d = {{.breath_rate = 0.043f, .wow_rate = 0.53f},
                                  {.breath = 0.37f, .breath_rate = 0.031f, .wow = 0.4f, .wow_rate = 0.41f}}};

static int pads_init(void);
static int stars_init(void);
#define PS_MAX 256u                      /* the pads and stars a block ahead (eng_render): frames a block, at most */
static int16_t *ps_buf;                  /* 2 halves of PS_MAX stereo frames (heap), PS_ONE to 1.0 */
#define PS_ONE 8192.0f                   /* (room for a sum four times full scale) */
static uint32_t pads_bytes(void), stars_bytes(void);
uint32_t eng_bytes(void)
{
    return (2u * DLY_N + AP_N + RV_N + 2u * CH_N) * 2u + SH_N * 2u + 4u * PS_MAX * 2u + pads_bytes() + stars_bytes();
}

int eng_init(void)
{
    uint32_t i;
    int16_t *p;
    dly = malloc(2u * DLY_N * 2u);
    p = malloc((AP_N + RV_N) * 2u);
    ch_line = malloc(2u * CH_N * 2u);
    sh_line = malloc(SH_N * sizeof(int16_t));
    ps_buf = malloc(2u * 2u * PS_MAX * sizeof(int16_t));
    if (!dly || !p || !ch_line || !sh_line || !ps_buf || pads_init() || stars_init())
        return -1;
    for (i = 0; i < SH_N; i++)
        sh_line[i] = 0;
    for (i = 0; i < 4u * PS_MAX; i++)
        ps_buf[i] = 0;
    for (i = 0; i < 2u * CH_N; i++)
        ch_line[i] = 0;
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
    target.hz[0] = m.hz[0];
    target.hz[1] = m.hz[1];
    target.pr[0] = m.pr[0];
    target.pr[1] = m.pr[1];
    target_chord = s->pr[0] > 0.0f;
    target.beat = m.beat;
    target.cutoff = m.cutoff;
    target.q = m.q;
    target.sub = m.sub;
    target.verb = m.verb;
    target.delay = m.delay;
    target.feedback = m.feedback;
    target.noise = m.noise;
    target.dry = m.dry;
    target.drift = m.drift;
    target.storm = m.storm;
    target_engine = s->engine >= 0 && s->engine < ENG_N ? s->engine : 0;
    target_voice = s->voice >= 0 && s->voice < V_N ? s->voice : 0;
    target_rhythm = s->rhythm;
    target_dlevel = s->drone_level;
    target.lit = m.lit;
    {
        int32_t k;
        for (k = 0; k < NCHORD; k++)
            target_notes[k] = s->notes[k];
        target_chord_n = s->chord_n;
    }
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

/* ---- a drone's block: its wanders, its pitch, its filter */
static void drone_block(drone_t *d, float hz, float pratio, float cutoff, uint32_t frames)
{
    float sp = 0.02f + 0.12f * e.drift, depth = e.drift;
    d->hz = glide(d->hz, hz, (float)frames / (GLIDE_S * SR));
    d->dp = fm_clampf(d->dp + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dc = fm_clampf(d->dc + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dl = fm_clampf(d->dl + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->dd = fm_clampf(d->dd + rnd_pm() * sp, -1.0f, 1.0f) * 0.999f;
    d->wow += d->wow_rate * (1.0f + 0.3f * d->dd) * (float)frames / SR;   /* the tape: slow, never quite even */
    if (d->wow >= 1.0f)
        d->wow -= 1.0f;
    hz = d->hz * fm_exp2f((d->dp * (3.0f + 30.0f * depth * depth) + e.eng.wow * fm_sinf(FM_TWO_PI * d->wow)) / 1200.0f);
    {   /* the tone, and the partner (the chord's, or the pressure's interval) a few cents off: they beat */
        float f[2], dt = fm_exp2f(e.eng.detune / 2400.0f);
        int32_t n, k;
        f[0] = hz * TO32;
        f[1] = hz * pratio * fm_exp2f((e.beat + d->dd * 4.0f * depth) / 1200.0f) * TO32;
        for (n = 0; n < 2; n++) {
            d->ci[n][0] = f[n] * dt;     /* the unison pairs, either side */
            d->ci[n][1] = f[n] / dt;
            d->ci[n][2] = f[n] * e.eng.oratio;   /* the octave's */
            for (k = 0; k < 3; k++)
                d->mi[n][k] = d->ci[n][k] * e.eng.mratio;
        }
    }
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

/* a note's three FM pairs, one sample: idx the modulators' depth, ol the octave pair's level, pm a push on the phase */
static inline float pairs(uint32_t *c, uint32_t *m, const float *ci, const float *mi, uint32_t pm, float idx, float ol)
{
    float x;
    c[0] += (uint32_t)ci[0];
    c[1] += (uint32_t)ci[1];
    c[2] += (uint32_t)ci[2];
    m[0] += (uint32_t)mi[0];
    m[1] += (uint32_t)mi[1];
    m[2] += (uint32_t)mi[2];
    idx *= 683565275.0f;                 /* radians to a 32-bit phase */
    x = wt(SINE, c[0] + pm + (uint32_t)(int32_t)(wt(SINE, m[0]) * idx));
    x += wt(SINE, c[1] + pm + (uint32_t)(int32_t)(wt(SINE, m[1]) * idx));
    x += wt(SINE, c[2] + pm + (uint32_t)(int32_t)(wt(SINE, m[2]) * idx * 0.6f)) * ol;
    return x;
}

/* a drone's sample: its pairs, sub and noise into its filter, breathing */
static inline float drone_tick(drone_t *d, float idx, float ol, uint32_t pm, float nz, float breath, float plv,
                               float slv)
{
    float x, v1, v2, v3;
    d->phs += (uint32_t)d->incs;
    x = pairs(d->c[0], d->m[0], d->ci[0], d->mi[0], pm, idx, ol) * 0.30f;
    x += pairs(d->c[1], d->m[1], d->ci[1], d->mi[1], pm, idx, ol) * 0.30f * plv;
    x += wt(SINE, d->phs) * slv + nz;
    v3 = x - d->s2;
    v1 = d->a1 * d->s1 + d->a2 * v3;
    v2 = d->s2 + d->a2 * d->s1 + d->a3 * v3;
    d->s1 = 2.0f * v1 - d->s1;
    d->s2 = 2.0f * v2 - d->s2;
    return sat(v2) * d->lvl * breath;
}

/* the second core's own noise (pads.c and stars.c run there: they draw from it, the drones from xs) */
static uint32_t rnd2 = 0x9E3779B9u;
static inline uint32_t xs2(void)
{
    rnd2 ^= rnd2 << 13;
    rnd2 ^= rnd2 >> 17;
    rnd2 ^= rnd2 << 5;
    return rnd2;
}
static inline float rnd_pm2(void) { return (float)(xs2() & 0xFFFFu) * (1.0f / 32768.0f) - 1.0f; }   /* -1..1 */
#include "pads.c"
#include "stars.c"

/* ---- the pads, the stars and the rhythm, a block ahead: the second core's job (one core: the same, after the
 * block). A block's worth of their stereo sum in one half of ps_buf while the drones and the space read the other */
extern int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);
extern uint32_t plat_ticks24(void);
static volatile uint32_t ld_main, ld_pads, ld_stars;   /* each part's time a block (24 ticks a us), smoothed */
#define LD_SMOOTH(a, v) ((a) = ((a) * 15u + (v)) / 16u)
extern int plat_cpu2_wait(void);
extern void plat_cpu2_sync(void);
static uint32_t ps_n, ps_cur;            /* the block's frames; the half the drones read now */
static float ps_p0, ps_p1, ps_p2, ps_level;   /* its pink noise; the stars' level (the sky on, off) */
static void ps_render(uint32_t half)
{
    int16_t *o = ps_buf + half * 2u * PS_MAX;
    uint32_t i, n = ps_n, t0 = plat_ticks24(), t1;
    pads_block(n, target_engine, target_don[1], target_notes, target_chord_n);
    stars_block(n, target_voice, target_rhythm, target_on);
    for (i = 0; i < n; i++) {            /* the pads, under PULSAR's gate, and its beat */
        float w = rnd_pm2(), pk, l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f, g;
        ps_p0 = 0.99765f * ps_p0 + w * 0.0990460f;
        ps_p1 = 0.96300f * ps_p1 + w * 0.2965164f;
        ps_p2 = 0.57000f * ps_p2 + w * 1.0526913f;
        pk = (ps_p0 + ps_p1 + ps_p2 + w * 0.1848f) * 0.25f;
        g = rhythm_tick(&l, &r);
        pads_tick(&pl, &pr, w, pk);
        o[2u * i] = (int16_t)(fm_clampf(l + pl * g, -3.99f, 3.99f) * PS_ONE);
        o[2u * i + 1u] = (int16_t)(fm_clampf(r + pr * g, -3.99f, 3.99f) * PS_ONE);
    }
    t1 = plat_ticks24();
    for (i = 0; i < 2u * n; i++)         /* the star voices on them, a voice at a time */
        st.buf[i] = 0.0f;
    stars_render(st.buf, n);
    for (i = 0; i < n; i++) {
        ps_level += ((target_on ? 1.0f : 0.0f) - ps_level) * 0.00008f;   /* the planet's voice */
        o[2u * i] = (int16_t)fm_clampf((float)o[2u * i] + st.buf[2u * i] * ps_level * PS_ONE, -32767.0f, 32767.0f);
        o[2u * i + 1u] = (int16_t)fm_clampf((float)o[2u * i + 1u] + st.buf[2u * i + 1u] * ps_level * PS_ONE,
                                            -32767.0f, 32767.0f);
    }
    LD_SMOOTH(ld_pads, t1 - t0);
    LD_SMOOTH(ld_stars, plat_ticks24() - t1);
}
static void ps_job(uint32_t half)
{
    plat_cpu2_sync();
    ps_render(half);
    plat_cpu2_sync();
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i, ps_next, tm = plat_ticks24();
    int ps_two;
    const int16_t *ps;
    float incR, incW, incC, ring, dry_amp, to0, k0, brA, decay, plv, slv, nzl, verb, idx, ol, chr;
    if (!dly || !sh_line || !pd_p || !st_p || !ps_buf || n > PS_MAX) {   /* no lines (no heap): silence */
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
        e.sub = target.sub;
        e.verb = target.verb;
        e.delay = target.delay;
        e.feedback = target.feedback;
        e.noise = target.noise;
        e.dry = target.dry;
        e.drift = target.drift;
        e.eng = DRONE;
        e.started = 1;
    }
    /* a block's glides (5.8 ms): the interval ~4 s, the filters ~1 s, the moon ~6 s, the space ~2 s */
    e.pr[0] = glide(e.pr[0], target.pr[0], (float)n / (GLIDE_S * SR));   /* the partners slide with the chords */
    e.pr[1] = glide(e.pr[1], target.pr[1], (float)n / (GLIDE_S * SR));
    e.beat = glide(e.beat, target.beat, 0.003f);
    e.cutoff = glide(e.cutoff, target.cutoff, 0.006f);
    e.q = glide(e.q, target.q, 0.002f);
    e.sub = glide(e.sub, target.sub, 0.002f);
    e.verb = glide(e.verb, target.verb, 0.003f);
    e.delay = glide(e.delay, target.delay, 0.003f);
    e.feedback = glide(e.feedback, target.feedback, 0.003f);
    e.noise = glide(e.noise, target.noise, 0.003f);
    e.dry = glide(e.dry, target.dry, 0.003f);
    e.drift = glide(e.drift, target.drift, 0.005f);
    e.storm = glide(e.storm, target.storm, 0.004f);
    drone_block(&e.d[0], target.hz[0], e.pr[0], e.cutoff, n);
    if (n != ps_n) {                     /* the first block (or a new length): this one's now, the pipe primed */
        ps_n = n;
        ps_render(ps_cur);
    }
    ps_next = ps_cur ^ 1u;
    plat_cpu2_sync();
    ps_two = plat_cpu2_run(ps_job, ps_next);   /* the next block's, beside this one */
    ps = ps_buf + ps_cur * 2u * PS_MAX;
    /* the pad's own chorus and reverb; AURORA's shimmer: more with Kp (the aurora) and the full moon */
    e.chr = glide(e.chr, PAD[target_engine].chorus, 0.003f);
    e.vadd = glide(e.vadd, PAD[target_engine].verb, 0.003f);
    e.lit = glide(e.lit, target.lit, 0.002f);
    e.dlevel = glide(e.dlevel, target_dlevel * target_dlevel, 0.02f);   /* squared: the knob feels even */
    e.shim = glide(e.shim, target_engine == P_AURORA ? 0.30f + 0.35f * e.drift + 0.15f * e.lit : 0.0f, 0.002f);
    incR = (e.d[0].hz * 2.71f + 3.0f) * TO32;
    incW = 0.37f * TO32;
    incC = 0.23f * TO32;
    /* the moon's light: new, the sines nearly pure; full, the modulators at a little over their engine's */
    idx = e.eng.index * (0.35f + 1.3f * fm_clampf((e.sub - 0.05f) / 0.45f, 0.0f, 1.0f));
    ol = e.eng.olevel;
    chr = e.chr;
    plv = 0.85f * e.eng.partner;
    slv = e.sub * e.eng.sub;
    nzl = e.noise * e.eng.noise;
    verb = fm_clampf(e.verb + e.vadd, 0.0f, 0.95f);
    ring = 0.35f * e.storm;
    dry_amp = 1.0f - 0.25f * e.storm;
    decay = 0.80f + 0.13f * fm_clampf((verb - 0.10f) / 0.70f, 0.0f, 1.0f);       /* the clouds lengthen it */
    /* each drone breathes: up to a quarter quieter at the bottom of its swell */
    brA = 1.0f - 0.22f * (0.5f - 0.5f * fm_cosf(FM_TWO_PI * e.d[0].breath));
    to0 = target_don[0] ? 1.0f : 0.0f;  /* the drone fades in over ~0.6 s, out over ~0.3 s */
    k0 = to0 > e.gate[0] ? 0.00004f : 0.00008f;
    gain *= 8300000.0f * 0.3f;
    for (i = 0; i < n; i++) {
        float white, nzA, a, l, r, ro, dL, dR, rin, o[4], y[4], outL, outR, pos, fr;
        uint32_t j, ri, rj;
        e.gate[0] += (to0 - e.gate[0]) * k0;
        /* pink noise, a little different in each drone */
        white = (float)(int32_t)xs() * (1.0f / 2147483648.0f);
        e.p0 = 0.99765f * e.p0 + white * 0.0990460f;
        e.p1 = 0.96300f * e.p1 + white * 0.2965164f;
        e.p2 = 0.57000f * e.p2 + white * 1.0526913f;
        nzA = (e.p0 + e.p1 + e.p2 + white * 0.1848f) * 0.18f * nzl;
        a = drone_tick(&e.d[0], idx, ol, 0u, nzA, brA, plv, slv) * e.gate[0] * 0.55f * e.dlevel;   /* the drone, under */
        e.phR += (uint32_t)incR;
        ro = wt(SINE, e.phR);
        a = a * dry_amp + a * ro * ring;
        l = a + (float)ps[2u * i] * (1.0f / PS_ONE);   /* the pad on top (PULSAR's gate on it), the star voices: a
                                                         * block ago's */
        r = a + (float)ps[2u * i + 1u] * (1.0f / PS_ONE);
        {   /* the chorus: each side read back from a short line, its delay swaying slowly, the two a quarter turn apart */
            float pl, pr2, wl, wr, fl, fr2;
            uint32_t il, ir;
            ch_line[2u * ch_i] = (int16_t)(fm_clampf(l, -1.0f, 1.0f) * 32767.0f);
            ch_line[2u * ch_i + 1u] = (int16_t)(fm_clampf(r, -1.0f, 1.0f) * 32767.0f);
            e.phC += (uint32_t)incC;
            pl = CH_C + CH_D * wt(SINE, e.phC);
            pr2 = CH_C + CH_D * wt(SINE, e.phC + 0x40000000u);
            il = (uint32_t)pl;
            ir = (uint32_t)pr2;
            fl = pl - (float)il;
            fr2 = pr2 - (float)ir;
            wl = ((float)ch_line[2u * ((ch_i - il) & (CH_N - 1u))] * (1.0f - fl)
                  + (float)ch_line[2u * ((ch_i - il - 1u) & (CH_N - 1u))] * fl) * (1.0f / 32768.0f);
            wr = ((float)ch_line[2u * ((ch_i - ir) & (CH_N - 1u)) + 1u] * (1.0f - fr2)
                  + (float)ch_line[2u * ((ch_i - ir - 1u) & (CH_N - 1u)) + 1u] * fr2) * (1.0f / 32768.0f);
            ch_i = (ch_i + 1u) & (CH_N - 1u);
            l = l * (1.0f - 0.35f * chr) + wr * 0.6f * chr;   /* crossed: wider */
            r = r * (1.0f - 0.35f * chr) + wl * 0.6f * chr;
        }
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
        if (e.shim > 0.001f) {           /* AURORA: the tail an octave up (two taps sweeping through a grain), fed back */
            float ph = e.shph, s1, s2, p1, p2;
            uint32_t i1, i2;
            e.shph += 1.0f / SH_G;
            e.shph -= (float)(int32_t)e.shph;
            p1 = SH_G * (1.0f - ph);
            p2 = SH_G * (1.0f - (ph + 0.5f - (float)(int32_t)(ph + 0.5f)));
            i1 = (uint32_t)p1;
            i2 = (uint32_t)p2;
            s1 = (float)sh_line[(sh_i - i1) & (SH_N - 1u)] * (1.0f / 32768.0f) * (0.5f - 0.5f * wt(SINE, (uint32_t)(ph * 4294967296.0f) + 0x40000000u));
            s2 = (float)sh_line[(sh_i - i2) & (SH_N - 1u)] * (1.0f / 32768.0f)
                 * (0.5f - 0.5f * wt(SINE, (uint32_t)((ph + 0.5f) * 4294967296.0f) + 0x40000000u));
            rin += (s1 + s2) * e.shim;
        }
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
        sh_line[sh_i] = (int16_t)(fm_clampf((o[0] + o[1] + o[2] + o[3]) * 0.5f, -1.0f, 1.0f) * 32767.0f);
        sh_i = (sh_i + 1u) & (SH_N - 1u);
        outL += (o[0] + o[2]) * verb;
        outR += (o[1] + o[3]) * verb;
        outL = outL / (1.0f + fm_fabsf(outL) * 0.4f);
        outR = outR / (1.0f + fm_fabsf(outR) * 0.4f);
        lr[2u * i] = (int32_t)(outL * gain);
        lr[2u * i + 1u] = (int32_t)(outR * gain);
    }
    LD_SMOOTH(ld_main, plat_ticks24() - tm);
    if (!ps_two || plat_cpu2_wait() != 0)
        ps_render(ps_next);              /* one core (or it was given up): the next block's now */
    plat_cpu2_sync();
    ps_cur = ps_next;
}

/* each part's load in % of a block's time: the drones and the space (the first core), the pads, the stars */
void eng_load(uint32_t *main, uint32_t *pads, uint32_t *stars)
{
    uint32_t budget = ps_n ? ps_n * 544u : 1u;   /* 24 MHz ticks in a block at 44.1 kHz */
    *main = ld_main * 100u / budget;
    *pads = ld_pads * 100u / budget;
    *stars = ld_stars * 100u / budget;
}
