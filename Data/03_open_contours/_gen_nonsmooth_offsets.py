#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate OPEN contours whose EQUIDISTANT (geometric offset) is non-smooth
(develops cusps / kinks / self-intersections) while the solution of the
Laplace equation on the band is significantly smoother.

Why the contrast exists
------------------------
* A geometric offset (equidistant) of a curve is  p(s) + d * n(s).
  It becomes singular wherever the offset distance d reaches the local radius
  of curvature  rho = 1/kappa  (the offset touches the curve's EVOLUTE) and at
  every concave corner (where the normals cross immediately).  Result: cusps,
  swallowtails and self-intersections -> a C0-but-not-C1 curve.

* The Laplace solution T (harmonic, div(grad T)=0) is real-analytic in the
  interior.  Its level sets are smooth (C-infinity) no matter how spiky the
  source is, so the harmonic "distance" regularises exactly the features that
  break the geometric offset.

So each contour below is chosen to MAXIMISE offset singularities:
sharp corners (kappa -> inf) and/or smooth pieces with radius of curvature
smaller than a typical offset distance (rho < d).

CSV3D output: a node list (T=0 for the source) + an open edge polyline
0-1-2-...-N (no closing edge).  Matches the other files in this folder.
"""

import numpy as np
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def write_csv3d(path, pts):
    """pts: (N,2) array in XY. Writes an open polyline (edges 0-1-...-N-1)."""
    pts = np.asarray(pts, dtype=float)
    n = len(pts)
    with open(path, "w", newline="\n") as f:
        f.write("#nodes\n")
        f.write("node_id;x;y;z;T\n")
        for i, (x, y) in enumerate(pts):
            f.write(f"{i};{x:.7f};{y:.7f};0.0000000;0.0000000\n")
        f.write("#triangles\n")
        f.write("#face;0\n")
        f.write(f"#edge;{n-1}\n")
        for i in range(n - 1):
            f.write(f"{i};{i+1}\n")
    print(f"  {os.path.basename(path):42s} {n:4d} nodes")


def resample_arclen(xy, step):
    """Resample a dense polyline to ~uniform arc-length spacing 'step'.
    Used for SMOOTH curves (keeps them smooth, equidistant still cusps)."""
    xy = np.asarray(xy, float)
    seg = np.linalg.norm(np.diff(xy, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    total = s[-1]
    m = max(2, int(round(total / step)) + 1)
    si = np.linspace(0.0, total, m)
    x = np.interp(si, s, xy[:, 0])
    y = np.interp(si, s, xy[:, 1])
    return np.column_stack([x, y])


def densify_polyline(corners, step):
    """Subdivide each straight segment of a corner list to ~'step' spacing,
    keeping the EXACT corner vertices (so sharp angles survive)."""
    corners = np.asarray(corners, float)
    out = [corners[0]]
    for a, b in zip(corners[:-1], corners[1:]):
        L = np.linalg.norm(b - a)
        k = max(1, int(round(L / step)))
        for j in range(1, k + 1):
            out.append(a + (b - a) * (j / k))
    return np.array(out)


# ----------------------------------------------------------------------------
# 1) SAWTOOTH  -- sharp V corners (kappa -> inf at every peak/valley).
#    Offset: a cusp on the concave side of every corner; arcs on the convex
#    side. Strongly C0-only. Laplace: smooth.
# ----------------------------------------------------------------------------
def gen_sawtooth():
    teeth = 8
    A = 45.0
    x0, x1 = -150.0, 150.0
    xs = np.linspace(x0, x1, teeth + 1)
    corners = []
    for i, x in enumerate(xs):
        corners.append((x, A if i % 2 == 0 else -A))
    return densify_polyline(corners, step=2.5)


# ----------------------------------------------------------------------------
# 2) COMB OF SPIKES -- tall thin triangular needles on a baseline.
#    Extreme curvature at the tips AND in the narrow valleys between spikes;
#    the offset self-intersects and forms swallowtails almost everywhere.
# ----------------------------------------------------------------------------
def gen_comb_spikes():
    n_spikes = 9
    base_y = -10.0
    tip_y = 90.0
    half_w = 6.0           # half-width of each spike base -> very sharp tip
    x0, x1 = -150.0, 150.0
    centers = np.linspace(x0 + 18, x1 - 18, n_spikes)
    corners = [(x0, base_y)]
    for cx in centers:
        corners.append((cx - half_w, base_y))
        corners.append((cx, tip_y))
        corners.append((cx + half_w, base_y))
    corners.append((x1, base_y))
    return densify_polyline(corners, step=2.5)


# ----------------------------------------------------------------------------
# 3) RECTIFIED SINE  y = A*|sin(k x)|  -- slope discontinuity (corner) at every
#    zero crossing. Curve is C0; its equidistant cusps at each corner. The
#    smooth arches keep |kappa| high near the crests too.
# ----------------------------------------------------------------------------
def gen_rectified_sine():
    x = np.linspace(-150.0, 150.0, 4001)
    k = 2 * np.pi / 60.0          # period 60
    y = 55.0 * np.abs(np.sin(k * x))
    dense = np.column_stack([x, y])
    # arc-length resample but the corners at zeros are preserved well enough
    return resample_arclen(dense, step=2.5)


# ----------------------------------------------------------------------------
# 4) ASTROID ARC  x=a cos^3 t, y=a sin^3 t  -- the curve ITSELF has cusps, and
#    its evolute is another (scaled, rotated) astroid, so the equidistant grows
#    a rich nested-cusp structure. Two intrinsic cusps included in the arc.
# ----------------------------------------------------------------------------
def gen_astroid_arc():
    a = 130.0
    t = np.linspace(np.pi / 2 - 1.15, np.pi + 1.15, 4001)
    x = a * np.cos(t) ** 3
    y = a * np.sin(t) ** 3
    dense = np.column_stack([x, y])
    return resample_arclen(dense, step=2.0)


# ----------------------------------------------------------------------------
# 5) SMOOTH HIGH-CURVATURE BUMPS -- the cleanest illustration.
#    The source is C-infinity smooth (a sum of sines), but the radius of
#    curvature at the crests/troughs is deliberately << offset distance, so the
#    equidistant develops cusps even though the source has NO corners at all.
#    rho_min ~ 1/(A k^2); here A k^2 is made large.
# ----------------------------------------------------------------------------
def gen_smooth_highcurv_bumps():
    x = np.linspace(-150.0, 150.0, 8001)
    # base wave + a higher harmonic to push curvature up locally, still smooth.
    # rho_min ~ 1/(A k^2): here ~3 units, well below a typical offset of 20-40,
    # so the equidistant cusps even though the source has NO corners.
    y = (30.0 * np.sin(2 * np.pi * x / 80.0)
         + 7.0 * np.sin(2 * np.pi * x / 33.0 + 0.7))
    dense = np.column_stack([x, y])
    # fine arc-length sampling so the polyline stays VISIBLY smooth (small
    # turn per vertex); the cusps must come from the offset, not the sampling.
    return resample_arclen(dense, step=0.8)


# ----------------------------------------------------------------------------
# 6) STAIRCASE -- 90-degree steps. Alternating convex/concave right angles;
#    every concave inner corner produces an offset cusp.
# ----------------------------------------------------------------------------
def gen_staircase():
    steps = 7
    run = 300.0 / steps
    rise = 22.0
    x = -150.0
    y = -((steps - 1) * rise) / 2.0
    corners = [(x, y)]
    for i in range(steps):
        x += run
        corners.append((x, y))     # horizontal run
        if i < steps - 1:
            y += rise
            corners.append((x, y)) # vertical rise
    return densify_polyline(corners, step=3.0)


def main():
    jobs = [
        ("sawtooth_open.csv3d",             gen_sawtooth),
        ("comb_spikes_open.csv3d",          gen_comb_spikes),
        ("rectified_sine_open.csv3d",       gen_rectified_sine),
        ("astroid_cusps_open.csv3d",        gen_astroid_arc),
        ("smooth_highcurv_bumps_open.csv3d", gen_smooth_highcurv_bumps),
        ("staircase_open.csv3d",            gen_staircase),
    ]
    print("Generating non-smooth-equidistant open contours:")
    for name, fn in jobs:
        write_csv3d(os.path.join(HERE, name), fn())


if __name__ == "__main__":
    main()
