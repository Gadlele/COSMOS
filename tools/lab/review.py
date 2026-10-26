# SPDX-License-Identifier: GPL-3.0-or-later
import sys, glob, wave, numpy as np
from PIL import Image, ImageDraw
files = sorted(glob.glob(sys.argv[1] + "/*.wav"))
W, H = 400, 140
sheet = Image.new("RGB", (2 * (W + 10), 10 * (H + 22)), "black")
for k, f in enumerate(files):
    w = wave.open(f); a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(float) / 32768
    m = a.mean(1)
    hop = len(m) // W
    spec = []
    for i in range(W):
        seg = m[i * hop:i * hop + 4096]
        if len(seg) < 4096: seg = np.pad(seg, (0, 4096 - len(seg)))
        S = np.abs(np.fft.rfft(seg * np.hanning(4096)))[:1860]   # up to 20 kHz
        # log frequency axis
        idx = np.unique(np.geomspace(2, 1859, H).astype(int))
        spec.append(20 * np.log10(S[idx] + 1e-6))
    sp = np.array(spec).T[::-1]
    sp = np.clip((sp + 30) / 70, 0, 1)
    img = Image.fromarray((sp * 255).astype(np.uint8)).resize((W, H)).convert("RGB")
    x0, y0 = (k % 2) * (W + 10), (k // 2) * (H + 22)
    sheet.paste(img, (x0, y0 + 18))
    d = ImageDraw.Draw(sheet)
    env = [np.sqrt((a[i:i + 22050] ** 2).mean()) for i in range(0, len(a), 22050)]
    S = np.abs(np.fft.rfft(m)) ** 2; fr = np.fft.rfftfreq(len(m), 1 / 44100)
    hf = S[fr > 6000].sum() / S.sum()
    jump = np.abs(np.diff(m)).max()
    d.text((x0, y0 + 2), f"{f.split('/')[-1][:-4]}  rms {np.sqrt((a**2).mean()):.2f} min/max env {min(env[3:-4]):.2f}/{max(env):.2f} hf {hf*100:.1f}% jump {jump:.2f}", fill="white")
sheet.save(sys.argv[2])
