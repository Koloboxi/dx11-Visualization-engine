"""
Generator for example 3D forging (поковка) meshes in CSV3D format.

Each mesh is a genuine 3D triangulated surface whose explicit boundary chain
(#edge) lies entirely in the XY plane (z = 0). Two forgings have a CLOSED
boundary chain (the rim is a loop), two have an OPEN boundary chain (the rim is
a polyline). The body of every forging rises into +z, so the parts are 3D while
the граничная цепь stays planar (plane XY).

CSV3D layout written per file:
    #nodes
    node_id;x;y;z;T
    <id;x;y;z;T ...>
    #triangles
    <i;j;k ...>
    #edge;<count>
    <i;j ...>

T is a scalar "temperature" in [0,1] used by the viewer's thermal shading; here
it is a simple gradient that fades from hot (1) at the planar rim to cooler
toward the apex, purely for illustration.
"""
import math
import os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def write_csv3d(path, nodes, triangles, edges, edge_closed):
    """nodes: list of (x,y,z,T); triangles: list of (i,j,k); edges: list of (i,j)."""
    lines = []
    lines.append("#nodes")
    lines.append("node_id;x;y;z")
    for i, (x, y, z, t) in enumerate(nodes):
        lines.append(f"{i};{x:.7f};{y:.7f};{z:.7f}")
    lines.append("#triangles")
    for (a, b, c) in triangles:
        lines.append(f"{a};{b};{c}")
    lines.append(f"#edge;{len(edges)}")
    for (a, b) in edges:
        lines.append(f"{a};{b}")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    kind = "closed" if edge_closed else "open"
    print(f"  {os.path.basename(path):42s} nodes={len(nodes):4d} tris={len(triangles):4d} edge={len(edges):3d} ({kind})")


def chain_edges(loop_ids, closed):
    """Build #edge pairs from an ordered list of node ids."""
    e = []
    n = len(loop_ids)
    last = n if closed else n - 1
    for k in range(last):
        e.append((loop_ids[k], loop_ids[(k + 1) % n]))
    return e


# ---------------------------------------------------------------------------
# 1) CLOSED boundary: hemispherical dome forging (купол).
#    Rim = full circle on z=0 (closed loop). Surface rises to a single apex.
# ---------------------------------------------------------------------------
def dome_forging(R=80.0, H=60.0, segs=48, rings=10):
    nodes = []
    tris = []
    # latitude rings, theta=0 at equator (rim) -> pi/2 at apex
    ring_ids = []  # ring_ids[i] = list of node ids around ring i
    for i in range(rings):
        theta = (math.pi / 2.0) * (i / rings)
        r = R * math.cos(theta)
        z = H * math.sin(theta)
        t = 0.0
        ids = []
        for j in range(segs):
            a = 2.0 * math.pi * j / segs
            nodes.append((r * math.cos(a), r * math.sin(a), z, t))
            ids.append(len(nodes) - 1)
        ring_ids.append(ids)
    apex = len(nodes)
    nodes.append((0.0, 0.0, H, 0.25))
    # quad bands between rings
    for i in range(rings - 1):
        cur, nxt = ring_ids[i], ring_ids[i + 1]
        for j in range(segs):
            j2 = (j + 1) % segs
            tris.append((cur[j], cur[j2], nxt[j2]))
            tris.append((cur[j], nxt[j2], nxt[j]))
    # cap fan to apex
    top = ring_ids[-1]
    for j in range(segs):
        j2 = (j + 1) % segs
        tris.append((top[j], top[j2], apex))
    edges = chain_edges(ring_ids[0], closed=True)
    return nodes, tris, edges, True


# ---------------------------------------------------------------------------
# 2) CLOSED boundary: rectangular boss / frustum forging (бобышка).
#    Rim = base rectangle on z=0 (closed loop). Tapered walls + flat top.
# ---------------------------------------------------------------------------
def rect_boss_forging(bx=120.0, by=80.0, tx=70.0, ty=40.0, H=55.0, nx=8, ny=6):
    nodes = []
    tris = []

    def grid(w, h, z, t, ox=0.0, oy=0.0):
        ids = [[0] * (nx + 1) for _ in range(ny + 1)]
        for iy in range(ny + 1):
            for ix in range(nx + 1):
                x = -w / 2 + w * ix / nx + ox
                y = -h / 2 + h * iy / ny + oy
                nodes.append((x, y, z, t))
                ids[iy][ix] = len(nodes) - 1
        return ids

    def perimeter(ids):
        loop = []
        loop += [ids[0][ix] for ix in range(nx + 1)]          # bottom edge
        loop += [ids[iy][nx] for iy in range(1, ny + 1)]      # right edge
        loop += [ids[ny][ix] for ix in range(nx - 1, -1, -1)] # top edge
        loop += [ids[iy][0] for iy in range(ny - 1, 0, -1)]   # left edge
        return loop

    base = grid(bx, by, 0.0, 1.0)        # planar rim (hot)
    top = grid(tx, ty, H, 0.3)           # raised top (cool)

    # flat top cap
    for iy in range(ny):
        for ix in range(nx):
            a = top[iy][ix]; b = top[iy][ix + 1]
            c = top[iy + 1][ix + 1]; d = top[iy + 1][ix]
            tris.append((a, b, c))
            tris.append((a, c, d))

    # tapered side walls: stitch base perimeter to top perimeter
    bp = perimeter(base)
    tp = perimeter(top)
    m = len(bp)
    for k in range(m):
        k2 = (k + 1) % m
        tris.append((bp[k], bp[k2], tp[k2]))
        tris.append((bp[k], tp[k2], tp[k]))

    edges = chain_edges(bp, closed=True)
    return nodes, tris, edges, True


# ---------------------------------------------------------------------------
# 3) OPEN boundary: corrugated forged blade / ramp (гофрированная лопасть).
#    Boundary = straight base polyline on z=0 (open). Sheet ramps up in +z
#    while waving, so the body is fully 3D.
# ---------------------------------------------------------------------------
def corrugated_blade_forging(L=180.0, W=70.0, H=45.0, A=14.0, waves=3.0, nx=40, ny=10):
    nodes = []
    tris = []
    ids = [[0] * (nx + 1) for _ in range(ny + 1)]
    for iy in range(ny + 1):
        v = iy / ny
        for ix in range(nx + 1):
            x = -L / 2 + L * ix / nx
            y = W * v
            # base row (iy=0) stays at z=0 -> boundary lies in plane XY
            z = v * (H + A * math.sin(2.0 * math.pi * waves * ix / nx))
            t = 1.0 - 0.8 * v
            nodes.append((x, y, z, t))
            ids[iy][ix] = len(nodes) - 1
    for iy in range(ny):
        for ix in range(nx):
            a = ids[iy][ix]; b = ids[iy][ix + 1]
            c = ids[iy + 1][ix + 1]; d = ids[iy + 1][ix]
            tris.append((a, b, c))
            tris.append((a, c, d))
    base_row = [ids[0][ix] for ix in range(nx + 1)]
    edges = chain_edges(base_row, closed=False)
    return nodes, tris, edges, False


# ---------------------------------------------------------------------------
# 4) OPEN boundary: conical fan / sector vault (секторный свод).
#    Boundary = open circular arc on z=0 (open polyline). Surface fans up to a
#    single raised apex, giving a curved 3D shell.
# ---------------------------------------------------------------------------
def fan_vault_forging(R=95.0, H=70.0, sweep_deg=200.0, segs=40, rings=8):
    nodes = []
    tris = []
    sweep = math.radians(sweep_deg)
    a0 = -sweep / 2.0
    apex = 0
    nodes.append((0.0, 0.0, H, 0.2))  # apex first (id 0)
    ring_ids = []
    for i in range(1, rings + 1):
        f = i / rings              # 0..1 from apex to rim
        r = R * f
        # apex at z=H, rim at z=0; lift mid following a quarter-cos arch
        z = H * math.cos(f * math.pi / 2.0)
        t = 0.2 + 0.8 * f          # hot toward planar rim
        ids = []
        for j in range(segs + 1):
            a = a0 + sweep * j / segs
            nodes.append((r * math.cos(a), r * math.sin(a), z, t))
            ids.append(len(nodes) - 1)
        ring_ids.append(ids)
    # fan from apex to first ring
    first = ring_ids[0]
    for j in range(segs):
        tris.append((apex, first[j], first[j + 1]))
    # bands between rings
    for i in range(rings - 1):
        cur, nxt = ring_ids[i], ring_ids[i + 1]
        for j in range(segs):
            tris.append((cur[j], nxt[j], nxt[j + 1]))
            tris.append((cur[j], nxt[j + 1], cur[j + 1]))
    rim = ring_ids[-1]  # outer arc at z=0
    edges = chain_edges(rim, closed=False)
    return nodes, tris, edges, False


def main():
    print("Generating 3D forging CSV3D examples:")
    jobs = [
        ("dome_forging_closed.csv3d", dome_forging),
        ("rect_boss_forging_closed.csv3d", rect_boss_forging),
        ("corrugated_blade_forging_open.csv3d", corrugated_blade_forging),
        ("fan_vault_forging_open.csv3d", fan_vault_forging),
    ]
    for name, fn in jobs:
        nodes, tris, edges, closed = fn()
        write_csv3d(os.path.join(OUT_DIR, name), nodes, tris, edges, closed)


if __name__ == "__main__":
    main()
