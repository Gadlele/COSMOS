# COSMOS

A drone synthesizer firmware for the M-VAVE FM-1, played by the sky.

Two drones that never stop, shaped by what is happening above you: the height of the sun, the clouds, the air
pressure, the geomagnetic activity (Kp index) and the phase of the moon. The screen is a window on the real sky
over your location, stars, moon and planets, with the readings in plain white text on top.

The slower a source changes, the deeper its effect: the moon and the pressure decide what instrument COSMOS is
this week, a geomagnetic storm tears it apart now and then, the clouds and the sun colour it through the day.

**Status: design.** Nothing to install yet. See [docs/DESIGN.md](docs/DESIGN.md) for the plan and
[docs/MEMORY.md](docs/MEMORY.md) for the measured memory budget.

## Built on

- [fm1-base](https://github.com/charlesvestal/fm1-base): the FM-1 firmware platform, with WiFi (MPL-2.0)
- [fm1-radio](https://github.com/charlesvestal/fm1-radio): its network and HTTPS code (MPL-2.0)

## Inspired by

- [Niagara Drift](https://github.com/evanyerburgh/niagara-drift): the idea of two drifting drones. COSMOS
  shares no code with it; its sound engine is written from scratch.

## Data

- Weather: [Open-Meteo](https://open-meteo.com) (cloud cover, pressure)
- Kp index: [NOAA Space Weather Prediction Center](https://www.swpc.noaa.gov)
- Sun, moon, planets and stars are computed on the FM-1 itself; they work offline.

M-VAVE and FM-1 are trademarks of their owners. COSMOS is an independent project.
