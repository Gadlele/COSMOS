# COSMOS

A drone synthesizer firmware for the M-VAVE FM-1, played by the sky.

Two drones that never stop, shaped by what is happening above you: the height of the sun, the clouds, the air
pressure, the geomagnetic activity (Kp index) and the phase of the moon. The screen is a window on the real sky
over your location, stars, moon and planets, with the readings in plain white text on top.

The slower a source changes, the deeper its effect: the moon and the pressure decide what instrument COSMOS is
this week, a geomagnetic storm tears it apart now and then, the clouds and the sun colour it through the day.

**Status: design, and a test firmware.** [Install COSMOS PROBE](https://gadlele.github.io/COSMOS/install/), which
joins WiFi, fetches COSMOS's data and measures the memory left. See [docs/DESIGN.md](docs/DESIGN.md) for the plan,
[docs/MEMORY.md](docs/MEMORY.md) for the memory budget and [docs/BUILDING.md](docs/BUILDING.md) to build it.

`web/emulator/` is a sketch of COSMOS's sound in Web Audio, to try the mappings without an FM-1:
`index.src.html` with the Terminus glyphs and the star catalog (`data.js`) put in its `/*DATA*/` makes `index.html`.

## Data

- Weather: [Open-Meteo](https://open-meteo.com) (cloud cover, pressure)
- Kp index: [NOAA Space Weather Prediction Center](https://www.swpc.noaa.gov)
- Sun, moon, planets and stars are computed on the FM-1 itself; they work offline.

## Licence

COSMOS is free software under the [GNU General Public License v3.0 or later](LICENSE). You may use, study, change
and share it; versions you distribute must stay under the GPL, with their source. The code it includes from others
keeps its own licence: see [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their owners. COSMOS is an independent project.
