# COSMOS design

## Platform

- **Base:** fm1-base with `wifi = true`. Bluetooth is off (fm1-base cannot run WiFi and Bluetooth together).
- **Network:** the connection, HTTPS (BearSSL) and download code comes from fm1-radio. The radio itself (Radio
  Garden, MP3/AAC decoders) is left out.
- **Sound engine:** written from scratch.
- **UI:** COSMOS's own, including every WiFi screen (network list, password, status, city). fm1-radio's UI is not
  used.

## Sound engine

- Two drones (oscillators always on), each detuned against the other
- Sub oscillator
- Resonant filter
- Reverb
- Delay (short, or longer if the heap allows: see MEMORY.md)
- Noise
- Ring modulation and cross-modulation, for geomagnetic storms

No presets, no scenes: the sound moves continuously, never jumps.

## Sources and what they do

The slower a source changes, the more drastic its effect. Otherwise the sound stays too similar to itself.

| Source | Pace | Controls | Depth |
| --- | --- | --- | --- |
| **Moon phase** | ~29.5 days | The instrument's character: oscillator types and base timbre, plus the sub level | Drastic. Each phase is a different world: new moon, pure dark sines; waxing, soft waves; full, rich waves with full sub and high resonance; waning, rough metallic timbres. Morphs continuously over days. |
| **Pressure** | days | Harmony: the interval between the drones and the mode | Drastic. High: consonances, bright colours. Falling: minor modes, tension. Very low (storm): tritone, clusters. The trend over the last hours counts more than the value. |
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

- **Keys:** the drones' base note
- **SKY knob:** how much the sky drives the sound, 0 (manual) to 100 (all sky)
- **Volume**

## Screen

240×240, white on black, Terminus bitmap font (in fm1-base), English, text only.

```
COSMOS                    A2
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

- Delay length: depends on the free heap with WiFi (MEMORY.md)
