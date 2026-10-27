/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's sound: one drone underneath (a soft FM pad on the chord's root and fifth, a sub, noise, its filter), a pad
 * on top playing the whole chord, each chord attacked anew (sky_t.engine: fifteen, pads.c), the star sequencer's
 * voices (sky_t.voice: eight, and the rhythms', stars.c), a chorus, a delay and a reverb, shaped by the five sources
 * (docs/DESIGN.md). The star sequencer's notes come from the main loop (eng_note). The main loop sets the sky (eng_sky); the audio interrupt
 * renders (eng_render), gliding every parameter to where the sky puts it: the slow sources slowly. */
#pragma once
#include <stdint.h>

/* the seven classical planets, the order SELECT goes round */
enum { PL_NONE, PL_SUN, PL_MOON, PL_MERCURY, PL_VENUS, PL_MARS, PL_JUPITER, PL_SATURN, PL_N };
#define ENG_N 15                         /* the pads (pads.c): NEBULA .. STRINGS */
#define NVOICE 8                         /* the star voices the moon picks (stars.c) */
enum { V_DROPS, V_KALIMBA, V_HARP, V_CELESTA, V_CHIMES, V_GLASS, V_MUSICBOX, V_FLUTE,
       V_PULSE, V_SEQ };                 /* and the rhythms' own: PULSAR's pluck, STARGATE's sequence */
enum { R_NONE, R_STARGATE, R_PULSAR };
#define NCHORD 4                         /* the instrument's chord: four notes */

typedef struct {
    float sun;                           /* the sun's height, degrees (-90..90) */
    float clouds;                        /* cloud cover, % */
    float press;                         /* sea-level pressure, hPa */
    int32_t trend;                       /* the pressure: -1 falling, 0 steady, 1 rising */
    float kp;                            /* Kp index, 0..9 */
    float moon;                          /* the moon's cycle: 0 new, 0.25 first quarter, 0.5 full, 0.75 last */
    float hz[2];                         /* each drone's pitch, Hz */
    float pr[2];                         /* each partner above its drone, a ratio; 0: the pressure's interval (or an
                                          * octave when not linked) */
    int8_t scale[7];                     /* the scale the planet's voice plays in: semitones above its root */
    float root_hz;                       /* the scale's root */
    int32_t linked;                      /* the pressure sets the partners' interval (else an octave) */
    int32_t planet;                      /* PL_*: the chosen planet (its scale and chords are the app's) */
    int32_t engine;                      /* the pad, 0..ENG_N-1 */
    int32_t voice;                       /* the star voice, 0..NVOICE-1 */
    int32_t rhythm;                      /* R_*: STARGATE's sequence, PULSAR's gate */
    float notes[NCHORD];                 /* the chord it plays, Hz (0: a note left out) */
    uint32_t chord_n;                    /* changed: a new chord, attacked anew (the same notes: held on) */
    int32_t on;                          /* the sky plays: the drones and the planet's voice (they fade in and out) */
    float drone_level;                   /* the drone's level, 0..1 (PRESETS) */
    int32_t drone_on[2];                 /* [0] the drone, [1] the instrument: the sky's, or a key's while it is stopped */
} sky_t;

/* what the sky does to the sound: the targets, also what the screen shows */
typedef struct {
    float hz[2];                         /* each drone */
    float pr[2];                         /* each partner above its drone, a ratio */
    int32_t interval;                    /* the pressure's interval, semitones (when the partners follow it) */
    const char *interval_name;           /* "P8", "P5", "m3", "TT"... */
    float beat;                          /* the partners detuned, cents (tension beats faster) */
    float cutoff, q;                     /* the filters: Hz, resonance */
    float sub;                           /* the sub's level, 0..1 */
    float lit;                           /* the moon's lit fraction, 0..1 */
    float morph;                         /* the oscillators' character, 0..4 around the moon's four */
    float verb, delay, feedback, noise, dry;   /* the space: the clouds */
    float drift;                         /* 0..1: how far everything wanders (Kp) */
    float storm;                         /* 0..1: ring and cross-modulation (Kp from 5) */
} sky_map_t;

void sky_map(const sky_t *s, sky_map_t *m);   /* pure: the mappings */
const char *moon_name(float moon);            /* "NEW", "WAXING", "FULL", "WANING" */
const char *planet_name(int32_t p);           /* "SUN", "MOON", "MERCURY"... ("" for none) */
const char *engine_name(int32_t k);           /* the pad: "NEBULA", "AURORA", "RINGS"... */
const char *voice_name(int32_t k);            /* the star voice: "DROPS", "KALIMBA", "HARP"... */
float tension(const sky_t *s);                /* the pressure's tension: -1.4 high and rising .. 2.6 deep and falling */

int eng_init(void);                      /* the delay's and the reverb's lines, from the heap: 0 = done */
void eng_sky(const sky_t *s);            /* main loop: where the sky is now */
void eng_render(int32_t *lr, uint32_t n, float gain);   /* audio interrupt: n stereo frames, 24-bit; gain 0..1 */
void eng_remote(float open, float res);   /* the phone's filter: 0 closed .. 1 open (out of the way), its resonance 0..1 */
float eng_remote_at(void);               /* where it is now */
uint32_t eng_bytes(void);                /* what eng_init took from the heap */
void eng_note(float hz, float amp, float pan, int32_t voice, uint32_t delay_ms);   /* main loop: a star's note, at
                                         * pan -1..1, in a voice (-1: the moon's), after delay_ms (dropped if 16 wait) */
