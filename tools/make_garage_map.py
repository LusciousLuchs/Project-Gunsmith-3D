#!/usr/bin/env python3
"""The career's starter building, the home garage on a suburban street: writes assets/defaults/maps/garage.gsmap
(embedded by tools/embed_defaults.py). Laid out after the owner's own street map (the road, the lots, the houses
where they stood), with far more detail: houses with foundations, corner boards, windows (frames, sills, glass,
mullions), doors, porches, gable roofs with overhangs, fascia and chimneys; curbs, road markings, street lights,
trees, hedges, fences with posts, mailboxes, a hydrant, parked cars; and the player's garage fitted out as a
workshop (open roll-up door, a window, shelving, a water heater, lights).

Coordinates: metres, y up. The garage floor is at 0, lawns at G = -0.2. Every block is locked except the garage's
contents (the player's to move)."""
import itertools, json, math, os

G = -0.2                  # lawn height
ROAD = G - 0.15           # the road sits a curb below the sidewalk

brushes, ents = [], []

def hexc(c):
    return None if c is None else (c.upper() + 'FF' if len(c) == 6 else c.upper())

def hull_planes(pts):
    """the faces of the convex hull of pts: (normal, d) with dot(n, p) <= d inside"""
    out = []
    n = len(pts)
    for a, b, c in itertools.combinations(range(n), 3):
        pa, pb, pc = pts[a], pts[b], pts[c]
        u = [pb[i] - pa[i] for i in range(3)]; v = [pc[i] - pa[i] for i in range(3)]
        nn = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        L = math.sqrt(sum(x * x for x in nn))
        if L < 1e-9:
            continue
        nn = [x / L for x in nn]
        d = sum(nn[i] * pa[i] for i in range(3))
        above = below = 0
        for p in pts:
            e = sum(nn[i] * p[i] for i in range(3)) - d
            if e > 1e-7: above += 1
            elif e < -1e-7: below += 1
        if above and below:
            continue
        if not above and not below:
            continue
        if above:
            nn = [-x for x in nn]; d = -d
        if any(abs(o[1] - d) < 1e-6 and sum(o[0][i] * nn[i] for i in range(3)) > 1 - 1e-7 for o in out):
            continue
        out.append((nn, d))
    return out

def solid(pts, mat, color=None, scale=1.0, locked=True, look=None):
    """a block round these points; look(normal) -> (mat, color, scale) per face, else mat/color/scale"""
    faces = []
    for nn, d in hull_planes(pts):
        m, c, s = (look(nn) if look else None) or (mat, color, scale)
        f = {"n": [round(x, 7) for x in nn], "d": round(d, 6), "mat": m}
        if s != 1: f["scale"] = s
        if c: f["color"] = hexc(c)
        faces.append(f)
    b = {"faces": faces}
    if locked: b["locked"] = True
    brushes.append(b)
    return b

def box(a, b, mat, color=None, scale=1.0, locked=True, look=None):
    x0, x1 = sorted((a[0], b[0])); y0, y1 = sorted((a[1], b[1])); z0, z1 = sorted((a[2], b[2]))
    if x1 - x0 < 1e-4 or y1 - y0 < 1e-4 or z1 - z0 < 1e-4:
        return None
    pts = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    return solid(pts, mat, color, scale, locked, look)

def cyl(cx, cz, r, y0, y1, mat, color=None, sides=10, locked=True, scale=1.0):
    pts = []
    for k in range(sides):
        t = 2 * math.pi * k / sides + math.pi / sides
        for y in (y0, y1):
            pts.append((cx + r * math.cos(t), y, cz + r * math.sin(t)))
    return solid(pts, mat, color, scale, locked)

def hcyl(c, r, half, axis, mat, color=None, sides=12, locked=True):
    """a cylinder lying along x or z (wheels)"""
    pts = []
    for k in range(sides):
        t = 2 * math.pi * k / sides
        for s in (-half, half):
            if axis == 'x': pts.append((c[0] + s, c[1] + r * math.cos(t), c[2] + r * math.sin(t)))
            else: pts.append((c[0] + r * math.cos(t), c[1] + r * math.sin(t), c[2] + s))
    return solid(pts, mat, color, 1.0, locked)

def ball(c, r, mat, color=None, sides=8, squash=1.0, locked=True, scale=1.0):
    pts = [(c[0], c[1] + r * squash, c[2]), (c[0], c[1] - r * squash, c[2])]
    rings = 4
    for i in range(1, rings):
        ph = math.pi * i / rings
        for k in range(sides):
            t = 2 * math.pi * k / sides + (i % 2) * math.pi / sides
            pts.append((c[0] + r * math.sin(ph) * math.cos(t), c[1] + r * squash * math.cos(ph), c[2] + r * math.sin(ph) * math.sin(t)))
    return solid(pts, mat, color, scale, locked)

def ent(t, pos, yaw=0.0, **kw):
    e = {"type": t, "pos": [round(pos[0], 4), round(pos[1], 4), round(pos[2], 4)], "yaw": yaw}
    e.update(kw)
    ents.append(e)

def lamp(pos, power, rng, color=(1.0, 0.86, 0.66), cone=0.0, pitch=-90.0):
    ent("light", pos, 0.0, light={"color": list(color), "power": power, "range": rng, "cone": cone, "pitch": pitch})

# ---------------------------------------------------------------- the ground, the road, the sidewalks
box((-24, -1.2, -60), (18.5, G, 52), "grass", "8DB36A", 0.6)         # west lawns
box((29.5, -1.2, -60), (72, G, 52), "grass", "8DB36A", 0.6)          # east lawns
box((18.5, -1.2, -60), (20.1, G + 0.02, 52), "concrete", "C9C6BE", 0.5)   # west sidewalk (its edge is the curb)
box((27.9, -1.2, -60), (29.5, G + 0.02, 52), "concrete", "C9C6BE", 0.5)   # east sidewalk
box((20.1, -1.2, -60), (27.9, ROAD, 52), "asphalt", "6E6E70", 1.0)  # the road
box((23.88, ROAD, -60), (23.96, ROAD + 0.006, 52), "paint_white", "E2B93B")
box((24.04, ROAD, -60), (24.12, ROAD + 0.006, 52), "paint_white", "E2B93B")
box((20.45, ROAD, -60), (20.55, ROAD + 0.006, 52), "paint_white", "E8E8E2")
box((27.45, ROAD, -60), (27.55, ROAD + 0.006, 52), "paint_white", "E8E8E2")

def apron(x_side, z0, z1):
    """a driveway's dip down to the road at the curb (west side x_side=-1, east +1)"""
    if x_side < 0:
        solid([(20.1, ROAD, z0), (20.1, ROAD, z1), (20.1, G + 0.02, z0), (20.1, G + 0.02, z1), (20.75, ROAD, z0), (20.75, ROAD, z1)], "concrete", "BEBAB1", 0.5)
    else:
        solid([(27.9, ROAD, z0), (27.9, ROAD, z1), (27.9, G + 0.02, z0), (27.9, G + 0.02, z1), (27.25, ROAD, z0), (27.25, ROAD, z1)], "concrete", "BEBAB1", 0.5)

# ---------------------------------------------------------------- street furniture
def street_light(x, z, arm):   # arm: -1 reaches toward -x
    cyl(x, z, 0.09, G, G + 6.4, "steel", "4A4F55", 8)
    cyl(x, z, 0.16, G, G + 0.5, "steel", "3E4247", 8)
    ax = x + arm * 1.9
    box((min(x, ax), G + 6.25, z - 0.05), (max(x, ax), G + 6.35, z + 0.05), "steel", "4A4F55")
    box((ax - 0.35, G + 6.12, z - 0.18), (ax + 0.35, G + 6.27, z + 0.18), "steel", "3E4247")
    box((ax - 0.3, G + 6.1, z - 0.14), (ax + 0.3, G + 6.12, z + 0.14), "paint_white", "FFF3D6")
    lamp((ax, G + 5.9, z), 5.5, 16, (1.0, 0.82, 0.58), 130, -90)

for z in (-42, -18, 6, 30):
    street_light(28.9, z, -1)

def hydrant(x, z):
    cyl(x, z, 0.13, G, G + 0.62, "steel", "B8352C", 8)
    ball((x, G + 0.66, z), 0.14, "steel", "B8352C", 8, 0.7)
    hcyl((x, G + 0.42, z), 0.06, 0.24, 'x', "steel", "B8352C", 8)

hydrant(28.4, -6.2)

def mailbox(x, z, color):
    box((x - 0.05, G, z - 0.05), (x + 0.05, G + 1.0, z + 0.05), "wood_planks", "6B5440")
    box((x - 0.24, G + 1.0, z - 0.12), (x + 0.24, G + 1.24, z + 0.12), "steel", color)
    box((x - 0.02, G + 1.12, z + 0.12), (x + 0.02, G + 1.34, z + 0.14), "steel", "C33A2E")   # the flag

def tree(x, z, h=1.0, tint="4A7A34"):
    cyl(x, z, 0.17 * h, G, G + 2.6 * h, "wood_planks", "5A4632", 8)
    ball((x, G + 3.3 * h, z), 1.7 * h, "grass", tint, 9, 0.85, scale=0.4)
    ball((x + 0.6 * h, G + 4.2 * h, z - 0.3 * h), 1.25 * h, "grass", "3E6B2E", 8, 0.85, scale=0.4)
    ball((x - 0.4 * h, G + 4.8 * h, z + 0.4 * h), 0.95 * h, "grass", "567F3A", 8, 0.9, scale=0.4)

def bush(x, z, r=0.5, tint="3F6A2F"):
    ball((x, G + r * 0.7, z), r, "grass", tint, 8, 0.75, scale=0.35)

def hedge(x0, x1, z0, z1, h=0.95):
    box((x0, G, z0), (x1, G + h, z1), "grass", "2F5A28", 0.35)
    box((x0 + 0.05, G + h, z0 + 0.05), (x1 - 0.05, G + h + 0.08, z1 - 0.05), "grass", "3A6B2F", 0.35)

def fence(a, b, h=1.8, tint="B08A5E"):
    """a privacy fence from a to b (along x or z) with posts every 2.4 m and a top rail"""
    (xa, za), (xb, zb) = a, b
    along_x = abs(xb - xa) > abs(zb - za)
    if along_x:
        x0, x1 = sorted((xa, xb)); box((x0, G, za - 0.025), (x1, G + h, za + 0.025), "wood_planks", tint, 0.6)
        box((x0, G + h, za - 0.05), (x1, G + h + 0.06, za + 0.05), "wood_planks", "8E6E48")
        n = max(1, int((x1 - x0) / 2.4))
        for i in range(n + 1):
            x = x0 + (x1 - x0) * i / n
            box((x - 0.05, G, za - 0.06), (x + 0.05, G + h + 0.12, za + 0.06), "wood_planks", "7D6140")
    else:
        z0, z1 = sorted((za, zb)); box((xa - 0.025, G, z0), (xa + 0.025, G + h, z1), "wood_planks", tint, 0.6)
        box((xa - 0.05, G + h, z0), (xa + 0.05, G + h + 0.06, z1), "wood_planks", "8E6E48")
        n = max(1, int((z1 - z0) / 2.4))
        for i in range(n + 1):
            z = z0 + (z1 - z0) * i / n
            box((xa - 0.06, G, z - 0.05), (xa + 0.06, G + h + 0.12, z + 0.05), "wood_planks", "7D6140")

def car(cx, cz, along, color):
    """a parked car (body, cabin with glass, wheels), long side along x or z"""
    L, W = 4.4, 1.8
    hx, hz = (L / 2, W / 2) if along == 'x' else (W / 2, L / 2)
    y0 = G + 0.28
    box((cx - hx, y0, cz - hz), (cx + hx, y0 + 0.62, cz + hz), "metal_plate", color, 0.5)
    # the cabin: narrower at the top (glass on its sides)
    lx, lz = (L * 0.28, W / 2 - 0.05) if along == 'x' else (W / 2 - 0.05, L * 0.28)
    tx, tz = (L * 0.2, W / 2 - 0.2) if along == 'x' else (W / 2 - 0.2, L * 0.2)
    ox, oz = (-0.2, 0) if along == 'x' else (0, -0.2)
    pts = [(cx + ox + sx * lx, y0 + 0.62, cz + oz + sz * lz) for sx in (-1, 1) for sz in (-1, 1)] + \
          [(cx + ox + sx * tx, y0 + 1.12, cz + oz + sz * tz) for sx in (-1, 1) for sz in (-1, 1)]
    solid(pts, "glass", None, 1.0, True, look=lambda n: ("metal_plate", color, 0.5) if n[1] > 0.9 else ("glass", None, 1.0))
    for sx in (-1, 1):
        for sz in (-1, 1):
            if along == 'x': hcyl((cx + sx * (L / 2 - 0.75), G + 0.34, cz + sz * (W / 2 - 0.05)), 0.34, 0.11, 'z', "rust", "1E1E20", 12)
            else: hcyl((cx + sx * (W / 2 - 0.05), G + 0.34, cz + sz * (L / 2 - 0.75)), 0.34, 0.11, 'x', "rust", "1E1E20", 12)
    # lights
    if along == 'x':
        for sz in (-1, 1):
            box((cx + hx, y0 + 0.4, cz + sz * 0.6 - 0.15), (cx + hx + 0.02, y0 + 0.52, cz + sz * 0.6 + 0.15), "paint_white", "FFF6D8")
            box((cx - hx - 0.02, y0 + 0.4, cz + sz * 0.6 - 0.15), (cx - hx, y0 + 0.52, cz + sz * 0.6 + 0.15), "paint_white", "B3241E")
    else:
        for sx in (-1, 1):
            box((cx + sx * 0.6 - 0.15, y0 + 0.4, cz + hz), (cx + sx * 0.6 + 0.15, y0 + 0.52, cz + hz + 0.02), "paint_white", "FFF6D8")
            box((cx + sx * 0.6 - 0.15, y0 + 0.4, cz - hz - 0.02), (cx + sx * 0.6 + 0.15, y0 + 0.52, cz - hz), "paint_white", "B3241E")

# ---------------------------------------------------------------- houses
def window_on(face_x, sign, z0, z1, y0, y1, trim, shutters=None, wall_out=None):
    """a window on a wall facing +x (sign 1) or -x (sign -1) at x = face_x, spanning z0..z1, y0..y1"""
    s = sign
    def X(a, b): return (face_x + s * a, face_x + s * b)
    xa, xb = X(0.0, 0.012); box((xa, y0, z0), (xb, y1, z1), "plywood", "1B2028")                 # the dark room behind
    xa, xb = X(0.0, 0.03);  box((xa, y0 + 0.06, z0 + 0.06), (xb, y1 - 0.06, z1 - 0.06), "glass")    # the pane
    t = 0.08
    xa, xb = X(0.0, 0.06)
    box((xa, y0, z0 - t), (xb, y1 + t, z0), "paint_white", trim)          # frame
    box((xa, y0, z1), (xb, y1 + t, z1 + t), "paint_white", trim)
    box((xa, y1, z0 - t), (xb, y1 + t, z1 + t), "paint_white", trim)
    xa, xb = X(0.0, 0.1); box((xa, y0 - 0.06, z0 - 0.12), (xb, y0, z1 + 0.12), "paint_white", trim)   # sill
    xa, xb = X(0.0, 0.035)
    zm, ym = (z0 + z1) / 2, (y0 + y1) / 2
    box((xa, y0, zm - 0.02), (xb, y1, zm + 0.02), "paint_white", trim)    # mullions
    box((xa, ym - 0.02, z0), (xb, ym + 0.02, z1), "paint_white", trim)
    if shutters:
        xa, xb = X(0.0, 0.04)
        w = (z1 - z0) * 0.45
        box((xa, y0, z0 - t - w), (xb, y1, z0 - t - 0.03), "wood_planks", shutters, 0.4)
        box((xa, y0, z1 + t + 0.03), (xb, y1, z1 + t + w), "wood_planks", shutters, 0.4)

def window_on_z(face_z, sign, x0, x1, y0, y1, trim):
    s = sign
    def Z(a, b): return (face_z + s * a, face_z + s * b)
    za, zb = Z(0.0, 0.012); box((x0, y0, za), (x1, y1, zb), "plywood", "1B2028")
    za, zb = Z(0.0, 0.03);  box((x0 + 0.06, y0 + 0.06, za), (x1 - 0.06, y1 - 0.06, zb), "glass")
    t = 0.08
    za, zb = Z(0.0, 0.06)
    box((x0 - t, y0, za), (x0, y1 + t, zb), "paint_white", trim)
    box((x1, y0, za), (x1 + t, y1 + t, zb), "paint_white", trim)
    box((x0 - t, y1, za), (x1 + t, y1 + t, zb), "paint_white", trim)
    za, zb = Z(0.0, 0.1); box((x0 - 0.12, y0 - 0.06, za), (x1 + 0.12, y0, zb), "paint_white", trim)
    za, zb = Z(0.0, 0.035)
    xm, ym = (x0 + x1) / 2, (y0 + y1) / 2
    box((xm - 0.02, y0, za), (xm + 0.02, y1, zb), "paint_white", trim)
    box((x0, ym - 0.02, za), (x1, ym + 0.02, zb), "paint_white", trim)

def gable_roof(x0, x1, z0, z1, ywall, rise, roof, trim, wall_mat, wall_col, over=0.45, gable_over=0.35):
    """a roof along z: the gable triangles in the wall's own finish, two sloped slabs with an overhang, fascia, ridge cap"""
    xm = (x0 + x1) / 2
    yr = ywall + rise
    for zz in (z0, z1):   # the gable ends (a triangle prism a hair inside the slabs)
        solid([(x0, ywall, zz), (x1, ywall, zz), (xm, yr - 0.05, zz), (x0, ywall, zz + (0.3 if zz == z0 else -0.3)),
               (x1, ywall, zz + (0.3 if zz == z0 else -0.3)), (xm, yr - 0.05, zz + (0.3 if zz == z0 else -0.3))], wall_mat, wall_col, 0.5)
    slope = rise / (xm - x0)
    t = 0.16
    ye = ywall - over * slope
    for side in (-1, 1):
        xe = x0 - over if side < 0 else x1 + over
        pts = []
        for zz in (z0 - gable_over, z1 + gable_over):
            pts += [(xe, ye, zz), (xm, yr, zz), (xe, ye + t, zz), (xm, yr + t, zz)]
        solid(pts, "shingles", roof, 0.5, True,
              look=lambda n: ("plywood", "EDE8DE", 1.0) if n[1] < -0.3 else ("shingles", roof, 0.5) if n[1] > 0.3 else ("paint_white", trim, 1.0))
        fx = xe - 0.03 * (1 if side > 0 else -1)
        box((min(xe, xe - side * 0.05), ye - 0.18, z0 - gable_over), (max(xe, xe - side * 0.05) + (0.03 if side > 0 else -0.03) * 0, ye + 0.02, z1 + gable_over), "paint_white", trim)   # fascia
        box((min(xe, xe + side * 0.12), ye - 0.16, z0 - gable_over), (max(xe, xe + side * 0.12), ye - 0.04, z1 + gable_over), "steel", "E9E9E4")   # gutter
    box((xm - 0.18, yr + t - 0.02, z0 - gable_over), (xm + 0.18, yr + t + 0.07, z1 + gable_over), "shingles", roof, 0.5)   # ridge cap
    for zz in (z0, z1):   # rake boards along the gable edges
        sgn = -1 if zz == z0 else 1
        zo = zz + sgn * gable_over
        for side in (-1, 1):
            xe = x0 - over if side < 0 else x1 + over
            solid([(xe, ye - 0.12, zo), (xm, yr - 0.12, zo), (xe, ye + t, zo), (xm, yr + t, zo),
                   (xe, ye - 0.12, zo - sgn * 0.04), (xm, yr - 0.12, zo - sgn * 0.04), (xe, ye + t, zo - sgn * 0.04), (xm, yr + t, zo - sgn * 0.04)], "paint_white", trim)

def house(x0, x1, z0, z1, front, wall_mat, wall_col, trim, roof, door_z, garage_z=None, shutters=None, chimney=True,
          porch=True, h=2.8, rise=2.2, porch_light=True, wscale=0.5):
    """a one-storey house: front = +1 (faces +x at x1) or -1 (faces -x at x0)"""
    fx = x1 if front > 0 else x0
    box((x0 - 0.05, G, z0 - 0.05), (x1 + 0.05, G + 0.45, z1 + 0.05), "concrete", "A9A49A", 0.6)   # foundation, a little proud
    box((x0, G + 0.45, z0), (x1, G + h, z1), wall_mat, wall_col, wscale)
    for x in (x0, x1):   # corner boards
        for z in (z0, z1):
            box((x - 0.04, G + 0.45, z - 0.04), (x + 0.04, G + h, z + 0.04), "paint_white", trim)
    box((x0 - 0.03, G + h - 0.18, z0 - 0.03), (x1 + 0.03, G + h, z1 + 0.03), "paint_white", trim)   # frieze under the eaves
    gable_roof(x0, x1, z0, z1, G + h, rise, roof, trim, wall_mat, wall_col)
    if chimney:
        cz = z0 + (z1 - z0) * 0.78
        cx = (x0 + x1) / 2 - front * 2.0
        box((cx - 0.45, G + h, cz - 0.4), (cx + 0.45, G + h + rise + 0.9, cz + 0.4), "brick", "9A5A44", 0.5)
        box((cx - 0.52, G + h + rise + 0.9, cz - 0.47), (cx + 0.52, G + h + rise + 1.0, cz + 0.47), "concrete", "8F8B84")
    # the front: door, windows, garage door, porch
    y0w, y1w = G + 1.0, G + 2.3
    zs = sorted([door_z] + ([sum(garage_z) / 2] if garage_z else []))
    open_spans = [(door_z - 0.9, door_z + 0.9)] + ([(garage_z[0] - 0.3, garage_z[1] + 0.3)] if garage_z else [])
    def free(za, zb): return all(zb < a or za > b for a, b in open_spans)
    span = z1 - z0
    for k in range(5):
        zc = z0 + span * (k + 0.5) / 5
        za, zb = zc - 0.7, zc + 0.7
        if za > z0 + 0.5 and zb < z1 - 0.5 and free(za, zb):
            window_on(fx, front, za, zb, y0w, y1w, trim, shutters)
    # the door
    s = front
    dx0, dx1 = sorted((fx, fx + s * 0.04))
    box((dx0, G + 0.6, door_z - 0.48), (dx1, G + 2.62, door_z + 0.48), "door_wood", "7A4A2C" if not shutters else shutters, 1.0)
    dx0, dx1 = sorted((fx, fx + s * 0.07))
    box((dx0, G + 0.6, door_z - 0.6), (dx1, G + 2.74, door_z - 0.48), "paint_white", trim)
    box((dx0, G + 0.6, door_z + 0.48), (dx1, G + 2.74, door_z + 0.6), "paint_white", trim)
    box((dx0, G + 2.62, door_z - 0.6), (dx1, G + 2.74, door_z + 0.6), "paint_white", trim)
    hx0, hx1 = sorted((fx + s * 0.04, fx + s * 0.09))
    box((hx0, G + 1.55, door_z + 0.32), (hx1, G + 1.62, door_z + 0.38), "steel", "C9A94A")   # the handle
    if porch:   # a porch slab with a step, a little roof on two posts, a light
        px0, px1 = sorted((fx, fx + s * 1.7))
        box((px0, G, door_z - 1.3), (px1, G + 0.45, door_z + 1.3), "concrete", "B6B1A7", 0.6)
        sx0, sx1 = sorted((fx + s * 1.7, fx + s * 2.05))
        box((sx0, G, door_z - 0.8), (sx1, G + 0.22, door_z + 0.8), "concrete", "B6B1A7", 0.6)
        for zz in (door_z - 1.2, door_z + 1.2):
            pxc = fx + s * 1.55
            box((pxc - 0.07, G + 0.45, zz - 0.07), (pxc + 0.07, G + 2.6, zz + 0.07), "paint_white", trim)
        rx0, rx1 = sorted((fx, fx + s * 1.8))
        solid([(rx0, G + 2.6, door_z - 1.4), (rx1, G + 2.6, door_z - 1.4), (rx0, G + 2.6, door_z + 1.4), (rx1, G + 2.6, door_z + 1.4),
               (fx, G + 2.95, door_z - 1.4), (fx, G + 2.95, door_z + 1.4), (fx + s * 1.8, G + 2.72, door_z - 1.4), (fx + s * 1.8, G + 2.72, door_z + 1.4)],
              "shingles", roof, 0.5, True, look=lambda n: ("plywood", "EDE8DE", 1.0) if n[1] < -0.3 else ("shingles", roof, 0.5) if n[1] > 0.3 else ("paint_white", trim, 1.0))
        if porch_light:
            lx0, lx1 = sorted((fx, fx + s * 0.1))
            box((lx0, G + 2.0, door_z - 0.85), (lx1, G + 2.25, door_z - 0.72), "steel", "2B2B2B")
            box((lx0, G + 2.03, door_z - 0.83), (lx1, G + 2.2, door_z - 0.74), "paint_white", "FFE9B0")
            lamp((fx + s * 0.4, G + 2.1, door_z - 0.78), 2.2, 5, (1.0, 0.8, 0.55))
    if garage_z:   # a closed garage door (panels), its frame, and a light over it
        ga, gb = garage_z
        gx0, gx1 = sorted((fx, fx + s * 0.03))
        box((gx0, G + 0.45, ga), (gx1, G + 2.55, gb), "paint_white", "E8E6E0")
        for k in range(1, 4):
            y = G + 0.45 + 2.1 * k / 4
            box((gx0, y - 0.015, ga), (gx1 + s * 0.005 if s > 0 else gx1, y + 0.015, gb), "paint_white", "C9C6BE")
        fx0, fx1 = sorted((fx, fx + s * 0.07))
        box((fx0, G + 0.45, ga - 0.12), (fx1, G + 2.67, ga), "paint_white", trim)
        box((fx0, G + 0.45, gb), (fx1, G + 2.67, gb + 0.12), "paint_white", trim)
        box((fx0, G + 2.55, ga - 0.12), (fx1, G + 2.67, gb + 0.12), "paint_white", trim)
    # windows on the sides (along z) and the back
    for zz, sg in ((z0, -1), (z1, 1)):
        n = max(1, int((x1 - x0) / 4.5))
        for k in range(n):
            xc = x0 + (x1 - x0) * (k + 0.5) / n
            window_on_z(zz, sg, xc - 0.6, xc + 0.6, y0w, y1w, trim)
    bx = x0 if front > 0 else x1
    for k in range(3):
        zc = z0 + span * (k + 0.5) / 3
        window_on(bx, -front, zc - 0.6, zc + 0.6, y0w, y1w, trim)
    # bushes along the front
    for k in range(6):
        zc = z0 + span * (k + 0.5) / 6
        if free(zc - 0.6, zc + 0.6) and abs(zc - door_z) > 1.6:
            bush(fx + front * 0.55, zc, 0.45 + 0.08 * (k % 2))

# ---------------------------------------------------------------- the lots: west side faces the road (+x), east side faces it (-x)
# lot lines and back fences
fence((-15.0, -19.2), (6.6, -19.2)); fence((-15.0, 9.65), (6.6, 9.65)); fence((-15.0, -51.6), (6.6, -51.6)); fence((-15.0, 44.0), (6.6, 44.0))
fence((-15.0, -51.6), (-15.0, 44.0))
fence((41.5, -19.9), (63.0, -19.9)); fence((41.5, 11.9), (63.0, 11.9)); fence((41.5, -51.55), (63.0, -51.55)); fence((41.5, 43.95), (63.0, 43.95))
fence((63.0, -51.55), (63.0, 43.95))

# the north neighbour (west side): pale blue siding, navy shutters, a garage door on the front
house(-6.0, 6.0, 16.0, 32.0, 1, "wood_planks", "A9C0D2", "F4F2EC", "3B3D42", 27.4, garage_z=(16.6, 19.6), shutters="2E3E5C")
box((6.0, G, 16.4), (18.5, G + 0.02, 19.8), "concrete", "C2BEB5", 0.5); apron(-1, 16.4, 19.8)
box((6.0 + 2.05, G, 26.9), (18.5, G + 0.02, 27.9), "concrete", "C2BEB5", 0.5)
car(12.5, 18.1, 'x', "8C1F1F")
mailbox(18.0, 20.6, "2B2B2B")
hedge(17.4, 18.2, 20.8, 26.4); hedge(17.4, 18.2, 28.4, 43.6); hedge(17.4, 18.2, 10.0, 16.0)
tree(12.0, 36.0, 1.05); tree(-11.0, 22.0, 0.9, "557F37"); tree(-10.0, 38.0, 1.1)

# the south neighbour (west side): old brick, cream trim, brown roof
house(-6.0, 6.3, -42.9, -26.9, 1, "old_brick", "C8A189", "EFE6CF", "5A4234", -31.2, garage_z=(-42.2, -39.2))
box((6.3, G, -42.6), (18.5, G + 0.02, -38.8), "concrete", "C2BEB5", 0.5); apron(-1, -42.6, -38.8)
box((6.3 + 2.05, G, -31.7), (18.5, G + 0.02, -30.7), "concrete", "C2BEB5", 0.5)
mailbox(18.0, -38.0, "3A5C3A")
hedge(17.4, 18.2, -51.2, -42.9); hedge(17.4, 18.2, -38.3, -32.0); hedge(17.4, 18.2, -30.2, -19.6)
tree(12.5, -25.0, 1.0); tree(-11.0, -46.0, 1.1); tree(-9.0, -24.0, 0.85, "5B8540")

# the east side: three houses facing the road (fronts at x = 41.76, facing -x)
EAST = [((-44.15, -28.15), "plaster", "A9B79C", "F2EFE6", "445046", -40.2, (-31.0, -28.6), "3F5A3B"),
        ((-12.15, 3.85), "wood_planks", "E8DEC4", "FFFFFF", "6E3B30", -8.2, (0.3, 3.3), None),
        ((19.55, 35.55), "brick", "B07560", "EDE7DC", "34363B", 23.6, (32.0, 35.0), "1F1F22")]
for (zz, wm, wc, tr, rf, dz, gz, sh) in EAST:
    house(41.76, 53.76, zz[0], zz[1], -1, wm, wc, tr, rf, dz, garage_z=gz, shutters=sh)
    box((29.5, G, gz[0] - 0.3), (41.76, G + 0.02, gz[1] + 0.3), "concrete", "C2BEB5", 0.5); apron(1, gz[0] - 0.3, gz[1] + 0.3)
    box((29.5, G, dz - 0.5), (41.76 - 2.05, G + 0.02, dz + 0.5), "concrete", "C2BEB5", 0.5)
    mailbox(30.0, gz[0] - 1.0, "2B2B2B")
    hedge(29.8, 30.6, zz[0] - 6.5, dz - 0.8); hedge(29.8, 30.6, dz + 0.8, gz[0] - 1.6)
    tree(36.0, zz[0] - 3.5, 1.0); tree(58.0, (zz[0] + zz[1]) / 2, 1.1, "557F37")
car(36.0, 1.8, 'x', "D8D8D2")
car(26.6, -16.0, 'z', "27486B")   # parked at the east curb
car(26.6, 14.0, 'z', "5B5E61")

# ---------------------------------------------------------------- your house and garage (west side, middle lot)
TRIM, ROOF = "F4F2EC", "4A4C52"
# the main house (solid: lived in), brick with white trim
box((-6.45, -0.45, -12.35), (6.45, 0.0, 5.15), "concrete", "A9A49A", 0.6)   # the slab and foundation (shows above the lawn)
box((-6.4, 0.0, -4.8), (6.4, 2.8, 5.1), "brick", "B8705A", 0.5)
for x in (-6.4, 6.4):
    for z in (-12.3, 5.1):
        box((x - 0.04, 0.0, z - 0.04), (x + 0.04, 2.8, z + 0.04), "paint_white", TRIM)
for (a, b) in (((-6.43, 2.62, -12.33), (-6.4, 2.8, 5.13)), ((6.4, 2.62, -12.33), (6.43, 2.8, 5.13)),
               ((-6.43, 2.62, -12.33), (6.43, 2.8, -12.3)), ((-6.43, 2.62, 5.1), (6.43, 2.8, 5.13))):
    box(a, b, "paint_white", TRIM)   # the trim band round the top of the walls (outside only)
gable_roof(-6.4, 6.4, -12.3, 5.1, 2.8, 2.4, ROOF, TRIM, "brick", "B8705A")
box((-3.4, 2.8, 2.6), (-2.5, 6.4, 3.5), "brick", "9A5A44", 0.5); box((-3.47, 6.4, 2.53), (-2.43, 6.5, 3.57), "concrete", "8F8B84")
# its front: the door (x = 6.4, at z 1.0), windows, a porch
window_on(6.4, 1, -3.8, -2.4, 1.0, 2.3, TRIM, "2E3E5C"); window_on(6.4, 1, 2.9, 4.3, 1.0, 2.3, TRIM, "2E3E5C")
box((6.4, 0.05, 0.52), (6.44, 2.1, 1.48), "door_wood", "2E3E5C")
box((6.4, 0.05, 0.4), (6.47, 2.22, 0.52), "paint_white", TRIM); box((6.4, 0.05, 1.48), (6.47, 2.22, 1.6), "paint_white", TRIM); box((6.4, 2.1, 0.4), (6.47, 2.22, 1.6), "paint_white", TRIM)
box((6.44, 1.0, 1.32), (6.49, 1.07, 1.38), "steel", "C9A94A")
box((6.4, -0.2, -0.4), (8.1, 0.0, 2.4), "concrete", "B6B1A7", 0.6)   # porch (one step up from the path)
box((8.1, -0.2, 0.2), (8.45, -0.1, 1.8), "concrete", "B6B1A7", 0.6)
for zz in (-0.3, 2.3):
    box((7.88, 0.0, zz - 0.07), (8.02, 2.5, zz + 0.07), "paint_white", TRIM)
solid([(6.4, 2.5, -0.5), (8.2, 2.5, -0.5), (6.4, 2.5, 2.5), (8.2, 2.5, 2.5), (6.4, 2.85, -0.5), (6.4, 2.85, 2.5), (8.2, 2.62, -0.5), (8.2, 2.62, 2.5)],
      "shingles", ROOF, 0.5, True, look=lambda n: ("plywood", "EDE8DE", 1.0) if n[1] < -0.3 else ("shingles", ROOF, 0.5) if n[1] > 0.3 else ("paint_white", TRIM, 1.0))
box((6.4, 1.6, -0.2), (6.5, 1.85, -0.07), "steel", "2B2B2B"); lamp((6.8, 1.75, -0.13), 2.2, 5, (1.0, 0.8, 0.55))
for zz, sg in ((5.1, 1),):
    for xc in (-3.0, 2.0): window_on_z(zz, sg, xc - 0.6, xc + 0.6, 1.0, 2.3, TRIM)
for zc in (-2.5, 2.5): window_on(-6.4, -1, zc - 0.6, zc + 0.6, 1.0, 2.3, TRIM)
box((8.45, G, 0.5), (18.5, G + 0.02, 1.5), "concrete", "C2BEB5", 0.5)     # the path to the sidewalk
for zc in (-3.4, 3.4, 4.6): bush(6.95, zc, 0.45)

# the garage: brick outside, painted drywall inside; a wide open door on the front, a window on the side
IN = ("plaster", "ECE6D8", 1.0)
def gwall(a, b, inward):   # a wall whose face toward the garage is drywall
    box(a, b, "brick", "B8705A", 0.5, True, look=lambda n: IN if sum(n[i] * inward[i] for i in range(3)) > 0.9 else ("brick", "B8705A", 0.5))
gwall((-6.4, 0.0, -12.3), (-6.0, 2.8, -4.8), (1, 0, 0))                       # back
gwall((-6.0, 0.0, -5.1), (6.4, 2.8, -4.8), (0, 0, -1))                        # the house wall
# the side wall with a window (x -2.2..-0.2, y 1.0..2.0)
gwall((-6.0, 0.0, -12.3), (-2.2, 2.8, -11.95), (0, 0, 1)); gwall((-0.2, 0.0, -12.3), (6.4, 2.8, -11.95), (0, 0, 1))
gwall((-2.2, 0.0, -12.3), (-0.2, 1.0, -11.95), (0, 0, 1)); gwall((-2.2, 2.0, -12.3), (-0.2, 2.8, -11.95), (0, 0, 1))
box((-2.2, 1.0, -12.15), (-0.2, 2.0, -12.11), "glass")
for zf, zb in ((-12.36, -12.3), (-11.95, -11.89)):
    box((-2.28, 1.0, zf), (-2.2, 2.08, zb), "paint_white", TRIM); box((-0.2, 1.0, zf), (-0.12, 2.08, zb), "paint_white", TRIM)
    box((-2.28, 2.0, zf), (-0.12, 2.08, zb), "paint_white", TRIM)
    box((-1.22, 1.0, zf), (-1.18, 2.0, zb), "paint_white", TRIM)
box((-2.35, 0.94, -12.42), (-0.05, 1.0, -12.3), "paint_white", TRIM); box((-2.3, 0.95, -11.95), (-0.1, 1.0, -11.86), "paint_white", TRIM)
# the front wall: piers each side of the door opening (z -11.6 .. -5.4), a header over it
gwall((6.0, 0.0, -12.3), (6.4, 2.8, -11.6), (-1, 0, 0)); gwall((6.0, 0.0, -5.4), (6.4, 2.8, -4.8), (-1, 0, 0))
gwall((6.0, 2.4, -11.6), (6.4, 2.8, -5.4), (-1, 0, 0))
for zz in (-11.6, -5.4):   # trim round the opening
    box((6.4, 0.0, zz - (0.1 if zz < -6 else 0)), (6.46, 2.5, zz + (0 if zz < -6 else 0.1)), "paint_white", TRIM)
box((6.4, 2.4, -11.7), (6.46, 2.5, -5.3), "paint_white", TRIM)
box((6.4, 2.6, -8.65), (6.48, 2.75, -8.35), "steel", "2B2B2B")   # the light over the door
lamp((6.9, 2.5, -8.5), 3.0, 7, (1.0, 0.8, 0.55))
# the ceiling
box((-6.0, 2.75, -11.95), (6.0, 2.8, -5.1), "plaster", "F2EEE6", 1.0)
# the open roll-up door: its panels tucked under the ceiling, the tracks down the sides, the opener
box((1.6, 2.58, -11.45), (5.95, 2.64, -5.55), "paint_white", "E4E2DC")
for zz in (-11.55, -5.45):
    box((5.9, 0.0, zz - 0.03), (5.98, 2.62, zz + 0.03), "steel", "8E9399")
    box((1.5, 2.6, zz - 0.03), (5.98, 2.68, zz + 0.03), "steel", "8E9399")
box((0.9, 2.5, -8.75), (1.6, 2.72, -8.25), "steel", "D9D6CF"); box((1.6, 2.68, -8.53), (5.9, 2.72, -8.47), "steel", "8E9399")
box((6.0, 0.0, -11.6), (6.4, 0.012, -5.4), "steel", "3B3D40")   # the threshold strip
# the driveway, out to the road
box((6.4, G, -11.8), (18.5, G + 0.02, -5.2), "stone_wall", "C9C4BA", 0.5)
solid([(6.4, G + 0.02, -11.6), (6.4, G + 0.02, -5.4), (6.4, 0.0, -11.6), (6.4, 0.0, -5.4), (6.9, G + 0.02, -11.6), (6.9, G + 0.02, -5.4)], "concrete", "BEBAB1", 0.5)
apron(-1, -11.8, -5.2)
mailbox(18.0, -4.6, "2B2B2B")
hedge(17.4, 18.2, -18.8, -12.2); hedge(17.4, 18.2, -4.4, 0.0); hedge(17.4, 18.2, 2.0, 9.2)
tree(13.0, -16.5, 1.0); tree(12.5, 6.5, 0.9, "557F37"); tree(-11.5, -14.0, 1.15); tree(-11.0, 4.0, 1.0)
# the back yard: a patio and a garden bed
box((-9.5, G, -4.0), (-6.4, G + 0.04, 3.5), "stone_wall", "B3AEA4", 0.4)
box((-14.6, G, -18.6), (-13.4, G + 0.12, -6.0), "dirt", "6B4F38", 0.5)
for zc in (-17.0, -14.5, -12.0, -9.5, -7.0): bush(-14.0, zc, 0.38, "4C7A33")

# inside the garage (yours to move: not locked): shelving, boxes, a water heater, a pegboard, lights, a fridge
def free_box(a, b, mat, color=None, scale=1.0): return box(a, b, mat, color, scale, locked=False)
for k in range(4):   # a steel shelving unit along the house wall
    y = 0.15 + k * 0.6
    free_box((-4.6, y, -5.6), (-1.4, y + 0.04, -5.12), "steel", "6F757C")
for x in (-4.6, -1.44):
    for zz in (-5.6, -5.16):
        free_box((x, 0.0, zz), (x + 0.04, 2.0, zz + 0.04), "steel", "4E545B")
for (x0, x1, k, c) in ((-4.4, -3.7, 0, "C8A27A"), (-3.5, -2.6, 0, "B8925F"), (-4.3, -3.3, 1, "C8A27A"), (-2.9, -1.7, 1, "D2B48C"),
                       (-4.4, -3.9, 2, "B8925F"), (-3.6, -2.2, 2, "C8A27A"), (-2.0, -1.6, 3, "8C2F2B")):
    y = 0.19 + k * 0.6
    free_box((x0, y, -5.55), (x1, y + 0.36, -5.18), "plywood", c)
cyl_hz = None
b = cyl(-5.45, -5.65, 0.3, 0.0, 1.55, "paint_white", "E9E6DE", 12, locked=False)   # the water heater
b = cyl(-5.45, -5.65, 0.06, 1.55, 2.75, "steel", "8E9399", 6, locked=False)
free_box((-5.99, 0.9, -10.6), (-5.96, 2.1, -7.6), "plywood", "C9A26B", 0.4)          # the pegboard
for (z, y, h) in ((-10.2, 1.5, 0.35), (-9.7, 1.6, 0.25), (-9.1, 1.45, 0.4), (-8.4, 1.55, 0.3), (-7.9, 1.65, 0.2)):
    free_box((-5.96, y, z - 0.03), (-5.93, y + h, z + 0.03), "steel", "3E4247")      # tools hanging on it
free_box((-5.99, 1.6, -11.6), (-5.9, 2.1, -11.2), "steel", "9EA2A6")                # the breaker panel
free_box((3.2, 0.0, -5.75), (3.95, 1.75, -5.1), "paint_white", "E3E0D8")            # an old fridge
free_box((3.21, 1.2, -5.77), (3.94, 1.22, -5.75), "steel", "8E9399")
for x in (-3.0, 2.5):   # ceiling lights (tubes), their lamps
    free_box((x - 0.65, 2.68, -8.62), (x + 0.65, 2.75, -8.38), "steel", "D9D6CF")
    free_box((x - 0.6, 2.665, -8.58), (x + 0.6, 2.68, -8.42), "paint_white", "FFFBEF")
    lamp((x, 2.55, -8.5), 6.5, 9, (1.0, 0.95, 0.86))
box((3.0, 0.0, -5.1), (3.96, 2.05, -5.08), "door_wood", "E8E3D8")   # the door into the house
box((2.9, 0.0, -5.1), (3.0, 2.15, -5.06), "paint_white", TRIM); box((3.96, 0.0, -5.1), (4.06, 2.15, -5.06), "paint_white", TRIM); box((2.9, 2.05, -5.1), (4.06, 2.15, -5.06), "paint_white", TRIM)
box((3.0, 0.0, -5.6), (3.96, 0.17, -5.1), "concrete", "A9A49A", 0.6)               # its step

# the shop's things, as a new career has them (the player rearranges them in build mode)
ent("player_start", (4.4, 0.0, -8.5), 180.0)
ent("station_bench", (-5.2, 0.0, -11.2), 90.0)
ent("shop_computer", (-5.2, 0.0, -6.6), 0.0)
ent("shop_counter", (5.2, 0.0, -6.3), 180.0)
ent("shop_door", (5.7, 0.0, -9.0), 180.0)

m = {
    "format": "gunsmith3d-map", "version": 1, "name": "Home garage",
    "look": {"ambient": [0.48, 0.52, 0.6], "fogEnd": 450.0, "fogStart": 80.0, "skyHorizon": [0.78, 0.84, 0.9], "skyTop": [0.36, 0.55, 0.8],
             "sun": [1.0, 0.96, 0.88], "sunPitch": 55.0, "sunYaw": 35.0},
    "brushes": brushes, "entities": ents,
    "career": {"build": [-6.0, -11.95, 6.0, -5.1], "store": [2.0, -11.95, 6.0, -5.1], "roof": 2.79},
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'defaults', 'maps', 'garage.gsmap')
os.makedirs(os.path.dirname(out), exist_ok=True)
json.dump(m, open(out, 'w'), separators=(',', ':'))
print(f"wrote {out}: {len(brushes)} blocks, {len(ents)} things")
