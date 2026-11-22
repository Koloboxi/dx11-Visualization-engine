"""
Demo forging that a flat parting plane cannot split: a cranked lever (коленчатый
рычаг) - two bosses at different heights joined by an arm that climbs between them.

    left boss   axis Z at x = -70, radius 26, z from -18 to 18 (mid-plane z = 0)
    right boss  axis Z at x = +70, radius 22, z from  18 to 50 (mid-plane z = 34)
    arm         14 thick, 20 wide at the left boss narrowing to 16 at the right;
                its mid-surface is level at z = 0 up to x = -35, climbs smoothly
                to z = 34 at x = +35 and is level again after that
    rounds      3 mm on the boss edges, 2.5 mm on the arm, ~4 mm fillets where
                the arm meets the bosses (smooth union)

Any horizontal plane leaves one boss wholly on one side of it, so the die of that
side would have to form the boss's far face too - an undercut. Parted along the
arm's mid-surface instead - in the front view (XZ, draw along Z, swept along Y):

    (-200, 0)  (-35, 0)  (35, 34)  (200, 34)

each die forms one face of the lever all along. Units are millimetres.

The surface is the zero level of a signed distance field, extracted by marching
tetrahedra on a regular grid (watertight by construction), then evened out by the
isotropic remesher next to this script (remesh_csv3d.py).

Usage:  python _gen_cranked_lever.py [--step 1.4] [--tris 24000] [--raw]
"""
import argparse
import math
import os
import sys

import numpy as np

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
NAME = "cranked_lever_forging_closed.csv3d"

# ---- the shape -------------------------------------------------------------------

RISE = 34.0          # height of the right boss's mid-plane over the left one's
X_RAMP = 35.0        # the arm climbs between x = -X_RAMP and x = +X_RAMP


def smoothstep(u):
    u = np.clip(u, 0.0, 1.0)
    return u * u * (3.0 - 2.0 * u)


def arm_mid(x):
    """Height of the arm's mid-surface over x."""
    return RISE * smoothstep((x + X_RAMP) / (2.0 * X_RAMP))


def sd_round_cylinder(x, y, z, cx, cy, cz, radius, half_h, rnd):
    """Rounded cylinder along Z (exact signed distance)."""
    qx = np.hypot(x - cx, y - cy) - (radius - rnd)
    qz = np.abs(z - cz) - (half_h - rnd)
    outside = np.hypot(np.maximum(qx, 0.0), np.maximum(qz, 0.0))
    return np.minimum(np.maximum(qx, qz), 0.0) + outside - rnd


def sd_arm(x, y, z, rnd=2.5):
    """The arm: a rounded box in coordinates sheared to follow its mid-surface."""
    half_w = 10.0 - 2.0 * (x + 70.0) / 140.0          # 10 at x = -70, 8 at x = +70
    zq = z - arm_mid(x)
    dx = np.abs(x) - (70.0 - rnd)
    dy = np.abs(y) - (half_w - rnd)
    dz = np.abs(zq) - (7.0 - rnd)
    outside = np.sqrt(np.maximum(dx, 0.0) ** 2 + np.maximum(dy, 0.0) ** 2 + np.maximum(dz, 0.0) ** 2)
    return np.minimum(np.maximum(np.maximum(dx, dy), dz), 0.0) + outside - rnd


def smin(a, b, k):
    """Polynomial smooth minimum: a fillet of about k / 2 where the two meet."""
    h = np.maximum(k - np.abs(a - b), 0.0) / k
    return np.minimum(a, b) - h * h * k * 0.25


def sdf(x, y, z):
    left = sd_round_cylinder(x, y, z, -70.0, 0.0, 0.0, 26.0, 18.0, 3.0)
    right = sd_round_cylinder(x, y, z, 70.0, 0.0, RISE, 22.0, 16.0, 3.0)
    return smin(smin(left, sd_arm(x, y, z), 8.0), right, 8.0)


# ---- marching tetrahedra -----------------------------------------------------------

# The cube split into six tetrahedra around its main diagonal (corner c = dx + 2dy + 4dz);
# every cube is split the same way, so neighbouring cubes agree on their shared faces.
TETS = [(0, 1, 3, 7), (0, 1, 5, 7), (0, 2, 3, 7), (0, 2, 6, 7), (0, 4, 5, 7), (0, 4, 6, 7)]
OTHERS = np.array([[1, 2, 3], [0, 2, 3], [0, 1, 3], [0, 1, 2]])
# Two corners inside: the inside pair and the outside pair, by the 4-bit inside mask.
PAIRS = {0b0011: ((0, 1), (2, 3)), 0b0101: ((0, 2), (1, 3)), 0b0110: ((1, 2), (0, 3)),
         0b1001: ((0, 3), (1, 2)), 0b1010: ((1, 3), (0, 2)), 0b1100: ((2, 3), (0, 1))}


def marching_tets(xs, ys, zs, F):
    nx, ny, nz = F.shape
    gid = np.arange(nx * ny * nz, dtype=np.int64).reshape(nx, ny, nz)
    corner = {}
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                corner[dx + 2 * dy + 4 * dz] = gid[dx:nx - 1 + dx, dy:ny - 1 + dy, dz:nz - 1 + dz].ravel()
    f_all = F.ravel()

    # Each triangle vertex is the crossing on a grid edge (a, b); `inside`/`outside`
    # of its first vertex orient it.
    ea, eb, tin, tout = [], [], [], []
    for tet in TETS:
        v = np.stack([corner[c] for c in tet], axis=1)          # (m, 4) grid ids
        f = f_all[v]
        inside = f < 0.0
        mask = (inside[:, 0] * 1 + inside[:, 1] * 2 + inside[:, 2] * 4 + inside[:, 3] * 8)
        n_in = inside.sum(axis=1)
        # One corner apart from the other three: one triangle on its three edges.
        for lone_inside in (True, False):
            sel = np.where(n_in == (1 if lone_inside else 3))[0]
            if sel.size == 0:
                continue
            flag = inside[sel] if lone_inside else ~inside[sel]
            lone = np.argmax(flag, axis=1)
            oth = OTHERS[lone]
            vs = v[sel]
            L = vs[np.arange(sel.size), lone]
            O = np.take_along_axis(vs, oth, axis=1)
            for k in range(3):
                ea.append(L); eb.append(O[:, k])
            if lone_inside:
                tin.append(L); tout.append(O[:, 0])
            else:
                tin.append(O[:, 0]); tout.append(L)
        # Two and two: a quad on the four edges between the pairs, as two triangles.
        for m, ((i0, i1), (o0, o1)) in PAIRS.items():
            sel = np.where(mask == m)[0]
            if sel.size == 0:
                continue
            vs = v[sel]
            a, b, c, d = vs[:, i0], vs[:, i1], vs[:, o0], vs[:, o1]
            # quad a-c, a-d, b-d, b-c
            for (p, q) in ((a, c), (a, d), (b, d)):
                ea.append(p); eb.append(q)
            tin.append(a); tout.append(c)
            for (p, q) in ((a, c), (b, d), (b, c)):
                ea.append(p); eb.append(q)
            tin.append(a); tout.append(c)

    # Interleave per triangle: the lists above were filled in blocks of three.
    ea_blocks, eb_blocks = [], []
    k = 0
    tri_count = 0
    while k < len(ea):
        ea_blocks.append(np.stack([ea[k], ea[k + 1], ea[k + 2]], axis=1))
        eb_blocks.append(np.stack([eb[k], eb[k + 1], eb[k + 2]], axis=1))
        tri_count += ea[k].size
        k += 3
    A = np.concatenate(ea_blocks)          # (T, 3)
    B = np.concatenate(eb_blocks)
    tin = np.concatenate(tin)
    tout = np.concatenate(tout)

    lo = np.minimum(A, B)
    hi = np.maximum(A, B)
    keys = lo * (nx * ny * nz) + hi
    uniq, inv = np.unique(keys.ravel(), return_inverse=True)
    ua = uniq // (nx * ny * nz)
    ub = uniq % (nx * ny * nz)

    def pos(ids):
        i, rem = np.divmod(ids, ny * nz)
        j, kk = np.divmod(rem, nz)
        return np.stack([xs[i], ys[j], zs[kk]], axis=1)

    pa, pb = pos(ua), pos(ub)
    fa, fb = f_all[ua], f_all[ub]
    t = (fa / (fa - fb))[:, None]
    P = pa + t * (pb - pa)
    T = inv.reshape(-1, 3)

    # Outward: from the inside corner of a triangle's tetrahedron to the outside one.
    n = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    d = pos(tout) - pos(tin)
    flip = np.einsum("ij,ij->i", n, d) < 0.0
    T[flip] = T[flip][:, [0, 2, 1]]
    # Drop triangles that collapsed onto a grid point.
    keep = (T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])
    return P, T[keep]


def check_closed(T):
    e = np.sort(np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]]), axis=1)
    _, counts = np.unique(e[:, 0].astype(np.int64) * (T.max() + 1) + e[:, 1], return_counts=True)
    return int((counts != 2).sum())


def volume(P, T):
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    return float(np.einsum("ij,ij->i", a, np.cross(b, c)).sum() / 6.0)


def write_csv3d(path, P, T):
    with open(path, "w", newline="\n") as f:
        f.write("#nodes\nnode_id;x;y;z;T\n")
        for i, (x, y, z) in enumerate(P):
            f.write(f"{i};{x:.7f};{y:.7f};{z:.7f};0\n")
        f.write("#triangles\n")
        for a, b, c in T:
            f.write(f"{a};{b};{c}\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--step", type=float, default=1.4, help="grid step of the extraction, mm")
    ap.add_argument("--tris", type=int, default=24000, help="triangles after the remesh")
    ap.add_argument("--raw", action="store_true", help="write the extracted mesh, skip the remesh")
    args = ap.parse_args(argv)

    h = args.step
    xs = np.arange(-104.0, 98.0 + h, h)
    ys = np.arange(-32.0, 32.0 + h, h)
    zs = np.arange(-24.0, 56.0 + h, h)
    X, Y, Z = np.meshgrid(xs, ys, zs, indexing="ij")
    F = sdf(X, Y, Z)
    F[np.abs(F) < 1e-7] = 1e-7          # never exactly on a grid point
    P, T = marching_tets(xs, ys, zs, F)
    print(f"extracted: {len(P)} nodes, {len(T)} triangles, open edges {check_closed(T)}, "
          f"volume {volume(P, T):.1f} mm^3")

    out = os.path.join(OUT_DIR, NAME)
    if args.raw:
        write_csv3d(out, P, T)
        print(f"wrote {out}")
        return
    raw = out + ".raw.csv3d"
    write_csv3d(raw, P, T)
    sys.path.insert(0, OUT_DIR)
    import remesh_csv3d
    remesh_csv3d.main([raw, out, "--tris", str(args.tris), "--feature", "60", "--quiet"])
    os.remove(raw)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
