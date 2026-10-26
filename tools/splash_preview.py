"""COSMOS's splash, as a GIF: scattered stars gather into the word COSMOS, hold, and fade.
  python3 tools/splash_preview.py OUT.gif ter-u32b.bdf
Needs Pillow; the BDF is fm1-base's assets/fonts/ter-u32b.bdf. The firmware draws the same (app_splash)."""
import math, random, sys
from PIL import Image

OUT, BDF = sys.argv[1], sys.argv[2]
W, FPS = 240, 20
WORD = "COSMOS"


def glyphs(path):
    g, cur, rows, enc, w = {}, None, None, 0, 16
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "ENCODING":
            enc = int(p[1])
        elif p[0] == "BITMAP":
            rows = []
        elif p[0] == "ENDCHAR":
            g[chr(enc)] = rows
            rows = None
        elif rows is not None:
            rows.append(int(p[0], 16))
    return g


G = glyphs(BDF)
# the word in Terminus 32 bold (16 x 32 a letter), every other pixel a star (a checkerboard: dotted, not solid),
# at twice the size: 192 x 64, centred
targets = []
for i, ch in enumerate(WORD):
    for y, bits in enumerate(G[ch]):
        for x in range(16):
            if bits >> (15 - x) & 1 and (x + y) % 2 == 0:
                targets.append((24 + (i * 16 + x) * 2, 88 + y * 2))
rng = random.Random(3)
stars = []
for tx, ty in targets:
    stars.append({"x0": rng.uniform(0, W), "y0": rng.uniform(0, W), "tx": tx, "ty": ty,
                  "g0": rng.uniform(50, 150), "delay": rng.uniform(0, 0.45), "phase": rng.uniform(0, 6.3)})
sky = [(rng.uniform(0, W), rng.uniform(0, W), rng.uniform(35, 90), rng.uniform(0, 6.3)) for _ in range(45)]


def ease(t):
    t = max(0.0, min(1.0, t))
    return 4 * t * t * t if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2


def frame(t):
    im = Image.new("L", (W, W), 0)
    px = im.load()

    def put(x, y, v):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < W and 0 <= y < W and px[x, y] < v:
            px[x, y] = int(v)

    fade_in = min(1.0, t / 0.6)
    fade_out = 1.0 - max(0.0, min(1.0, (t - 3.6) / 0.6))
    for x, y, g, ph in sky:                       # the sky behind, dim, twinkling
        put(x, y, g * fade_in * fade_out * (0.75 + 0.25 * math.sin(t * 5 + ph)))
    for s in stars:
        k = ease((t - 0.6 - s["delay"]) / 1.4)    # 0.6 s scattered, then each glides in over 1.4 s
        x = s["x0"] + (s["tx"] - s["x0"]) * k
        y = s["y0"] + (s["ty"] - s["y0"]) * k
        g = s["g0"] + (235 - s["g0"]) * k         # brighter as they gather
        g *= 0.85 + 0.15 * math.sin(t * (7 - 4 * k) + s["phase"])
        put(x, y, g * fade_in * fade_out)
    return im


frames = [frame(n / FPS).resize((W * 2, W * 2), Image.NEAREST) for n in range(int(4.4 * FPS))]
frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=1000 // FPS, loop=0)
print(f"{OUT}: {len(frames)} frames, {len(targets)} stars in the word")
