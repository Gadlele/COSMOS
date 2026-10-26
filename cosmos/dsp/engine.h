/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's sound: two drones, each a small instrument (a main oscillator and its partner, a sub, noise, its own
 * driven resonant filter), ring and cross-modulation, a delay and a reverb, shaped by the five sources
 * (docs/DESIGN.md). A planet, when one is chosen, changes the drones' engine. The arpeggiator's plucks come from the
 * main loop (eng_note). The main loop sets the sky (eng_sky); the audio interrupt renders (eng_render), gliding every
 * parameter to where the sky puts it: the slow sources slowly. */
#pragma once
#include <stdint.h>

/* the seven classical planets, the order SELECT goes round */
enum { PL_NONE, PL_SUN, PL_MOON, PL_MERCURY, PL_VENUS, PL_MARS, PL_JUPITER, PL_SATURN, PL_N };

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
    int32_t planet;                      /* PL_*: the engine and the rhythmic voice */
    int32_t on;                          /* the sky plays: the drones and the planet's voice (they fade in and out) */
    int32_t drone_on[2];                 /* each drone sounds: the sky's, or a key's while the sky is stopped */
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
const char *planet_voice(int32_t p);          /* its engine in a word: "RADIANT", "SILVER", "BRIGHT", "SWEET"... */
float tension(const sky_t *s);                /* the pressure's tension: -1.4 high and rising .. 2.6 deep and falling */

int eng_init(void);                      /* the delay's and the reverb's lines, from the heap: 0 = done */
void eng_sky(const sky_t *s);            /* main loop: where the sky is now */
void eng_render(int32_t *lr, uint32_t n, float gain);   /* audio interrupt: n stereo frames, 24-bit; gain 0..1 */
uint32_t eng_bytes(void);                /* what eng_init took from the heap */
void eng_note(float hz, float amp);      /* main loop: an arpeggiator's pluck (dropped if 8 wait) */
