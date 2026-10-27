# COSMOS design

## Platform

- **Base:** fm1-base with `wifi = true`. Bluetooth is off (fm1-base cannot run WiFi and Bluetooth together).
- **Network:** the connection, HTTPS (BearSSL) and download code comes from fm1-radio. The radio itself (Radio
  Garden, MP3/AAC decoders) is left out.
- **Sound engine:** written from scratch.
- **UI:** COSMOS's own, including every WiFi screen (network list, password, status). fm1-radio's UI is not used.

## Sound engine

Three layers over a drone, each led by the source whose pace suits it.

**The drone**, soft, on the chord's root and its fifth, sliding to the next chord in 1.5 s:

- each note three FM pairs (a sine carrier and a light modulator): two a few cents either side of the note, so they
  beat, and one an octave up; the moon's light sets how much the modulators colour the sines (new moon nearly pure)
- a sub an octave down, pink noise, a gentle filter (the sun opens it), a slow breath

**1. The pad: the weather, by day or by night.** It plays the whole chord an octave over the drone, each new chord
attacked anew (the old one let go). The weather's kind comes from Open-Meteo (its WMO code); offline and in EDIT it is
guessed from the clouds and the pressure. Day is the sun above −6° (civil twilight). A new pad takes over at the next
chord. The settings' PAD can fix one (`cosmos/dsp/pads.c`).

| Weather | Day | Night |
| --- | --- | --- |
| Clear | NEBULA: four saws a note fanned out (±22 cents), a filter opening through each chord | AURORA: sine, triangle and octave, the reverb's tail climbing in octaves (shimmer), more with Kp and the full moon |
| Partly cloudy | STRINGS: a string machine, an ensemble of three chorus lines, a phaser | QUASAR: a cloud of short sine grains on the chord, scattered across the stereo |
| Overcast | ORGAN: drawbars 16' to 1', the pipes' chiff, a slow rotor, a cathedral | VOYAGER: three voices a note with their own vibratos, formants moving from vowel to vowel |
| Fog | BACKGROUND: 24 harmonics of the root, those on the chord lit, the rest a breathing veil | HORIZON: FM swells growing into each chord as if played backwards |
| Drizzle | TAPE: saws on a worn tape, wow and flutter, saturated, hiss | DEEP FIELD: sines through a slowly changing wavefolder |
| Rain | CRYSTAL: inharmonic FM (1.41, 3.5), glassy | RINGS: resonators on each note and its harmonics, bowed by noise |
| Snow | SOLAR WIND: noise through narrow band-passes on the chord, in gusts | SOLAR WIND |
| Thunderstorm | SUPERNOVA: brass, saws bending in, a filter swelling over 3.5 s | GRAVITY: saws falling into each chord, a wobbling low-pass, driven, a sub |

**2. The star voice: the moon.** What the star sequencer plays changes with the phase, one each eighth of the
cycle; the settings' STARS can fix one, or choose PAD: each pad's own voice, the stars playing with the
instrument (`cosmos/dsp/stars.c`). PAD's voices: NEBULA drops, AURORA a bell (FM 2:1), RINGS a struck string,
SOLAR WIND a whistle (noise in a narrow band), GRAVITY a low bell, QUASAR a burst of grains, ORGAN the flute,
VOYAGER a bell over a low shadow, TAPE the music box, HORIZON a bell played backwards, BACKGROUND a glint, DEEP
FIELD a bongo (a low-pass gate), CRYSTAL glass, SUPERNOVA a bell of pulses, STRINGS a synth pluck.

| Moon | Voice |
| --- | --- |
| New | DROPS: a sine and its octave, dying away |
| Waxing crescent | KALIMBA: a metal tine, two inharmonic partials, a wooden box |
| First quarter | HARP: a plucked string (Karplus-Strong) and its octave |
| Waxing gibbous | CELESTA: a soft FM bell, a low shadow |
| Full | CHIMES: a tube's four inharmonic modes; a gust swings the neighbours too |
| Waning gibbous | GLASS: glassy FM and a partial at 2.76 |
| Last quarter | MUSIC BOX: an FM tine on the tape's wow |
| Waning crescent | FLUTE: a pipe, its octave, breath |

**3. The rhythm: Kp, only when the Sun stirs.** From Kp 4 the star sequencer becomes STARGATE: each sixteenth at
112 BPM it plays the star nearest the line, a resonant analog voice whose filter snaps. From Kp 5 (a geomagnetic
storm) PULSAR: the pad is gated in sixteenths at 104 BPM over a soft beat, and the stars pluck on the grid. Three
tenths of Kp back before it leaves.

Then a slow stereo chorus (each pad its own depth), a 0.4 s ping-pong delay whose read wanders like tape, a warm
drive over the sound and its echoes together, and a reverb: four all-passes diffuse the input into four long lines
(68 to 105 ms) mixed by a Hadamard matrix, longer under clouds (each pad adds its own). In a storm the drone is
ring-modulated. The delay, the reverb, the chorus, the shimmer, the harp strings and the pads' state take 124 KB of
the WiFi heap (MEMORY.md: about 125 KB is the sound's).

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

The sound is not the planet's: the pad is the weather's.

| Planet | Note (cosmic) | Natural | Chords (degrees) |
| --- | --- | --- | --- |
| Sun | 126.22 Hz (B) | F | I V VI IV |
| Moon | 210.42 Hz (G#) | C | I VI III IV |
| Mercury | 141.27 Hz (C#) | D | I II IV V |
| Venus | 221.23 Hz (A) | E | I IV VI V |
| Mars | 144.72 Hz (D) | G | I VII VI V |
| Jupiter | 183.58 Hz (F#) | A | I IV II V |
| Saturn | 147.85 Hz (D) | B | I III VII VI |

## The star sequencer

A line sweeps the square left to right in 16 s. Each star brighter than magnitude 3 it crosses plays a note in the
moon's voice: its height a degree of the scale now (two octaves up from the root two octaves over the drones), its
brightness the loudness, its place across the screen its place in the stereo, two at most at once. The planets it crosses pluck too, louder (the chosen one most). Clouds and daylight
hide stars, so they thin the arpeggio out; a clear night is busy. On the ARP screen the line shows, and what plays
lights up: a star flashes, a planet glows.

## Chords

Always a chord on a degree of the scale, in the sun's mode, coloured by the pressure: high and steady, the triad
and its octave; falling, the seventh; deep down, suspended with the ninth. The drone holds its root and fifth, the
pad the four notes an octave up. Four chords always go round, one every 10 s: with no planet on the last
key's note, with one on the planet's; a key plays the chord of the scale on its note for a while (held and 10 s
after).

## Sources and what they do

The slower a source changes, the more drastic its effect. Otherwise the sound stays too similar to itself.

| Source | Pace | Controls | Depth |
| --- | --- | --- | --- |
| **Weather's kind** | hours | The pad (with the sun: day or night) | Drastic: a new instrument from the next chord |
| **Pressure** | hours to days | The chords' colour (triad, seventh, suspended ninth); how fast the drone beats | Medium |
| **Moon phase** | ~29.5 days | The star voice; the drone's light (how much the modulators colour its sines, the sub, the filter's resonance) | A new voice every three or four nights |
| **Kp index** | hours; storms are rare | A gentle drift; from Kp 4 STARGATE, from 5 PULSAR and the drone ring-modulated | Rare but drastic |
| **Cloud cover** | hours | Space: reverb, delay, noise | Medium |
| **Sun height** | continuous, daily | The mode; day or night for the pad; light: filter cutoff (450 Hz at night, 1.2 kHz at the horizon, 7 kHz high) | Gentle, and twice a day drastic |

Sun height, not clock time: the sound follows the seasons by itself, and dawn sounds at dawn.

## Updates

- The WiFi turns on at power-on (it holds the sound for about a second as it starts) and joins the networks it
  knows: up to six, the last joined first, whichever is in range.
- As soon as it is connected, and then every half hour, it fetches over HTTPS the weather at the player's
  coordinates (Open-Meteo: cloud cover, sea-level pressure and its last three hours, which give the trend, and the
  place's offset from UTC, summer time included) and the planetary Kp (NOAA). The servers' Date header sets the
  clock. A fetch that fails is tried again in 5 minutes.
- In HOME the values glide to the new ones over a few minutes: no jumps. Without news for three hours, the weather
  drifts by itself again.
- Sun, moon, planets and stars are computed on the FM-1 and need no network.
- The servers' certificates are not checked (no roots, no clock to check them with): for weather numbers the worst a
  man in the middle can do is change the weather.

## WiFi (ENV)

ENV opens the WiFi page, and closes it. The state, the network, the signal, how old the weather is, then the networks
around, the strongest first (SAVED: known). SELECT picks one, SAVE joins it (a known or open one at once, else the
password), FX looks again, REC turns WiFi off or on (offline), PLAY fetches now.

The password: SELECT turns through the characters, ALGO jumps a group (abc ABC 123 #+=), PLAY adds, HOME deletes,
SAVE joins (8 characters at least), SEL goes back to the list.

## Offline mode

With WiFi off (REC on the WiFi page), or no network in range:

- Sun, moon, stars and planets work as online.
- Clouds, pressure and Kp come from a randomizer: a simulated weather that drifts by itself, at each source's own
  pace (pressure over days, clouds over hours, a rare geomagnetic storm), so the sound keeps changing as online.
- The place is set by its coordinates, the time and the offset from UTC by hand (GLO); the FM-1 keeps counting
  while it is on, and a power-on starts from the time last saved.

## Controls

| Button | |
| --- | --- |
| **HOME** | back to nature, at once: the sun and the moon as they are, the weather as it is now (nature's weather goes on while EDIT has the knobs) (over WiFi; offline, or with no news for three hours, drifting by itself). The readings' last line says how old it is |
| **ENV** | the WiFi page (above), and back; lit bright while the weather is the network's |
| **EDIT** | everything by hand, the readings held on the screen: K1 the sun's height, K2 the clouds, K3 the pressure (its trend: the way it was turned), K4 Kp, ALGO the moon's cycle |
| **PRESETS** | the drone's level, in HOME and in EDIT: 0 to 100 % in steps of 5, kept with the settings |
| **SEL** | the next planet up (in EDIT any of the seven classical ones), or none |
| **LFO** | the sky alone (a button or a knob brings the readings back for two seconds; the keys only play), and back |
| **ARP** | the ARP screen: the sky, the arpeggiator on, its line, what it plays lighting up; again: off, and back |
| **GLO** | the settings: latitude, longitude, UTC offset, summer time, date, time (local), the pad (AUTO: the weather's, or one of the fifteen), the star voice (AUTO: the moon's, or one of the eight), the tuning (COSMIC, EQUAL or NATURAL) |
| **PLAY** | the sky stopped or playing; stopped, a key still sounds its chord, held and for 10 s after. Stopping saves |
| **Lights** | the buttons that do something glow, the ones in use bright; the keys of the scale glow, the root's bright |
| **OCT− / OCT+** | the octave |
| **Keys** | the chord on the key's note: with no planet the new root; with one, the chord of its scale on the nearest degree, held and for 10 s after |
| **MASTER** | the volume |
| **Battery** | on the readings' and the settings' bottom lines |

## Screen

240×240, white on black, Terminus bitmap font (in fm1-base), English, text only.

```
COSMOS                 RINGS
----------------------------
SUN      -57    FILTER     0
RAIN     85%    REVERB    66
PRESS 1003 v    CHORD    7TH
KP       2.3    DRIFT     37
MOON    FULL          CHIMES
----------------------------
SKY 100%         UPD 12 MIN
```

- Left: the reading from the world. Right: what it is doing to the sound.
- The top line names the pad playing. The second reading is the weather's kind and the cloud cover. PRESS shows
  the trend (^ rising, v falling, = steady), and on its right the chord's colour. Kp's right side names the rhythm
  when the Sun stirs (STARGATE, PULSAR, in orange). The moon's right side names the star voice.
- The readings stay. On the sky alone (LFO, ARP) they fade in (0.3 s) on a touch, hold (2 s after the last touch),
  then fade out (1.5 s). See `docs/img/rome-2026-11-09-fade.gif`.
- Saving to flash silences the sound for a moment, so COSMOS saves only when PLAY stops it and when the settings
  close.

### Splash

At power-on, scattered stars gather into the word COSMOS, set in Michroma (212 pixels wide, a star on every other
pixel: 397 stars), hold, and fade into the sky: 4.4 s. See `tools/splash_preview.py` and `docs/img/splash.gif`;
the firmware draws it in its first frames (`splash_frame`, the word's points from `tools/gen_splash.py` at build
time, each star's start, delay and twinkle from a hash of its number), timed from its first frame; a new button
press cuts it short. The sound waits for it and fades in with the sky; WiFi starts after it too: its start holds the
main loop and the panel's scan for about a second.

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
