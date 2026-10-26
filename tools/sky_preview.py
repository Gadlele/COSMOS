"""COSMOS screen preview: the real sky over Rome, 240x240, Terminus text on top.
  python3 tools/sky_preview.py OUT.png  ter-u16n.bdf "YYYY/MM/DD HH:MM" [--clouds N] [--kp N] [--sky]
  python3 tools/sky_preview.py OUT.gif  ter-u16n.bdf "YYYY/MM/DD HH:MM" [--clouds N] [--kp N]
A .gif is the text fading in on a touch, holding, then fading out to the sky, the stars twinkling by Kp.
The time is UTC. Needs Pillow and ephem (pip3 install pillow ephem); the BDF is fm1-base's
assets/fonts/ter-u16n.bdf. ephem's catalog has only 116 bright stars: the firmware will carry 300-500.
The weather values are examples."""
import argparse, math, os, random, tempfile
import ephem, ephem.stars
from PIL import Image, ImageDraw, BdfFontFile, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("bdf")
ap.add_argument("when")
ap.add_argument("--clouds", type=float, default=15)
ap.add_argument("--kp", type=float, default=2)
ap.add_argument("--sky", action="store_true", help="the sky alone, no text")
args = ap.parse_args()

W = 240
R = 170                    # horizon radius: the square's corners touch the horizon

obs = ephem.Observer()
obs.lat, obs.lon, obs.elevation = "41.9028", "12.4964", 21
obs.date = args.when


def proj(alt, az):
    """Stereographic, zenith at the centre, north up, east on the left (looking up)."""
    r = R * math.tan((math.pi / 2 - alt) / 2)
    return W / 2 - r * math.sin(az), W / 2 - r * math.cos(az)


def grey(mag):
    # the sky stays in the dark half: the text alone is white
    return int(max(70, min(200, 200 - (mag + 1.5) * 26)))


sun = ephem.Sun(obs)
moon = ephem.Moon(obs)
sun_alt = math.degrees(sun.alt)
# sky brightness: the sun up hides everything, twilight the faint ones, the moon and clouds too
limit = 4.6
if sun_alt > -6:
    limit = -5
elif sun_alt > -12:
    limit = 1.0
elif sun_alt > -18:
    limit = 3.0
if moon.alt > 0:
    limit -= 0.8 * moon.moon_phase
limit -= args.clouds / 100 * 4.0

# what is up: (x, y, grey, kind)
points = []
for name in ephem.stars.stars:
    s = ephem.star(name, obs)
    if s.alt > 0 and s.mag <= limit:
        x, y = proj(s.alt, s.az)
        points.append((int(x), int(y), grey(s.mag), "star"))
for P, size in ((ephem.Venus, 2), (ephem.Jupiter, 2), (ephem.Saturn, 1), (ephem.Mars, 1), (ephem.Mercury, 1)):
    p = P(obs)
    if p.alt > 0 and p.mag <= limit + 1:
        x, y = map(int, proj(p.alt, p.az))
        g = min(185, grey(p.mag) + 10)
        points += [(x + dx, y + dy, g, "planet") for dx in range(size) for dy in range(size)]
        if P is ephem.Saturn:
            points += [(x - 1, y, g // 2, "planet"), (x + 1, y, g // 2, "planet")]
if moon.alt > 0:
    mx, my = map(int, proj(moon.alt, moon.az))
    k = moon.moon_phase                       # illuminated fraction
    waxing = ephem.next_full_moon(obs.date) < ephem.next_new_moon(obs.date)
    rr = 4
    for dy in range(-rr, rr + 1):
        for dx in range(-rr, rr + 1):
            if dx * dx + dy * dy > rr * rr + 1:
                continue
            half = math.sqrt(max(0, rr * rr - dy * dy))
            edge = half * (1 - 2 * k)         # terminator, from the lit limb
            lit = (dx > edge) if waxing else (-dx > edge)
            points.append((mx + dx, my + dy, 210 if lit else 50, "moon"))


def sky(rng):
    """One frame of the sky: the stars twinkle (more with Kp), planets and moon hold steady."""
    img = Image.new("L", (W, W), 0)
    px = img.load()
    tw = 6 + 6 * args.kp
    for x, y, g, kind in points:
        if kind == "star":
            g = int(max(30, min(210, g + rng.uniform(-tw, tw))))
        if 0 <= x < W and 0 <= y < W:
            px[x, y] = max(px[x, y], g)
    return img


# ---- text, Terminus 16, white with a 1-px black outline
tmp = tempfile.mkdtemp()
BdfFontFile.BdfFontFile(open(args.bdf, "rb")).save(os.path.join(tmp, "terminus16"))
font = ImageFont.load(os.path.join(tmp, "terminus16.pil"))

lines = [
    "COSMOS                    A2",
    "----------------------------",
    f"SUN    {sun_alt:+4.0f}°    FILTER    18",
    f"CLOUDS  {args.clouds:3.0f}%    REVERB    {int(20 + args.clouds * .6)}",
    "PRESS 1017 =    INTERVAL  P5",
    f"KP        {args.kp:2.0f}    DRIFT     {int(10 + args.kp * 6)}",
    f"MOON    {moon.moon_phase * 100:3.0f}%    SUB       {int(moon.moon_phase * 100)}",
    "----------------------------",
    "SKY 100%          UPD 12 MIN",
]
ink = Image.new("L", (W, W), 0)      # the letters
cover = Image.new("L", (W, W), 0)    # the letters and their outline: where the text hides the sky
di, dc = ImageDraw.Draw(ink), ImageDraw.Draw(cover)
y0 = 120 - len(lines) * 16 // 2
for i, ln in enumerate(lines):
    x, y = 8, y0 + i * 16
    for ox in (-1, 0, 1):
        for oy in (-1, 0, 1):
            dc.text((x + ox, y + oy), ln, font=font, fill=255)
    di.text((x, y), ln, font=font, fill=255)


def frame(rng, a):
    """The sky with the text at opacity a (0..1)."""
    out = sky(rng)
    if a > 0:
        o, i, c = out.load(), ink.load(), cover.load()
        for y in range(W):
            for x in range(W):
                if c[x, y]:
                    o[x, y] = int(o[x, y] * (1 - a) + i[x, y] * a)
    return out


rng = random.Random(7)
if args.out.endswith(".gif"):
    # 80 ms a frame: the sky, a touch, the text in (0.3 s), held (2 s), out (1.5 s), the sky again
    alphas = [0] * 8 + [n / 4 for n in range(1, 5)] + [1] * 25 + [1 - n / 19 for n in range(1, 20)] + [0] * 12
    frames = [frame(rng, a).resize((W * 2, W * 2), Image.NEAREST) for a in alphas]
    frames[0].save(args.out, save_all=True, append_images=frames[1:], duration=80, loop=0)
else:
    frame(rng, 0 if args.sky else 1).resize((W * 3, W * 3), Image.NEAREST).save(args.out)

print(f"sun {sun_alt:.1f}  moon alt {math.degrees(moon.alt):.1f} phase {moon.moon_phase:.2f}  limit {limit:.1f}")
