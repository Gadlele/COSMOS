/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's pads (included by engine.c, in its scope): the chord on top, a voice a note, each chord attacked anew.
 * Fifteen of them, one for each weather by day and by night (the app's choice: sky_t.engine). A voice keeps the pad
 * it began on, so a change of pad crosses over at the next chord. Each pad sums its voices into a stereo bus of its
 * own, which its filters shape; a bus with no voice sounding is skipped.
 *   NEBULA      four saws a note fanned out (±22 cents), a filter opening through each chord
 *   AURORA      sine, triangle and octave, soft; the reverb's tail climbs in octaves (the shimmer, engine.c)
 *   RINGS       resonators on each note and its harmonics, bowed by noise
 *   SOLAR WIND  noise through narrow band-passes on each note and its octave, in gusts
 *   GRAVITY     saws falling two semitones into each chord, a wobbling low-pass, driven, a sub
 *   QUASAR      a cloud of short sine grains on the chord's notes, scattered across the stereo
 *   ORGAN       drawbars 16' to 1' on one phase, the pipes' chiff, a slow rotor, a cathedral
 *   VOYAGER     three saws a note with their own vibratos, through formants moving from vowel to vowel
 *   TAPE        two saws and a triangle on a worn tape (wow and flutter), saturated, hiss
 *   HORIZON     FM swells that grow into each chord as if played backwards, then hold
 *   BACKGROUND  24 harmonics of the root: those on the chord's notes lit, the rest a breathing veil
 *   DEEP FIELD  sines through a wavefolder whose fold changes slowly
 *   CRYSTAL     inharmonic FM (1.41 and 3.5), glassy
 *   SUPERNOVA   brass: two saws a note bending up, a filter swelling open over 3.5 s, driven
 *   STRINGS     a string machine: saws and a sub pulse, an ensemble of three chorus lines, a phaser */

enum { P_NEBULA, P_AURORA, P_RINGS, P_WIND, P_GRAVITY, P_QUASAR, P_ORGAN, P_VOYAGER, P_TAPE, P_HORIZON,
       P_BACKGROUND, P_FIELD, P_CRYSTAL, P_SUPERNOVA, P_STRINGS };
static const char *const ENG_NAME[ENG_N] = {"NEBULA", "AURORA", "RINGS", "SOLAR WIND", "GRAVITY", "QUASAR",
    "ORGAN", "VOYAGER", "TAPE", "HORIZON", "BACKGROUND", "DEEP FIELD", "CRYSTAL", "SUPERNOVA", "STRINGS"};
const char *engine_name(int32_t k) { return k >= 0 && k < ENG_N ? ENG_NAME[k] : ""; }

/* each pad: its rise and fall (s), its level, the chorus it wants, the reverb it adds */
static const struct { float att, rel, gain, chorus, verb; } PAD[ENG_N] = {
    {2.5f, 4.0f, 0.250f, 0.9f, 0.10f},    /* NEBULA */
    {3.0f, 5.0f, 0.096f, 0.6f, 0.15f},    /* AURORA */
    {2.0f, 3.0f, 1.750f, 0.4f, 0.05f},    /* RINGS */
    {2.5f, 3.0f, 0.394f, 0.5f, 0.12f},    /* SOLAR WIND */
    {1.2f, 3.0f, 0.241f, 0.3f, 0.00f},    /* GRAVITY */
    {1.0f, 2.0f, 0.500f, 0.3f, 0.10f},    /* QUASAR */
    {0.08f, 0.6f, 0.135f, 0.2f, 0.25f},    /* ORGAN */
    {1.6f, 3.0f, 0.382f, 0.7f, 0.10f},    /* VOYAGER */
    {0.6f, 2.5f, 0.560f, 0.3f, -0.05f},    /* TAPE */
    {2.5f, 2.0f, 0.150f, 0.4f, 0.20f},    /* HORIZON */
    {2.0f, 3.0f, 0.250f, 0.3f, 0.05f},    /* BACKGROUND */
    {1.5f, 3.0f, 0.280f, 0.5f, 0.00f},    /* DEEP FIELD */
    {2.5f, 4.0f, 0.255f, 0.6f, 0.15f},    /* CRYSTAL */
    {1.2f, 3.5f, 0.270f, 0.5f, 0.12f},    /* SUPERNOVA */
    {0.8f, 2.5f, 0.275f, 0.3f, 0.05f},    /* STRINGS */
};

#define NV 10                            /* the chord's notes, and the last chord's dying away */
typedef struct {
    uint32_t ph[4];                      /* its oscillators (a pad's own use) */
    float inc[4], hz, pan;
    float env, t;                        /* its level 0..1; seconds since its attack */
    float rs[6][2], rc[3];               /* resonators (RINGS: three modes; SOLAR WIND: two band-passes) */
    float wa[2][3];                      /* SOLAR WIND: its band-passes' coefficients */
    float aux;                           /* ORGAN: the chiff; GRAVITY: the bend */
    int32_t held, pad, idx;              /* held, or dying away; the pad it began on; its place in the chord */
} voice_t;

#define NG 24                            /* QUASAR's grains */
typedef struct { uint32_t ph, inc; float pos, len, amp, pan; } grain_t;
#define NH 24                            /* BACKGROUND's harmonics */
#define ENS_N 1024u                      /* STRINGS' ensemble line */

static struct pd_s {
    voice_t v[NV];
    uint32_t chord_n, chords;            /* the chord last attacked; how many so far */
    int32_t on;
    float chord_t;                       /* seconds since the last chord began */
    float ka[ENG_N], kr[ENG_N];          /* the envelopes' steps a sample */
    float bus[ENG_N][2][2];              /* each pad's bus filter: left and right, Simper's two states */
    float ba[ENG_N][3];                  /* its coefficients this block */
    float fs[6][2], fa[3][3];            /* VOYAGER's formants: three a side; their coefficients */
    float bus_drive[ENG_N];              /* GRAVITY, SUPERNOVA, TAPE: the bus driven */
    int32_t active[ENG_N];               /* voices sounding, by pad */
    float rot, rotL, rotR;               /* ORGAN's rotor */
    float wow, wowm, flut;               /* TAPE's wow and flutter (phases), the pitch this block */
    float gust, gph[2];                  /* SOLAR WIND's gusts */
    grain_t g[NG];                       /* QUASAR */
    float grain_level;                   /* the chord's envelope, for the grains */
    uint32_t hph[NH];                    /* BACKGROUND: the harmonics' phases, steps, levels gliding to their targets */
    float hinc[NH], hamp[NH], hto[NH], hdrift[NH];
    float hroot, henv;
    int16_t *ens;                        /* STRINGS: the ensemble's line (heap), its sway, the phaser */
    uint32_t ens_i;
    float ensph, apz[2][4], apy[2][4], apa;
    float hiss;
} *pd_p;   /* on the heap (the static RAM is full) */
#define pd (*pd_p)

static void svf_set(float *a, float fc, float k)
{
    float g = fm_tanf(FM_PI * fm_clampf(fc, 40.0f, 16000.0f) / SR);
    a[0] = 1.0f / (1.0f + g * (g + k));
    a[1] = g * a[0];
    a[2] = g * a[1];
}
/* Simper's state-variable filter, one sample: the low-pass (bp: the band-pass instead) */
static inline float svf(float *st, const float *a, float x, int bp)
{
    float v3 = x - st[1], v1 = a[0] * st[0] + a[1] * v3, v2 = st[1] + a[1] * st[0] + a[2] * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    return bp ? v1 : v2;
}
/* a saw, its step rounded (polyBLEP) */
static inline float saw(uint32_t ph, float inc)
{
    float t = (float)ph * (1.0f / 4294967296.0f), dt = inc * (1.0f / 4294967296.0f), x = 2.0f * t - 1.0f;
    if (t < dt) {
        t /= dt;
        x -= t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        x -= t * t + t + t + 1.0f;
    }
    return x;
}
static inline float tri(uint32_t ph) { return fm_fabsf((float)ph * (4.0f / 4294967296.0f) - 2.0f) - 1.0f; }
/* a two-pole resonator: c = 2r cos(w), r2 = r squared; output scaled to about its input's level */
static inline float reso(float *s, float c, float r2, float x)
{
    float y = x + c * s[0] - r2 * s[1];
    s[1] = s[0];
    s[0] = y;
    return y;
}
static inline float rnd01(void) { return (float)(xs2() & 0xFFFFu) * (1.0f / 65536.0f); }
static inline float sin01(float x) { return wt(SINE, (uint32_t)(x * 4294967296.0f)); }   /* sin(2 pi x), x 0..1 */

static const float VOWEL[4][3][2] = {   /* a, o, u, e: three formants, Hz and level */
    {{800, 1.0f}, {1150, 0.5f}, {2900, 0.25f}}, {{450, 1.0f}, {800, 0.4f}, {2830, 0.1f}},
    {{325, 1.0f}, {700, 0.25f}, {2700, 0.05f}}, {{400, 1.0f}, {1600, 0.35f}, {2700, 0.2f}}};

int pads_init(void)
{
    {
        uint32_t b;
        pd_p = malloc(sizeof *pd_p);
        if (!pd_p)
            return -1;
        for (b = 0; b < sizeof *pd_p; b++)
            ((uint8_t *)pd_p)[b] = 0;
    }
    uint32_t i;
    pd.ens = malloc(ENS_N * sizeof(int16_t));
    if (!pd.ens)
        return -1;
    for (i = 0; i < ENS_N; i++)
        pd.ens[i] = 0;
    return 0;
}
uint32_t pads_bytes(void) { return sizeof *pd_p + ENS_N * sizeof(int16_t); }

/* a new voice's own state */
static void voice_start(voice_t *v, int32_t pad, float hz, int32_t idx)
{
    int32_t k;
    v->held = 1;
    v->pad = pad;
    v->hz = hz;
    v->idx = idx;
    v->env = 0.0f;
    v->t = 0.0f;
    v->pan = ((float)idx / 1.5f - 1.0f) * 0.6f;   /* the chord spread across the stereo */
    v->aux = 1.0f;
    for (k = 0; k < 4; k++)
        v->ph[k] = xs2();
    for (k = 0; k < 6; k++)
        v->rs[k][0] = v->rs[k][1] = 0.0f;
    if (pad == P_RINGS) {               /* three modes, a few hertz wide */
        for (k = 0; k < 3; k++)
            v->rc[k] = 2.0f * 0.9997f * fm_cosf(FM_TWO_PI * hz * (float)(k + 1) * (1.0f + 0.003f * (float)k) / SR);
    }
    if (pad == P_WIND) {                /* the note and its octave, Q 60 */
        svf_set(v->wa[0], hz, 1.0f / 60.0f);
        svf_set(v->wa[1], hz * 2.0f, 1.0f / 60.0f);
    }
}

/* a voice's steps for the block: vibratos, bends, wows */
static void voice_steps(voice_t *v, float dt)
{
    float f = v->hz * TO32, b;
    v->t += dt;
    switch (v->pad) {
    case P_NEBULA:                       /* four saws fanned out */
        v->inc[0] = f * 0.98738f;        /* -22 cents */
        v->inc[1] = f * 0.99539f;        /* -8 */
        v->inc[2] = f * 1.00463f;        /* +8 */
        v->inc[3] = f * 1.01279f;        /* +22 */
        break;
    case P_AURORA:
        v->inc[0] = f;
        v->inc[1] = f * 1.002f;
        v->inc[2] = f * 2.001f;
        break;
    case P_GRAVITY:                      /* falling into place: two semitones over about a second */
        b = fm_exp2f(-2.0f * fm_expf(-v->t / 0.9f) / 12.0f);
        v->inc[0] = f * b * 0.996f;
        v->inc[1] = f * b * 1.004f;
        v->inc[2] = f * b * 2.006f;
        v->inc[3] = f * b * 0.5f;
        break;
    case P_ORGAN:                        /* one phase at half the note: the drawbars its multiples */
        v->inc[0] = f * 0.5f * (1.0f + 0.0004f * (float)(v->idx - 2));
        break;
    case P_VOYAGER: {                    /* three singers, each with a vibrato of their own */
        int32_t k;
        for (k = 0; k < 3; k++)
            v->inc[k] = f * (1.0f + 0.005f * fm_sinf(FM_TWO_PI * (5.0f + 0.4f * (float)k) * v->t + (float)k)
                             + 0.002f * ((float)k - 1.0f));
        break;
    }
    case P_TAPE:
        v->inc[0] = f * 0.99481f * pd.wowm;
        v->inc[1] = f * 1.00578f * pd.wowm;
        v->inc[2] = f * 0.5f * pd.wowm;
        break;
    case P_HORIZON:                      /* the swell's index grows to the peak, then settles */
        v->inc[0] = f;
        v->inc[1] = f;                   /* its modulator: 1:1 */
        v->aux = v->t < 2.5f ? 0.5f + 2.0f * (v->t / 2.5f) * (v->t / 2.5f) : 0.5f + 2.0f * fm_expf(-(v->t - 2.5f) / 0.5f);
        break;
    case P_FIELD:                        /* the fold: 1.2 .. 3.0, going round in 9 s */
        v->inc[0] = f;
        v->inc[1] = f * 1.5f;
        v->aux = 1.2f + 1.8f * (0.5f - 0.5f * fm_cosf(FM_TWO_PI * v->t / 9.0f + (float)v->idx));
        break;
    case P_CRYSTAL:
        v->inc[0] = f;
        v->inc[1] = f * 1.41f;
        v->inc[2] = f * 2.0f;
        v->inc[3] = f * 7.0f;
        v->aux = 0.9f + 0.5f * fm_sinf(FM_TWO_PI * 0.11f * v->t + (float)v->idx);
        break;
    case P_SUPERNOVA:                    /* a little bend up into each note */
        b = fm_exp2f(-0.6f * fm_expf(-v->t / 0.25f) / 12.0f);
        v->inc[0] = f * b * 0.996f;
        v->inc[1] = f * b * 1.004f;
        break;
    case P_STRINGS:
        v->inc[0] = f;
        v->inc[1] = f * 2.0f;
        v->inc[2] = f * 0.5f;
        break;
    default:
        v->inc[0] = f;
        break;
    }
}

/* a block: a new chord (the old one let go, the new attacked), the pads' slow movements, their filters */
static void pads_block(uint32_t frames, int32_t pad, int32_t on, const volatile float *notes, uint32_t chord_n)
{
    int32_t k, j;
    float dt = (float)frames / SR, sunk = fm_clampf(e.cutoff / 1200.0f, 0.6f, 1.8f);
    if (!pd.ka[0])
        for (k = 0; k < ENG_N; k++) {
            pd.ka[k] = 1.0f - fm_expf(-3.0f / (PAD[k].att * SR));
            pd.kr[k] = fm_expf(-4.0f / (PAD[k].rel * SR));
        }
    if (on && (!pd.on || pd.chord_n != chord_n)) {
        for (j = 0; j < NV; j++)         /* the old chord let go */
            pd.v[j].held = 0;
        for (k = 0; k < NCHORD; k++) {   /* the new one: a voice each, the free or the quietest */
            int32_t q = 0;
            if (notes[k] <= 0.0f)
                continue;
            for (j = 1; j < NV; j++)
                if (!pd.v[j].held && (pd.v[q].held || pd.v[j].env < pd.v[q].env))
                    q = j;
            voice_start(&pd.v[q], pad, notes[k], k);
        }
        pd.chord_n = chord_n;
        pd.chords++;
        pd.chord_t = 0.0f;
        if (pad == P_BACKGROUND) {       /* its harmonics: lit on the chord's notes, a veil elsewhere */
            float root = notes[0] * 0.25f;
            int32_t pcs[NCHORD];
            for (k = 0; k < NCHORD; k++)
                pcs[k] = ((int32_t)(12.0f * fm_log2f(notes[k] / root) + 120.5f)) % 12;
            pd.hroot = root;
            for (j = 0; j < NH; j++) {
                float st = 12.0f * fm_log2f((float)(j + 1));
                int32_t pc = (int32_t)(st + 0.5f) % 12, lit = 0;
                if (fm_fabsf(st - (float)(int32_t)(st + 0.5f)) < 0.2f)
                    for (k = 0; k < NCHORD; k++)
                        lit |= pcs[k] == pc;
                pd.hto[j] = (lit ? 1.0f : 0.08f) / fm_powf((float)(j + 1), 0.7f);
                pd.hinc[j] = root * (float)(j + 1) * (1.0f + 0.0003f * fm_sinf((float)j)) * TO32;
            }
        }
    }
    if (!on && pd.on)
        for (j = 0; j < NV; j++)
            pd.v[j].held = 0;
    pd.on = on;
    pd.chord_t += dt;
    /* the slow movements: the rotor (0.8 Hz), the tape (0.5 Hz, 18 cents; flutter 6.3 Hz), the gusts */
    pd.rot += 0.8f * dt;
    pd.rot -= (float)(int32_t)pd.rot;
    pd.rotL = 1.0f + 0.16f * sin01(pd.rot);
    pd.rotR = 1.0f - 0.16f * sin01(pd.rot);
    pd.wow += 0.5f * dt;
    pd.wow -= (float)(int32_t)pd.wow;
    pd.flut += 6.3f * dt;
    pd.flut -= (float)(int32_t)pd.flut;
    pd.wowm = fm_exp2f((18.0f * sin01(pd.wow) + 2.0f * sin01(pd.flut)) / 1200.0f);
    pd.gph[0] += 0.07f * dt;
    pd.gph[1] += 0.023f * dt;
    pd.gph[0] -= (float)(int32_t)pd.gph[0];
    pd.gph[1] -= (float)(int32_t)pd.gph[1];
    pd.gust = 0.55f + 0.45f * sin01(pd.gph[0]) * sin01(pd.gph[1] + 0.16f) * (0.6f + 0.6f * e.drift);
    for (k = 0; k < ENG_N; k++)
        pd.active[k] = 0;
    pd.grain_level = 0.0f;
    pd.henv = 0.0f;
    for (j = 0; j < NV; j++) {
        voice_t *v = &pd.v[j];
        if (!v->held && v->env < 0.0005f)
            continue;
        pd.active[v->pad] = 1;
        voice_steps(v, dt);
        if (v->pad == P_QUASAR && v->env > pd.grain_level)
            pd.grain_level = v->env;
        if (v->pad == P_BACKGROUND && v->env > pd.henv)
            pd.henv = v->env;
    }
    /* the buses' filters this block: the sun opens most of them a little */
    {
        float rise = fm_clampf(pd.chord_t / 8.0f, 0.0f, 1.0f), swell = fm_clampf(pd.chord_t / 3.5f, 0.0f, 1.0f);
        rise = 0.5f - 0.5f * fm_cosf(FM_PI * rise);
        svf_set(pd.ba[P_NEBULA], (500.0f + 3500.0f * rise) * sunk, 1.0f / 0.9f);
        svf_set(pd.ba[P_GRAVITY], (380.0f + 260.0f * (0.5f + 0.5f * fm_sinf(FM_TWO_PI * 0.16f * pd.chord_t))) * sunk,
                1.0f / 1.6f);
        svf_set(pd.ba[P_TAPE], 1300.0f * sunk, 1.0f / 0.8f);
        svf_set(pd.ba[P_SUPERNOVA], (300.0f + 3200.0f * swell * fm_sqrtf(swell)
                                     * (0.85f + 0.15f * fm_sinf(FM_TWO_PI * 5.5f * pd.chord_t))) * sunk, 1.0f / 1.4f);
        svf_set(pd.ba[P_STRINGS], 2600.0f * sunk, 1.0f / 0.7f);
        svf_set(pd.ba[P_WIND], 2500.0f + 1500.0f * pd.gust, 1.0f / 0.7f);   /* the air above the tones */
        if (pd.active[P_VOYAGER]) {      /* the vowel moves across the chord to the next one's */
            const float (*a)[2] = VOWEL[pd.chords % 4u], (*b)[2] = VOWEL[(pd.chords + 1u) % 4u];
            float m = fm_clampf(pd.chord_t / 10.0f, 0.0f, 1.0f);
            for (k = 0; k < 3; k++)
                svf_set(pd.fa[k], a[k][0] + (b[k][0] - a[k][0]) * m, 1.0f / 8.0f);
        }
        if (pd.active[P_BACKGROUND])     /* each harmonic glides to its weight and breathes */
            for (j = 0; j < NH; j++) {
                pd.hamp[j] += (pd.hto[j] - pd.hamp[j]) * fm_minf(1.0f, dt * 1.6f);
                pd.hdrift[j] = 0.6f + 0.4f * fm_sinf(FM_TWO_PI * (0.02f + 0.013f * (float)j) * pd.chord_t + (float)j);
            }
        pd.ensph += 0.6f * dt;
        pd.ensph -= (float)(int32_t)pd.ensph;
        pd.apa = -(0.5f + 0.4f * fm_sinf(FM_TWO_PI * 0.08f * pd.chord_t));
    }
}

/* QUASAR: a new grain now and then (70 a second at full level), on one of the held notes */
static inline void grain_spawn(void)
{
    int32_t k, q = -1, j;
    float hz;
    if (rnd01() > 70.0f / SR * pd.grain_level * 1.0f)
        return;
    for (k = 0; k < NG; k++)
        if (pd.g[k].pos >= pd.g[k].len) {
            q = k;
            break;
        }
    if (q < 0)
        return;
    j = (int32_t)(xs2() % NV);
    for (k = 0; k < NV && !(pd.v[(j + k) % NV].held && pd.v[(j + k) % NV].pad == P_QUASAR); k++)
        ;
    if (k == NV)
        return;
    hz = pd.v[(j + k) % NV].hz * (rnd01() < 0.4f ? 2.0f : 1.0f) * (1.0f + 0.0023f * (rnd01() - 0.5f) * 2.0f);
    pd.g[q].ph = xs2();
    pd.g[q].inc = (uint32_t)(hz * TO32);
    pd.g[q].pos = 0.0f;
    pd.g[q].len = 2600.0f * (0.5f + rnd01());
    pd.g[q].amp = 0.6f + 0.4f * rnd01();
    pd.g[q].pan = rnd01() * 2.0f - 1.0f;
}

/* a sample of the pads, added to l and r; w a white noise sample */
static inline void pads_tick(float *l, float *r, float w, float pink)
{
    float bl[ENG_N], br[ENG_N];
    int32_t j, k;
    for (k = 0; k < ENG_N; k++)
        bl[k] = br[k] = 0.0f;
    for (j = 0; j < NV; j++) {
        voice_t *v = &pd.v[j];
        float x = 0.0f, y = 0.0f, pl, pr;
        uint32_t p;
        int32_t pad = v->pad;
        if (v->held)
            v->env += (1.0f - v->env) * pd.ka[pad];
        else if (v->env < 0.0005f)
            continue;
        else
            v->env *= pd.kr[pad];
        switch (pad) {
        case P_NEBULA:                   /* two saws a side */
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            v->ph[3] += (uint32_t)v->inc[3];
            x = saw(v->ph[0], v->inc[0]) * 0.7f + saw(v->ph[2], v->inc[2]);
            y = saw(v->ph[1], v->inc[1]) + saw(v->ph[3], v->inc[3]) * 0.7f;
            break;
        case P_AURORA:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            x = y = wt(SINE, v->ph[0]) + 0.5f * tri(v->ph[1]) + 0.25f * wt(SINE, v->ph[2]);
            break;
        case P_RINGS:                    /* the bow: noise into three resonant modes */
            x = w * 0.5f;
            x = reso(v->rs[0], v->rc[0], 0.99940009f, x) + 0.5f * reso(v->rs[1], v->rc[1], 0.99940009f, x)
                + 0.25f * reso(v->rs[2], v->rc[2], 0.99940009f, x);
            x = y = x * 0.0006f;
            break;
        case P_WIND:                     /* the wind through two narrow band-passes: the note, its octave */
            x = y = (svf(v->rs[0], v->wa[0], pink, 1) + 0.5f * svf(v->rs[1], v->wa[1], pink, 1)) * pd.gust;
            break;
        case P_GRAVITY:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            x = y = saw(v->ph[0], v->inc[0]) + saw(v->ph[1], v->inc[1]) + 0.6f * saw(v->ph[2], v->inc[2]);
            if (v->idx == 0) {           /* the lowest note carries a sub */
                v->ph[3] += (uint32_t)v->inc[3];
                x = y = x + 1.2f * wt(SINE, v->ph[3]);
            }
            break;
        case P_QUASAR:                   /* the voices only hold the notes: the grains sing them */
            break;
        case P_ORGAN:                    /* 16' 8' 4' 2 2/3' 2' 1 1/3' 1', and the pipes' chiff */
            v->ph[0] += (uint32_t)v->inc[0];
            p = v->ph[0];
            x = wt(SINE, p) * 0.5f + wt(SINE, p * 2u) + wt(SINE, p * 4u) * 0.7f + wt(SINE, p * 6u) * 0.35f
                + wt(SINE, p * 8u) * 0.4f + wt(SINE, p * 12u) * 0.18f + wt(SINE, p * 16u) * 0.15f;
            if (v->aux > 0.001f) {
                x += w * v->aux * 0.25f;
                v->aux *= 0.9995f;       /* ~50 ms */
            }
            y = x;
            break;
        case P_VOYAGER:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            x = saw(v->ph[0], v->inc[0]) + saw(v->ph[2], v->inc[2]) * 0.6f;
            y = saw(v->ph[1], v->inc[1]) + saw(v->ph[2], v->inc[2]) * 0.6f;
            break;
        case P_TAPE:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            x = y = saw(v->ph[0], v->inc[0]) + saw(v->ph[1], v->inc[1]) + 0.5f * tri(v->ph[2]);
            break;
        case P_HORIZON: {                /* the swell: a cubic rise to the peak, then the held chord */
            float sw = v->t < 2.5f ? (v->t / 2.5f) * (v->t / 2.5f) * (v->t / 2.5f) : 0.45f + 0.55f * fm_expf(-(v->t - 2.5f) / 0.5f);
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            x = y = wt(SINE, v->ph[0] + (uint32_t)(int32_t)(wt(SINE, v->ph[1]) * v->aux * 683565275.0f)) * sw * 1.6f;
            break;
        }
        case P_FIELD: {                  /* the fold: sin of the sum, folded */
            float s;
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            s = wt(SINE, v->ph[0]) + 0.3f * wt(SINE, v->ph[1]);
            x = y = wt(SINE, (uint32_t)(int32_t)(s * v->aux * 683565275.0f));
            break;
        }
        case P_CRYSTAL:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            v->ph[3] += (uint32_t)v->inc[3];
            x = y = wt(SINE, v->ph[0] + (uint32_t)(int32_t)(wt(SINE, v->ph[1]) * v->aux * 683565275.0f))
                    + 0.4f * wt(SINE, v->ph[2] + (uint32_t)(int32_t)(wt(SINE, v->ph[3]) * 0.4f * 683565275.0f));
            break;
        case P_SUPERNOVA:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            x = y = saw(v->ph[0], v->inc[0]) + saw(v->ph[1], v->inc[1]);
            break;
        case P_STRINGS:
            v->ph[0] += (uint32_t)v->inc[0];
            v->ph[1] += (uint32_t)v->inc[1];
            v->ph[2] += (uint32_t)v->inc[2];
            x = y = saw(v->ph[0], v->inc[0]) + 0.6f * saw(v->ph[1], v->inc[1])
                    + 0.25f * (v->ph[2] < 0x80000000u ? 1.0f : -1.0f);
            break;
        default:                         /* BACKGROUND: the voices hold the envelope; the harmonics sing */
            break;
        }
        pl = 0.7f - 0.3f * v->pan;       /* the note's place in the stereo */
        pr = 0.7f + 0.3f * v->pan;
        bl[pad] += x * v->env * pl;
        br[pad] += y * v->env * pr;
    }
    /* the buses */
    if (pd.active[P_NEBULA]) {
        bl[P_NEBULA] = svf(pd.bus[P_NEBULA][0], pd.ba[P_NEBULA], bl[P_NEBULA], 0);
        br[P_NEBULA] = svf(pd.bus[P_NEBULA][1], pd.ba[P_NEBULA], br[P_NEBULA], 0);
    }
    if (pd.active[P_GRAVITY]) {
        bl[P_GRAVITY] = sat(svf(pd.bus[P_GRAVITY][0], pd.ba[P_GRAVITY], bl[P_GRAVITY], 0) * 1.8f);
        br[P_GRAVITY] = sat(svf(pd.bus[P_GRAVITY][1], pd.ba[P_GRAVITY], br[P_GRAVITY], 0) * 1.8f);
    }
    if (pd.active[P_TAPE]) {             /* the tape: low-passed, saturated, hissing */
        float h = (w - pd.hiss) * 0.004f;
        pd.hiss = w;
        bl[P_TAPE] = sat(svf(pd.bus[P_TAPE][0], pd.ba[P_TAPE], bl[P_TAPE], 0) * 2.2f) * 0.45f + h;
        br[P_TAPE] = sat(svf(pd.bus[P_TAPE][1], pd.ba[P_TAPE], br[P_TAPE], 0) * 2.2f) * 0.45f + h;
    }
    if (pd.active[P_SUPERNOVA]) {
        bl[P_SUPERNOVA] = sat(svf(pd.bus[P_SUPERNOVA][0], pd.ba[P_SUPERNOVA], bl[P_SUPERNOVA], 0) * 1.3f);
        br[P_SUPERNOVA] = sat(svf(pd.bus[P_SUPERNOVA][1], pd.ba[P_SUPERNOVA], br[P_SUPERNOVA], 0) * 1.3f);
    }
    if (pd.active[P_WIND]) {             /* and the air over it */
        float air = svf(pd.bus[P_WIND][0], pd.ba[P_WIND], pink, 1) * 0.05f * pd.gust;
        bl[P_WIND] += air;
        br[P_WIND] += svf(pd.bus[P_WIND][1], pd.ba[P_WIND], -pink, 1) * 0.05f * pd.gust;
    }
    if (pd.active[P_VOYAGER]) {          /* "a", "o", "u", "e": three formants a side, and a breath */
        float xl = bl[P_VOYAGER] + w * 0.01f, xr = br[P_VOYAGER] + w * 0.01f;
        const float (*a)[2] = VOWEL[pd.chords % 4u];
        bl[P_VOYAGER] = (svf(pd.fs[0], pd.fa[0], xl, 1) * a[0][1] + svf(pd.fs[1], pd.fa[1], xl, 1) * a[1][1]
                         + svf(pd.fs[2], pd.fa[2], xl, 1) * a[2][1]) * 0.25f;
        br[P_VOYAGER] = (svf(pd.fs[3], pd.fa[0], xr, 1) * a[0][1] + svf(pd.fs[4], pd.fa[1], xr, 1) * a[1][1]
                         + svf(pd.fs[5], pd.fa[2], xr, 1) * a[2][1]) * 0.25f;
    }
    if (pd.active[P_ORGAN]) {            /* the organ turns between the sides */
        bl[P_ORGAN] *= pd.rotL;
        br[P_ORGAN] *= pd.rotR;
    }
    if (pd.active[P_STRINGS]) {          /* the ensemble: three lines a third of a turn apart; then the phaser */
        float m = svf(pd.bus[P_STRINGS][0], pd.ba[P_STRINGS], (bl[P_STRINGS] + br[P_STRINGS]) * 0.5f, 0), t3[3], o[2];
        int32_t c, s;
        pd.ens[pd.ens_i] = (int16_t)(fm_clampf(m, -1.0f, 1.0f) * 32767.0f);
        for (c = 0; c < 3; c++) {
            float d = (8.0f + 3.0f * sin01(pd.ensph + 0.3333f * (float)c)) * (SR / 1000.0f), fr;
            uint32_t di = (uint32_t)d;
            fr = d - (float)di;
            t3[c] = ((float)pd.ens[(pd.ens_i - di) & (ENS_N - 1u)] * (1.0f - fr)
                     + (float)pd.ens[(pd.ens_i - di - 1u) & (ENS_N - 1u)] * fr) * (1.0f / 32768.0f);
        }
        pd.ens_i = (pd.ens_i + 1u) & (ENS_N - 1u);
        o[0] = m + 0.5f * (t3[0] + t3[2]);
        o[1] = m + 0.5f * (t3[1] + t3[2]);
        for (c = 0; c < 2; c++) {
            float y = o[c];
            for (s = 0; s < 4; s++) {    /* first-order all-passes, the pole sweeping */
                float out = pd.apa * y + pd.apz[c][s] - pd.apa * pd.apy[c][s];
                pd.apz[c][s] = y;
                pd.apy[c][s] = out;
                y = out;
            }
            o[c] = (o[c] + y) * 0.5f;
        }
        bl[P_STRINGS] = o[0];
        br[P_STRINGS] = o[1];
    }
    if (pd.grain_level > 0.0005f) {      /* QUASAR's grains, each in its own place */
        grain_spawn();
        for (k = 0; k < NG; k++) {
            grain_t *g = &pd.g[k];
            float s;
            if (g->pos >= g->len)
                continue;
            g->ph += g->inc;
            s = 0.5f - 0.5f * wt(SINE, (uint32_t)(g->pos / g->len * 4294967296.0f) + 0x40000000u);   /* sin^2 */
            s *= wt(SINE, g->ph) * g->amp * pd.grain_level;
            bl[P_QUASAR] += s * (0.7f - 0.3f * g->pan);
            br[P_QUASAR] += s * (0.7f + 0.3f * g->pan);
            g->pos += 1.0f;
        }
    }
    if (pd.henv > 0.0005f) {             /* BACKGROUND's harmonics */
        float sl = 0.0f, sr = 0.0f;
        for (k = 0; k < NH; k++) {
            float s;
            pd.hph[k] += (uint32_t)pd.hinc[k];
            s = wt(SINE, pd.hph[k]) * pd.hamp[k] * pd.hdrift[k];
            if (k & 1)
                sr += s;
            else
                sl += s;
        }
        bl[P_BACKGROUND] += (sl + sr * 0.4f) * pd.henv;
        br[P_BACKGROUND] += (sr + sl * 0.4f) * pd.henv;
    }
    for (k = 0; k < ENG_N; k++)
        if (pd.active[k] || (k == P_QUASAR && pd.grain_level > 0.0005f)) {
            *l += bl[k] * PAD[k].gain;
            *r += br[k] * PAD[k].gain;
        }
}
