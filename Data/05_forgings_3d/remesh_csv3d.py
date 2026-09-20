"""
Isotropic remesher for CSV3D surface meshes (.csv3d).

Rebuilds the triangulation of an existing CSV3D surface so that the triangles
are close to equilateral and roughly uniform in size, while the shape itself is
preserved: every vertex is projected back onto the original surface, and sharp
creases / open boundaries are kept as explicit constraints.

Algorithm: incremental isotropic remeshing (Botsch & Kobbelt, "A Remeshing
Approach to Multiresolution Modeling", SGP 2004). One iteration is

    1. split    every edge longer than 4/3 * L
    2. collapse every edge shorter than 4/5 * L
    3. flip     edges that bring vertex valences closer to 6 (4 on a boundary)
    4. tangential relaxation, then projection back onto the input surface

Constraints kept through all four steps:
    * feature edges - dihedral angle above --feature degrees, plus every open
                      boundary edge. They stay a connected polyline; vertices
                      on them slide only along the original crease.
    * corners       - feature vertices with a number of feature edges other
                      than two, or a turn sharper than --corner degrees, are
                      pinned in place.
    * orientation   - no operation may fold or flip a triangle, so a closed
                      input stays closed and consistently oriented.

Usage:
    python remesh_csv3d.py in.csv3d out.csv3d [options]

    --target L     target edge length, in the file's own units (the meshes in
                   this folder are in metres). Default: 1.5% of the bounding
                   box diagonal.
    --tris N       choose the target edge length so the result has about N
                   triangles (overrides --target).
    --iters N      remeshing iterations, default 10.
    --feature DEG  dihedral angle above which an edge is a feature, default 25.
    --corner DEG   turn along a feature line below which a vertex is pinned,
                   default 120.
    --min-crease R drop crease chains shorter than R target edge lengths, so a
                   crease the new mesh cannot resolve does not pin vertices and
                   force slivers around them. Default 0.5, 0 keeps them all.
    --weld-corners R
                   merge crease corners closer than R target edge lengths,
                   default 0.25, 0 keeps every corner.
    --smooth N     relaxation sub-steps per iteration, default 3.
    --no-project   skip the projection step (faster, but the shape drifts).
    --quiet        only print the final summary.

Reads and writes the CSV3D sections the viewer's loader understands: #nodes
(the optional per-node temperature column is carried through by interpolation),
#triangles, and #edge (rebuilt from the remeshed boundary if the input had one).

Requires numpy and scipy.
"""
import argparse
import math
import os
import sys
from collections import defaultdict

import numpy as np
from scipy.spatial import cKDTree


# ---------------------------------------------------------------------------
# CSV3D io
# ---------------------------------------------------------------------------

def _is_num(s):
    s = s.strip()
    return bool(s) and (s[0] in "+-." or s[0].isdigit())


def read_csv3d(path):
    """Return (positions Nx3, temperatures N or None, triangles Mx3, edges Kx2)."""
    section = None
    pos, temp, tris, edges = [], [], [], []
    with open(path, "r") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            if s[0] == "#":
                section = s[1:].split(";")[0].split()[0].strip().lower()
                continue
            tok = s.split(";")
            if section == "nodes":
                if len(tok) < 4 or not _is_num(tok[1]):
                    continue
                pos.append([float(tok[1]), float(tok[2]), float(tok[3])])
                temp.append(float(tok[4]) if len(tok) > 4 and _is_num(tok[4]) else None)
            elif section == "triangles":
                if len(tok) < 3 or not _is_num(tok[0]):
                    continue
                tris.append([int(tok[0]), int(tok[1]), int(tok[2])])
            elif section == "edge":
                if len(tok) < 2 or not _is_num(tok[0]):
                    continue
                edges.append([int(tok[0]), int(tok[1])])
    P = np.asarray(pos, dtype=np.float64)
    T = np.asarray(tris, dtype=np.int64)
    E = np.asarray(edges, dtype=np.int64).reshape(-1, 2)
    Tv = None if any(t is None for t in temp) else np.asarray(temp, dtype=np.float64)
    return P, Tv, T, E


def write_csv3d(path, P, Tv, T, edges=None):
    out = ["#nodes", "node_id;x;y;z;T" if Tv is not None else "node_id;x;y;z"]
    for i, p in enumerate(P):
        if Tv is not None:
            out.append("%d;%.7f;%.7f;%.7f;%.7f" % (i, p[0], p[1], p[2], Tv[i]))
        else:
            out.append("%d;%.7f;%.7f;%.7f" % (i, p[0], p[1], p[2]))
    out.append("#triangles")
    for t in T:
        out.append("%d;%d;%d" % (t[0], t[1], t[2]))
    if edges:
        out.append("#edge;%d" % len(edges))
        for a, b in edges:
            out.append("%d;%d" % (a, b))
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")


# ---------------------------------------------------------------------------
# geometry helpers
# ---------------------------------------------------------------------------

def tri_normals(P, T):
    """Unit normals and areas of every triangle."""
    n = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    l = np.linalg.norm(n, axis=1)
    return n / np.maximum(l, 1e-300)[:, None], 0.5 * l


def closest_on_triangles(q, A, B, C):
    """Closest point to q[i] on triangle (A[i], B[i], C[i]); every array is Nx3."""
    ab, ac = B - A, C - A
    ap, bp, cp = q - A, q - B, q - C
    d1 = np.einsum("ij,ij->i", ab, ap)
    d2 = np.einsum("ij,ij->i", ac, ap)
    d3 = np.einsum("ij,ij->i", ab, bp)
    d4 = np.einsum("ij,ij->i", ac, bp)
    d5 = np.einsum("ij,ij->i", ab, cp)
    d6 = np.einsum("ij,ij->i", ac, cp)
    va = d3 * d6 - d5 * d4
    vb = d5 * d2 - d1 * d6
    vc = d1 * d4 - d3 * d2
    denom = va + vb + vc
    safe = np.where(np.abs(denom) < 1e-300, 1e-300, denom)
    res = A + ab * (vb / safe)[:, None] + ac * (vc / safe)[:, None]

    def lerp(Pa, Pb, num, den):
        t = np.clip(num / np.where(np.abs(den) < 1e-300, 1e-300, den), 0.0, 1.0)
        return Pa + (Pb - Pa) * t[:, None]

    m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
    if m.any():
        res[m] = lerp(B, C, d4 - d3, (d4 - d3) + (d5 - d6))[m]
    m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
    if m.any():
        res[m] = lerp(A, C, d2, d2 - d6)[m]
    m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
    if m.any():
        res[m] = lerp(A, B, d1, d1 - d3)[m]
    m = (d1 <= 0) & (d2 <= 0)
    res[m] = A[m]
    m = (d3 >= 0) & (d4 <= d3)
    res[m] = B[m]
    m = (d6 >= 0) & (d5 <= d6)
    res[m] = C[m]
    return res


def split_soup(A, B, C, max_edge, max_rounds=10):
    """1:4 subdivide triangles until every edge is at most max_edge.

    Midpoint subdivision of a planar triangle covers exactly the same surface,
    so this changes nothing geometrically - it only makes the nearest-triangle
    lookup below reliable when the input has a few very large triangles.
    """
    for _ in range(max_rounds):
        big = np.maximum.reduce([np.linalg.norm(B - A, axis=1),
                                 np.linalg.norm(C - B, axis=1),
                                 np.linalg.norm(A - C, axis=1)]) > max_edge
        if not big.any():
            break
        a, b, c = A[big], B[big], C[big]
        ab, bc, ca = 0.5 * (a + b), 0.5 * (b + c), 0.5 * (c + a)
        A = np.concatenate([A[~big], a, ab, ca, ab])
        B = np.concatenate([B[~big], ab, b, bc, bc])
        C = np.concatenate([C[~big], ca, bc, c, ca])
    return A, B, C


class SurfaceProjector:
    """Nearest-point lookup on a fixed triangle soup."""

    def __init__(self, P, T, max_edge=None, k=16):
        A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
        if max_edge:
            A, B, C = split_soup(A, B, C, max_edge)
        self.A, self.B, self.C = A, B, C
        pts = np.concatenate([(A + B + C) / 3.0, A, B, C,
                              0.5 * (A + B), 0.5 * (B + C), 0.5 * (C + A)])
        self.owner = np.tile(np.arange(len(A)), 7)
        self.tree = cKDTree(pts)
        self.k = min(k, len(pts))

    def project(self, Q):
        """Closest surface point for every row of Q, plus the distance to it."""
        _, idx = self.tree.query(Q, k=self.k, workers=-1)
        cand = self.owner[np.atleast_2d(idx)]
        n, k = cand.shape
        q = np.repeat(Q, k, axis=0)
        f = cand.ravel()
        cp = closest_on_triangles(q, self.A[f], self.B[f], self.C[f])
        d2 = np.einsum("ij,ij->i", cp - q, cp - q).reshape(n, k)
        best = np.argmin(d2, axis=1)
        rows = np.arange(n)
        return cp.reshape(n, k, 3)[rows, best], np.sqrt(d2[rows, best])


class PolylineProjector:
    """Nearest-point lookup on a fixed set of segments (creases, boundaries)."""

    def __init__(self, P, segs, max_edge=None, k=8):
        A, B = P[segs[:, 0]], P[segs[:, 1]]
        for _ in range(10):
            if not max_edge:
                break
            long = np.linalg.norm(B - A, axis=1) > max_edge
            if not long.any():
                break
            m = 0.5 * (A[long] + B[long])
            A = np.concatenate([A[~long], A[long], m])
            B = np.concatenate([B[~long], m, B[long]])
        self.A, self.B = A, B
        pts = np.concatenate([A, B, 0.5 * (A + B)])
        self.owner = np.tile(np.arange(len(A)), 3)
        self.tree = cKDTree(pts)
        self.k = min(k, len(pts))

    def project(self, Q):
        _, idx = self.tree.query(Q, k=self.k, workers=-1)
        cand = self.owner[np.atleast_2d(idx)]
        n, k = cand.shape
        q = np.repeat(Q, k, axis=0)
        a, ab = self.A[cand.ravel()], self.B[cand.ravel()] - self.A[cand.ravel()]
        t = np.clip(np.einsum("ij,ij->i", q - a, ab) /
                    np.maximum(np.einsum("ij,ij->i", ab, ab), 1e-300), 0.0, 1.0)
        cp = a + ab * t[:, None]
        d2 = np.einsum("ij,ij->i", cp - q, cp - q).reshape(n, k)
        best = np.argmin(d2, axis=1)
        return cp.reshape(n, k, 3)[np.arange(n), best]


# ---------------------------------------------------------------------------
# mesh under construction
# ---------------------------------------------------------------------------

FREE, LINE, CORNER = 0, 1, 2

MIN_AREA = 1e-18
FOLD_DOT = 0.2          # a triangle may not swing more than ~78 deg in one step
FLIP_DOT = 0.85         # a flip may not swing a triangle more than ~32 deg


def key(u, v):
    return (u, v) if u < v else (v, u)


class Mesh:
    """Triangle mesh with dynamic vertex/face arrays and vertex-to-face links."""

    def __init__(self, P, Tv, T):
        self.V = [np.array(p, dtype=np.float64) for p in P]
        self.Tv = list(Tv) if Tv is not None else None
        self.F = [list(map(int, t)) for t in T]
        self.tag = [FREE] * len(self.V)
        self.vf = [set() for _ in self.V]
        for f, tri in enumerate(self.F):
            for x in tri:
                self.vf[x].add(f)
        self.feat = set()                               # creases + boundary
        self.boundary = set()                           # edges with one face
        self.chain = set()                              # the input's #edge chain

    # -- topology queries ---------------------------------------------------

    def ring(self, v):
        """Vertices sharing a face with v."""
        out = set()
        for f in self.vf[v]:
            out.update(self.F[f])
        out.discard(v)
        return out

    def edge_faces(self, u, v):
        return self.vf[u] & self.vf[v]

    def edges(self):
        out = set()
        for tri in self.F:
            if tri is not None:
                a, b, c = tri
                out.add(key(a, b))
                out.add(key(b, c))
                out.add(key(c, a))
        return out

    def opposite(self, f, u, v):
        for x in self.F[f]:
            if x != u and x != v:
                return x
        return None

    def normal(self, tri):
        a, b, c = (self.V[i] for i in tri)
        return np.cross(b - a, c - a)

    # -- low level edits ----------------------------------------------------

    def set_face(self, f, tri):
        old = self.F[f]
        if old is not None:
            for x in old:
                self.vf[x].discard(f)
        self.F[f] = tri
        if tri is not None:
            for x in tri:
                self.vf[x].add(f)

    def add_face(self, tri):
        self.F.append(None)
        self.set_face(len(self.F) - 1, tri)
        return len(self.F) - 1

    def add_vertex(self, p, t, tag):
        self.V.append(p)
        self.vf.append(set())
        self.tag.append(tag)
        if self.Tv is not None:
            self.Tv.append(t)
        return len(self.V) - 1

    # -- operators ----------------------------------------------------------

    def split_edge(self, u, v):
        faces = list(self.edge_faces(u, v))
        if not 1 <= len(faces) <= 2:
            return False
        is_feat = key(u, v) in self.feat
        t = None
        if self.Tv is not None:
            t = 0.5 * (self.Tv[u] + self.Tv[v])
        m = self.add_vertex(0.5 * (self.V[u] + self.V[v]), t, LINE if is_feat else FREE)
        for f in faces:
            tri = self.F[f]
            first = [m if x == v else x for x in tri]
            second = [m if x == u else x for x in tri]
            self.set_face(f, first)
            self.add_face(second)
        for tracked in (self.feat, self.boundary, self.chain):
            if key(u, v) in tracked:
                tracked.discard(key(u, v))
                tracked.add(key(u, m))
                tracked.add(key(m, v))
        return True

    def collapse_ok(self, u, v, p, l_high):
        """Can v be merged into u, with u placed at p, without breaking the mesh?"""
        shared = self.edge_faces(u, v)
        if len(shared) != (1 if key(u, v) in self.boundary else 2):
            return False
        opp = {self.opposite(f, u, v) for f in shared}
        if self.ring(u) & self.ring(v) != opp:          # link condition
            return False
        moved = (self.vf[u] | self.vf[v]) - shared
        keep = [self.F[f] for f in moved]
        for tri in keep:
            new = [u if x == v else x for x in tri]
            if len(set(new)) != 3:
                return False
            old_n = self.normal(tri)
            a, b, c = (p if x == u else self.V[x] for x in new)
            new_n = np.cross(b - a, c - a)
            area = 0.5 * np.linalg.norm(new_n)
            if area < MIN_AREA:
                return False
            ln = np.linalg.norm(old_n)
            if ln > 1e-300 and float(old_n @ new_n) / (ln * 2.0 * area) < FOLD_DOT:
                return False
            for x, y in ((a, b), (b, c), (c, a)):
                if np.linalg.norm(x - y) > l_high:
                    return False
        return True

    def collapse_edge(self, u, v, p):
        """Merge v into u, moving u to p. Caller must have checked collapse_ok."""
        shared = list(self.edge_faces(u, v))
        ring_v = self.ring(v)
        self.V[u] = p
        if self.Tv is not None:
            self.Tv[u] = 0.5 * (self.Tv[u] + self.Tv[v]) if self.tag[u] == FREE else self.Tv[u]
        for f in shared:
            self.set_face(f, None)
        for f in list(self.vf[v]):
            self.set_face(f, [u if x == v else x for x in self.F[f]])
        for w in ring_v:
            k = key(v, w)
            for tracked in (self.feat, self.boundary, self.chain):
                if k in tracked:
                    tracked.discard(k)
                    if w != u:
                        tracked.add(key(u, w))
        for tracked in (self.feat, self.boundary, self.chain):
            tracked.discard(key(u, v))
        self.vf[v].clear()
        self.tag[v] = FREE

    def flip_edge(self, u, v):
        faces = list(self.edge_faces(u, v))
        if len(faces) != 2:
            return False
        f1, f2 = faces
        a, b = self.opposite(f1, u, v), self.opposite(f2, u, v)
        if a is None or b is None or a == b or self.edge_faces(a, b):
            return False
        tri1 = self.F[f1]
        if tri1[(tri1.index(u) + 1) % 3] != v:          # make f1 the one with u -> v
            f1, f2, tri1 = f2, f1, self.F[f2]
            a, b = b, a
        new1, new2 = [a, u, b], [b, v, a]
        n1o, n2o = self.normal(self.F[f1]), self.normal(self.F[f2])
        for tri, old in ((new1, n1o), (new2, n2o)):
            p, q, r = (self.V[i] for i in tri)
            n = np.cross(q - p, r - p)
            area = 0.5 * np.linalg.norm(n)
            if area < MIN_AREA:
                return False
            lo = np.linalg.norm(old)
            if lo > 1e-300 and float(old @ n) / (lo * 2.0 * area) < FLIP_DOT:
                return False
        self.set_face(f1, new1)
        self.set_face(f2, new2)
        return True

    # -- extraction ---------------------------------------------------------

    def compact(self):
        used = sorted({x for tri in self.F if tri is not None for x in tri})
        remap = {v: i for i, v in enumerate(used)}
        P = np.array([self.V[v] for v in used])
        Tv = np.array([self.Tv[v] for v in used]) if self.Tv is not None else None
        T = np.array([[remap[x] for x in tri] for tri in self.F if tri is not None],
                     dtype=np.int64)
        self.remap = remap
        return P, Tv, T


# ---------------------------------------------------------------------------
# constraints
# ---------------------------------------------------------------------------

def detect_features(mesh, feature_deg, corner_deg, min_chain=0.0):
    """Mark feature/boundary edges and tag every vertex FREE, LINE or CORNER.

    Crease chains shorter than min_chain are dropped: a feature below the
    sampling resolution cannot be represented by the new triangulation anyway,
    it only pins vertices and forces slivers around them. Boundaries are never
    dropped.
    """
    e2f = defaultdict(list)
    for f, tri in enumerate(mesh.F):
        if tri is None:
            continue
        a, b, c = tri
        for u, v in ((a, b), (b, c), (c, a)):
            e2f[key(u, v)].append(f)
    cos_lim = math.cos(math.radians(feature_deg))
    mesh.feat, mesh.boundary = set(), set()
    for e, fs in e2f.items():
        if len(fs) == 1:
            mesh.boundary.add(e)
            mesh.feat.add(e)
        elif len(fs) == 2:
            n1 = mesh.normal(mesh.F[fs[0]])
            n2 = mesh.normal(mesh.F[fs[1]])
            l1, l2 = np.linalg.norm(n1), np.linalg.norm(n2)
            if l1 > 1e-300 and l2 > 1e-300 and float(n1 @ n2) / (l1 * l2) < cos_lim:
                mesh.feat.add(e)
        else:
            mesh.feat.add(e)                            # non-manifold: freeze it

    creases = [e for e in mesh.feat if e not in mesh.boundary]
    if min_chain > 0.0 and creases:
        root = {}

        def find(x):
            root.setdefault(x, x)
            while root[x] != x:
                root[x] = root[root[x]]
                x = root[x]
            return x

        for u, v in creases:
            a, b = find(u), find(v)
            if a != b:
                root[a] = b
        length = defaultdict(float)
        for u, v in creases:
            length[find(u)] += float(np.linalg.norm(mesh.V[u] - mesh.V[v]))
        for e in creases:
            if length[find(e[0])] < min_chain:
                mesh.feat.discard(e)

    inc = defaultdict(list)
    for u, v in mesh.feat:
        inc[u].append(v)
        inc[v].append(u)
    corner_cos = math.cos(math.radians(corner_deg))
    mesh.tag = [FREE] * len(mesh.V)
    for v, nb in inc.items():
        if len(nb) != 2:
            mesh.tag[v] = CORNER
            continue
        d1 = mesh.V[nb[0]] - mesh.V[v]
        d2 = mesh.V[nb[1]] - mesh.V[v]
        l1, l2 = np.linalg.norm(d1), np.linalg.norm(d2)
        cs = float(d1 @ d2) / (l1 * l2) if l1 > 1e-300 and l2 > 1e-300 else 0.0
        mesh.tag[v] = CORNER if cs > corner_cos else LINE
    return len(mesh.feat), len(mesh.boundary), sum(1 for t in mesh.tag if t == CORNER)


# ---------------------------------------------------------------------------
# remeshing steps
# ---------------------------------------------------------------------------

def split_long(mesh, l_high, max_passes=12):
    total = 0
    for _ in range(max_passes):
        todo = [(u, v) for u, v in mesh.edges()
                if np.linalg.norm(mesh.V[u] - mesh.V[v]) > l_high]
        if not todo:
            break
        for u, v in todo:
            if mesh.edge_faces(u, v) and np.linalg.norm(mesh.V[u] - mesh.V[v]) > l_high:
                total += mesh.split_edge(u, v)
    return total


def collapse_short(mesh, l_low, l_high, weld_corners=0.0, weld_degenerate=0.0):
    total = 0
    for u, v in sorted(mesh.edges(), key=lambda e: np.linalg.norm(mesh.V[e[0]] - mesh.V[e[1]])):
        if not mesh.edge_faces(u, v):
            continue
        length = float(np.linalg.norm(mesh.V[u] - mesh.V[v]))
        if length >= l_low:
            continue
        tu, tv = mesh.tag[u], mesh.tag[v]
        is_feat = key(u, v) in mesh.feat
        # An edge this short carries no geometry left to protect: welding it is
        # what keeps two constrained vertices from landing on the same point and
        # leaving a zero-area triangle behind.
        degenerate = length <= weld_degenerate
        if tu == CORNER and tv == CORNER:
            # two corners closer than the mesh can resolve: merge them into one
            # along their own crease, which shortens the chain but keeps it.
            if not degenerate and not (is_feat and key(u, v) not in mesh.boundary
                                       and length < weld_corners):
                continue
        elif tu != FREE and tv != FREE and not is_feat and not degenerate:
            continue                                    # would weld two creases
        if tv > tu:
            u, v, tu, tv = v, u, tv, tu                 # keep the constrained end
        if tu == FREE or (tu == LINE and tv == LINE and is_feat):
            p = 0.5 * (mesh.V[u] + mesh.V[v])
        else:
            p = mesh.V[u]
        if mesh.collapse_ok(u, v, p, l_high):
            mesh.collapse_edge(u, v, p)
            mesh.tag[u] = tu
            total += 1
    return total


def valence_flips(mesh):
    val = defaultdict(int)
    for u, v in mesh.edges():
        val[u] += 1
        val[v] += 1
    on_bnd = set()
    for u, v in mesh.boundary:
        on_bnd.add(u)
        on_bnd.add(v)

    def target(x):
        return 4 if x in on_bnd else 6

    total = 0
    for u, v in mesh.edges():
        if key(u, v) in mesh.feat:
            continue
        faces = list(mesh.edge_faces(u, v))
        if len(faces) != 2:
            continue
        a, b = mesh.opposite(faces[0], u, v), mesh.opposite(faces[1], u, v)
        if a is None or b is None:
            continue
        before = (abs(val[u] - target(u)) + abs(val[v] - target(v)) +
                  abs(val[a] - target(a)) + abs(val[b] - target(b)))
        after = (abs(val[u] - 1 - target(u)) + abs(val[v] - 1 - target(v)) +
                 abs(val[a] + 1 - target(a)) + abs(val[b] + 1 - target(b)))
        if after >= before:
            continue
        if mesh.flip_edge(u, v):
            val[u] -= 1
            val[v] -= 1
            val[a] += 1
            val[b] += 1
            total += 1
    return total


def _min_angle(p, q, r):
    best = 180.0
    for a, b, c in ((p, q, r), (q, r, p), (r, p, q)):
        u, v = b - a, c - a
        lu, lv = np.linalg.norm(u), np.linalg.norm(v)
        if lu < 1e-300 or lv < 1e-300:
            return 0.0
        best = min(best, math.degrees(math.acos(max(-1.0, min(1.0, float(u @ v) / (lu * lv))))))
    return best


def angle_flips(mesh):
    """Flip edges that raise the smallest angle of the two triangles they join."""
    total = 0
    for u, v in mesh.edges():
        if key(u, v) in mesh.feat:
            continue
        faces = list(mesh.edge_faces(u, v))
        if len(faces) != 2:
            continue
        a, b = mesh.opposite(faces[0], u, v), mesh.opposite(faces[1], u, v)
        if a is None or b is None or a == b or mesh.edge_faces(a, b):
            continue
        V = mesh.V
        before = min(_min_angle(V[u], V[v], V[a]), _min_angle(V[u], V[v], V[b]))
        after = min(_min_angle(V[a], V[b], V[u]), _min_angle(V[a], V[b], V[v]))
        if after > before + 1e-9 and mesh.flip_edge(u, v):
            total += 1
    return total


def relax(mesh, weight=1.0):
    """One tangential smoothing step; constrained vertices stay on their line."""
    n = len(mesh.V)
    P = np.array(mesh.V)
    acc = np.zeros((n, 3))
    cnt = np.zeros(n)
    normal = np.zeros((n, 3))
    for tri in mesh.F:
        if tri is None:
            continue
        a, b, c = tri
        fn = mesh.normal(tri)
        for x in (a, b, c):
            normal[x] += fn
        for x, y in ((a, b), (b, c), (c, a)):
            acc[x] += P[y]
            acc[y] += P[x]
            cnt[x] += 1
            cnt[y] += 1
    live = cnt > 0
    centroid = np.zeros((n, 3))
    centroid[live] = acc[live] / cnt[live][:, None]

    tags = np.array(mesh.tag)
    free = live & (tags == FREE)
    d = centroid[free] - P[free]
    nn = normal[free]
    nl = np.linalg.norm(nn, axis=1)
    nn = nn / np.maximum(nl, 1e-300)[:, None]
    d -= nn * np.einsum("ij,ij->i", d, nn)[:, None]     # tangential component only
    P[free] += weight * d

    line_nb = defaultdict(list)
    for u, v in mesh.feat:
        line_nb[u].append(v)
        line_nb[v].append(u)
    before = P.copy()                                   # simultaneous update:
    for v, nb in line_nb.items():                       # sequential 1D smoothing
        if mesh.tag[v] == LINE and len(nb) == 2:        # bunches crease vertices
            P[v] += 0.5 * weight * (0.5 * (before[nb[0]] + before[nb[1]]) - before[v])
    for i in range(n):
        mesh.V[i] = P[i]


def project(mesh, surf, crease):
    tags = np.array(mesh.tag)
    alive = np.array([bool(mesh.vf[i]) for i in range(len(mesh.V))])
    P = np.array(mesh.V)
    idx = np.where(alive & (tags == FREE))[0]
    if len(idx):
        P[idx], _ = surf.project(P[idx])
    if crease is not None:
        idx = np.where(alive & (tags == LINE))[0]
        if len(idx):
            P[idx] = crease.project(P[idx])
    for i in range(len(P)):
        mesh.V[i] = P[i]


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------

def quality(P, T):
    ang = []
    for i in range(3):
        u = P[T[:, (i + 1) % 3]] - P[T[:, i]]
        v = P[T[:, (i + 2) % 3]] - P[T[:, i]]
        cs = np.einsum("ij,ij->i", u, v) / np.maximum(
            np.linalg.norm(u, axis=1) * np.linalg.norm(v, axis=1), 1e-300)
        ang.append(np.degrees(np.arccos(np.clip(cs, -1.0, 1.0))))
    return np.array(ang).T


def sample_points(P, T):
    """Vertices, edge midpoints and centroid of every triangle."""
    A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    return np.concatenate([A, B, C, 0.5 * (A + B), 0.5 * (B + C), 0.5 * (C + A),
                           (A + B + C) / 3.0])


def deviation(P0, T0, P1, T1, max_edge):
    """Two-sided sampled distance between the input and the remeshed surface."""
    _, a = SurfaceProjector(P0, T0, max_edge=max_edge).project(sample_points(P1, T1))
    _, b = SurfaceProjector(P1, T1, max_edge=max_edge).project(sample_points(P0, T0))
    return max(a.max(), b.max()), 0.5 * (a.mean() + b.mean())


def enclosed_volume(P, T):
    return float(np.einsum("ij,ij->i", P[T[:, 0]], np.cross(P[T[:, 1]], P[T[:, 2]])).sum() / 6.0)


def topology(T):
    e2f = defaultdict(int)
    directed = defaultdict(int)
    for a, b, c in T:
        for u, v in ((a, b), (b, c), (c, a)):
            e2f[key(u, v)] += 1
            directed[(u, v)] += 1
    boundary = [e for e, n in e2f.items() if n == 1]
    nonman = sum(1 for n in e2f.values() if n > 2)
    oriented = all(n == 1 for n in directed.values())
    return boundary, nonman, oriented


def boundary_chain(boundary):
    """Order boundary edges into chains so the #edge section stays walkable."""
    adj = defaultdict(list)
    for u, v in boundary:
        adj[u].append(v)
        adj[v].append(u)
    seen, out = set(), []
    for start in sorted(adj):
        if start in seen:
            continue
        prev, cur = None, start
        while True:
            seen.add(cur)
            nxt = None
            for w in adj[cur]:
                if w != prev and (w not in seen or w == start):
                    nxt = w
                    break
            if nxt is None:
                break
            out.append((cur, nxt))
            prev, cur = cur, nxt
            if cur == start:
                break
    return out


def report(name, P, T, log):
    mn = quality(P, T).min(axis=1)
    e = set()
    for a, b, c in T:
        for u, v in ((a, b), (b, c), (c, a)):
            e.add(key(u, v))
    e = np.array(sorted(e))
    el = np.linalg.norm(P[e[:, 0]] - P[e[:, 1]], axis=1)
    log("%-9s nodes=%-7d tris=%-7d  min-angle: worst %5.1f  median %5.1f  "
        "<30deg %5.1f%%  edge len %.4g +- %.1f%%"
        % (name, len(P), len(T), mn.min(), np.median(mn), 100.0 * (mn < 30).mean(),
           el.mean(), 100.0 * el.std() / max(el.mean(), 1e-300)))


# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description="isotropic remesher for CSV3D surfaces")
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--target", type=float, default=None,
                    help="target edge length in file units (default: 1.5%% of bbox diagonal)")
    ap.add_argument("--tris", type=int, default=None,
                    help="pick the target edge length for about this many triangles")
    ap.add_argument("--iters", type=int, default=10)
    ap.add_argument("--feature", type=float, default=25.0)
    ap.add_argument("--corner", type=float, default=120.0)
    ap.add_argument("--min-crease", type=float, default=0.5,
                    help="drop crease chains shorter than this many target edge lengths")
    ap.add_argument("--weld-corners", type=float, default=0.25,
                    help="merge crease corners closer than this many target edge lengths")
    ap.add_argument("--smooth", type=int, default=3)
    ap.add_argument("--no-project", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    def log(*a):
        if not args.quiet:
            print(*a)

    P0, Tv0, T0, E0 = read_csv3d(args.input)
    if len(P0) == 0 or len(T0) == 0:
        sys.exit("no mesh in %s" % args.input)

    _, area = tri_normals(P0, T0)
    total_area = float(area.sum())
    diag = float(np.linalg.norm(P0.max(axis=0) - P0.min(axis=0)))
    if args.tris:
        L = math.sqrt(4.0 * total_area / (math.sqrt(3.0) * args.tris))
    else:
        L = args.target if args.target else 0.015 * diag
    l_high, l_low = 4.0 / 3.0 * L, 4.0 / 5.0 * L

    log("input  : %s" % os.path.basename(args.input))
    report("  before", P0, T0, log)
    log("target : edge %.6g (bbox diagonal %.6g, surface area %.6g)" % (L, diag, total_area))

    mesh = Mesh(P0, Tv0, T0)
    nfeat, nbnd, ncorner = detect_features(mesh, args.feature, args.corner,
                                           min_chain=args.min_crease * L)
    if len(E0):
        mesh.chain = {key(int(a), int(b)) for a, b in E0}
        mesh.feat |= mesh.chain                         # the chain must survive
        for u, v in mesh.chain:
            for x in (u, v):
                if mesh.tag[x] == FREE:
                    mesh.tag[x] = LINE
        nfeat = len(mesh.feat)
    log("features: %d crease/boundary edges (%d boundary), %d pinned corners"
        % (nfeat, nbnd, ncorner))

    surf = None if args.no_project else SurfaceProjector(P0, T0, max_edge=0.5 * L)
    crease = None
    if not args.no_project and mesh.feat:
        crease = PolylineProjector(P0, np.array(sorted(mesh.feat), dtype=np.int64),
                                   max_edge=0.5 * L)

    for it in range(args.iters):
        ns = split_long(mesh, l_high)
        nc = collapse_short(mesh, l_low, l_high, weld_corners=args.weld_corners * L,
                            weld_degenerate=1e-3 * L)
        nf = valence_flips(mesh)
        if it >= args.iters - 2:                        # finishing passes
            nf += angle_flips(mesh)
        for _ in range(args.smooth):
            relax(mesh)
            if surf is not None:
                project(mesh, surf, crease)
        P, _, T = mesh.compact()
        ang = quality(P, T).min(axis=1)
        log("  iter %2d: +%-6d splits  -%-6d collapses  %-6d flips  -> %6d tris, "
            "min-angle median %4.1f, <30deg %4.1f%%"
            % (it + 1, ns, nc, nf, len(T), np.median(ang), 100.0 * (ang < 30).mean()))

    collapse_short(mesh, 1e-3 * L, l_high, weld_degenerate=1e-3 * L)
    angle_flips(mesh)
    P, Tv, T = mesh.compact()
    bnd, nonman, oriented = topology(T)
    edges = None
    if mesh.chain:
        # #edge is the chain the pipeline extends from, and in an open mesh it
        # may cover only part of the boundary, so it is tracked through every
        # split and collapse instead of being re-derived from the boundary.
        edges = boundary_chain([key(mesh.remap[u], mesh.remap[v])
                                for u, v in mesh.chain
                                if u in mesh.remap and v in mesh.remap])
    write_csv3d(args.output, P, Tv, T, edges)

    report("  after", P, T, log)
    dmax, dmean = deviation(P0, T0, P, T, 0.5 * L)
    log("deviation from input surface (sampled both ways): mean %.4g  max %.4g  "
        "(%.3f%% of diagonal)" % (dmean, dmax, 100.0 * dmax / diag))
    _, area1 = tri_normals(P, T)
    log("area: %.6g -> %.6g (%+.3f%%), smallest triangle %.3g, degenerate %d"
        % (total_area, area1.sum(), 100.0 * (area1.sum() - total_area) / total_area,
           area1.min(), int((area1 <= 0.0).sum())))
    if not bnd:
        v0, v1 = enclosed_volume(P0, T0), enclosed_volume(P, T)
        log("enclosed volume: %.6g -> %.6g (%+.3f%%)" % (v0, v1, 100.0 * (v1 - v0) / v0))
    log("topology: boundary edges %d, non-manifold %d, consistently oriented %s"
        % (len(bnd), nonman, oriented))
    print("wrote %s  (nodes %d, triangles %d%s)"
          % (args.output, len(P), len(T), ", #edge %d" % len(edges) if edges else ""))


if __name__ == "__main__":
    main()
