"""COSMOS's splash, as a GIF: scattered stars gather into the word COSMOS, hold, and fade.
  python3 tools/splash_preview.py OUT.gif [FONT.ttf]
Needs Pillow. The word is set in Michroma (third_party/fonts/michroma, SIL OFL 1.1), 212 pixels wide, and a star
sits on every other pixel of it (a 2-pixel grid: dotted, not solid). The firmware draws the same from the stars'
positions alone (app_splash)."""
import math, random, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
FONT = sys.argv[2] if len(sys.argv) > 2 else str(Path(__file__).resolve().parents[1] / "third_party" / "fonts" /
                                                 "michroma" / "Michroma-Regular.ttf")
W, FPS = 240, 20
WORD = "COSMOS"
WIDE = 212                                       # the word's width on the screen, pixels


def word_targets():
    f = ImageFont.truetype(FONT, 44)
    m = Image.new("L", (W * 2, 120), 0)
    ImageDraw.Draw(m).text((0, 10), WORD, font=f, fill=255)
    m = m.crop(m.getbbox())
    m = m.resize((WIDE, max(1, round(m.height * WIDE / m.width))), Image.LANCZOS)
    ox, oy, mp = (W - m.width) // 2, (W - m.height) // 2, m.load()
    return [(ox + x, oy + y) for y in range(0, m.height, 2) for x in range(0, m.width, 2) if mp[x, y] > 128]


targets = word_targets()
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
