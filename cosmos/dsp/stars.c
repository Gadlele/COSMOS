/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's star voices (included by engine.c, in its scope): what the star sequencer plays. The moon picks one of
 * eight (the app's choice: sky_t.voice); a geomagnetic storm brings the rhythms (sky_t.rhythm):
 *   DROPS     a sine and its octave, dying away: pure drops of light (new moon)
 *   KALIMBA   a metal tine and its two inharmonic partials, a wooden box
 *   HARP      a plucked string (Karplus-Strong) and its octave
 *   CELESTA   a soft FM bell, a low shadow under it
 *   CHIMES    a tube: four inharmonic modes, long (full moon); the app adds the neighbours a gust swings
 *   GLASS     glassy FM (3.53) and a partial at 2.76
 *   MUSIC BOX an FM tine on the worn tape's wow
 *   FLUTE     a pipe: a sine, its octave, breath
 * and the rhythms' own:
 *   PULSE     (PULSAR, Kp 5 and up) an analog pluck, held to the pad's sixteenths; the pad gated, a beat under it
 *   SEQ       (STARGATE, Kp 4) a resonant analog voice, its filter snapping, a sixteenth each (the app's clock)
 * A voice is placed in the stereo where its star is on the screen. */

static const char *const VOICE_NAME[NVOICE] = {"DROPS", "KALIMBA", "HARP", "CELESTA", "CHIMES", "GLASS",
                                               "MUSIC BOX", "FLUTE"};
const char *voice_name(int32_t k) { return k >= 0 && k < NVOICE ? VOICE_NAME[k] : ""; }

#define NS 12                            /* star voices at once */
#define KS_N 256u                        /* a harp string's line: notes from 175 Hz (lower ones an octave up) */
typedef struct {
    int32_t type, wait;                  /* its voice; samples before it sounds */
    uint32_t ph[3], inc[3];
    float t, amp, pan;                   /* samples since its start, its level, its place */
    float s[2][2], a[3];                 /* a resonator (KALIMBA's box) or a filter (PULSE, SEQ) */
    float ks_lp, ks_len;                 /* HARP: the loop's low-pass, the string's length in samples */
    float d[4], k[4];                    /* its envelopes now and their decay a sample: partials, the FM index */
    float life;                          /* samples until it is let go */
    float vel;
    int32_t on;
} star_t;

static struct st_s {
    star_t v[NS];
    int16_t *ks;                         /* the harp strings' lines (heap), KS_N a voice */
    uint32_t ks_w[NS];
    uint32_t step_n, step_len;           /* PULSAR's sixteenths: the samples into this one, its length */
    uint32_t step_i;                     /* which sixteenth of the bar */
    float gate, gdec, gmix;              /* the pad's gate now, its fall a sample, how much of it */
    float thump_t, thump_ph, tf, ta, tkf, tka;   /* the beat: its time, phase, pitch and level envelopes */
    float seq_lfo;                       /* STARGATE's filter sweep, 0..1 over 16 s */
} *st_p;   /* on the heap (the static RAM is full) */
#define st (*st_p)

static float decay(float tau);
int stars_init(void)
{
    {
        uint32_t b;
        st_p = malloc(sizeof *st_p);
        if (!st_p)
            return -1;
        for (b = 0; b < sizeof *st_p; b++)
            ((uint8_t *)st_p)[b] = 0;
    }
    uint32_t i;
    st.ks = malloc(NS * KS_N * sizeof(int16_t));
    if (!st.ks)
        return -1;
    for (i = 0; i < NS * KS_N; i++)
        st.ks[i] = 0;
    st.step_len = (uint32_t)(SR * 60.0f / 104.0f / 4.0f);
    st.gdec = fm_expf(-3.2f / (float)st.step_len);
    st.tkf = decay(0.03f);
    st.tka = decay(0.22f);
    st.thump_t = 1.0f;
    return 0;
}
uint32_t stars_bytes(void) { return sizeof *st_p + NS * KS_N * sizeof(int16_t); }

/* the arpeggiator's notes, from the main loop: a small queue the render takes from at each block */
#define NQ 16
static volatile struct { float hz, amp, pan; int32_t voice; uint32_t wait; } note_q[NQ];
static volatile uint32_t q_w, q_r;

void eng_note(float hz, float amp, float pan, int32_t voice, uint32_t delay_ms)
{
    uint32_t w = q_w;
    if (w - q_r >= NQ)
        return;                          /* full: the note is dropped */
    note_q[w % NQ].hz = hz;
    note_q[w % NQ].amp = amp;
    note_q[w % NQ].pan = pan;
    note_q[w % NQ].voice = voice;
    note_q[w % NQ].wait = delay_ms;
    q_w = w + 1u;
}

static float decay(float tau) { return fm_expf(-1.0f / (tau * SR)); }
static void envs(star_t *v, float t0, float t1, float t2, float ti, float life)
{
    v->d[0] = v->d[1] = v->d[2] = v->d[3] = 1.0f;
    v->k[0] = decay(t0);
    v->k[1] = decay(t1);
    v->k[2] = decay(t2);
    v->k[3] = decay(ti);
    v->life = life * SR;
}

static void star_start(float hz, float amp, float pan, int32_t type, int32_t wait)
{
    int32_t i, q = 0;
    star_t *v;
    for (i = 1; i < NS; i++)             /* a free one, or the quietest */
        if (!st.v[q].on ? 0 : (!st.v[i].on || st.v[i].amp < st.v[q].amp))
            q = i;
    v = &st.v[q];
    v->on = 1;
    v->type = type;
    v->wait = wait;
    v->t = 0.0f;
    v->amp = amp;
    v->vel = amp;
    v->pan = fm_clampf(pan, -1.0f, 1.0f);
    v->ph[0] = v->ph[1] = v->ph[2] = 0u;
    v->s[0][0] = v->s[0][1] = v->s[1][0] = v->s[1][1] = 0.0f;
    switch (type) {
    case V_KALIMBA:                      /* an octave down: a tine is low */
        hz *= 0.5f;
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 5.4f * TO32);
        v->inc[2] = (uint32_t)(hz * 9.1f * TO32);
        v->a[0] = 2.0f * 0.995f * fm_cosf(FM_TWO_PI * 420.0f / SR);
        envs(v, 1.6f, 0.15f, 0.06f, 1.0f, 4.0f);
        break;
    case V_HARP: {                       /* an octave down; the string filled with a burst of noise */
        uint32_t k, n;
        int16_t *b = st.ks + (uint32_t)q * KS_N;
        hz *= 0.5f;
        while (SR / hz > (float)KS_N - 2.0f)
            hz *= 2.0f;
        v->ks_len = fm_clampf(SR / hz, 2.0f, (float)KS_N - 2.0f);
        n = (uint32_t)v->ks_len + 2u;
        {   /* a burst of noise, softened: a finger, not a plectrum */
            float sm = 0.0f;
            for (k = 0; k < KS_N; k++) {
                sm += 0.5f * (rnd_pm() - sm);
                b[k] = (int16_t)(k < n ? sm * 28000.0f : 0.0f);
            }
        }
        st.ks_w[q] = 0;
        v->ks_lp = 0.0f;
        v->inc[1] = (uint32_t)(hz * 2.0f * TO32);
        envs(v, 1.2f, 1.0f, 1.0f, 1.0f, 5.0f);
        break;
    }
    case V_CELESTA:
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 4.0f * TO32);
        v->inc[2] = (uint32_t)(hz * 0.5f * TO32);
        envs(v, 1.2f, 0.8f, 1.0f, 0.2f, 4.0f);
        break;
    case V_CHIMES:
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 2.76f * TO32);
        v->inc[2] = (uint32_t)(hz * 5.40f * TO32);
        envs(v, 3.5f, 2.0f, 1.0f, 1.0f, 7.0f);
        break;
    case V_GLASS:
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 3.53f * TO32);
        v->inc[2] = (uint32_t)(hz * 2.76f * TO32);
        envs(v, 1.4f, 0.5f, 1.0f, 0.08f, 4.5f);
        break;
    case V_MUSICBOX:
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 5.0f * TO32);
        envs(v, 1.2f, 1.0f, 1.0f, 0.15f, 4.0f);
        break;
    case V_FLUTE:
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 2.0f * TO32);
        svf_set(v->a, hz, 1.0f / 4.0f);  /* the breath, around the note */
        envs(v, 1.0f, 1.0f, 1.0f, 1.0f, 0.66f);
        break;
    case V_PULSE:
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 1.005f * TO32);
        envs(v, 0.25f, 1.0f, 1.0f, 1.0f, 1.2f);
        break;
    case V_SEQ:                          /* a sixteenth of the sequence: an octave down */
        hz *= 0.5f;
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 1.006f * TO32);
        v->inc[2] = (uint32_t)(hz * 0.5f * TO32);
        envs(v, 0.12f, 1.0f, 1.0f, 1.0f, 0.6f);
        break;
    default:                             /* DROPS */
        v->inc[0] = (uint32_t)(hz * TO32);
        v->inc[1] = (uint32_t)(hz * 2.0f * TO32);
        envs(v, 0.9f, 0.25f, 1.0f, 1.0f, 3.5f);
        break;
    }
}

/* a block: the queue (PULSAR holds its notes to the next sixteenth), the rhythms' clocks */
static void stars_block(uint32_t frames, int32_t voice, int32_t rhythm, int32_t on)
{
    uint32_t to_step = st.step_len - st.step_n;
    while (q_r != q_w) {
        uint32_t r = q_r;
        int32_t type = note_q[r % NQ].voice >= 0 ? note_q[r % NQ].voice : voice;
        int32_t wait = (int32_t)(note_q[r % NQ].wait * (uint32_t)(SR / 1000.0f));
        if (rhythm == 2 && note_q[r % NQ].voice < 0) {
            type = V_PULSE;              /* on the grid */
            wait = (int32_t)to_step;
        }
        if (on)
            star_start(note_q[r % NQ].hz, note_q[r % NQ].amp, note_q[r % NQ].pan, type, wait);
        q_r = r + 1u;
    }
    st.gmix += ((rhythm == 2 ? 1.0f : 0.0f) - st.gmix) * fm_minf(1.0f, (float)frames / (2.0f * SR));
    st.seq_lfo += (float)frames / (16.0f * SR);
    st.seq_lfo -= (float)(int32_t)st.seq_lfo;
}

/* PULSAR's pattern: each sixteenth's level, a beat on the quarters */
static const float PULSE_PAT[16] = {1, .3f, .7f, .3f, 1, .3f, .8f, .5f, 1, .3f, .7f, .3f, 1, .6f, .4f, .9f};

/* a sample of the rhythm: the pad's gate (returned), the beat added to l and r */
static inline float rhythm_tick(float *l, float *r)
{
    if (++st.step_n >= st.step_len) {
        st.step_n = 0;
        st.step_i = (st.step_i + 1u) & 15u;
        st.gate = PULSE_PAT[st.step_i];
        if (!(st.step_i & 3u)) {         /* a beat on each quarter */
            st.thump_t = 0.0f;
            st.tf = st.ta = 1.0f;
        }
    } else {
        st.gate *= st.gdec;
    }
    if (st.gmix < 0.001f)
        return 1.0f;
    if (st.thump_t < 0.6f) {             /* a soft low beat: 105 Hz falling to 45 */
        float b;
        st.thump_ph += (45.0f + 60.0f * st.tf) * (1.0f / SR);
        st.thump_ph -= (float)(int32_t)st.thump_ph;
        b = sin01(st.thump_ph) * st.ta * 0.35f * st.gmix;
        st.tf *= st.tkf;
        st.ta *= st.tka;
        *l += b;
        *r += b;
        st.thump_t += 1.0f / SR;
    }
    return 1.0f - st.gmix + st.gmix * st.gate * fm_minf(1.0f, (float)st.step_n * (1.0f / 176.0f));   /* 4 ms in */
}

/* each voice's level, so that they sound alike: DROPS .. FLUTE, then PULSE and SEQ */
static const float VGAIN[NVOICE + 2] = {1.0f, 0.5f, 1.9f, 0.95f, 0.95f, 0.95f, 1.1f, 1.1f, 0.8f, 1.2f};

/* a sample of the star voices, added to l and r; w a white noise sample */
static inline void stars_tick(float *l, float *r, float w)
{
    int32_t i;
    for (i = 0; i < NS; i++) {
        star_t *v = &st.v[i];
        float x = 0.0f, a;
        if (!v->on)
            continue;
        if (v->wait > 0) {
            v->wait--;
            continue;
        }
        a = v->t < 88.0f ? v->t * (1.0f / 88.0f) : 1.0f;   /* 2 ms in: no click */
        switch (v->type) {
        case V_KALIMBA:
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            v->ph[2] += v->inc[2];
            x = wt(SINE, v->ph[0]) * v->d[0] + 0.35f * wt(SINE, v->ph[1]) * v->d[1] + 0.15f * wt(SINE, v->ph[2]) * v->d[2];
            x += reso(v->s[0], v->a[0], 0.990025f, x) * 0.002f;   /* the wooden box */
            break;
        case V_HARP: {
            int16_t *b = st.ks + (uint32_t)i * KS_N;
            float y;
            uint32_t n = (uint32_t)v->ks_len + 2u, wi = st.ks_w[i];
            float fr = v->ks_len - (float)(n - 2u);
            y = ((float)b[wi % n] + (float)(b[(wi + 1u) % n] - b[wi % n]) * fr) * (1.0f / 28000.0f);
            v->ks_lp += 0.45f * (y - v->ks_lp);
            b[wi % n] = (int16_t)(fm_clampf(v->ks_lp * 0.9993f, -1.15f, 1.15f) * 28000.0f);
            st.ks_w[i] = wi + 1u;
            v->ph[1] += v->inc[1];
            v->s[0][0] += 0.5f * (y - v->s[0][0]);   /* the body takes the edge off */
            x = v->s[0][0] * 0.8f + 0.3f * wt(SINE, v->ph[1]) * v->d[0];
            break;
        }
        case V_CELESTA:
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            v->ph[2] += v->inc[2];
            x = wt(SINE, v->ph[0] + (uint32_t)(int32_t)(wt(SINE, v->ph[1]) * 1.5f * v->d[3] * 683565275.0f)) * v->d[0]
                + 0.3f * wt(SINE, v->ph[2]) * v->d[1];
            break;
        case V_CHIMES:
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            v->ph[2] += v->inc[2];
            x = (wt(SINE, v->ph[0]) * v->d[0] + 0.5f * wt(SINE, v->ph[1]) * v->d[1] + 0.25f * wt(SINE, v->ph[2]) * v->d[2])
                * 0.7f;
            break;
        case V_GLASS:
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            v->ph[2] += v->inc[2];
            x = wt(SINE, v->ph[0] + (uint32_t)(int32_t)(wt(SINE, v->ph[1]) * 1.4f * v->d[3] * 683565275.0f)) * v->d[0]
                + 0.3f * wt(SINE, v->ph[2]) * v->d[1];
            break;
        case V_MUSICBOX:
            v->ph[0] += (uint32_t)((float)v->inc[0] * pd.wowm);
            v->ph[1] += (uint32_t)((float)v->inc[1] * pd.wowm);
            x = wt(SINE, v->ph[0] + (uint32_t)(int32_t)(wt(SINE, v->ph[1]) * 1.2f * v->d[3] * 683565275.0f)) * v->d[0];
            break;
        case V_FLUTE: {
            float env = v->t < 1764.0f ? v->t * (1.0f / 1764.0f) : v->t < 15435.0f ? 1.0f
                        : fm_maxf(0.0f, 1.0f - (v->t - 15435.0f) * (1.0f / 13230.0f));
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            x = (wt(SINE, v->ph[0]) + 0.2f * wt(SINE, v->ph[1]) + 0.15f * svf(v->s[0], v->a, w, 1)) * env;
            break;
        }
        case V_PULSE:                    /* an analog pluck: two saws through a closing low-pass */
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            if (((uint32_t)v->t & 31u) == 0u)
                svf_set(v->a, 300.0f + 2500.0f * v->d[0] * v->d[0], 0.5f);
            x = svf(v->s[0], v->a, saw(v->ph[0], (float)v->inc[0]) + saw(v->ph[1], (float)v->inc[1]), 0) * v->d[0] * 0.6f;
            break;
        case V_SEQ: {                    /* the sequence's voice: its filter snapping, driven */
            float o;
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            v->ph[2] += v->inc[2];
            if (((uint32_t)v->t & 31u) == 0u) {
                float snap = v->d[0] * v->d[0] * v->d[0];   /* (0.12 s)^1.5: about 0.08 s */
                svf_set(v->a, fm_minf(200.0f + (900.0f + 3500.0f * (0.5f + 0.5f * sin01(st.seq_lfo))) * snap
                                      * (0.5f + v->vel * 2.0f), 9000.0f), 0.25f);
            }
            o = saw(v->ph[0], (float)v->inc[0]) + 0.7f * saw(v->ph[1], (float)v->inc[1])
                + 0.5f * (v->ph[2] < 0x80000000u ? 1.0f : -1.0f);
            x = sat(svf(v->s[0], v->a, o * 0.5f, 0) * 1.5f) * v->d[0] * 0.8f;
            break;
        }
        default:                         /* DROPS */
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            x = wt(SINE, v->ph[0]) * v->d[0] + 0.3f * wt(SINE, v->ph[1]) * v->d[1];
            break;
        }
        v->d[0] *= v->k[0];
        v->d[1] *= v->k[1];
        v->d[2] *= v->k[2];
        v->d[3] *= v->k[3];
        x *= v->amp * a * VGAIN[v->type];
        *l += x * (0.7f - 0.4f * v->pan);
        *r += x * (0.7f + 0.4f * v->pan);
        v->t += 1.0f;
        if (v->t > v->life)
            v->on = 0;
    }
}
