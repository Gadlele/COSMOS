# COSMOS design

## Platform

- **Base:** fm1-base with `wifi = true`. Bluetooth is off (fm1-base cannot run WiFi and Bluetooth together).
- **Network:** the connection, HTTPS (BearSSL) and download code comes from fm1-radio. The radio itself (Radio
  Garden, MP3/AAC decoders) is left out.
- **Sound engine:** written from scratch.
- **UI:** COSMOS's own, including every WiFi screen (network list, password, status, city). fm1-radio's UI is not
  used.

## Sound engine

Two layers: one drone underneath, an instrument on top.

**The drone**, soft, on the chord's root and its fifth, sliding to the next chord in 1.5 s:

- each note three FM pairs (a sine carrier and a light modulator): two a few cents either side of the note, so they
  beat, and one an octave up; the moon's light sets how much the modulators colour the sines (new moon nearly pure)
- a sub an octave down, pink noise, a gentle filter (the sun opens it), a slow breath

**The instrument** plays the whole chord, a seventh of the scale, an octave over the drone. Each new chord is
attacked anew (the old one let go), so the chords are heard as chords. The pressure picks it (below); the settings'
INSTRUMENT can fix one.

| Instrument | Made of | Attack / release |
| --- | --- | --- |
| ORGAN | drawbars 16', 8', 4', 2 2/3', 2', 1 1/3' on one phase, a soft key click, a slow rotor between the sides | 0.03 / 0.35 s |
| STRINGS | three saws a few cents apart, low-passed together | 1.1 / 2.2 s |
| CHOIR | a saw with a vibrato and a breath, through three formants ("aah") | 0.9 / 1.8 s |
| WARM PAD | two saws 9 cents either side, a tape wow, dark | 0.7 / 2.0 s |

Then the arpeggiator's plucks, a slow stereo chorus, a 0.4 s ping-pong delay whose read wanders like tape, a warm
drive over the sound and its echoes together, and a reverb: four all-passes diffuse the input into four long lines
(68 to 105 ms) mixed by a Hadamard matrix, longer under clouds. In a storm (Kp 5 and up) the drone is ring-modulated.
The delay, the reverb and the chorus take 107 KB of the WiFi heap.

No presets, no scenes: the sound moves continuously, never jumps.

## Planets: notes and chords

The seven classical planets, the Sun and the Moon among them, can be chosen (SELECT) while they are up (in EDIT,
any of them); when one sets, it goes. A planet brings two things:

- **Its note**: its period raised by octaves into hearing (Hans Cousto's cosmic octave): the Earth's year for the Sun,
  the synodic month for the Moon, each planet's orbit. TUNING COSMIC plays the exact frequency, EQUAL the nearest
  tempered note, NATURAL a natural note each, in the spheres' classical order outward (Moon C, Mercury D, Venus E,
  Sun F, Mars G, Jupiter A, Saturn B).
- **Four chords** on that note, one every 10 s, in the mode the sun's height gives: Phrygian below -18 degrees,
  Aeolian to -6, Dorian to the horizon, Mixolydian to 12, Ionian to 30, Lydian above.

The sound is not the planet's: the instrument is the pressure's.

| Planet | Note (cosmic) | Natural | Chords (degrees) |
| --- | --- | --- | --- |
| Sun | 126.22 Hz (B) | F | I V VI IV |
| Moon | 210.42 Hz (G#) | C | I VI III IV |
| Mercury | 141.27 Hz (C#) | D | I II IV V |
| Venus | 221.23 Hz (A) | E | I IV VI V |
| Mars | 144.72 Hz (D) | G | I VII VI V |
| Jupiter | 183.58 Hz (F#) | A | I IV II V |
| Saturn | 147.85 Hz (D) | B | I III VII VI |

## The arpeggiator

A line sweeps the square left to right in 16 s. Each star brighter than magnitude 3 it crosses plucks a note: its
height a degree of the scale now (two octaves up from the root two octaves over the drones), its brightness the
loudness, two at most at once. The planets it crosses pluck too, louder (the chosen one most). Clouds and daylight
hide stars, so they thin the arpeggio out; a clear night is busy. On the ARP screen the line shows, and what plays
lights up: a star flashes, a planet glows.

## Chords

Always a chord, the seventh chord of the scale on a degree, in the sun's mode: the drone holds its root and fifth,
the instrument the four notes an octave up. Four chords always go round, one every 10 s: with no planet on the last
key's note, with one on the planet's; a key plays the chord of the scale on its note for a while (held and 10 s
after).

## Sources and what they do

The slower a source changes, the more drastic its effect. Otherwise the sound stays too similar to itself.

| Source | Pace | Controls | Depth |
| --- | --- | --- | --- |
| **Pressure** | hours to days | The instrument: 1020 hPa and up the ORGAN, 1012 to 1020 STRINGS, 1004 to 1012 the WARM PAD, below 1004 the CHOIR (1.5 hPa either side before it changes); how fast the drone beats | Drastic: a new instrument from the next chord |
| **Moon phase** | ~29.5 days | The drone's light: how much the modulators colour its sines, the sub, the filter's resonance | New moon nearly pure sines; full, a veil of harmonics, a full sub, a singing filter |
| **Kp index** | hours; storms are rare | Events: low Kp, a gentle drift; Kp 5 and up ring-modulates the drone | Rare but drastic |
| **Cloud cover** | hours | Space: reverb, delay, noise | Medium |
| **Sun height** | continuous, daily | Light: filter cutoff (450 Hz at night, 1.2 kHz at the horizon, 7 kHz high) | Gentle |

Sun height, not clock time: the sound follows the seasons by itself, and dawn sounds at dawn.

## Updates

- Every 15–30 minutes the WiFi turns on, fetches pressure and clouds (Open-Meteo) and Kp (NOAA), syncs the time,
  and turns off again.
- fm1-base holds the sound for about a second when the WiFi starts. COSMOS makes it a breath: the drones fade out
  before the connection and come back after.
- Between readings the values glide to the new ones over the following minutes: no jumps.
- Sun, moon, planets and stars are computed on the FM-1 and need no network.

## Offline mode

For players who do not want to connect it:

- Sun, moon, stars and planets work as online.
- Clouds, pressure and Kp come from a randomizer: a simulated weather that drifts by itself, at each source's own
  pace (pressure over days, clouds over hours, a rare geomagnetic storm), so the sound keeps changing as online.
- The city is picked from a list and kept.
- The time is set by hand once; the FM-1 keeps counting while it is on.
- No WiFi means no breath every half hour.

## Controls

| Button | |
| --- | --- |
| **HOME** | back to nature: the sun and the moon as they are, the weather as it comes (fetched over WiFi in stage 3; until then, or offline, drifting by itself) |
| **EDIT** | everything by hand, the readings held on the screen: K1 the sun's height, K2 the clouds, K3 the pressure, K4 Kp, ALGO the moon's cycle, PRESET the pressure's trend |
| **SEL** | the next planet up (in EDIT any of the seven classical ones), or none |
| **LFO** | the sky alone (a touch brings the readings back for two seconds), and back |
| **ARP** | the ARP screen: the sky, the arpeggiator on, its line, what it plays lighting up; again: off, and back |
| **GLO** | the settings: latitude, longitude, UTC offset, summer time, date, time (local), the instrument (AUTO: the pressure's, or ORGAN, STRINGS, CHOIR, WARM PAD), the tuning (COSMIC, EQUAL or NATURAL) |
| **PLAY** | the sky stopped or playing; stopped, a key still sounds its chord, held and for 10 s after. Stopping saves |
| **Lights** | the buttons that do something glow, the ones in use bright; the keys of the scale glow, the root's bright |
| **OCT− / OCT+** | the octave |
| **Keys** | the chord on the key's note: with no planet the new root; with one, the chord of its scale on the nearest degree, held and for 10 s after |
| **MASTER** | the volume |
| **Battery** | on the readings' and the settings' bottom lines |

## Screen

240×240, white on black, Terminus bitmap font (in fm1-base), English, text only.

```
COSMOS                 A2 E3
----------------------------
SUN     +38°    FILTER    62
CLOUDS   72%    REVERB    71
PRESS 1008 ↓       STRINGS
KP         4    DRIFT     44
MOON     83%    SUB       83
----------------------------
SKY 100%         UPD 12 MIN
```

- Left: the reading from the world. Right: what it is doing to the sound.
- PRESS shows the trend: ↑ rising, ↓ falling, → steady. On its right, the instrument the pressure picks.
- The readings stay. On the sky alone (LFO, ARP) they fade in (0.3 s) on a touch, hold (2 s after the last touch),
  then fade out (1.5 s). See `docs/img/rome-2026-11-09-fade.gif`.
- Saving to flash silences the sound for a moment, so COSMOS saves only when PLAY stops it and when the settings
  close.

### Splash

At power-on, scattered stars gather into the word COSMOS, set in Michroma (212 pixels wide, a star on every other
pixel: 397 stars), hold, and fade into the sky: 4.4 s. See `tools/splash_preview.py` and `docs/img/splash.gif`;
the firmware draws it in `app_splash` (fm1-base's APP_SPLASH) from the stars' positions alone.

### The sky behind the text

The real sky over the player's location, now:

- **Projection:** stereographic, zenith at the centre, north up, east on the left (as seen looking up). It fills
  the whole square, no circle: the corners touch the horizon.
- **Stars:** a catalog of the 300–500 brightest (2–3 KB of flash), in grey by magnitude. They turn with the Earth.
- **Planets** (Venus, Jupiter, Saturn, Mars, Mercury): steady points; they do not twinkle, the stars do. Venus and
  Jupiter 2×2 px, Saturn with a faint pixel on each side.
- **Moon:** a 7–9 px disc with its real phase and position.
- **Linked to the weather:** clouds hide the stars, faintest first (the brightest, down to magnitude 3, stay, faint
  and steady, so the sky and the arpeggio never empty; hidden ones play softly); the sun up switches the sky off, twilight
  leaves only the brightest; a full moon washes out the faint ones; Kp makes the stars twinkle.
- **Readability:** the background stays in the dark half of the grey scale; every letter has a 1-px black
  outline; the moon dims while it passes behind text.

See `tools/sky_preview.py` and `docs/img/`.

## Open questions

- Delay length: ~125 KB for the sound measured on the FM-1 (MEMORY.md): up to 1 s stereo beside the reverb
