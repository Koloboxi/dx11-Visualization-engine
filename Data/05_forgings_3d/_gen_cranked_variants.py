"""Five nonplanar-parting forgings, generated from implicit geometry.

Requires NumPy, _gen_cranked_lever.py and a built surface_extender_mesher.dll.
The model coordinates and the suggested parting profiles are in millimetres.
"""
import argparse
import ctypes as C
from pathlib import Path

import numpy as np

from _gen_cranked_lever import marching_tets, check_closed, volume, write_csv3d


# Main bosses along X: (x, y, radius_x, radius_y, half thickness).
# Additional bosses, links (ax, ay, bx, by, half widths at a/b), and bored
# apertures (x, y, radius_x, radius_y) define each plan.
MODELS = {
    "cranked_tandem_rocker": {
        "bosses": [(-90, 0, 24, 23, 13), (-30, 0, 19, 20, 12),
                   (30, 0, 23, 22, 13), (90, 0, 21, 20, 12),
                   (84, 34, 14, 15, 10)],
        "links": [(-90, 0, -30, 0, 14, 11), (-30, 0, 30, 0, 11, 12),
                  (30, 0, 90, 0, 12, 13), (90, 0, 84, 34, 12, 9)],
        "bores": [(-90, 0, 8, 8), (-30, 0, 6, 6), (30, 0, 8, 8), (84, 34, 5, 5)],
        "levels": [-25, 21, -25, 27], "shelf": 10, "web": 7.5, "round": 2.6,
    },
    "cranked_forked_clevis": {
        "bosses": [(-88, 0, 22, 24, 14), (-31, 0, 20, 22, 11),
                   (28, 0, 21, 23, 13), (92, -24, 20, 17, 11),
                   (92, 24, 20, 17, 11)],
        "links": [(-88, 0, -31, 0, 15, 13), (-31, 0, 28, 0, 12, 17),
                  (28, 0, 92, -24, 15, 10), (28, 0, 92, 24, 15, 10)],
        "bores": [(-88, 0, 9, 9), (-31, 0, 6, 6), (92, -24, 7, 7), (92, 24, 7, 7)],
        "levels": [-19, 29, -23, 18], "shelf": 12, "web": 8, "round": 3.1,
    },
    "cranked_window_bridge": {
        "bosses": [(-105, 0, 23, 24, 13), (-35, 0, 21, 22, 13),
                   (35, 0, 23, 24, 13), (105, 0, 24, 22, 13),
                   (-35, 36, 18, 17, 9), (35, 36, 17, 16, 9)],
        "links": [(-105, 0, -35, 0, 14, 13), (-35, 0, 35, 0, 12, 13),
                  (35, 0, 105, 0, 12, 14), (-35, 0, -35, 36, 12, 10),
                  (-35, 36, 35, 36, 10, 10), (35, 36, 35, 0, 10, 12)],
        "bores": [(-105, 0, 9, 9), (105, 0, 10, 10), (-35, 36, 5, 5), (35, 36, 5, 5)],
        "levels": [-25, 25, -25, 22], "shelf": 12, "web": 8.5, "round": 3,
    },
    "cranked_dogleg_yoke": {
        "bosses": [(-124, 0, 22, 25, 14), (-62, 0, 20, 22, 13),
                   (0, 0, 24, 24, 13), (62, 0, 22, 23, 12), (124, 0, 24, 23, 13),
                   (-45, -41, 16, 18, 10), (41, -36, 18, 16, 11)],
        "links": [(-124, 0, -62, 0, 15, 11), (-62, 0, 0, 0, 11, 14),
                  (0, 0, 62, 0, 13, 12), (62, 0, 124, 0, 12, 16),
                  (-62, 0, -45, -41, 12, 8), (-45, -41, 41, -36, 9, 10),
                  (41, -36, 62, 0, 10, 13)],
        "bores": [(-124, 0, 9, 9), (-62, 0, 6, 6), (0, 0, 8, 7), (124, 0, 9, 9),
                  (-45, -41, 5, 6), (41, -36, 7, 5)],
        "levels": [-28, 26, -23, 30, -18], "shelf": 11, "web": 8.5, "round": 3.2,
    },
    "cranked_twin_window_carrier": {
        "bosses": [(-100, 0, 20, 22, 12), (-35, 0, 24, 23, 15),
                   (35, 0, 23, 26, 13), (100, 0, 24, 25, 14),
                   (-78, 39, 17, 18, 10), (45, 45, 20, 17, 10),
                   (-35, -42, 16, 19, 11), (89, -40, 18, 19, 11)],
        "links": [(-100, 0, -35, 0, 13, 14), (-35, 0, 35, 0, 13, 14),
                  (35, 0, 100, 0, 14, 15), (-100, 0, -78, 39, 12, 10),
                  (-78, 39, 45, 45, 10, 11), (45, 45, 35, 0, 11, 14),
                  (-35, 0, -35, -42, 13, 9), (-35, -42, 89, -40, 9, 10),
                  (89, -40, 100, 0, 10, 14)],
        "bores": [(-100, 0, 7, 7), (-35, 0, 8, 7), (35, 0, 8, 9), (100, 0, 8, 8),
                  (-78, 39, 5, 5), (45, 45, 6, 5), (89, -40, 6, 7)],
        "levels": [-21, 32, -24, 26], "shelf": 12, "web": 8.5, "round": 3,
    },
}


def parting_profile(model):
    """The suggested Broken line points as (x, z) pairs in the XZ view."""
    pad = model["shelf"]
    return [(boss[0] + dx, z)
            for boss, z in zip(model["bosses"], model["levels"])
            for dx in (-pad, pad)]


def smoothstep(u):
    u = np.clip(u, 0.0, 1.0)
    return u * u * (3.0 - 2.0 * u)


def smooth_min(a, b, blend):
    h = np.maximum(blend - np.abs(a - b), 0.0) / blend
    return np.minimum(a, b) - 0.25 * blend * h * h


def ellipse(x, y, cx, cy, rx, ry):
    return (np.hypot((x - cx) / rx, (y - cy) / ry) - 1.0) * min(rx, ry)


def planar_geometry(x, y, model):
    profile = np.zeros_like(x) + model["levels"][0]
    centers = [b[0] for b in model["bosses"][:len(model["levels"])]]
    pad = model["shelf"]
    for a, b, za, zb in zip(centers, centers[1:], model["levels"], model["levels"][1:]):
        profile += (zb - za) * smoothstep((x - a - pad) / (b - a - 2 * pad))

    half = np.full((len(x), y.shape[1]), model["web"], dtype=float)
    outline = np.full_like(half, np.inf)
    for cx, cy, rx, ry, height in model["bosses"]:
        radius = np.hypot((x - cx) / rx, (y - cy) / ry)
        outline = smooth_min(outline, (radius - 1.0) * min(rx, ry), 5.0)
        half = np.maximum(half, model["web"] + (height - model["web"])
                          * (1.0 - smoothstep((radius - 0.68) / 0.36)))
    for ax, ay, bx, by, ra, rb in model["links"]:
        dx, dy = bx - ax, by - ay
        q = np.clip(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.0, 1.0)
        link = np.hypot(x - (ax + q * dx), y - (ay + q * dy)) - (ra + (rb - ra) * q)
        outline = smooth_min(outline, link, 4.5)
    for bore in model["bores"]:
        outline = np.maximum(outline, -ellipse(x, y, *bore))
    return outline, profile, half


def extract(model):
    b = np.asarray(model["bosses"])
    lo = (min(b[:, 0] - b[:, 2]) - 5, min(b[:, 1] - b[:, 3]) - 5,
          min(model["levels"]) - max(b[:, 4]) - 5)
    hi = (max(b[:, 0] + b[:, 2]) + 5, max(b[:, 1] + b[:, 3]) + 5,
          max(model["levels"]) + max(b[:, 4]) + 5)
    spacing = (hi[0] - lo[0]) / 255
    xs, ys, zs = [np.arange(a + 0.173 * spacing, c + spacing, spacing) for a, c in zip(lo, hi)]
    outline, mid, half = planar_geometry(xs[:, None], ys[None, :], model)

    # The rounded intersection of the extruded plan and the variable slab.
    # Each draw ray through material meets one interval, even at a rising web.
    radius = model["round"]
    pts, tris = [], []
    for i in range(0, len(xs) - 1, 32):
        sl = slice(i, i + 33)
        u = outline[sl, :, None] + radius
        v = np.abs(zs[None, None, :] - mid[sl, :, None]) - half[sl, :, None] + radius
        f = np.minimum(np.maximum(u, v), 0.0) + np.hypot(np.maximum(u, 0.0), np.maximum(v, 0.0)) - radius
        f[np.abs(f) < 1e-6] = 1e-6
        if f.min() >= 0:
            continue
        p, t = marching_tets(xs[sl], ys, zs, f)
        tris.append(t + sum(len(chunk) for chunk in pts))
        pts.append(p)

    # Cube slabs agree on their shared faces; merge only coincident crossings.
    p, inv = np.unique(np.round(np.concatenate(pts), 10), axis=0, return_inverse=True)
    return p, inv[np.concatenate(tris)]


class MeshView(C.Structure):
    _fields_ = [
        ("coords", C.POINTER(C.c_double)), ("T", C.POINTER(C.c_double)),
        ("normals", C.POINTER(C.c_double)), ("edges", C.POINTER(C.c_int)),
        ("tris", C.POINTER(C.c_int)), ("tets", C.POINTER(C.c_int)),
        ("num_nodes", C.c_int), ("num_edges", C.c_int),
        ("num_tris", C.c_int), ("num_tets", C.c_int),
    ]


def load_mesher(path):
    sem = C.CDLL(str(path.resolve()))
    sem.SEM_GetLastError.restype = C.c_char_p
    sem.SEM_RemeshAdaptiveStandaloneSurface3D.argtypes = [
        C.c_void_p, C.c_int, C.c_double, C.c_double, C.c_double, C.c_double,
        C.c_double, C.c_int, C.POINTER(C.c_double), C.c_int, C.POINTER(C.c_int),
        C.c_int, C.POINTER(MeshView),
    ]
    return sem


def edge_lengths(p, t):
    e = np.concatenate((t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]))
    u = np.unique(np.sort(e, axis=1), axis=0)
    return np.linalg.norm(p[u[:, 0]] - p[u[:, 1]], axis=1)


def adaptive(sem, p, t, edge_fraction):
    p = np.ascontiguousarray(p, dtype=np.float64)
    t = np.ascontiguousarray(t, dtype=np.int32)
    scale = np.ptp(p, axis=0).max() / edge_lengths(p, t).mean()
    view = MeshView()
    rc = sem.SEM_RemeshAdaptiveStandaloneSurface3D(
        None, 0, 0.0, 1.55 * scale * edge_fraction, 0.75 * scale * edge_fraction,
        0.055 * scale * edge_fraction, 52.0, 8,
        p.ctypes.data_as(C.POINTER(C.c_double)), len(p),
        t.ctypes.data_as(C.POINTER(C.c_int)), len(t), C.byref(view),
    )
    if rc:
        raise RuntimeError(sem.SEM_GetLastError().decode())
    return (np.ctypeslib.as_array(view.coords, (3 * view.num_nodes,)).copy().reshape(-1, 3),
            np.ctypeslib.as_array(view.tris, (3 * view.num_tris,)).copy().reshape(-1, 3))


def verify(p, t):
    assert check_closed(t) == 0
    directed = np.concatenate((t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]))
    assert len(np.unique(directed, axis=0)) == len(directed)
    assert volume(p, t) > 0
    assert np.all(np.linalg.norm(np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]]), axis=1) > 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--edge-fraction", type=float, default=0.00616)
    parser.add_argument("--model", choices=MODELS)
    args = parser.parse_args()
    if args.edge_fraction <= 0:
        parser.error("--edge-fraction must be positive")
    sem = load_mesher(args.dll)
    for name, shape in MODELS.items():
        if args.model is not None and name != args.model:
            continue
        path = args.output / (name + "_forging_closed.csv3d")
        if path.exists():
            raise FileExistsError(path)
        p, t = extract(shape)
        print(f"{name}: extracted {len(t)} triangles", flush=True)
        verify(p, t)
        p, t = adaptive(sem, p, t, args.edge_fraction)
        p = np.round(p, 7)      # verify the coordinates that are written
        verify(p, t)
        l = edge_lengths(p, t)
        write_csv3d(path, p, t)
        print(f"{path.name}: {len(t)} triangles, mean edge {l.mean():.3f} mm "
              f"({l.mean()/np.ptp(p,axis=0).max():.2%}), "
              f"P10/P90 {np.quantile(l,0.1):.3f}/{np.quantile(l,0.9):.3f} mm", flush=True)


if __name__ == "__main__":
    main()
