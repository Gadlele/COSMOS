# The sound lab

Sketches of COSMOS's engines in Python (numpy, scipy, numba), rendered offline: the twenty proposals the firmware's
pads and star voices came from. `lab.py` holds the shared parts (the harmony as COSMOS plays it, the star sequencer
from the real sky, effects); `engines.py` the proposals; `review.py` a contact sheet of spectrograms and levels.

    pip install numpy scipy numba
    python3 tools/lab/engines.py OUT_DIR [NAME ...]      # 40 s each, WAV and MP3 (needs ffmpeg)
    python3 tools/lab/review.py OUT_DIR sheet.png

`STARS_CSV` in `lab.py` points at `cosmos/data/stars.csv`.
