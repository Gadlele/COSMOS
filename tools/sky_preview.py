"""COSMOS screen preview: the real sky over Rome, 240x240, Terminus text on top.
  python3 tools/sky_preview.py OUT.png ter-u16n.bdf "YYYY/MM/DD HH:MM" (UTC) [CLOUDS%] [sky]
Needs Pillow and ephem (pip3 install pillow ephem); the BDF is fm1-base's assets/fonts/ter-u16n.bdf.
ephem's catalog has only 116 bright stars: the firmware will carry 300-500. The weather values are examples."""
import math, random, sys
import ephem, ephem.stars
from PIL import Image, ImageDraw, BdfFontFile, ImageFont

OUT = sys.argv[1]
BDF = sys.argv[2]          # Terminus ter-u16n.bdf from fm1-base
WHEN = sys.argv[3]         # UTC, e.g. "2026/10/26 21:00"
CLOUDS = float(sys.argv[4]) if len(sys.argv) > 4 else 15

W = 240
R = 170                    # horizon radius: the square's corners touch the horizon
img = Image.new("L", (W, W), 0)
px = img.load()

obs = ephem.Observer()
obs.lat, obs.lon, obs.elevation = "41.9028", "12.4964", 21
obs.date = WHEN


def proj(alt, az):
    """Stereographic, zenith at the centre, north up, east on the left (looking up)."""
    r = R * math.tan((math.pi / 2 - alt) / 2)
    return W / 2 - r * math.sin(az), W / 2 - r * math.cos(az)


def grey(mag):
    # brightest ~ 175, faintest visible ~ 45: the sky stays in the dark half
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
limit -= CLOUDS / 100 * 4.0

def put(x, y, g):
    x, y = int(x), int(y)
    if 0 <= x < W and 0 <= y < W:
        px[x, y] = max(px[x, y], g)

random.seed(7)
for name in ephem.stars.stars:
    s = ephem.star(name, obs)
    if s.alt <= 0 or s.mag > limit:
        continue
    x, y = proj(s.alt, s.az)
    put(x, y, grey(s.mag) + random.randint(-10, 10))  # twinkle

for P, size in ((ephem.Venus, 2), (ephem.Jupiter, 2), (ephem.Saturn, 1), (ephem.Mars, 1), (ephem.Mercury, 1)):
    p = P(obs)
    if p.alt <= 0 or p.mag > limit + 1:
        continue
    x, y = map(int, proj(p.alt, p.az))
    g = min(185, grey(p.mag) + 10)
    for dx in range(size):
        for dy in range(size):
            put(x + dx, y + dy, g)
    if P is ephem.Saturn:
        put(x - 1, y, g // 2); put(x + 1, y, g // 2)

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
            put(mx + dx, my + dy, 210 if lit else 50)

# ---- text, Terminus 16, white with a 1-px black outline
pf = BdfFontFile.BdfFontFile(open(BDF, "rb"))
pf.save("terminus16")
font = ImageFont.load("terminus16.pil")
d = ImageDraw.Draw(img)

lines = [
    "COSMOS                    A2",
    "----------------------------",
    f"SUN    {sun_alt:+4.0f}°    FILTER    18",
    f"CLOUDS  {CLOUDS:3.0f}%    REVERB    {int(20 + CLOUDS * .6)}",
    "PRESS 1017 =    INTERVAL  P5",
    "KP         2    DRIFT     21",
    f"MOON    {moon.moon_phase * 100:3.0f}%    SUB       {int(moon.moon_phase * 100)}",
    "----------------------------",
    "SKY 100%          UPD 12 MIN",
]
if len(sys.argv) > 5 and sys.argv[5] == 'sky':
    lines = []
y0 = 120 - len(lines) * 16 // 2
for i, ln in enumerate(lines):
    x, y = 8, y0 + i * 16
    for ox in (-1, 0, 1):
        for oy in (-1, 0, 1):
            if ox or oy:
                d.text((x + ox, y + oy), ln, font=font, fill=0)
for i, ln in enumerate(lines):
    d.text((8, y0 + i * 16), ln, font=font, fill=255)

img.resize((W * 3, W * 3), Image.NEAREST).save(OUT)

sky = img.copy()
print(f"sun {sun_alt:.1f}  moon alt {math.degrees(moon.alt):.1f} phase {moon.moon_phase:.2f}  limit {limit:.1f}")
for P in (ephem.Venus, ephem.Jupiter, ephem.Saturn, ephem.Mars, ephem.Mercury):
    p = P(obs)
    print(P.__name__, f"alt {math.degrees(p.alt):.0f} az {math.degrees(p.az):.0f} mag {p.mag:.1f}")
