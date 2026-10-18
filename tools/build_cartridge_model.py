"""Build the OGPlay game-cartridge model and export it as glTF.

blender -b --factory-startup -P tools/build_cartridge_model.py -- webui/apps/gui/assets/cartridge.glb [preview-prefix] [art.png]
(preview-prefix renders <prefix>-front/back/bottom.png with Cycles). Built with Blender 5.2; the launcher's 3D cartridge
(webui/apps/gui/cart3d) loads the GLB and <out>-ao.png.

Model units: x right, y up, z towards the viewer (label side). The cartridge stands on y=0.
Materials are named so the page can drive them per game:
  Shell     – tinted per game; AO comes from <out>-ao.png (UV 0, 1024 grayscale)
The GLB is Draco-compressed, so the page needs GLTFLoader with a DRACOLoader.
  Art       – front square sticker for the game icon, UV 0..1, supplied by the page
  Title     – front name strip under the icon, UV 0..1, supplied by the page
  BackLabel – rear info sticker, UV 0..1 as read from behind
  LED       – status light pipe, coloured by game status, UV 0..1 for a glow map
  Spine     – top-edge name strip, UV 0..1 read from the front-top
  Holo      – security seal on the rear sticker, UV 0..1
  PCB       – circuit board filling the shell, UV 0..1 across its bounds; extras.ogp_board (JSON) lists the
              component footprints, screw holes and gold fingers so the page can print matching traces
  Chip / Metal / Passive – board components: IC bodies, metal parts (pins, cell, crystal, caps), SMD parts
  Gold / Screw – fixed detail materials
  Trim      – polished metal accents: seam band, front wordmark inlay, status light bezel
  Marking   – component top markings, one atlas; extras.ogp_marks lists its cells (ref, top, h; height)
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = os.path.abspath(argv[0] if argv else 'cartridge.glb')
PREVIEW = os.path.abspath(argv[1]) if len(argv) > 1 else None
LABEL_IMG = argv[2] if len(argv) > 2 else None
FONT = '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf'

# ---------------------------------------------------------------- dimensions
BW = 1.10             # half width of the body
TW = 0.88             # half width of the plug (connector tongue)
TY = 0.30             # plug height
H = 3.16              # overall height incl. plug
D = 0.46              # depth
FRONT, BACK = D / 2, -D / 2
BEVEL = 0.022         # moulded edge radius: small, so the faces read flat and the edges crisp
R_TOP, R_SHOULDER, R_PLUG, R_CAVE = 0.24, 0.10, 0.06, 0.05

# front layout over the body face (shoulder TY .. top H): a near-square label (square icon + a title strip about
# a sixth of its height) with ~0.26 of shell either side; the status light / wordmark row sits 0.19 above the
# frame, aligned to its edges, leaving ~0.35 clear to the top; the insert chevron sits in the 0.32 band below
WELL = dict(x0=-0.79, x1=0.79, y0=0.67, y1=2.57, r=0.11)    # label well: square icon sticker above a title strip
FRAME = dict(x0=-0.84, x1=0.84, y0=0.62, y1=2.62, r=0.15)   # shallow outer step around it
FRAME_DEPTH, WELL_DEPTH = 0.012, 0.042
LABEL_INSET = 0.014                                          # sticker margin to the well wall
TOP_ROW_Y = FRAME['y1'] + 0.19                               # status light + wordmark row
LED = dict(x0=FRAME['x0'], x1=FRAME['x0'] + 0.26, y0=TOP_ROW_Y - 0.045, y1=TOP_ROW_Y + 0.045, r=0.045)  # status light pipe
CHEVRON_Y = (TY + FRAME['y0']) / 2                           # insert arrow, centred in the band under the label
SEAM_Z = 0.0                                                 # front / rear shell split, mid-depth
REAR_STEP = 0.008                                            # rear shell sits this much inside the front one
SCREW_INSET = 0.38                                           # same margin from the top edge and the shoulder
SCREW_Y = (H - SCREW_INSET, TY + SCREW_INSET)
SCREWS = [Vector((sx, sy)) for sy in SCREW_Y for sx in (-0.86, 0.86)]   # four corner screws
SCREW_SEAT = BACK + 0.05                                     # floor of the screw well: the head's flat underside
SHANK_END, SHANK_R = 0.08, 0.047                             # shank runs through the board into the front shell
# hollow moulding: walls deep enough for every recess cut into them; a boss per screw on each half clamps the board
WALL, SIDE_WALL = 0.06, 0.07                                 # front/back and side wall thickness
BOSS_R, BOARD_GAP = 0.075, 0.012                             # boss radius; the bosses stop this far either side of z 0
BACK_CY = sum(SCREW_Y) / 2                                   # rear sticker sits centred between them
BACKLABEL = dict(x0=-0.82, x1=0.82, y0=BACK_CY - 0.71, y1=BACK_CY + 0.71, r=0.1)
HOLO = (-0.52, BACK_CY - 0.20, 0.10)                         # security seal on the rear sticker: x, y, radius
SPINE = dict(x0=-0.66, x1=0.66, y0=0.045, y1=0.185, r=0.04)  # top-edge name strip, in (x, z) on the front shell
SPINE_DEPTH = 0.010
STICKER_T = 0.0035                                           # sticker thickness: the paper edge catches light
# (x, z) plane -> top face: the 2D loop's y becomes depth (z) and its extrusion becomes height (y)
XZ_TO_TOP = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
# side grip: a shallow recess on the upper half of each side, across both shells, with fine pull grooves
GRIP = dict(y0=1.86, y1=2.70, zh=0.16, r=0.05, depth=0.012, grooves=11, groove_depth=0.006)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
cutters = bpy.data.collections.new('cutters')
scene.collection.children.link(cutters)


# ---------------------------------------------------------------- 2D helpers
def fillet_poly(corners, seg=10):
    """corners: [(x, y, r)] counter-clockwise; returns a polyline with an arc at each corner."""
    pts, n = [], len(corners)
    for i, (x, y, r) in enumerate(corners):
        p = Vector((x, y))
        if r <= 1e-6:
            pts.append(p)
            continue
        a = (Vector(corners[i - 1][:2]) - p).normalized()
        b = (Vector(corners[(i + 1) % n][:2]) - p).normalized()
        half = a.angle(b) / 2
        p0, p1 = p + a * (r / math.tan(half)), p + b * (r / math.tan(half))
        c = p + (a + b).normalized() * (r / math.sin(half))
        a0, a1 = math.atan2(*(p0 - c).yx), math.atan2(*(p1 - c).yx)
        da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
        pts += [c + Vector((math.cos(a0 + da * k / seg), math.sin(a0 + da * k / seg))) * r for k in range(seg + 1)]
    return pts


def outline(off=0.0):
    """Silhouette (body + plug) offset by `off` (negative = inward)."""
    bw, tw = BW + off, TW + off
    y0, y1, ty = -off, H + off, TY - off
    vex = lambda r: max(r + off, 0.0)
    cave = lambda r: max(r - off, 0.0)
    return [(-tw, y0, vex(R_PLUG)), (tw, y0, vex(R_PLUG)), (tw, ty, cave(R_CAVE)), (bw, ty, vex(R_SHOULDER)),
            (bw, y1, vex(R_TOP)), (-bw, y1, vex(R_TOP)), (-bw, ty, vex(R_SHOULDER)), (-tw, ty, cave(R_CAVE))]


def rrect(x0, y0, x1, y1, r):
    return [(x0, y0, r), (x1, y0, r), (x1, y1, r), (x0, y1, r)]


def rr(d, inset=0.0):
    return rrect(d['x0'] + inset, d['y0'] + inset, d['x1'] - inset, d['y1'] - inset, max(d['r'] - inset, 0))


# ---------------------------------------------------------------- object helpers
def link(ob, coll=None):
    (coll or scene.collection).objects.link(ob)
    return ob


def bmesh_clean(ob, merge=1e-5):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=merge)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()


def bake(ob, name, coll=None, clean=True):
    """Evaluate modifiers / curve geometry into a plain mesh object (replacing `ob`)."""
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    new = link(bpy.data.objects.new('tmp', me), coll)
    new.matrix_world = ob.matrix_basis        # matrix_world is stale until the depsgraph updates
    old = ob.data
    bpy.data.objects.remove(ob)
    if old.users == 0:
        (bpy.data.curves if isinstance(old, bpy.types.Curve) else bpy.data.meshes).remove(old)
    new.name = name
    if clean:                                 # a bmesh round trip would drop custom normals
        bmesh_clean(new)
    return new


def curve_obj(name, loops, extrude, bevel=0.0, z=0.0, bevel_res=6):
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions, cu.fill_mode = '2D', 'BOTH'
    cu.extrude, cu.bevel_depth, cu.bevel_resolution = extrude, bevel, bevel_res
    for loop in loops:
        sp = cu.splines.new('POLY')
        sp.points.add(len(loop) - 1)
        for p, v in zip(sp.points, loop):
            p.co = (v[0], v[1], 0, 1)
        sp.use_cyclic_u = True
    ob = bpy.data.objects.new(name, cu)
    ob.location.z = z
    return link(ob)


def prism(name, loop, z0, z1, coll=None):
    """Mesh prism of a 2D loop between z0 and z1."""
    return bake(curve_obj(name, [loop], (z1 - z0) / 2, z=(z0 + z1) / 2), name, coll)


def box(name, x0, x1, y0, y1, z0, z1, coll=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.LocRotScale(
        Vector(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)), None, Vector((x1 - x0, y1 - y0, z1 - z0))))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new(name, me), coll)


def text_mesh(body, cx, cy, width, z0, z1, grow=0.0, coll=None):
    """Extruded text centred on (cx, cy), scaled so the plain lettering is `width` wide. `grow` (model units)
    fattens (+) or thins (-) the strokes while keeping that scale, so an inlay can sit inside its own deboss."""
    def build(grow_font_units):
        cu = bpy.data.curves.new('txt', 'FONT')
        cu.body = body
        cu.font = bpy.data.fonts.load(FONT, check_existing=True)
        cu.align_x, cu.align_y = 'CENTER', 'CENTER'
        cu.extrude = (z1 - z0) / 2
        cu.resolution_u = 5                   # Blender's 12 is far more than lettering this small needs
        cu.offset += grow_font_units          # stored as a stroke width factor, 1.0 = as drawn
        return bake(link(bpy.data.objects.new('txt', cu)), 'txt', coll)
    ob = build(0.0)
    xs = [v.co.x for v in ob.data.vertices]
    ys = [v.co.y for v in ob.data.vertices]
    s = width / (max(xs) - min(xs))
    mx, my = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    if grow:
        bpy.data.objects.remove(ob)
        ob = build(grow / s)
    ob.data.transform(Matrix.Translation((cx, cy, (z0 + z1) / 2)) @ Matrix.Diagonal((s, s, 1, 1)) @ Matrix.Translation((-mx, -my, 0)))
    return ob


def text_cutter(body, cx, cy, width, z0, z1):
    return text_mesh(body, cx, cy, width, z0, z1, coll=cutters)


def subtract_cutters(target):
    mod = target.modifiers.new('bool', 'BOOLEAN')
    mod.operation, mod.operand_type, mod.collection, mod.solver = 'DIFFERENCE', 'COLLECTION', cutters, 'EXACT'
    out = bake(target, target.name)
    for ob in list(cutters.objects):
        bpy.data.objects.remove(ob)
    return out


def split_loose(ob):
    """Split a mesh object into one object per connected piece (not linked to any collection)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    seen, pieces = set(), []
    for f in bm.faces:
        if f in seen:
            continue
        seen.add(f)
        stack, piece = [f], []
        while stack:
            g = stack.pop()
            piece.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h not in seen:
                        seen.add(h)
                        stack.append(h)
        pieces.append(piece)
    out = []
    for i, piece in enumerate(pieces):
        nb, vmap = bmesh.new(), {}
        for f in piece:
            nb.faces.new([vmap.get(v) or vmap.setdefault(v, nb.verts.new(v.co)) for v in f.verts])
        me = bpy.data.meshes.new(f'{ob.name}.{i}')
        nb.to_mesh(me)
        nb.free()
        new = bpy.data.objects.new(me.name, me)
        new.matrix_world = ob.matrix_basis
        out.append(new)
    bm.free()
    bpy.data.objects.remove(ob)
    return out


def uv_planar(ob, x0, x1, y0, y1, mirror=False):
    me = ob.data
    for layer in list(me.uv_layers):          # curve-built meshes come with their own UVs; the export takes the first layer
        me.uv_layers.remove(layer)
    uv = me.uv_layers.new(name='UVMap')
    for lp in me.loops:
        co = me.vertices[lp.vertex_index].co
        u = (co.x - x0) / (x1 - x0)
        uv.data[lp.index].uv = (1 - u if mirror else u, (co.y - y0) / (y1 - y0))


def sticker(name, loop, z, thick=0.0, flip=False):
    """Sticker whose printed face lies at z (facing +z, or -z when flip), `thick` deep into the surface."""
    bm = bmesh.new()
    verts = [bm.verts.new((p.x, p.y, z)) for p in loop]
    face = bm.faces.new(verts[::-1] if flip else verts)
    if thick:
        ext = bmesh.ops.extrude_face_region(bm, geom=[face], use_keep_orig=True)
        moved = [g for g in ext['geom'] if isinstance(g, bmesh.types.BMVert)]
        bmesh.ops.translate(bm, verts=moved, vec=(0, 0, thick if flip else -thick))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bmesh.ops.triangulate(bm, faces=bm.faces[:], quad_method='BEAUTY', ngon_method='BEAUTY')
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new(name, me))


def to_side(ob, sx):
    """Move an object built in the (z, y) plane, extruded along its z, onto the side face sx (+1 right, -1 left)."""
    m = Matrix(((0, 0, sx, 0), (0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 0, 1)))
    ob.data.transform(m @ ob.matrix_basis)
    ob.matrix_basis = Matrix.Identity(4)
    bmesh_clean(ob)
    return ob


def to_top(ob):
    """Move an object built in the (x, z) plane onto the top face (see XZ_TO_TOP)."""
    ob.data.transform(XZ_TO_TOP @ ob.matrix_basis)
    ob.matrix_basis = Matrix.Identity(4)
    bmesh_clean(ob)                           # the swap mirrors, so normals need recomputing
    return ob


def rear_text(body, cx, cy, width, face_z, depth):
    """Debossed text on a rear-facing surface at face_z, turned so it reads from behind; returns glyph cutters."""
    z0, z1 = face_z - 0.5, face_z + depth
    zc = (z0 + z1) / 2
    ob = text_cutter(body, cx, cy, width, z0, z1)
    ob.data.transform(Matrix.Translation((cx, 0, zc)) @ Matrix.Rotation(math.pi, 4, 'Y') @ Matrix.Translation((-cx, 0, -zc)))
    cutters.objects.unlink(ob)
    return split_loose(ob)


# ---------------------------------------------------------------- shell
shell = bake(curve_obj('shell', [fillet_poly(outline(-BEVEL), 14)], D / 2 - BEVEL, BEVEL, bevel_res=8), 'Shell')

C = cutters
# interior cavity, minus a boss on each half at every screw (the board sits in the gap between them)
cavity = prism('cavity', fillet_poly(outline(-SIDE_WALL), 14), BACK + WALL, FRONT - WALL)
boss_cut = bpy.data.collections.new('boss_cut')
scene.collection.children.link(boss_cut)
for i, sc in enumerate(SCREWS):
    loop = fillet_poly(rrect(sc.x - BOSS_R, sc.y - BOSS_R, sc.x + BOSS_R, sc.y + BOSS_R, BOSS_R), 12)
    prism(f'boss_back{i}', loop, BACK + WALL - 0.01, -BOARD_GAP, boss_cut)
    prism(f'boss_front{i}', loop, BOARD_GAP, FRONT - WALL + 0.01, boss_cut)
mod = cavity.modifiers.new('bosses', 'BOOLEAN')
mod.operation, mod.operand_type, mod.collection, mod.solver = 'DIFFERENCE', 'COLLECTION', boss_cut, 'EXACT'
cavity = bake(cavity, 'cavity')
for ob in list(boss_cut.objects):
    bpy.data.objects.remove(ob)
bpy.data.collections.remove(boss_cut)
cutters.objects.link(cavity)
scene.collection.objects.unlink(cavity)
# front: stepped label frame, status light pipe, wordmark, insert chevron
prism('frame', fillet_poly(rr(FRAME), 12), FRONT - FRAME_DEPTH, FRONT + 1, C)
prism('well', fillet_poly(rr(WELL), 12), FRONT - WELL_DEPTH, FRONT + 1, C)
prism('led', fillet_poly(rr(LED), 8), FRONT - 0.03, FRONT + 1, C)
text_cutter('OGPLAY', FRAME['x1'] - 0.33, TOP_ROW_Y, 0.66, FRONT - 0.016, FRONT + 0.5)
CHEVRON = [(-0.10, CHEVRON_Y + 0.042, 0.02), (0.0, CHEVRON_Y - 0.043, 0.025), (0.10, CHEVRON_Y + 0.042, 0.02)]
prism('chevron', fillet_poly(CHEVRON, 6), FRONT - 0.012, FRONT + 1, C)
# seam between the front and rear shells
bake(curve_obj('seam', [fillet_poly(rrect(-3, -1, 3, 4, 0)), fillet_poly(outline(-0.03), 14)], 0.007, z=SEAM_Z), 'seam', C)
# rear shell a hair smaller than the front one, so the front lip stands proud at the seam
z0, z1 = BACK - 0.5, SEAM_Z - 0.007
bake(curve_obj('rear_step', [fillet_poly(rrect(-3, -1, 3, 4, 0)), fillet_poly(outline(-REAR_STEP), 14)], (z1 - z0) / 2,
               z=(z0 + z1) / 2), 'rear_step', C)
# plug: slimmer than the body, connector mouth on its bottom face
box('plug_front', -2, 2, -1, TY - 0.03, FRONT - 0.045, 1, C)
box('plug_back', -2, 2, -1, TY - 0.03, -1, BACK + 0.045, C)
box('mouth', -0.70, 0.70, -1, 0.24, -0.075, 0.075, C)
# top edge: shallow well for the spine label, on the front shell
link(to_top(prism('spine_well', fillet_poly(rr(SPINE), 8), H - SPINE_DEPTH, H + 1)), C)
# sides: grip recess with pull grooves, built in (z, y) and stood on each side face
for sx in (-1, 1):
    g = GRIP
    link(to_side(prism(f'grip{sx}', fillet_poly(rrect(-g['zh'], g['y0'], g['zh'], g['y1'], g['r']), 8),
                       BW - g['depth'], BW + 1), sx), C)
    pitch = (g['y1'] - g['y0']) / (g['grooves'] + 1)
    for k in range(1, g['grooves'] + 1):
        y = g['y0'] + pitch * k
        link(to_side(prism(f'groove{sx}{k}', fillet_poly(rrect(-g['zh'] + 0.03, y - 0.009, g['zh'] - 0.03, y + 0.009, 0.009), 4),
                           BW - g['depth'] - g['groove_depth'], BW + 1), sx), C)
# plug back: moulded resin code triangle and cavity ring
PLUG_BACK = BACK + 0.045
tri = lambda cx, cy, s, r: [(cx - s / 2, cy - s * 0.29, r), (cx + s / 2, cy - s * 0.29, r), (cx, cy + s * 0.58, r)]
bake(curve_obj('resin', [fillet_poly(tri(0.50, 0.135, 0.15, 0.02), 6), fillet_poly(tri(0.50, 0.135, 0.105, 0.008), 6)],
               0.25, z=PLUG_BACK - 0.25 + 0.005), 'resin', C)
bake(curve_obj('cavity', [fillet_poly(rrect(-0.50, 0.085, -0.40, 0.185, 0.05), 8), fillet_poly(rrect(-0.488, 0.097, -0.412, 0.173, 0.038), 8)],
               0.25, z=PLUG_BACK - 0.25 + 0.005), 'cavity', C)
# back: sticker recess + screw well
prism('back_recess', fillet_poly(rr(BACKLABEL), 10), BACK - 1, BACK + 0.012, C)
for i, sc in enumerate(SCREWS):
    prism(f'screw_well{i}', fillet_poly(rrect(sc.x - 0.085, sc.y - 0.085, sc.x + 0.085, sc.y + 0.085, 0.085), 10),
          BACK - 1, BACK + 0.05, C)
    # pilot hole the shank threads into, running on into the front shell's boss
    prism(f'pilot{i}', fillet_poly(rrect(sc.x - SHANK_R, sc.y - SHANK_R, sc.x + SHANK_R, sc.y + SHANK_R, SHANK_R), 10),
          BACK + 0.04, SHANK_END + 0.02, C)
shell = subtract_cutters(shell)
# rear lettering, turned to read from behind: wordmark between the top screws, moulding marks between the bottom
# ones and on the plug. One boolean per glyph: as a single cutter the EXACT solver silently drops some letters.
glyphs = (rear_text('OGPLAY', 0, SCREWS[0].y, 0.6, BACK, 0.012)
          + rear_text('MODEL OGP-01   MADE FOR OGPLAY', 0, SCREW_Y[1], 1.12, BACK, 0.006)
          + rear_text('5', 0.50, 0.122, 0.028, PLUG_BACK, 0.005)
          + rear_text('PP', 0.29, 0.135, 0.075, PLUG_BACK, 0.005)
          + rear_text('3', -0.45, 0.135, 0.026, PLUG_BACK, 0.005))
print(f'[cartridge] rear lettering: {len(glyphs)} glyph cutters')
for i, glyph in enumerate(glyphs):
    cutters.objects.link(glyph)
    before = len(shell.data.vertices)
    shell = subtract_cutters(shell)
    if len(shell.data.vertices) == before:
        raise SystemExit(f'[cartridge] rear lettering glyph {i} did not cut the shell')

bev = shell.modifiers.new('rims', 'BEVEL')
bev.limit_method, bev.angle_limit = 'ANGLE', math.radians(40)
bev.width, bev.segments, bev.use_clamp_overlap = 0.006, 3, True
shell = bake(shell, 'Shell')

# ---------------------------------------------------------------- polished trim (one 'Trim' part)
# A metal band set into the seam groove all round, chrome letters inlaid in the front wordmark's deboss (thinner
# than the cut, so a hairline shadow outlines them), and a bezel ring around the status light pipe.
def merge(name, obs):
    bm = bmesh.new()
    for ob in obs:
        ob.data.transform(ob.matrix_basis)
        bm.from_mesh(ob.data)
        bpy.data.objects.remove(ob)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new(name, me))


def shrink(corners, k):
    cx = sum(c[0] for c in corners) / len(corners)
    cy = sum(c[1] for c in corners) / len(corners)
    return [(cx + (x - cx) * k, cy + (y - cy) * k, r * k) for x, y, r in corners]


def rear_inlay(body, cx, cy, width, z0, z1, grow):
    """Inlay letters for a rear deboss: built like the cutter, then turned to read from behind."""
    ob = text_mesh(body, cx, cy, width, z0, z1, grow=grow)
    zc = (z0 + z1) / 2
    ob.data.transform(Matrix.Translation((cx, 0, zc)) @ Matrix.Rotation(math.pi, 4, 'Y') @ Matrix.Translation((-cx, 0, -zc)))
    return ob


# front pieces travel with the front half when the model is taken apart (the seam band too), the rear inlay with the rear
trim = merge('Trim', [
    bake(curve_obj('band', [fillet_poly(outline(-REAR_STEP - 0.003), 14), fillet_poly(outline(-0.034), 14)], 0.0052, z=SEAM_Z), 'band'),
    text_mesh('OGPLAY', FRAME['x1'] - 0.33, TOP_ROW_Y, 0.66, FRONT - 0.016, FRONT - 0.004, grow=-0.0032),
    bake(curve_obj('bezel', [fillet_poly(rr(LED, 0.002), 8), fillet_poly(rr(LED, 0.012), 8)], 0.013, z=FRONT - 0.017), 'bezel'),
    prism('chevron_inlay', fillet_poly(shrink(CHEVRON, 0.78), 6), FRONT - 0.012, FRONT - 0.003),
])
trim_back = merge('TrimBack', [rear_inlay('OGPLAY', 0, SCREWS[0].y, 0.6, BACK + 0.003, BACK + 0.012, grow=-0.003)])
trim['ogp_half'], trim_back['ogp_half'] = 'front', 'back'

# ---------------------------------------------------------------- inserts
# front: a square icon sticker butted onto a title strip, so together they read as one rounded label;
# the outer corners follow the well, the shared edge is straight
lx0, lx1 = WELL['x0'] + LABEL_INSET, WELL['x1'] - LABEL_INSET
ly0, ly1 = WELL['y0'] + LABEL_INSET, WELL['y1'] - LABEL_INSET
rb, rs = WELL['r'] - LABEL_INSET, 0.0
art_y0 = ly1 - (lx1 - lx0)                                   # square
art = sticker('Art', fillet_poly([(lx0, art_y0, rs), (lx1, art_y0, rs), (lx1, ly1, rb), (lx0, ly1, rb)], 12),
              FRONT - WELL_DEPTH + 0.004, STICKER_T)
uv_planar(art, lx0, lx1, art_y0, ly1)
title_y1 = art_y0
title = sticker('Title', fillet_poly([(lx0, ly0, rb), (lx1, ly0, rb), (lx1, title_y1, rs), (lx0, title_y1, rs)], 12),
                FRONT - WELL_DEPTH + 0.004, STICKER_T)
uv_planar(title, lx0, lx1, ly0, title_y1)

back_label = sticker('BackLabel', fillet_poly(rr(BACKLABEL, 0.03), 10), BACK + 0.008, STICKER_T, flip=True)
uv_planar(back_label, BACKLABEL['x0'] + 0.03, BACKLABEL['x1'] - 0.03, BACKLABEL['y0'] + 0.03, BACKLABEL['y1'] - 0.03, mirror=True)

hx, hy, hr = HOLO
holo = sticker('Holo', fillet_poly(rrect(hx - hr, hy - hr, hx + hr, hy + hr, hr), 12), BACK + 0.0072, flip=True)
uv_planar(holo, hx - hr, hx + hr, hy - hr, hy + hr, mirror=True)

# spine label: built in the (x, z) plane, then stood on the top face; v runs front -> back so it reads from the front
sp = rr(SPINE, 0.008)
spine = sticker('Spine', fillet_poly(sp, 8), H - SPINE_DEPTH + 0.0045, STICKER_T)
uv_planar(spine, SPINE['x0'] + 0.008, SPINE['x1'] - 0.008, SPINE['y1'] - 0.008, SPINE['y0'] + 0.008)
to_top(spine)
# corner radius of each sticker as a fraction of its printed width, so the page draws the paper edge on the same curve
# corner radii [top-left, top-right, bottom-right, bottom-left] as fractions of the printed width
lw = lx1 - lx0
art['ogp_corners'] = [rb / lw, rb / lw, rs / lw, rs / lw]
title['ogp_corners'] = [rs / lw, rs / lw, rb / lw, rb / lw]
for ob, d, inset in ((back_label, BACKLABEL, 0.03), (spine, SPINE, 0.008)):
    ob['ogp_corners'] = [(d['r'] - inset) / (d['x1'] - d['x0'] - 2 * inset)] * 4

led = prism('LED', fillet_poly(rr(LED, 0.012), 8), FRONT - 0.03, FRONT - 0.012)
led.data.transform(led.matrix_basis)
led.matrix_basis = Matrix.Identity(4)
uv_planar(led, LED['x0'] + 0.012, LED['x1'] - 0.012, LED['y0'] + 0.012, LED['y1'] - 0.012)

# circuit board: fills the shell (narrow tongue in the plug, full width above), components on its front face
PCB_T = 0.012                                                # half thickness
pcb = prism('PCB', fillet_poly([(-0.80, 0.02, 0.03), (0.80, 0.02, 0.03), (0.80, TY + 0.02, 0.02), (0.96, TY + 0.02, 0.03),
                                (0.96, H - 0.21, 0.06), (-0.96, H - 0.21, 0.06), (-0.96, TY + 0.02, 0.03), (-0.80, TY + 0.02, 0.02)], 8),
            -PCB_T, PCB_T)
pcb.data.transform(pcb.matrix_basis)
pcb.matrix_basis = Matrix.Identity(4)
for i, sc in enumerate(SCREWS):                             # through holes for the screws (the print plates them)
    prism(f'pcb_hole{i}', fillet_poly(rrect(sc.x - 0.055, sc.y - 0.055, sc.x + 0.055, sc.y + 0.055, 0.055), 10), -1, 1, cutters)
pcb = subtract_cutters(pcb)
uv_planar(pcb, -0.96, 0.96, 0.02, H - 0.21)

cm = {k: bmesh.new() for k in ('Chip', 'Metal', 'Passive')}
footprints = []


def block(kind, x0, x1, y0, y1, z0, z1):
    bmesh.ops.create_cube(cm[kind], size=1.0, matrix=Matrix.LocRotScale(
        Vector(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)), None, Vector((x1 - x0, y1 - y0, z1 - z0))))


def drum(kind, cx, cy, r, z0, z1, seg=40):
    bmesh.ops.create_cone(cm[kind], cap_ends=True, segments=seg, radius1=r, radius2=r, depth=z1 - z0,
                          matrix=Matrix.Translation((cx, cy, (z0 + z1) / 2)))


def ic(ref, x0, x1, y0, y1, h, pins, sides='tb'):
    """IC body with gull-wing pins along the given sides (t/b/l/r)."""
    block('Chip', x0, x1, y0, y1, PCB_T, PCB_T + h)
    for side in sides:
        horiz = side in 'tb'
        a0, a1 = (x0, x1) if horiz else (y0, y1)
        step = (a1 - a0) / pins
        for k in range(pins):
            c = a0 + step * (k + 0.5)
            if horiz:
                ey = y1 if side == 't' else y0
                block('Metal', c - 0.011, c + 0.011, min(ey, ey + (0.04 if side == 't' else -0.04)),
                      max(ey, ey + (0.04 if side == 't' else -0.04)), PCB_T, PCB_T + 0.008)
            else:
                ex = x1 if side == 'r' else x0
                block('Metal', min(ex, ex + (0.04 if side == 'r' else -0.04)), max(ex, ex + (0.04 if side == 'r' else -0.04)),
                      c - 0.011, c + 0.011, PCB_T, PCB_T + 0.008)
    footprints.append(dict(ref=ref, kind='ic', box=[x0, y0, x1, y1], pins=pins, sides=sides))


def smd(ref, cx, cy, vertical=False):
    w, h = (0.035, 0.07) if vertical else (0.07, 0.035)
    block('Passive', cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2, PCB_T, PCB_T + 0.022)
    x0, x1, y0, y1, cap = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2, 0.016
    ends = ([(x0 - 0.002, x1 + 0.002, y0 - 0.002, y0 + cap), (x0 - 0.002, x1 + 0.002, y1 - cap, y1 + 0.002)] if vertical else
            [(x0 - 0.002, x0 + cap, y0 - 0.002, y1 + 0.002), (x1 - cap, x1 + 0.002, y0 - 0.002, y1 + 0.002)])
    for e in ends:                                           # tinned end caps
        block('Metal', *e, PCB_T, PCB_T + 0.024)
    footprints.append(dict(ref=ref, kind='smd', box=[cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2]))


ic('U1', -0.52, 0.52, 0.80, 1.12, 0.034, 16)                 # mask ROM
ic('U2', -0.24, 0.24, 1.44, 1.92, 0.03, 10, 'tblr')          # mapper / controller (QFP)
ic('U3', 0.20, 0.78, 2.14, 2.44, 0.03, 8)                    # save SRAM
drum('Metal', -0.48, 2.29, 0.28, PCB_T, PCB_T + 0.06, 64)    # BT1 coin cell + its strap
block('Metal', -0.50, -0.12, 2.25, 2.33, PCB_T + 0.06, PCB_T + 0.068)
block('Metal', -0.14, -0.10, 2.25, 2.33, PCB_T, PCB_T + 0.068)
footprints.append(dict(ref='BT1', kind='cell', box=[-0.76, 2.01, -0.20, 2.57]))
block('Metal', 0.44, 0.70, 1.58, 1.70, PCB_T, PCB_T + 0.04)  # Y1 crystal
footprints.append(dict(ref='Y1', kind='xtal', box=[0.44, 1.58, 0.70, 1.70]))
for ref, cx, cy in (('C1', 0.76, 1.30), ('C2', 0.78, 0.50)):  # electrolytic caps
    drum('Metal', cx, cy, 0.07, PCB_T, PCB_T + 0.11, 32)
    footprints.append(dict(ref=ref, kind='cap', box=[cx - 0.07, cy - 0.07, cx + 0.07, cy + 0.07]))
block('Passive', -0.76, -0.66, 2.79, 2.83, PCB_T, PCB_T + 0.018)  # D1 under the status light pipe
footprints.append(dict(ref='D1', kind='led', box=[-0.76, 2.79, -0.66, 2.83]))
for i, (cx, cy, v) in enumerate(((-0.40, 1.50, 0), (-0.40, 1.64, 0), (-0.40, 1.78, 0), (0.36, 1.48, 1), (0.36, 1.86, 1),
                                 (-0.66, 1.30, 0), (-0.66, 1.95, 1), (0.62, 1.95, 0), (0.06, 2.22, 1), (0.06, 2.38, 1),
                                 (-0.10, 2.66, 0), (0.10, 2.66, 0), (0.30, 2.66, 0), (-0.60, 0.60, 1), (0.60, 0.62, 1))):
    smd(f'{"RC"[i % 2]}{i + 3}', cx, cy, bool(v))
pcb['ogp_board'] = json.dumps(dict(
    parts=footprints, holes=[[sc.x, sc.y] for sc in SCREWS],
    fingers=dict(x0=-0.585, dx=0.09, n=14, y0=0.03, y1=0.23)))
board_parts = []
for kind, bm in cm.items():
    me = bpy.data.meshes.new(kind)
    bm.to_mesh(me)
    bm.free()
    board_parts.append(link(bpy.data.objects.new(kind, me)))
chips, metal, passive = board_parts
gm = bmesh.new()
for i in range(14):
    x = -0.585 + i * 0.09
    for zs in (-1, 1):
        bmesh.ops.create_cube(gm, size=1.0, matrix=Matrix.LocRotScale(Vector((x, 0.13, zs * 0.0135)), None, Vector((0.055, 0.2, 0.003))))
gold = link(bpy.data.objects.new('Gold', bpy.data.meshes.new('Gold')))
gm.to_mesh(gold.data)
gm.free()

# component markings: a hair above each part's top face, all mapped into one atlas the page prints
# (part numbers on the ICs, frequency on the crystal, cell type on the battery, vent cross on the caps).
# Atlas cells are stacked top to bottom, each with its quad's aspect; extras.ogp_marks lists them.
MARKS = [('U1', -0.48, 0.48, 0.84, 1.08, PCB_T + 0.034), ('U2', -0.20, 0.20, 1.48, 1.88, PCB_T + 0.03),
         ('U3', 0.23, 0.75, 2.17, 2.41, PCB_T + 0.03), ('Y1', 0.455, 0.685, 1.595, 1.685, PCB_T + 0.04),
         ('BT1', -0.64, -0.32, 2.35, 2.48, PCB_T + 0.06), ('BT1b', -0.62, -0.34, 2.10, 2.21, PCB_T + 0.06),
         ('C1', 0.715, 0.805, 1.255, 1.345, PCB_T + 0.11), ('C2', 0.735, 0.825, 0.455, 0.545, PCB_T + 0.11)]
GAP = 0.02
cells, top = [], 0.0
for ref, x0, x1, y0, y1, z in MARKS:
    h = (y1 - y0) / (x1 - x0)
    cells.append(dict(ref=ref, top=top, h=h))
    top += h + GAP
ATLAS_H = top
mb = bmesh.new()
uvl = mb.loops.layers.uv.new('UVMap')
for (ref, x0, x1, y0, y1, z), c in zip(MARKS, cells):
    z += 0.0006
    vs = [mb.verts.new(co) for co in ((x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z))]
    f = mb.faces.new(vs)
    vt, vb = 1 - c['top'] / ATLAS_H, 1 - (c['top'] + c['h']) / ATLAS_H     # canvas top = Blender v 1
    for loop, uv in zip(f.loops, ((0, vb), (1, vb), (1, vt), (0, vt))):
        loop[uvl].uv = uv
marking = link(bpy.data.objects.new('Marking', bpy.data.meshes.new('Marking')))
mb.to_mesh(marking.data)
mb.free()
marking['ogp_marks'] = json.dumps(dict(height=ATLAS_H, cells=cells))

# screws: low domes with a tri-wing (three slots at 120°) security recess
screws = []
for i, sc in enumerate(SCREWS):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=40, ring_count=16, radius=0.07, location=(sc.x, sc.y, BACK + 0.05))
    bpy.context.object.scale.z = 0.35
    screws.append(bake(bpy.context.object, f'screw{i}'))
    for k in range(3):
        a = math.radians(90 + 120 * k + 20 * i)          # each screw sits at its own angle, as if driven home
        d, n = Vector((math.cos(a), math.sin(a))), Vector((-math.sin(a), math.cos(a)))
        c0, c1, w = sc - d * 0.004, sc + d * 0.048, 0.0085
        quad = [(*(c0 - n * w), 0.002), (*(c1 - n * w), 0.004), (*(c1 + n * w), 0.004), (*(c0 + n * w), 0.002)]
        prism(f'wing{i}{k}', fillet_poly(quad, 3), BACK - 1, BACK + 0.04, C)
    prism(f'hub{i}', fillet_poly(rrect(sc.x - 0.013, sc.y - 0.013, sc.x + 0.013, sc.y + 0.013, 0.013), 6), BACK - 1, BACK + 0.04, C)
# the dome's buried half goes, leaving a pan head with a flat underside seated on the well floor
box('underside', -2, 2, -1, H + 1, SCREW_SEAT, 1, C)
bpy.ops.object.select_all(action='DESELECT')
for o in screws:
    o.select_set(True)
bpy.context.view_layer.objects.active = screws[0]
bpy.ops.object.join()
heads = subtract_cutters(bpy.context.object)


def shank(cx, cy, z0, z1, r_core=0.03, r_out=0.042, pitch=0.034, seg=28, tip=0.008):
    """Threaded screw shank along +z: a core rod with a flat, lightly chamfered end and a helical thread ridge."""
    bm = bmesh.new()
    body = z1 - tip - z0
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r_core, radius2=r_core, depth=body,
                          matrix=Matrix.Translation((cx, cy, z0 + body / 2)))
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r_core, radius2=r_core - tip, depth=tip,
                          matrix=Matrix.Translation((cx, cy, z1 - tip / 2)))
    steps = int((body - pitch) / pitch * seg)
    rings = []
    for k in range(steps + 1):
        a = 2 * math.pi * k / seg
        z = z0 + pitch * 0.6 + pitch * k / seg
        ca, sa = math.cos(a), math.sin(a)
        rings.append([bm.verts.new((cx + r * ca, cy + r * sa, z + dz))
                      for r, dz in ((r_core - 0.003, -pitch * 0.34), (r_out, 0.0), (r_core - 0.003, pitch * 0.34))])
    for k in range(steps):
        for j in range(2):
            bm.faces.new((rings[k][j], rings[k + 1][j], rings[k + 1][j + 1], rings[k][j + 1]))
    bm.faces.new(rings[0])
    bm.faces.new(rings[-1][::-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new('shank')
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new('shank', me))


screw = merge('Screw', [heads] + [shank(sc.x, sc.y, SCREW_SEAT, SHANK_END) for sc in SCREWS])
bpy.data.collections.remove(cutters)


# ---------------------------------------------------------------- materials
def srgb(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    return tuple(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c)


def mat(name, color, rough, metal=0.0, coat=0.0, coat_rough=0.1, emit=None):
    m = bpy.data.materials.new(name)
    p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*srgb(color), 1)
    p.inputs['Roughness'].default_value = rough
    p.inputs['Metallic'].default_value = metal
    p.inputs['Coat Weight'].default_value = coat
    p.inputs['Coat Roughness'].default_value = coat_rough
    if emit:
        p.inputs['Emission Color'].default_value = (*srgb(emit), 1)
        p.inputs['Emission Strength'].default_value = 3
    return m


m_shell = mat('Shell', '#ffc21a', 0.45, coat=0.25, coat_rough=0.3)
m_art = mat('Art', '#2f86eb', 0.3, coat=0.8, coat_rough=0.08)
if LABEL_IMG:
    tex = m_art.node_tree.nodes.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(LABEL_IMG)
    m_art.node_tree.links.new(tex.outputs['Color'], m_art.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
assign = [(shell, m_shell), (art, m_art), (title, mat('Title', '#141a2c', 0.35, coat=0.8, coat_rough=0.08)), (back_label, mat('BackLabel', '#f2f4fa', 0.5)),
          (led, mat('LED', '#3ddc84', 0.2, emit='#3ddc84')), (pcb, mat('PCB', '#163f30', 0.55)), (chips, mat('Chip', '#16181d', 0.45)),
          (metal, mat('Metal', '#c8ccd4', 0.3, metal=1.0)), (passive, mat('Passive', '#b89a6a', 0.5)),
          (gold, mat('Gold', '#f2c46b', 0.25, metal=1.0)), (screw, mat('Screw', '#b9c0cc', 0.28, metal=1.0)),
          (spine, mat('Spine', '#141a2c', 0.4)), (holo, mat('Holo', '#d8dde8', 0.2, metal=1.0)),
          (trim, mat('Trim', '#e4e7ec', 0.15, metal=1.0)), (marking, mat('Marking', '#d0d4dc', 0.6))]
assign.append((trim_back, assign[-2][1]))
for ob, m in assign:
    ob.data.materials.clear()
    ob.data.materials.append(m)
    ob.data.shade_smooth()
    ob.data.set_sharp_from_angle(angle=math.radians(34))
for ob in (gold, art, title, back_label, pcb, spine, holo, chips, passive, marking):
    ob.data.shade_flat()
wn = shell.modifiers.new('wn', 'WEIGHTED_NORMAL')
wn.mode, wn.weight, wn.keep_sharp = 'FACE_AREA', 100, True
shell = bake(shell, 'Shell', clean=False)
# read by the web shader (cartkit.js) for the two-tone shells and the glossy recess floors
shell['ogp_seam_z'], shell['ogp_face_z'], shell['ogp_plug_y'] = SEAM_Z, D / 2, TY
shell['ogp_grip'] = [GRIP['y0'], GRIP['y1'], GRIP['zh'], BW - GRIP['depth'] - GRIP['groove_depth'] - 0.004]  # y0, y1, half depth, |x| inside
parts = [shell, art, title, back_label, led, pcb, gold, screw, spine, holo, chips, metal, passive, trim, trim_back, marking]

# model space (y up, label +z) -> Blender (z up, label -y) == glTF (y up, label +z)
for ob in parts:
    ob.data.transform(Matrix.Rotation(math.radians(90), 4, 'X') @ ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)

# untextured parts carry no UVs (curve-built meshes come with them; they would only bloat the file)
for ob in (trim, trim_back, screw):
    for layer in list(ob.data.uv_layers):
        ob.data.uv_layers.remove(layer)

tris = sum(len(p.vertices) - 2 for o in parts for p in o.data.polygons)
print(f'[cartridge] parts={[o.name for o in parts]} tris={tris}')

# ---------------------------------------------------------------- ambient occlusion -> texture
AO_SIZE = 1024


def save_gray_png(img, path):
    """Write the red channel of a Non-Color image as an 8-bit grayscale PNG (Blender only writes RGB/RGBA)."""
    import struct
    import zlib
    w, h = img.size
    px = [0.0] * (w * h * 4)
    img.pixels.foreach_get(px)
    red = bytes(max(0, min(255, round(v * 255))) for v in px[0::4])
    # Blender stores rows bottom-up; PNG rows run top-down. Filter type 2 ("Up") suits the smooth AO gradients.
    rows = [red[(h - 1 - y) * w:(h - y) * w] for y in range(h)]
    raw = b''.join(b'\2' + bytes((a - b) & 255 for a, b in zip(row, prev)) for row, prev in zip(rows, [bytes(w)] + rows[:-1]))
    chunk = lambda tag, data: struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


# WebGL has no cheap AO: bake it into <out>-ao.png on a fresh UV layout of the shell.
AO_PNG = OUT.rsplit('.', 1)[0] + '-ao.png'
scene.render.engine = 'CYCLES'
scene.cycles.samples, scene.cycles.device = 256, 'CPU'
scene.world = scene.world or bpy.data.worlds.new('bake')
scene.world.light_settings.distance = 0.35
me = shell.data
for layer in list(me.uv_layers):
    me.uv_layers.remove(layer)
me.uv_layers.new(name='UVMap')
bpy.ops.object.select_all(action='DESELECT')
shell.select_set(True)
bpy.context.view_layer.objects.active = shell
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004)
bpy.ops.object.mode_set(mode='OBJECT')
ao_img = bpy.data.images.new('ShellAO', 2048, 2048)
ao_img.colorspace_settings.name = 'Non-Color'
node = m_shell.node_tree.nodes.new('ShaderNodeTexImage')
node.image = ao_img
m_shell.node_tree.nodes.active = node
bpy.ops.object.bake(type='AO', margin=16)
# AO is one low-frequency channel: bake at 2048 for clean edges, ship it as 1024 8-bit grayscale (~0.31 MB vs 1.8 MB RGB).
ao_img.scale(AO_SIZE, AO_SIZE)
save_gray_png(ao_img, AO_PNG)
m_shell.node_tree.nodes.remove(node)
print(f'[cartridge] wrote {AO_PNG}')

# ---------------------------------------------------------------- front / rear halves
# Split the finished, AO-mapped shell at the seam so the page can take the cartridge apart. Each half is the shell
# intersected with a half space (closed off at the seam); UVs carry over, normals are re-weighted per half.
# Model z (towards the label) is Blender -y here, so the front half is y < 0.
def half(name, y0, y1, side):
    ob = shell.copy()
    ob.data = shell.data.copy()
    link(ob)
    cut = box(f'{name}_box', -5, 5, y0, y1, -5, 10)
    mod = ob.modifiers.new('half', 'BOOLEAN')
    mod.operation, mod.object, mod.solver = 'INTERSECT', cut, 'EXACT'
    ob = bake(ob, name, clean=False)
    bpy.data.objects.remove(cut)
    ob.data.set_sharp_from_angle(angle=math.radians(34))
    wn = ob.modifiers.new('wn', 'WEIGHTED_NORMAL')
    wn.mode, wn.weight, wn.keep_sharp = 'FACE_AREA', 100, True
    ob = bake(ob, name, clean=False)
    for k in ('ogp_seam_z', 'ogp_face_z', 'ogp_plug_y', 'ogp_grip'):
        ob[k] = shell[k]
    ob['ogp_half'] = side
    return ob


shell_front, shell_back = half('ShellFront', -5, -SEAM_Z, 'front'), half('ShellBack', -SEAM_Z, 5, 'back')
bpy.data.objects.remove(shell)
parts[0:1] = [shell_front, shell_back]

# ---------------------------------------------------------------- export
bpy.ops.object.select_all(action='DESELECT')
for ob in parts:
    ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_extras=True, export_yup=True, export_apply=True,
                          export_materials='EXPORT', export_vertex_color='NONE',
                          export_image_format='NONE' if not LABEL_IMG else 'AUTO',
                          # Draco: 2.9 MB -> ~0.39 MB; 14-bit positions stay < 0.005 % of the model size, 10-bit normals < 0.3 deg (p99.9).
                          export_draco_mesh_compression_enable=True, export_draco_mesh_compression_level=10,
                          export_draco_position_quantization=14, export_draco_normal_quantization=10,
                          export_draco_texcoord_quantization=12)
print(f'[cartridge] wrote {OUT}')

# ---------------------------------------------------------------- preview renders
if PREVIEW:
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 900, 900
    world = bpy.data.worlds.new('w')
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (*srgb('#141a2c'), 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.7
    scene.world = world
    look = Vector((0, 0, H / 2))
    for name, loc, power, size in (('key', (3, -5, 5), 900, 3), ('fill', (-5, -3, 2), 300, 4), ('rim', (-2, 5, 4), 700, 2)):
        ld = bpy.data.lights.new(name, 'AREA')
        ld.energy, ld.size = power, size
        lo = link(bpy.data.objects.new(name, ld))
        lo.location = loc
        lo.rotation_euler = (look - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    root = link(bpy.data.objects.new('root', None))
    root.location = look
    for ob in parts:
        ob.parent = root
        ob.matrix_parent_inverse = Matrix.Translation(-look)
    cam = link(bpy.data.objects.new('cam', bpy.data.cameras.new('cam')))
    cam.data.lens = 70
    cam.location = (0, -9.5, 2.6 + H / 2)
    cam.rotation_euler = (look - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.camera = cam
    for view, rot in (('front', (0, 0, math.radians(-28))), ('back', (0, 0, math.radians(180 - 30))),
                      ('bottom', (math.radians(-62), 0, math.radians(-20)))):
        root.rotation_euler = rot
        scene.render.filepath = f'{PREVIEW}-{view}.png'
        bpy.ops.render.render(write_still=True)
    print(f'[cartridge] previews {PREVIEW}-*.png')
