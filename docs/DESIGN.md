# COSMOS design

## Platform

- **Base:** fm1-base with `wifi = true`. Bluetooth is off (fm1-base cannot run WiFi and Bluetooth together).
- **Network:** the connection, HTTPS (BearSSL) and download code comes from fm1-radio. The radio itself (Radio
  Garden, MP3/AAC decoders) is left out.
- **Sound engine:** written from scratch.
- **UI:** COSMOS's own, including every WiFi screen (network list, password, status, city). fm1-radio's UI is not
  used.

## Sound engine

Each drone is a small instrument of its own:

- a main oscillator in the moon's character (four wavetables, blended around the cycle)
- a partner an octave up, a few cents off, so the two beat; tension beats faster
- a sub an octave down, and pink noise
- all driven (a soft saturation) into the drone's own resonant filter
- breathing: each drone swells at its own slow rate

Drone A sits a little left, drone B a little right; in a storm A bends B's phase (cross-modulation) and both are
ring-modulated. Nothing is ever still: each drone's pitch, cutoff, level and partner detune wander on their own,
further with Kp. Then a 0.4 s ping-pong delay whose read wanders like tape, a warm drive over the sound and its
echoes together, and a reverb: four all-passes diffuse the input into four long lines (68 to 105 ms) mixed by a
Hadamard matrix, longer under clouds. The delay and the reverb take 103 KB of the WiFi heap.

No presets, no scenes: the sound moves continuously, never jumps.

## Planets: notes, chords, engines and voices

The seven classical planets, the Sun and the Moon among them, can be chosen (SELECT) while they are drawn on the
screen; when one sets or the sky hides it, it goes. A planet brings four things:

- **Its note**: its period raised by octaves into hearing (Hans Cousto's cosmic octave): the Earth's year for the Sun,
  the synodic month for the Moon, each planet's orbit. TUNING COSMIC plays the exact frequency, EQUAL the nearest
  tempered note, NATURAL a natural note each, in the spheres' classical order outward (Moon C, Mercury D, Venus E,
  Sun F, Mars G, Jupiter A, Saturn B).
- **Four chords** on that note, one every 10 s, in the mode the sun's height gives: Phrygian below -18 degrees,
  Aeolian to -6, Dorian to the horizon, Mixolydian to 12, Ionian to 30, Lydian above. Drone 1 plays the chord's root,
  its partner the fifth; drone 2 the third, its partner coloured by the pressure (the octave when high and steady, the
  seventh as it falls, a suspended fourth and the ninth deep down).
- **Its engine** for the drones, gliding in over two seconds.
- **Its rhythmic voice**, pulsing with its period (2.2 s times the square root of the period in years), in the scale.

| Planet | Note (cosmic) | Natural | Chords (degrees) | Engine | Voice | Pulse |
| --- | --- | --- | --- | --- | --- | --- |
| Sun | 126.22 Hz (B) | F | I V VI IV | radiant, open | FLARE: a soft bright chord flaring high | 2.2 s |
| Moon | 210.42 Hz (G#) | C | I VI III IV | silver, singing | RIPPLE: tiny glassy drops wandering the scale | 0.6 s |
| Mercury | 141.27 Hz (C#) | D | I II IV V | thin and bright | GLINT: quick high sparks up the chord | 1.1 s |
| Venus | 221.23 Hz (A) | E | I IV VI V | sweet, glass | BELLS: FM bells walking the scale as a star | 1.7 s |
| Mars | 144.72 Hz (D) | G | I VII VI V | raw, driven | PULSE: a low double beat, a heartbeat | 3.0 s |
| Jupiter | 183.58 Hz (F#) | A | I IV II V | deep and warm | SWELL: a sub below the sub, breathing | 7.6 s |
| Saturn | 147.85 Hz (D) | B | I III VII VI | metallic, ringed | RINGS: an inharmonic chime with a long tail | 11.9 s |

## Chords

The drones always play a chord: drone 1 its root, its partner the fifth; drone 2 the third (a suspended fourth when
the pressure is deep down), its partner coloured by the pressure (the octave high and steady, the seventh as it
falls, the ninth deep down), in the sun's mode. With no planet the chord stands on the last key's note; with one,
its four go round, one every 10 s, and a key plays the chord of the scale on its note for a while (held and 10 s
after). SEL in the settings' PRESS HARMONY leaves the pressure out: plain triads.

## Sources and what they do

The slower a source changes, the more drastic its effect. Otherwise the sound stays too similar to itself.

| Source | Pace | Controls | Depth |
| --- | --- | --- | --- |
| **Moon phase** | ~29.5 days | The instrument's character: oscillator types and base timbre, plus the sub level | Drastic. Each phase is a different world: new moon, pure dark sines; waxing, soft waves; full, rich waves with full sub and high resonance; waning, rough metallic timbres. Morphs continuously over days. |
| **Pressure** | days | Harmony: the interval of each drone's partner, and how fast they beat (SEL leaves it out: octaves) | Drastic. High: consonances, a slow beat. Falling: minor colours, tension, faster beating. Very low (storm): tritone, clusters. The trend over the last hours counts more than the value. |
| **Kp index** | hours; storms are rare | Events: low Kp, a gentle drift; Kp 5 and up turns on ring mod and cross-modulation | Rare but drastic |
| **Cloud cover** | hours | Space: reverb, delay, noise | Medium |
| **Sun height** | continuous, daily | Light: filter cutoff | Gentle |

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
| **SEL** | the next planet on the screen (the seven classical ones), or none |
| **GLO** | the settings: latitude, longitude, UTC offset, summer time, date, time (local), the pressure in the harmony, the tuning (COSMIC, EQUAL or NATURAL) |
| **PLAY** | the sky stopped or playing; stopped, a key still sounds its chord, held and for 10 s after |
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
PRESS 1008 ↓    INTERVAL  P5
KP         4    DRIFT     44
MOON     83%    SUB       83
----------------------------
SKY 100%         UPD 12 MIN
```

- Left: the reading from the world. Right: what it is doing to the sound.
- PRESS shows the trend: ↑ rising, ↓ falling, → steady. INTERVAL uses interval names (P5, P8, m3...).
- The text fades in (0.3 s) when a key or knob is touched, holds (2 s after the last touch), then fades out
  (1.5 s) and leaves the sky on the whole screen. See `docs/img/rome-2026-11-09-fade.gif`.

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
- **Linked to the weather:** clouds hide the stars, faintest first; the sun up switches the sky off, twilight
  leaves only the brightest; a full moon washes out the faint ones; Kp makes the stars twinkle.
- **Readability:** the background stays in the dark half of the grey scale; every letter has a 1-px black
  outline; the moon dims while it passes behind text.

See `tools/sky_preview.py` and `docs/img/`.

## Open questions

- Delay length: ~125 KB for the sound measured on the FM-1 (MEMORY.md): up to 1 s stereo beside the reverb
