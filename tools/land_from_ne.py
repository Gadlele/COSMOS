#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The land's outline for VOYAGE's map (cosmos/data/land.txt), from Natural Earth's 1:110m land (public domain):
  curl -LO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson
  tools/land_from_ne.py ne_110m_land.geojson > cosmos/data/land.txt
Each ring simplified (Douglas-Peucker, 0.15 degrees), its points on a grid of 0.2 degrees, rings under half a degree
across left out. A line a ring: x y x y ..., x = (longitude + 180) / 0.2, y = (latitude + 90) / 0.2."""
import json
import math
import sys

GRID, TOL, MIN_EXTENT = 0.2, 0.15, 0.5


def simplify(pts, tol):
    if len(pts) < 3:
        return pts
    (ax, ay), (bx, by) = pts[0], pts[-1]
    dx, dy = bx - ax, by - ay
    n = math.hypot(dx, dy)
    best, bi = -1.0, 0
    for i in range(1, len(pts) - 1):
        px, py = pts[i]
        d = math.hypot(px - ax, py - ay) if n == 0 else abs(dy * (px - ax) - dx * (py - ay)) / n
        if d > best:
            best, bi = d, i
    if best <= tol:
        return [pts[0], pts[-1]]
    return simplify(pts[:bi + 1], tol)[:-1] + simplify(pts[bi:], tol)


def main():
    data = json.load(open(sys.argv[1]))
    print("# VOYAGE's land outline: Natural Earth 1:110m land (public domain), by tools/land_from_ne.py")
    for f in data["features"]:
        g = f["geometry"]
        for poly in g["coordinates"] if g["type"] == "MultiPolygon" else [g["coordinates"]]:
            for ring in poly:
                xs, ys = [p[0] for p in ring], [p[1] for p in ring]
                if max(max(xs) - min(xs), max(ys) - min(ys)) < MIN_EXTENT:
                    continue
                q = []
                for x, y in simplify(ring, TOL):
                    p = (round((x + 180) / GRID), round((y + 90) / GRID))
                    if not q or p != q[-1]:
                        q.append(p)
                if len(q) >= 3:
                    print(" ".join(f"{x} {y}" for x, y in q))


if __name__ == "__main__":
    main()
