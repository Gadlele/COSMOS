/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's sound: two drones, a sub, noise, ring and cross-modulation, a resonant filter, a delay and a reverb,
 * shaped by the five sources (docs/DESIGN.md). The main loop sets the sky (eng_sky); the audio interrupt renders
 * (eng_render), gliding every parameter to where the sky puts it: the slow sources slowly. */
#pragma once
#include <stdint.h>

typedef struct {
    float sun;                           /* the sun's height, degrees (-90..90) */
    float clouds;                        /* cloud cover, % */
    float press;                         /* sea-level pressure, hPa */
    int32_t trend;                       /* the pressure: -1 falling, 0 steady, 1 rising */
    float kp;                            /* Kp index, 0..9 */
    float moon;                          /* the moon's cycle: 0 new, 0.25 first quarter, 0.5 full, 0.75 last */
    int32_t note;                        /* the drones' base note, MIDI */
    int32_t on;                          /* the drones sound (they fade in and out) */
} sky_t;

/* what the sky does to the sound: the targets, also what the screen shows */
typedef struct {
    float base_hz;                       /* drone A */
    int32_t interval;                    /* drone B above A, semitones */
    const char *interval_name;           /* "P5", "m3", "TT"... */
    float beat;                          /* B detuned from its interval, cents (tension) */
    float cutoff, q;                     /* the filter: Hz, resonance */
    float sub;                           /* the sub's level, 0..1 */
    float lit;                           /* the moon's lit fraction, 0..1 */
    float morph;                         /* the oscillators' character, 0..4 around the moon's four */
    float verb, delay, feedback, noise, dry;   /* the space: the clouds */
    float drift;                         /* 0..1: how far the pitches wander (Kp) */
    float storm;                         /* 0..1: ring and cross-modulation (Kp from 5) */
} sky_map_t;

void sky_map(const sky_t *s, sky_map_t *m);   /* pure: the mappings */
const char *moon_name(float moon);            /* "NEW", "WAXING", "FULL", "WANING" */

int eng_init(void);                      /* the delay's and the reverb's lines, from the heap: 0 = done */
void eng_sky(const sky_t *s);            /* main loop: where the sky is now */
void eng_render(int32_t *lr, uint32_t n, float gain);   /* audio interrupt: n stereo frames, 24-bit; gain 0..1 */
uint32_t eng_bytes(void);                /* what eng_init took from the heap */
