import math, os

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "Data", "07_symmetric_forgings_3d")
OUT_DIR = os.path.normpath(OUT_DIR)

class Mesh:
    def __init__(self):
        self.verts = []
        self.index = {}
        self.tris = []
    def vid(self, p):
        key = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
        i = self.index.get(key)
        if i is None:
            i = len(self.verts)
            self.index[key] = i
            self.verts.append((key[0], key[1], key[2]))
        return i
    def add_tri(self, a, b, c, outward):
        ax, ay, az = a; bx, by, bz = b; cx, cy, cz = c
        ux, uy, uz = bx - ax, by - ay, bz - az
        vx, vy, vz = cx - ax, cy - ay, cz - az
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        if nx * outward[0] + ny * outward[1] + nz * outward[2] < 0.0:
            b, c = c, b
        self.tris.append((self.vid(a), self.vid(b), self.vid(c)))
    def add_face(self, pts, outward):
        for i in range(1, len(pts) - 1):
            self.add_tri(pts[0], pts[i], pts[i + 1], outward)
    def write(self, path):
        with open(path, "w") as f:
            f.write("#nodes\n")
            f.write("node_id;x;y;z\n")
            for i, (x, y, z) in enumerate(self.verts):
                f.write("%d;%.7f;%.7f;%.7f\n" % (i, x, y, z))
            f.write("#triangles\n")
            for a, b, c in self.tris:
                f.write("%d;%d;%d\n" % (a, b, c))
        print("wrote %s  (%d nodes, %d triangles)" % (os.path.basename(path), len(self.verts), len(self.tris)))


def lathe(profile, segments):
    # profile: list of (r, z), ordered so the solid interior is on the LEFT of
    # travel. Revolved around the z axis into a closed surface. Outward normal of
    # each band is the revolved 2D right-normal of the profile segment.
    m = Mesh()
    th = [2.0 * math.pi * j / segments for j in range(segments)]
    cos = [math.cos(t) for t in th]
    sin = [math.sin(t) for t in th]

    def ring(r, z, j):
        return (r * cos[j], r * sin[j], z)

    for k in range(len(profile) - 1):
        r0, z0 = profile[k]
        r1, z1 = profile[k + 1]
        dr, dz = r1 - r0, z1 - z0
        L = math.hypot(dr, dz)
        if L < 1e-9:
            continue
        # 2D right-normal (interior on left -> outward on right).
        n2 = (dz / L, -dr / L)
        for j in range(segments):
            jn = (j + 1) % segments
            # Outward direction sampled at the mid angle of this quad.
            mc = math.cos(0.5 * (th[j] + (th[jn] if jn else 2 * math.pi)))
            ms = math.sin(0.5 * (th[j] + (th[jn] if jn else 2 * math.pi)))
            outward = (n2[0] * mc, n2[0] * ms, n2[1])
            if r0 < 1e-9:
                m.add_face([(0.0, 0.0, z0), ring(r1, z1, j), ring(r1, z1, jn)], outward)
            elif r1 < 1e-9:
                m.add_face([ring(r0, z0, j), (0.0, 0.0, z1), ring(r0, z0, jn)], outward)
            else:
                m.add_face([ring(r0, z0, j), ring(r1, z1, j), ring(r1, z1, jn), ring(r0, z0, jn)], outward)
    return m


def spool_grooved_sym():
    # Closed solid of revolution, symmetric about the XY plane (r(z)=r(-z)), with
    # a circumferential concave groove in each half-space (z>0 and z<0). Non-convex.
    R0, h = 42.0, 36.0
    zg, depth, sig = 20.0, 14.0, 6.0
    steps = 22
    prof = [(0.0, -h)]
    prof.append((R0, -h))
    for i in range(steps + 1):
        z = -h + 2.0 * h * i / steps
        r = R0 - depth * math.exp(-((abs(z) - zg) ** 2) / (2.0 * sig * sig))
        prof.append((r, z))
    prof.append((R0, h))
    prof.append((0.0, h))
    return lathe(prof, 40)


def cup_bore_forging():
    # NOT symmetric about the XY plane: a thick disc with a blind cylindrical bore
    # opened from the top. The bore is a concave element; the body is non-convex.
    R, H = 40.0, 50.0
    rb, hb = 22.0, 16.0
    prof = [
        (0.0, 0.0), (R, 0.0), (R, H), (rb, H), (rb, hb), (0.0, hb),
    ]
    return lathe(prof, 44)


def extruded_prism_sym(outline, C):
    # Extrude a star-shaped (about origin) 2D outline along z in [-C, C] and cap
    # both ends. Symmetric about the XY plane. A non-convex outline yields
    # re-entrant (concave) vertical faces present in both half-spaces.
    m = Mesh()
    n = len(outline)
    for i in range(n):
        x0, y0 = outline[i]
        x1, y1 = outline[(i + 1) % n]
        ex, ey = x1 - x0, y1 - y0
        L = math.hypot(ex, ey)
        if L < 1e-9:
            continue
        outward = (ey / L, -ex / L, 0.0)
        m.add_face([(x0, y0, -C), (x1, y1, -C), (x1, y1, C), (x0, y0, C)], outward)
    top = [(x, y, C) for (x, y) in outline]
    bot = [(x, y, -C) for (x, y) in outline]
    for i in range(n):
        m.add_tri((0.0, 0.0, C), top[i], top[(i + 1) % n], (0, 0, 1))
        m.add_tri((0.0, 0.0, -C), bot[i], bot[(i + 1) % n], (0, 0, -1))
    return m


def cross_prism_sym():
    w, Lr = 16.0, 46.0
    outline = [
        (Lr, -w), (Lr, w), (w, w), (w, Lr), (-w, Lr), (-w, w),
        (-Lr, w), (-Lr, -w), (-w, -w), (-w, -Lr), (w, -Lr), (w, -w),
    ]
    return extruded_prism_sym(outline, 22.0)


def gear_prism_sym():
    teeth = 8
    R_out, R_in = 46.0, 34.0
    outline = []
    per = 2 * teeth
    for k in range(per):
        a = 2.0 * math.pi * k / per
        r = R_out if (k % 2 == 0) else R_in
        outline.append((r * math.cos(a), r * math.sin(a)))
    return extruded_prism_sym(outline, 20.0)


def slab_pockets_sym():
    # Rectangular slab, symmetric about the XY plane, with a rectangular blind
    # pocket recessed into both the top (+z) and bottom (-z) faces. The pockets are
    # the concave elements (one per half-space); the body is non-convex.
    m = Mesh()
    A, B, C = 60.0, 44.0, 26.0
    pa, pb, d = 36.0, 24.0, 14.0

    # Outer side walls (full height).
    m.add_face([(A, -B, -C), (A, B, -C), (A, B, C), (A, -B, C)], (1, 0, 0))
    m.add_face([(-A, -B, -C), (-A, B, -C), (-A, B, C), (-A, -B, C)], (-1, 0, 0))
    m.add_face([(-A, B, -C), (A, B, -C), (A, B, C), (-A, B, C)], (0, 1, 0))
    m.add_face([(-A, -B, -C), (A, -B, -C), (A, -B, C), (-A, -B, C)], (0, -1, 0))

    for sgn in (1, -1):
        zc = sgn * C          # outer face plane
        zf = sgn * (C - d)    # pocket floor plane
        up = (0, 0, sgn)
        O = [(-A, -B), (A, -B), (A, B), (-A, B)]
        I = [(-pa, -pb), (pa, -pb), (pa, pb), (-pa, pb)]
        # Top/bottom rim (frame between outer rect and pocket rect).
        for i in range(4):
            o0, o1 = O[i], O[(i + 1) % 4]
            i0, i1 = I[i], I[(i + 1) % 4]
            m.add_face([(o0[0], o0[1], zc), (o1[0], o1[1], zc),
                        (i1[0], i1[1], zc), (i0[0], i0[1], zc)], up)
        # Pocket side walls (face toward the pocket interior / axis).
        for i in range(4):
            i0, i1 = I[i], I[(i + 1) % 4]
            cx, cy = (i0[0] + i1[0]) * 0.5, (i0[1] + i1[1]) * 0.5
            inward = (-cx, -cy, 0.0)
            m.add_face([(i0[0], i0[1], zc), (i1[0], i1[1], zc),
                        (i1[0], i1[1], zf), (i0[0], i0[1], zf)], inward)
        # Pocket floor (faces up out of the pocket, i.e. same sense as the face).
        m.add_face([(I[0][0], I[0][1], zf), (I[1][0], I[1][1], zf),
                    (I[2][0], I[2][1], zf), (I[3][0], I[3][1], zf)], up)
    return m


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    spool_grooved_sym().write(os.path.join(OUT_DIR, "spool_grooved_sym.csv3d"))
    slab_pockets_sym().write(os.path.join(OUT_DIR, "slab_pockets_sym.csv3d"))
    cross_prism_sym().write(os.path.join(OUT_DIR, "cross_prism_sym.csv3d"))
    gear_prism_sym().write(os.path.join(OUT_DIR, "gear_prism_sym.csv3d"))
    cup_bore_forging().write(os.path.join(OUT_DIR, "cup_bore_forging.csv3d"))


if __name__ == "__main__":
    main()
