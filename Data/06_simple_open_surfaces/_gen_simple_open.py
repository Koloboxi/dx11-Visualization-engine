"""
Generator for minimal 3D OPEN-surface examples in CSV3D format.

Each mesh is a tiny triangulated open surface (a sheet/shell that does not
enclose a volume) — only a handful to a couple dozen triangles each. Nodes use
the 3-coordinate layout (no T column):

    #nodes
    node_id;x;y;z
    <id;x;y;z ...>
    #triangles
    <i;j;k ...>
    #edge;<count>
    <i;j ...>           — boundary chain of the sheet

The #edge chain is the boundary of the open surface.
"""
import math
import os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def write_csv3d(path, nodes, triangles, edges):
    """nodes: list of (x,y,z); triangles: list of (i,j,k); edges: list of (i,j)."""
    lines = ["#nodes", "node_id;x;y;z"]
    for i, (x, y, z) in enumerate(nodes):
        lines.append(f"{i};{x:.7f};{y:.7f};{z:.7f}")
    lines.append("#triangles")
    for (a, b, c) in triangles:
        lines.append(f"{a};{b};{c}")
    lines.append(f"#edge;{len(edges)}")
    for (a, b) in edges:
        lines.append(f"{a};{b}")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  {os.path.basename(path):32s} nodes={len(nodes):3d} tris={len(triangles):3d} edge={len(edges):3d}")


def chain_edges(loop_ids, closed):
    e = []
    n = len(loop_ids)
    last = n if closed else n - 1
    for k in range(last):
        e.append((loop_ids[k], loop_ids[(k + 1) % n]))
    return e


# 1) A single tilted square sheet — the simplest open surface (2 triangles).
def tilted_quad():
    nodes = [
        (-20.0, -20.0, 0.0),
        ( 20.0, -20.0, 6.0),
        ( 20.0,  20.0, 12.0),
        (-20.0,  20.0, 6.0),
    ]
    tris = [(0, 1, 2), (0, 2, 3)]
    edges = chain_edges([0, 1, 2, 3], closed=True)
    return nodes, tris, edges


# 2) Tent / ridge roof — two slopes meeting at a raised ridge (4 triangles).
def tent_roof():
    nodes = [
        (-30.0, -15.0, 0.0),   # 0 base front-left
        ( 30.0, -15.0, 0.0),   # 1 base front-right
        (-30.0,  15.0, 0.0),   # 2 base back-left
        ( 30.0,  15.0, 0.0),   # 3 base back-right
        (-30.0,   0.0, 20.0),  # 4 ridge left
        ( 30.0,   0.0, 20.0),  # 5 ridge right
    ]
    tris = [
        (0, 1, 5), (0, 5, 4),   # front slope
        (3, 2, 4), (3, 4, 5),   # back slope
    ]
    # boundary = the four eave edges (front + back base lines, open gables)
    edges = [(0, 1), (1, 5), (5, 3), (3, 2), (2, 4), (4, 0)]
    return nodes, tris, edges


# 3) Open square pyramid — four sloped faces, NO base (4 triangles).
def open_pyramid():
    nodes = [
        (-18.0, -18.0, 0.0),  # 0
        ( 18.0, -18.0, 0.0),  # 1
        ( 18.0,  18.0, 0.0),  # 2
        (-18.0,  18.0, 0.0),  # 3
        (  0.0,   0.0, 28.0), # 4 apex
    ]
    tris = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
    edges = chain_edges([0, 1, 2, 3], closed=True)  # open base rim
    return nodes, tris, edges


# 4) Corrugated ramp strip — a small wavy sheet rising in +z (nx=6 -> 12 tris).
def wavy_ramp(L=60.0, W=20.0, H=18.0, A=4.0, nx=6):
    nodes = []
    rows = [[], []]
    for iy in range(2):
        y = -W / 2 + W * iy
        for ix in range(nx + 1):
            x = -L / 2 + L * ix / nx
            z = iy * (H + A * math.sin(2.0 * math.pi * ix / nx))
            nodes.append((x, y, z))
            rows[iy].append(len(nodes) - 1)
    tris = []
    for ix in range(nx):
        a, b = rows[0][ix], rows[0][ix + 1]
        c, d = rows[1][ix + 1], rows[1][ix]
        tris.append((a, b, c))
        tris.append((a, c, d))
    edges = chain_edges(rows[0], closed=False)  # straight base polyline (open)
    return nodes, tris, edges


# 5) Half-open cylindrical shell — an arched sheet (segs=8 -> 16 tris).
def half_cylinder(R=22.0, Hgt=40.0, segs=8):
    nodes = []
    rows = [[], []]
    for iy in range(2):
        y = -Hgt / 2 + Hgt * iy
        for j in range(segs + 1):
            a = math.pi * j / segs  # 0..pi half arc
            nodes.append((R * math.cos(a), y, R * math.sin(a)))
            rows[iy].append(len(nodes) - 1)
    tris = []
    for j in range(segs):
        a, b = rows[0][j], rows[0][j + 1]
        c, d = rows[1][j + 1], rows[1][j]
        tris.append((a, b, c))
        tris.append((a, c, d))
    edges = chain_edges(rows[0], closed=False)  # bottom arc (open)
    return nodes, tris, edges


# 6) Saddle patch — a 3x3 hyperbolic-paraboloid grid (8 triangles).
def saddle(S=24.0, K=0.05):
    nodes = []
    ids = [[0] * 3 for _ in range(3)]
    for iy in range(3):
        for ix in range(3):
            x = -S + S * ix
            y = -S + S * iy
            z = K * (x * x - y * y)
            nodes.append((x, y, z))
            ids[iy][ix] = len(nodes) - 1
    tris = []
    for iy in range(2):
        for ix in range(2):
            a, b = ids[iy][ix], ids[iy][ix + 1]
            c, d = ids[iy + 1][ix + 1], ids[iy + 1][ix]
            tris.append((a, b, c))
            tris.append((a, c, d))
    perim = [ids[0][0], ids[0][1], ids[0][2],
             ids[1][2], ids[2][2], ids[2][1],
             ids[2][0], ids[1][0]]
    edges = chain_edges(perim, closed=True)
    return nodes, tris, edges


def main():
    print("Generating simple OPEN-surface CSV3D examples:")
    jobs = [
        ("tilted_quad_open.csv3d", tilted_quad),
        ("tent_roof_open.csv3d", tent_roof),
        ("open_pyramid_open.csv3d", open_pyramid),
        ("wavy_ramp_open.csv3d", wavy_ramp),
        ("half_cylinder_open.csv3d", half_cylinder),
        ("saddle_open.csv3d", saddle),
    ]
    for name, fn in jobs:
        nodes, tris, edges = fn()
        write_csv3d(os.path.join(OUT_DIR, name), nodes, tris, edges)


if __name__ == "__main__":
    main()
