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

## Planets: engines and voices

A planet on the screen can be chosen (SELECT). It changes the drones' engine and adds a rhythmic voice whose pulse
comes from its orbit (2.2 s times the square root of the orbit in years). Only a planet drawn on the screen can be chosen;
when it sets or the sky hides it, its engine and its voice go.

| Planet | Engine | Voice | Pulse |
| --- | --- | --- | --- |
| none | COSMOS's own | | |
| Mercury | thin and bright: little sub, the filters open | GLINT: quick high sparks up the chord | 1.1 s |
| Venus | sweet, glass: the oscillators near sines, the partner two octaves up and soft, no drive | BELLS: FM bells walking the pentatonic as a pentagram (minor under tension) | 1.7 s |
| Mars | raw: driven hard, more noise | PULSE: a low double beat, a heartbeat | 3.0 s |
| Jupiter | deep and warm: a heavy sub, the filters low | SWELL: a sub below the sub, breathing | 7.6 s |
| Saturn | metallic: the partner at an inharmonic 2.76, more resonance | RINGS: an inharmonic chime with a long tail | 11.9 s |

The engines glide into each other over about two seconds.

## Notes

The keyboard is split: the left half (F3..F#4) sets drone 1's note, the right half (G4..G5) drone 2's; OCT− and OCT+
move both. Each drone's partner sits an interval above it: the pressure's (below), or an octave when the player
leaves the pressure out of the harmony (SEL).

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

- **Keys:** the left half drone 1's note, the right half drone 2's
- **SELECT:** a planet on the screen (its engine and its voice)
- **HOME:** the settings: city, date, time (local, with summer time), weather (MANUAL or RANDOM)
- **SEL:** the pressure in the harmony, or not
- **SKY knob:** how much the sky drives the sound, 0 (manual) to 100 (all sky)
- **Volume**
- **Battery**: on the screen's bottom line, four bars (orange when empty), USB when a computer powers it

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
