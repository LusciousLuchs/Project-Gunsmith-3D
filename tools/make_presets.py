#!/usr/bin/env python3
"""Builds src/core/presets.inc: the example guns.

Each example is built from named measurements (bore height, chamber position, slide travel, ...) so the parts stay
consistent with each other. Run it after changing anything here:  python3 tools/make_presets.py
Coordinates are millimetres in side view: x forward, y DOWN, z across the gun (+ is the near side in side view).
"""
import json, math, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'src', 'core', 'presets.inc')

# ------------------------------------------------------------------ helpers
def rect(x0, y0, x1, y1):
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]

def part(id, name, pts, z=0.0, thick=10.0, solid=True, fixed=False, mass=0, color='#8e9ab4', rad=None, bulge=None,
         ghost=False, damp=0, layer=None, **extra):
    # layer: which collision groups (letters) the part belongs to; parts touch only if they share a letter.
    # Rounds and cases touch every solid part.
    p = {'id': id, 'name': name, 'fixed': fixed, 'layer': layer if layer else ('ABC' if solid else 'none'), 'thick': thick, 'z': z,
         'color': color, 'pts': [[round(x, 3), round(y, 3)] for x, y in pts]}
    if mass: p['mass'] = mass
    if damp: p['damp'] = damp
    if ghost: p['ghost'] = True
    if rad: p['rad'] = rad
    if bulge: p['bulge'] = bulge
    p.update(extra)
    return p

def both_sides(p, z):
    """The part at +z and its mirror at -z (side walls, rails, grip panels)."""
    a = dict(p); b = dict(p)
    a['id'] = p['id'] + '_r'; a['name'] = p['name'] + ' (right)'; a['z'] = z
    b['id'] = p['id'] + '_l'; b['name'] = p['name'] + ' (left)'; b['z'] = -z
    return [a, b]

def weld(a, b, at): return {'type': 'weld', 'a': a, 'b': b, 'p': at}
def pin(a, b, at): return {'type': 'pin', 'a': a, 'b': b, 'p': at}
def track(a, b, at, path, lock=True): return {'type': 'track', 'a': a, 'b': b, 'p': at, 'path': path, 'lockRot': lock}
def coil(a, b, pa, pb, k, rest, guide='', dia=7, wire=1.0, turns=12, damp=0):
    return {'type': 'spring', 'kind': 'coil', 'a': a, 'b': b, 'pa': pa, 'pb': pb, 'k': k, 'rest': rest, 'damp': damp,
            'guide': guide, 'coilDia': dia, 'wire': wire, 'turns': turns}
def leaf(a, b, pa, pb, k, preload=1.5, flip=False):
    return {'type': 'spring', 'kind': 'leaf', 'a': a, 'b': b, 'pa': pa, 'pb': pb, 'k': k, 'preload': preload, 'flip': flip}

# cartridge sizes used for layout (must match the presets in design.cpp)
CART = {
    '.380 ACP': dict(oal=25.0, case=17.3, head=9.5),
    '9x19mm': dict(oal=29.69, case=19.15, head=9.96),
    '5.56x45mm': dict(oal=57.4, case=44.7, head=9.6),
    '7.62x54mmR': dict(oal=77.16, case=53.72, head=14.48),
    '.45 ACP': dict(oal=32.4, case=22.8, head=12.19),
}

STEEL = '#3f4654'; BLUE = '#3a6fc4'; BRIGHT = '#c9ccd2'; BRASS = '#e0b54a'; GRIP = '#2b2f36'; WOOD = '#7a5236'


# ------------------------------------------------------------------ pistols
# Both pistols share a frame, slide and striker fire control. The blowback one has a barrel fixed to the frame; the
# tilting one has a barrel that locks into the ejection port, recoils a few millimetres with the slide, then is cammed
# down off the slide by a fixed cross pin running in a slot in its lug (the Browning cam system).
def pistol(kind='blowback'):
    tilting = kind == 'tilting'
    cart = '9x19mm' if tilting else '.380 ACP'; C = CART[cart]
    bore = 20.0            # bore height
    face = 60.0            # chamber face (breech face in battery)
    muzzle = 165.0 if tilting else 150.0
    bRec = 6.0 if tilting else 0.0        # how far the barrel recoils before it stops
    travel = 42.0 if tilting else 34.0    # slide stroke to the rear stop
    lipsHead = face - C['oal'] - 2.0 - bRec  # head of the top round: clear of the barrel even when it has recoiled
    cap = 8 if tilting else 7
    lipsY = 23.0           # its centre line: a little below the bore so the nose meets the chamber mouth
    brkBottom = 21.0       # underside of the breech block: rides over the top round, strips it on the way back
    wallZ = 10.0           # slide walls and frame rails sit outside the barrel, with room inside for the sear
    railTop = 27.2
    dia = C['head'] * 1.02
    magTop = [lipsHead + C['oal'] / 2, lipsY - (3 + dia / 2)]

    parts = []
    # ---- frame: grip panels either side (just for looks), rails and a rear stop, the barrel, a guide rod
    rs = 5.0 - travel       # where the slide's rear end stops
    fe = muzzle - 20       # front of the frame
    grip = [[min(-30, rs), 27], [fe, 27], [fe, 38], [90, 38], [86, 44], [72, 44], [64, 104], [62, 108], [26, 108], [20, 104], [8, 38], [min(-30, rs), 38]]
    for p in both_sides(part('grip', 'Frame', grip, thick=4, solid=False, fixed=True, color=GRIP,
                             rad=[2, 2, 2, 3, 3, 0, 2, 3, 3, 2, 0, 2]), 9.0):
        parts.append(p)
    rails = [[rs - 5, 21], [rs - 1, 21], [rs - 1, railTop], [fe - 10, railTop], [fe - 10, railTop + 2.2], [rs - 5, railTop + 2.2]]
    for p in both_sides(part('rail', 'Frame rail', rails, thick=3, fixed=True, color=STEEL), wallZ):
        parts.append(p)
    blk = 85.8             # front of the chamber block: it bears on the front edge of the ejection port
    pinC = [78.0, 32.0]; pinR = 1.6
    if not tilting:
        barrel = [[face, bore - 6.5], [muzzle, bore - 6.5], [muzzle, bore + 6.5], [face + 7, bore + 6.5], [face, bore + 4.6]]
        parts.append(part('barrel', 'Barrel', barrel, thick=13, fixed=True, color=BRIGHT, rad=[0.6, 0.8, 0.8, 0, 0.6]))
        rod0 = 70
    else:
        # chamber block rises 1.3 mm into the port; the muzzle has a short bell that fits the slide's bushing closely,
        # the tube between is slimmer so the barrel can tilt inside the slide
        bell = muzzle - 3.5   # short, so the barrel can rock in the bushing as it tilts
        barrel = [[face, bore - 8.0], [blk, bore - 8.0], [blk, bore - 4.9], [bell, bore - 4.9], [bell + 0.6, bore - 5.3], [muzzle, bore - 5.3],
                  [muzzle, bore + 5.3], [bell + 0.6, bore + 5.3], [bell, bore + 4.9], [blk + 2, bore + 4.9], [blk, bore + 6.5], [face + 7, bore + 6.5], [face, bore + 4.6]]
        parts.append(part('barrel', 'Barrel', barrel, thick=11, mass=95, color=BRIGHT, rad=[0.6, 0, 0.4, 0, 0, 1.2, 1.2, 0, 0, 0.5, 0, 0, 0.6]))  # square locking corner
        # the cam lug: a hook under the chamber opening to the rear; the frame's cross pin rides in its slot.
        # Going back the lower jaw's slope pulls the breech down off the slide; coming home the upper jaw lifts it.
        px, py = pinC; c = pinR
        # the pin runs in a channel 0.2 mm wider than itself: flat for the first 2 mm (locked), then 2 mm down over 4 mm
        drop = 2.0
        w = c + 0.25
        lug = [[88.4, bore + 6.5], [88.4, 35.5], [77, 35.5], [77, py + w], [px + 2, py + w], [px + 6, py - drop + w],
               [px + 6 + c + 0.2, py - drop + w], [px + 6 + c + 0.2, py - drop - w], [px + 6, py - drop - w], [px + 2, py - w],
               [px - c - 0.2, py - w], [px - c - 0.2, py + 1.2], [74.4, py + 1.2], [74.4, bore + 6.5]]
        parts.append(part('blug', 'Barrel cam lug', lug, thick=7, mass=12, color=BRIGHT, rad=[0, 1, 0.6, 0.3, 0, 0, 0, 0, 0, 0, 0, 0.3, 0.6, 0]))  # its rear leg stops the barrel going forward
        parts.append(part('xpin', 'Locking cross pin', [[px + c * math.cos(t * 0.3927), py + c * math.sin(t * 0.3927)] for t in range(16)],
                          thick=12, fixed=True, color=STEEL))
        rod0 = 90
    parts.append(part('rod', 'Guide rod', rect(rod0, 29.2, muzzle - 5, 30.8), thick=3, solid=False, fixed=True, color=BRIGHT))
    # the ejector: a fin on the left of the frame that the empty case's head hits as the slide runs back
    ej = 1.25 if tilting else 0.0   # a little further forward on the tilting gun: a racked live round meets it with the slide still moving
    parts.append(part('ejector', 'Ejector', [[lipsHead - 5.5 + ej, brkBottom + 0.3], [lipsHead - 1.5 + ej, brkBottom + 0.3], [lipsHead - 1.5 + ej, 29], [lipsHead - 5.5 + ej, 29]],
                      z=-3.0, thick=3, fixed=True, color=STEEL))

    # ---- slide: a top with the ejection port cut out, two walls (the right one ported), rail lips, the breech block
    sx0, sx1 = 5.0, muzzle + 2
    parts.append(part('slide', 'Slide top (rear)', rect(sx0, 8, 58, bore - 6.7), thick=19.6, mass=70, color=BLUE, rad=[3, 0, 0, 0]))
    parts.append(part('slide_f', 'Slide top (front)', rect(86, 8, sx1, bore - 6.7), thick=19.6, mass=60, color=BLUE, rad=[0, 3, 0, 0]))
    wall = rect(sx0, 8, sx1, railTop - 0.2)
    p0 = 52 if tilting else 56   # rear edge of the ejection port: the longer stroke throws the case from further back
    port = [[sx0, 8], [p0, 8], [p0, 26], [88, 26], [88, 8], [sx1, 8], [sx1, railTop - 0.2], [sx0, railTop - 0.2]]
    parts.append(part('wall_l', 'Slide wall (left)', wall, z=-wallZ, thick=3, mass=75, color=BLUE, rad=[3, 3, 0, 0]))
    parts.append(part('wall_r', 'Slide wall (right, ported)', port, z=wallZ, thick=3, mass=65, color=BLUE, rad=[3, 0, 0, 0, 0, 3, 0, 0]))
    for p in both_sides(part('lip', 'Slide rail lip', rect(12, railTop + 2.4, 110, railTop + 3.8), thick=3, mass=12, color=BLUE), wallZ):
        parts.append(p)
    parts.append(part('breech', 'Breech block', rect(sx0, bore - 6.7, face, brkBottom), thick=9.8, mass=70, color=BLUE))
    # the nose seats the spring; on the tilting gun it also carries the barrel's muzzle (the bushing)
    if tilting:
        # its top sits a little below the muzzle: running back along the tilted barrel it must not lever it further over
        parts.append(part('nose', 'Slide nose (bushing and spring seat)', rect(muzzle - 4, bore + 6.1, sx1, 33), thick=8, mass=15, color=BLUE))
        parts.append(part('bush', 'Bushing (top)', rect(muzzle - 4, bore - 7.0, sx1, bore - 5.45), thick=8, mass=4, color=BLUE))
    else:
        parts.append(part('nose', 'Slide nose (spring seat)', rect(140, 27, sx1, 33), thick=8, mass=15, color=BLUE))

    # ---- striker: rides in the slide on a spring; its lug hangs down beside the magazine where the sear can catch it.
    # Drawn cocked: held 8 mm back by the sear. Pull the trigger, the link pulls the sear down, the striker flies
    # forward and hits the primer. Coming home, the slide drags the lug onto the sear again: that cocks it.
    sz = 6.0               # depth of the lug and sear: between the rounds and the slide wall
    cock = 8.0
    tip = face + 0.6
    parts.append(part('striker', 'Striker', rect(10 - cock, bore - 1, tip - cock, bore + 1), thick=2, solid=False, mass=6, color=BRIGHT, rad=[0, 0.8, 0.8, 0]))
    parts.append(part('slug', 'Striker lug', rect(42 - cock, brkBottom + 0.6, 46 - cock, 24.8), z=sz, thick=1.8, mass=1, color=BRIGHT))
    sear = [[38.2, 23.6], [40.6, 23.6], [42.5, 25.5], [48.0, 29.5], [48.4, 36.3], [46.6, 38.5], [45.2, 38.5], [45.2, 33.0], [38.2, 26.6]]  # bevelled toe: a returning link tooth cams down under it
    parts.append(part('sear', 'Sear', sear, z=sz, thick=1.8, mass=4, damp=25, color=BRASS, rad=[0.3, 0.3, 0, 0, 0.3, 0.3, 0, 0, 0.3]))
    parts.append(part('searstop', 'Sear stop', rect(44.2, 24.8, 46.2, 26.7), z=sz, thick=1.8, fixed=True, color=STEEL))
    parts.append(part('searstop2', 'Sear stop (lower)', rect(51.4, 33.5, 53.0, 38.0), z=sz, thick=1.8, fixed=True, color=STEEL, rad=[0.5, 0.5, 0.5, 0.5]))
    # trigger link: its tooth hooks behind the sear's lower arm and pulls it forward. Just past the release a tab on
    # the link runs down a fixed ramp (the connector), which pushes the tooth down under the arm: the sear is free to
    # catch the striker again even with the trigger held. Letting go walks the tab back up the ramp and the tooth
    # pops up behind the arm again (the reset). No slide-driven disconnector needed.
    link = [[26, 38.7], [43.4, 38.7], [43.4, 38.0], [44.2, 36.5], [45.0, 36.5], [45.0, 38.7], [88, 38.7], [91.5, 34.4], [91.5, 36.0], [88.5, 40.1], [26, 40.1]]
    parts.append(part('link', 'Trigger link', link, thick=1.8, mass=3, z=sz, damp=10, color=BRIGHT))
    parts.append(part('linkstop', 'Trigger link stop', rect(60, 42.5, 64, 44.4), z=sz, thick=1.8, fixed=True, color=STEEL, rad=[0.5, 0.5, 0.5, 0.5]))
    dz = 7.6               # the connector works beside the sear, between it and the slide wall
    conn = [[46.4, 36.0], [52.5, 36.0], [52.5, 41.0], [48.4, 41.0], [46.4, 38.6]]  # ramp face from (46.4, 38.6) down to (48.4, 41)
    parts.append(part('conn', 'Connector (disconnect ramp)', conn, z=dz, thick=1.2, fixed=True, color=STEEL, rad=[0.3, 0, 0, 0.4, 0.4]))
    parts.append(part('ltab', 'Trigger link tab', rect(42.5, 38.7, 45.5, 40.1), thick=1.2, mass=0.5, z=dz, color=BRIGHT))

    # ---- magazine and its catch, trigger
    parts.append(part('mag', 'Magazine', [[30, 50], [40, 50], [35, 60]], thick=12, solid=False, ghost=True, color='#3d434f',
                      magazine={'cap': cap, 'gen': {'stack': 1, 'curve': 0, 'wall': 1.0, 'angle': 0, 'top': magTop}}))
    parts.append(part('magbtn', 'Magazine catch', rect(66, 60, 72.5, 66), z=-6, thick=6, solid=False, mass=6, color=BRASS, rad=[0, 1.5, 1.5, 0],
                      control={'kind': 'button', 'key': 'magrelease', 'at': [72.5, 63], 'dir': 180, 'force': 14}))
    trig = [[89, 34.4], [92, 34.4], [93.6, 40], [93.6, 44.5], [93.2, 50.5], [91.5, 54.5], [89, 54.5], [88, 49.5], [87.8, 44.5], [88, 40]]
    parts.append(part('trigstop_r', 'Trigger stop (rear)', rect(81.2, 46, 83.7, 50.5), thick=6, fixed=True, color=STEEL, rad=[1, 1, 1, 1]))
    parts.append(part('trigstop_f', 'Trigger stop (front)', rect(93.65, 44.5, 96.5, 48.5), thick=6, fixed=True, color=STEEL, rad=[0.5, 0.5, 0.5, 0.5]))
    parts.append(part('trigger', 'Trigger', trig, thick=6, solid=True, mass=8, damp=25, color=BRASS, rad=[0.5, 0.5, 0, 0, 0, 1.5, 1.5, 0, 0, 0],
                      bulge=[0, 0, 0, -0.5, -0.7, 0, 0, 0.4, 0, 0], control={'kind': 'trigger', 'key': 'fire', 'at': [93.4, 50.5], 'dir': 180, 'force': 25}))
    guard = [[72, 38], [76, 38], [76, 56], [98, 56], [104, 38], [108, 38], [100, 60], [72, 60]]
    parts.append(part('guard', 'Trigger guard', guard, thick=8, solid=False, fixed=True, color=GRIP, rad=[0, 0, 3, 3, 0, 0, 4, 2]))

    joints = [
        weld('slide_f', 'slide', [100, 10]), weld('wall_l', 'slide', [30, 10]), weld('wall_r', 'slide', [30, 10]),
        weld('lip_l', 'wall_l', [20, 30]), weld('lip_r', 'wall_r', [20, 30]),
        weld('breech', 'slide', [30, 15]), weld('nose', 'slide_f', [muzzle - 4, 30]),
        coil('nose', 'grip_l', [muzzle - 3.5, 30], [rod0 + 2, 30], k=1.4 if tilting else 1.6, rest=105 if tilting else 110, guide='rod', dia=7, wire=0.9, turns=12 if tilting else 14),
        track('magbtn', 'grip_l', [69, 63], [[65.5, 63], [69, 63]]),
        coil('magbtn', 'grip_l', [67, 63], [54, 63], k=0.5, rest=16, dia=4, wire=0.6, turns=6, damp=2),
        pin('trigger', 'grip_l', [91, 42]),
        track('striker', 'slide', [35 - cock, bore], [[35 - cock - 2.5, bore], [35, bore]]),  # a little over-travel behind cocked
        weld('slug', 'striker', [44 - cock, 23]),
        coil('striker', 'breech', [24 - cock, bore], [6, bore], k=0.9, rest=26, guide='striker', dia=4, wire=0.5, turns=12),
        pin('sear', 'grip_l', [46.5, 31]),
        coil('sear', 'grip_l', [47.5, 37], [54, 37], k=1.2, rest=9, dia=3, wire=0.4, turns=8),
        pin('link', 'trigger', [90.5, 35.2]),
        coil('link', 'grip_l', [55, 40.1], [55, 46], k=1.0, rest=8.5, dia=3, wire=0.4, turns=8),
        weld('ltab', 'link', [44, 39.4]),
        leaf('guard', 'trigger', [88, 43], [88, 49.5], k=1.0, preload=1.5, flip=True),
        *([weld('blug', 'barrel', [81, 27]), weld('bush', 'slide_f', [muzzle - 3, bore - 6.5])] if tilting else []),
        {'type': 'magwell', 'a': 'mag', 'b': 'grip_l', 'p': [magTop[0], magTop[1] + 10], 'path': [[magTop[0], magTop[1] + 10], [magTop[0], magTop[1] + 110]],
         'rel': 'magbtn', 'releaseAt': 1.5},
    ]
    breech = {'breech': 'slide', 'chamber': 'barrel', 'face': [face, bore], 'muzzle': [muzzle, bore], 'cartridge': cart, 'dur': 0.6,
              'feed': 20, 'rounds': cap, 'auto': False, 'loaded': True, 'feedMode': 'magazine', 'physicalFeed': True,
              'ignition': 'striker', 'striker': 'striker', 'strikerTip': [tip - cock, bore], 'igniteSpeed': 1.5}  # tip as drawn (cocked)
    return {'name': 'Tilting-barrel pistol' if tilting else 'Blowback pistol', 'speed': 0.02, 'friction': 0.12, 'bodies': parts, 'joints': joints, 'breech': breech}


def blowback(): return pistol('blowback')
def tilting(): return pistol('tilting')


# ------------------------------------------------------------------ bolt-action rifle
# A turn-bolt: two lugs at the bolt head lock behind shoulders in the receiver ring when the handle is down (lugs
# out to the sides); lifting the handle swings them up and down, clear of the shoulders, and the bolt draws back.
# The striker cocks on closing: its cocking lug, hanging below the bolt's tail, is caught by the sear while the bolt
# goes the last few millimetres forward. Pull the trigger and its tail lifts the front of the sear, so the nose drops.
# The bolt head has a long slot under it for the ejector; the lower lug strips the next round off the magazine lips.
def rifle(kind='bolt'):
    single = kind == 'single'   # the same action with no magazine: you drop each round in by hand
    cart = '5.56x45mm'; C = CART[cart]
    bore = 20.0
    face = 120.0           # chamber face
    muzzle = 420.0
    rimR = C['head'] / 2
    br = 5.5               # bolt radius
    rampLen = 13.0   # a gentler ramp (about 23 degrees): a 10 mm one kicked the nose up into the chamber too steeply, and
                     # a longer one (16 mm) lengthens the bolt stroke enough to upset ejection
    engage = 2.0
    lipsY = bore + br + rimR - engage   # the top round's rim reaches this far up the bolt face
    lipsHead = face - C['oal'] - (rampLen + 2.0)   # its nose stays behind the feed ramps
    travel = face - (lipsHead - 3.0)     # bolt face goes back past the rim of the top round
    cock = 10.0
    tip = face + 0.6
    dia = C['head'] * 1.02
    magTop = [lipsHead + C['oal'] / 2, lipsY - (3 + dia / 2)]
    lugX0, lugX1 = face - 12, face - 3
    parts = []
    # ---- receiver and stock (the outer shell is see-through so the action shows), barrel, feed ramps
    rcv = [[-80, bore - 11], [138, bore - 11], [138, bore + 12], [124, bore + 14], [124, bore + 52], [46, bore + 52], [46, bore + 18],
           [-80, bore + 18]]
    parts.append(part('rcv', 'Receiver', rcv, thick=22, solid=False, fixed=True, ghost=True, color='#4b5260', rad=[2, 2, 2, 2, 2, 2, 2, 2]))
    stock = [[-80, bore - 6], [-330, bore + 4], [-330, bore + 95], [-300, bore + 95], [-130, bore + 40], [-30, bore + 40], [-6, bore + 64],
             [8, bore + 64], [12, bore + 30], [-80, bore + 18]]
    parts.append(part('stock', 'Stock', stock, thick=34, solid=False, fixed=True, color=WOOD, rad=[0, 6, 6, 4, 20, 10, 4, 4, 6, 0]))
    parts.append(part('fore', 'Fore-end', [[138, bore + 8], [330, bore + 8], [330, bore + 24], [138, bore + 30]], thick=30, solid=False, fixed=True,
                      color=WOOD, rad=[0, 4, 4, 0]))
    barrel = [[face, bore - 7], [muzzle, bore - 6], [muzzle, bore + 6], [face, bore + 7]]
    parts.append(part('barrel', 'Barrel', barrel, thick=14, fixed=True, color=STEEL, rad=[0.6, 1, 1, 0.6]))
    ramp = [[face - rampLen, bore + 10.5], [face, bore + rimR + 0.3], [face, bore + 14], [face - rampLen, bore + 14]]  # ends just under the case's way out
    for p in both_sides(part('ramp', 'Feed ramp', ramp, thick=2, fixed=True, color=STEEL, rad=[0, 0.5, 0, 0]), 4.0):  # either side of the lower lug's path
        parts.append(p)
    # locking shoulders in the receiver ring, behind where the lugs sit when the bolt is turned down
    for p in both_sides(part('shoulder', 'Locking shoulder', rect(lugX0 - 8, bore - 3, lugX0, bore + 3), thick=3, fixed=True, color=STEEL), 7.2):
        parts.append(p)
    # bolt stop: the upper lug (bolt turned up) runs into it at the end of the stroke
    parts.append(part('boltstop', 'Bolt stop', rect(lugX0 - travel - 5, bore - 9.5, lugX0 - travel, bore - 5.8), thick=4, fixed=True, color=STEEL))
    # receiver walls: the right one has the ejection port
    wall = rect(-80, bore - 9, face, bore + 9)
    port = [[-80, bore - 9], [lipsHead - 5, bore - 9], [lipsHead - 5, bore + 14], [face - 2, bore + 14], [face - 2, bore - 9], [face, bore - 9],
            [face, bore + 9], [-80, bore + 9]]
    parts.append(part('wall_l', 'Receiver wall (left)', wall, z=-10, thick=2, fixed=True, color=STEEL))
    parts.append(part('wall_r', 'Receiver wall (right, port)', port, z=10, thick=2, fixed=True, color=STEEL))
    # ejector: a blade in the floor of the bolt way; the empty case's rim hits it once the case is clear of the chamber
    ex = face - C['case'] - 2.5
    parts.append(part('ejector', 'Ejector', rect(ex - 4, bore + 1.4, ex, bore + 3.6), z=-2.5, thick=2, fixed=True, color=STEEL))

    # ---- bolt: a slot under its head clears the ejector; lugs drawn turned down (locked), out to the sides
    slot = face - (face - ex) - 4.5
    bolt = [[10, bore - br], [face, bore - br], [face, bore + 1], [slot, bore + 1], [slot, bore + br], [10, bore + br]]
    parts.append(part('bolt', 'Bolt', bolt, thick=2 * br, mass=180, damp=4, color=BRIGHT, rad=[2, 0.5, 0, 0, 0, 2],
                      roll={'on': True, 'min': 0, 'max': 90, 'handle': 34}))
    for p in both_sides(part('lug', 'Locking lug', rect(lugX0, bore - 2.5, lugX1, bore + 2.5), thick=3, mass=6, color=BRIGHT, rad=[0.5, 0.5, 0.5, 0.5]), 7.0):
        parts.append(p)
    # striker inside the bolt, drawn cocked; its cocking lug hangs below the bolt's tail
    parts.append(part('striker', 'Striker', rect(0, bore - 1.5, tip - cock, bore + 1.5), thick=3, solid=False, mass=14, color=BRIGHT, rad=[0, 1, 1, 0]))
    parts.append(part('clug', 'Cocking piece', rect(3, bore + br + 0.5, 7, bore + 11),  # runs under the bolt when it fires
                       thick=4, mass=4, color=BRIGHT, rad=[0.5, 0.5, 0, 0]))
    # sear: pivots ahead of its nose, so the striker's push holds it up; the trigger's tail lifts its front end
    sear = [[7, bore + 8.5], [8.5, bore + 8.5], [10.5, bore + 11.6], [27, bore + 11.6], [30, bore + 12.5], [30, bore + 15.5],
            [7, bore + 15.5]]
    parts.append(part('sear', 'Sear', sear, thick=4, mass=4, damp=10, color=BRASS, rad=[0.2, 0.3, 0, 0, 0.5, 0.5, 0.5]))
    trig = [[25, bore + 15.6], [37, bore + 15.6], [38.5, bore + 20], [38.5, bore + 30], [37.5, bore + 40], [35.5, bore + 43],
            [33.5, bore + 43], [33.5, bore + 38], [34, bore + 30], [33.5, bore + 20], [25, bore + 18]]
    parts.append(part('trigger', 'Trigger', trig, thick=5, mass=8, damp=20, color=BRASS, rad=[0.3, 0.5, 0, 0, 1.5, 1.5, 0, 0, 0, 0, 0.3],
                      control={'kind': 'trigger', 'key': 'fire', 'at': [38.2, bore + 38], 'dir': 180, 'force': 25}))
    parts.append(part('searstop', 'Sear stop', rect(26, bore + 7.8, 30, bore + 9), thick=4, fixed=True, color=STEEL))
    parts.append(part('trigstop_f', 'Trigger stop (front)', rect(39, bore + 26, 41, bore + 30), thick=5, fixed=True, color=STEEL))
    parts.append(part('trigstop_r', 'Trigger stop (rear)', rect(28.5, bore + 25, 30.3, bore + 30), thick=5, fixed=True, color=STEEL))
    guard = [[30, bore + 52], [34, bore + 52], [34, bore + 50], [44, bore + 50], [50, bore + 52], [54, bore + 52], [48, bore + 56], [30, bore + 56]]
    parts.append(part('guard', 'Trigger guard', guard, thick=8, solid=False, fixed=True, color=STEEL))
    # ---- magazine and its catch
    if not single:
      parts.append(part('mag', 'Magazine', [[60, 70], [70, 70], [65, 80]], thick=12, solid=False, ghost=True, color='#3d434f',
                      magazine={'cap': 5, 'gen': {'stack': 1, 'curve': 0, 'wall': 1.0, 'angle': 0, 'top': magTop}}))
      mbx = magTop[0] + C['oal'] / 2 + 6
      parts.append(part('magbtn', 'Magazine catch', rect(mbx, bore + 40, mbx + 6.5, bore + 46), z=-6, thick=6, solid=False, mass=6, color=BRASS,
                      rad=[0, 1.5, 1.5, 0], control={'kind': 'button', 'key': 'magrelease', 'at': [mbx + 6.5, bore + 43], 'dir': 180, 'force': 14}))

    joints = [
        track('bolt', 'rcv', [60, bore], [[60 - travel - 2, bore], [60, bore]]),
        weld('lug_r', 'bolt', [lugX0 + 4, bore]), weld('lug_l', 'bolt', [lugX0 + 4, bore]),
        track('striker', 'bolt', [50, bore], [[48, bore], [50 + cock, bore]]),
        weld('clug', 'striker', [5, bore + 7]),
        coil('striker', 'bolt', [40, bore], [13, bore], k=3.0, rest=47, guide='striker', dia=5, wire=0.8, turns=14),
        pin('sear', 'rcv', [20, bore + 13]),
        coil('sear', 'rcv', [14, bore + 15.5], [14, bore + 22], k=1.0, rest=8.5, dia=3, wire=0.4, turns=7),
        pin('trigger', 'rcv', [36, bore + 18]),
        coil('trigger', 'rcv', [34, bore + 21.5], [28.5, bore + 21.5], k=0.5, rest=8, dia=3, wire=0.4, turns=6),
    ]
    if not single:
        joints += [
            track('magbtn', 'rcv', [mbx + 3, bore + 43], [[mbx - 0.5, bore + 43], [mbx + 3, bore + 43]]),
            coil('magbtn', 'rcv', [mbx + 1, bore + 43], [mbx - 12, bore + 43], k=0.5, rest=16, dia=4, wire=0.6, turns=6, damp=2),
            {'type': 'magwell', 'a': 'mag', 'b': 'rcv', 'p': [magTop[0], magTop[1] + 10], 'path': [[magTop[0], magTop[1] + 10], [magTop[0], magTop[1] + 110]],
             'rel': 'magbtn', 'releaseAt': 1.5},
        ]
    breech = {'breech': 'bolt', 'chamber': 'barrel', 'face': [face, bore], 'muzzle': [muzzle, bore], 'cartridge': cart, 'dur': 1.0,
              'feed': 40, 'rounds': 5, 'auto': False, 'loaded': True, 'feedMode': 'single' if single else 'magazine', 'physicalFeed': True,
              'ignition': 'striker', 'striker': 'striker', 'strikerTip': [tip - cock, bore], 'igniteSpeed': 1.5}
    return {'name': 'Single-shot rifle' if single else 'Bolt-action rifle', 'speed': 0.05, 'friction': 0.12, 'bodies': parts, 'joints': joints, 'breech': breech}

# ------------------------------------------------------------------ pump-action carbine
# A tube magazine under the barrel and a lifter you can see. The tube's spring pushes each round out of its mouth,
# head first, onto the lifter; the lifter rises to the bore line and the bolt pushes the round into the chamber.
#  - The fore-end and action bar are one piece, on their own track. A pin on the bolt sits in a short slot in the bar
#    (lost motion): pulling the fore-end moves the bar 6 mm before it takes the bolt along.
#  - Those first millimetres lift the lock (a block behind the bolt, on a track up and down, driven by a slot in the
#    bar) out of the bolt's way. Closed, the lock sits behind the bolt: the shot can't blow the action open.
#  - The lifter is a fork either side of the bolt, on a straight track up and down, driven by another slot in the
#    bar. It stays up for nearly the whole stroke (blocking the tube: it's the cartridge stop too) and drops only at
#    the very end of the pull, letting the next round out onto it. Pushing the fore-end raises it again behind the
#    bolt face; the lost motion is what lets it be up on the way back and down only at the end of the way out.
#  - Guides above hold the round down when the lifter stops. The empty case is thrown out behind where it lies.
def pump():
    cart = '.45 ACP'; C = CART[cart]
    R = C['head'] / 2
    bore = 20.0; face = 120.0; muzzle = 420.0
    stopX = 70.0                      # the round's head stops here, on the lifter
    mouth = stopX + C['oal'] + 3.6    # the tube's mouth, just ahead of the round's nose
    tubeY = 33.0                      # tube axis: the round in it clears the bolt above
    lift = tubeY - bore
    lost = 6.0                        # fore-end travel before it takes the bolt along
    play = 1.5                        # and the bolt's free play the other way, so a shot never drags the bar
    travel = 86.0                     # bolt travel
    barTravel = travel + lost
    upTo, downFrom = 82.0, 88.0       # fore-end travel (mm): lifter up until here, all the way down from here
    ejX = 46.0                        # ejector: behind where the round lies, so the case flips out clear of the guides
    lfront = mouth - 3.0              # lifter's front face: blocks the tube while it's up
    lt = bore + R + 0.2               # lifter top, up: the round's underside on the bore line (a hair of clearance)
    boltRear = face - 40
    lpin = [86.0, lt + 8]             # lifter's pin
    kpin = [boltRear - 4.5, bore - 3] # lock's pin
    klift = 20.0                      # how far the lock lifts: clear of the bolt's top
    bpin = [boltRear + 15, bore - 3]  # the bolt's pin in the bar's lost-motion slot
    parts = []
    parts.append(part('rcv', 'Receiver', [[-10, bore - 36], [124, bore - 36], [124, tubeY + 12], [-10, tubeY + 12]], thick=26, solid=False,
                      fixed=True, ghost=True, color='#4b5260', rad=[3, 3, 3, 3]))
    parts.append(part('stock', 'Stock', [[-10, bore - 8], [-250, bore + 6], [-250, bore + 92], [-222, bore + 92], [-80, bore + 42], [-10, bore + 40]],
                      thick=34, solid=False, fixed=True, color=WOOD, rad=[0, 6, 6, 20, 10, 0]))
    parts.append(part('barrel', 'Barrel', rect(face, bore - 6.5, muzzle, bore + 6.5), thick=13, fixed=True, color=STEEL, round=True))
    parts.append(part('tube', 'Tube magazine', rect(mouth, tubeY - 7, 400, tubeY + 7), thick=14, solid=False, fixed=True, color=STEEL, round=True,
                      magazine={'cap': 6, 'builtIn': True, 'style': 'line', 'tubeFeed': 'physical', 'roundPath': [[mouth, tubeY], [400, tubeY]]}))
    # the head stop for a round coming out of the tube (below the bolt's way and the case's way out)
    parts.append(part('headstop', 'Head stop', rect(stopX - 6, bore + R + 0.4, stopX, tubeY + 18), thick=4, fixed=True, color=STEEL))
    for p in both_sides(part('guide', 'Round guide', rect(stopX + 2, bore - R - 4.5, lfront, bore - R - 0.3), thick=3, fixed=True, color=STEEL), 5.0):
        parts.append(p)
    parts.append(part('ejector', 'Ejector', rect(ejX - 6, bore + 4, ejX, bore + 8), thick=2, fixed=True, color=STEEL))
    parts.append(part('bolt', 'Bolt', rect(boltRear, bore - 8, face, bore + 3), thick=6, mass=120, damp=4, color=BRIGHT, rad=[2, 0.5, 0.5, 2]))
    # the lock's block, lifted by the bar; the holding is done by the lock joint below (a shot is too violent for two
    # blocks pressed together: the hold has to be a joint, as the bolt rifle's lugs are)
    parts.append(part('lock', 'Action lock', rect(boltRear - 8, bore - 12, boltRear - 0.5, bore + 6), thick=6, solid=False, mass=6, color=BRASS,
                      rad=[0.5, 0.5, 0.5, 0.5]))
    # action bar and fore-end: one piece, worked by hand. The bar's lug pushes the bolt home (a push can't drag it back
    # with the shot); the bolt's pin in the bar's slot pulls it open, once the bar's free travel is used up
    parts.append(part('barlug', 'Action bar lug', rect(boltRear - 5, bore - 2, boltRear, bore + 3), thick=6, mass=4, color=STEEL))
    # (a strip under the tube with an arm up beside the bolt, where its slots are; see-through so the action shows)
    bar = [[boltRear - 8, bore - 6], [boltRear + 18, bore - 6], [boltRear + 18, tubeY + 9], [270, tubeY + 9], [270, tubeY + 13], [boltRear - 8, tubeY + 13]]
    parts.append(part('bar', 'Action bar', bar, z=8.5, thick=2, solid=False, mass=30, color=STEEL, ghost=True, rad=[1, 1, 0, 1, 1, 1]))
    parts.append(part('forend', 'Fore-end', rect(190, bore + 2, 290, tubeY + 13), thick=34, solid=False, mass=80, damp=6, color=WOOD, rad=[6, 6, 6, 6],
                      control={'kind': 'handle', 'key': 'none', 'at': [240, tubeY], 'dir': 180, 'force': 25}))
    # its front top is a ramp: the empty case, sagging a little on the extractor as it's drawn back, rides up over it
    lifter = [[stopX + 0.5, lt], [lfront - 8, lt], [lfront, lt + 4], [lfront, lt + 16], [stopX + 0.5, lt + 16]]
    for p in both_sides(part('lifter', 'Lifter', lifter, thick=3, mass=10, color=BRASS, rad=[0.5, 0, 0.5, 0.5, 0.5]), 5.0):
        parts.append(p)
    parts.append(part('trigger', 'Trigger', [[60, tubeY + 12], [64, tubeY + 12], [65, tubeY + 24], [62, tubeY + 30], [60, tubeY + 30], [61, tubeY + 22]],
                      thick=5, solid=False, fixed=True, color=STEEL))
    parts.append(part('guard', 'Trigger guard', [[48, tubeY + 12], [52, tubeY + 12], [52, tubeY + 30], [76, tubeY + 30], [76, tubeY + 12], [80, tubeY + 12],
                      [80, tubeY + 34], [48, tubeY + 34]], thick=8, solid=False, fixed=True, color=STEEL))
    ls = [[lpin[0] - play, lpin[1]], [lpin[0] + upTo, lpin[1]], [lpin[0] + downFrom, lpin[1] + lift], [lpin[0] + barTravel, lpin[1] + lift]]
    ks = [[kpin[0] - play, kpin[1]], [kpin[0] + play, kpin[1]], [kpin[0] + lost - 0.5, kpin[1] - klift], [kpin[0] + barTravel, kpin[1] - klift]]
    joints = [
        track('bolt', 'rcv', [face - 20, bore], [[face - 20 - travel, bore], [face - 20, bore]]),
        track('forend', 'rcv', [240, tubeY], [[240 - barTravel, tubeY], [240, tubeY]]),
        weld('bar', 'forend', [230, tubeY]), weld('barlug', 'bar', [boltRear - 2, bore]),
        track('bolt', 'bar', bpin, [[bpin[0] - play, bpin[1]], [bpin[0] + lost, bpin[1]]]),  # the lost motion
        track('lock', 'rcv', kpin, [[kpin[0], kpin[1]], [kpin[0], kpin[1] - klift]]),       # the lock: up and down
        track('lock', 'bar', kpin, ks),                                                      # lifted by the bar's first 4 mm
        {'type': 'latch', 'a': 'bolt', 'b': 'rcv', 'catchAt': -0.05, 'rel': 'forend', 'releaseAt': lost - 1.5, 'holdShut': True},
        weld('lifter_l', 'lifter_r', lpin),
        track('lifter_r', 'rcv', lpin, [[lpin[0], lpin[1]], [lpin[0], lpin[1] + lift]]),   # the lifter: up and down
        track('lifter_r', 'bar', lpin, ls),                                                 # driven by its slot in the bar
    ]
    breech = {'breech': 'bolt', 'chamber': 'barrel', 'face': [face, bore], 'muzzle': [muzzle, bore], 'cartridge': cart, 'dur': 1.0,
              'feed': 40, 'rounds': 6, 'auto': False, 'loaded': True, 'feedMode': 'magazine', 'physicalFeed': True,
              'ignition': 'button', 'hammer': 'hammer', 'cockAt': 40, 'fireMode': 'semi'}
    return {'name': 'Pump-action carbine', 'speed': 0.05, 'friction': 0.12, 'bodies': parts, 'joints': joints, 'breech': breech}

# ------------------------------------------------------------------ output
NEW = [
    ('blowback', 'Blowback pistol', blowback),
    ('tilting', 'Tilting-barrel pistol', tilting),
    ('boltaction', 'Bolt-action rifle', rifle),
    ('single', 'Single-shot rifle', lambda: rifle('single')),
    ('pump', 'Pump-action carbine', pump),
]
LEGACY_LIST = [  # older examples built with simplified joints; kept for study and for the tests of those joints
    ('legacy-tilting', 'PRESET_TILTING'), ('legacy-blowback', 'PRESET_BLOWBACK'), ('longrecoil', 'PRESET_LONG'),
    ('legacy-turnbolt', 'PRESET_TURN'), ('legacy-single', 'PRESET_SINGLE'),
]
MENU = [  # what the example menu shows, in order
    ('blowback', 'Blowback pistol (.380, striker fired)'),
    ('tilting', 'Tilting-barrel pistol (9 mm, Browning cam lock)'),
    ('boltaction', 'Bolt-action rifle (5.56 mm, cock on close)'),
    ('single', 'Single-shot rifle (same action, load by hand)'),
    ('pump', 'Pump-action carbine (.45, tube magazine and lifter)'),
    ('longrecoil', 'Long recoil, Chauchat style (simplified cam and catch)'),
    ('blank', 'Blank frame'),
]

def main():
    legacy = open(os.path.join(HERE, 'legacy_presets.inc')).read()
    legacy = re.sub(r'static const char\* PRESET_BLANK = R"\(.*?\)";\n?', '', legacy, flags=re.S)
    out = ['// GENERATED by tools/make_presets.py - edit that, then run it. Schematic examples, not copies of real guns.', '']
    for key, label, fn in NEW:
        d = fn()
        out.append(f'static const char* NEW_{key.upper().replace("-", "_")} = R"PRESET({json.dumps(d, separators=(",", ":"))})PRESET";')
    blank = {'name': 'Untitled action', 'speed': 0.05, 'bodies': [
        part('frame', 'Frame', rect(0, 40, 220, 60), thick=20, fixed=True, ghost=True, color='#5d6575')], 'joints': [], 'breech': None}
    out.append(f'static const char* PRESET_BLANK = R"PRESET({json.dumps(blank, separators=(",", ":"))})PRESET";')
    out.append('')
    out.append('// ---- older examples (simplified joints and feeding)')
    out.append(legacy.replace('// Example actions. Schematic shapes for play and study, not copies of real parts.\n', ''))
    out.append('struct PresetEntry { const char* key; const char* label; const char* json; bool menu; };')
    out.append('static const PresetEntry PRESETS[] = {')
    keys = {k: f"NEW_{k.upper().replace('-', '_')}" for k, _, _ in NEW}
    keys.update({k: v for k, v in LEGACY_LIST})
    keys['blank'] = 'PRESET_BLANK'
    shown = set()
    for k, label in MENU:
        out.append(f'    {{"{k}", "{label}", {keys[k]}, true}},'); shown.add(k)
    for k, v in keys.items():
        if k not in shown: out.append(f'    {{"{k}", "{k}", {v}, false}},')
    out.append('};')
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print('wrote', os.path.relpath(OUT), 'with', len(keys), 'presets')

if __name__ == '__main__':
    main()
