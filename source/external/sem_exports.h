#pragma once

#ifdef SURFACEEXTENDERMESHER_EXPORTS
#  define SEM_API extern "C" __declspec(dllexport)
#else
#  define SEM_API extern "C" __declspec(dllimport)
#endif

// surface_extender_mesher public API.
//
// Two independent pipelines with parallel signatures: 2D (SEM_*) operates on a
// contour, 3D (SEM_*3D) on a triangle surface. Each runs the same stages —
// load -> (subdivide) -> offsets -> mesh -> solve thermal -> extract iso. Both
// share a single process-global cache but not its state; loading a new source
// clears that pipeline's cache.
//
// Return codes: 0 = success, any negative value = failure (the number is a
// coarse hint; read SEM_GetLastError for the reason). -10 specifically means
// the computation succeeded but writing the result to disk failed.

// ===========================================================================
//  Common (shared by both pipelines)
// ===========================================================================

// Set the directory for all serialized output. Empty/unset -> OS temp dir.
// Files are named <source-stem><suffix>.
SEM_API int SEM_SetWorkingDir(const char* path);

// Human-readable reason for the most recent failure.
SEM_API const char* SEM_GetLastError(void);

// Progress of the current long-running call, in [0,1].
SEM_API float SEM_GetProgress(void);

// Read-only view into the cache-owned buffers of a result (mesh / iso). Filled
// by the SEM_Get* accessors below. Every pointer points straight into the
// process-global cache and is valid ONLY until the next SEM_* call that mutates
// that pipeline (load / subdivide / offsets / mesh / solve / extract / clear).
// Copy out immediately if you need to keep the data. A pointer is null (and its
// count 0) when the corresponding array is absent: `T` before a thermal solve,
// `tets` for any 2D result, `edges` for a triangle/tet mesh, etc.
//
// Layout is flat for portable marshalling (P/Invoke, ctypes):
//   coords  : 3 * num_nodes doubles, x,y,z interleaved
//   T       : num_nodes doubles (per-node field), or null
//   normals : 3 * num_nodes doubles, x,y,z interleaved (angle-weighted unit vertex
//             pseudonormals), or null. Filled only for the Surface-backed views
//             (SEM_GetSourceSurface3D, SEM_GetSourceIsosurface3D); null otherwise.
//   edges   : 2 * num_edges ints (node indices)
//   tris    : 3 * num_tris  ints
//   tets    : 4 * num_tets  ints
// All indices are 0-based into the node array.
struct SEM_MeshView {
    const double* coords;
    const double* T;
    const double* normals;
    const int* edges;
    const int* tris;
    const int* tets;
    int num_nodes;
    int num_edges;
    int num_tris;
    int num_tets;
};

// ===========================================================================
//  2D pipeline
// ===========================================================================

// Load a 2D source contour from a .csv3d file and order its edges into a single
// polyline (closedness is detected from the geometry). Clears the 2D cache.
SEM_API int SEM_LoadCSV3D(const char* path);

// Subdivide the source contour. n = -1 disables (use the source as-is), n = 0
// adaptive (split each edge toward the mean edge length), n >= 2 uniform (split
// every edge into n parts). Invalidates downstream offsets/mesh.
SEM_API int SEM_SubdivideContour(int n);

// Average edge length of the loaded source contour (the unit for first_gap).
SEM_API double SEM_GetAvgEdgeLen();

// Mark the contour as an axisymmetric profile so the thermal solve uses
// revolution (cylindrical) weighting. axis: 1 = X, 2 = Y, 3 = Z. enable = 0
// turns it off. The contour endpoints must lie on the axis and must not cross
// it; validated here.
SEM_API int SEM_SetRevolution(int enable, int axis);

// Compute graded offset shells. first_gap is in multiples of SEM_GetAvgEdgeLen
// (sign sets the offset direction); num_offsets shells follow a geometric
// grading (1.0 = uniform). Offset 0 is always the (subdivided) source itself.
SEM_API int SEM_ComputeOffsets(double first_gap, int num_offsets, double grading);

// As SEM_ComputeOffsets but with explicit cumulative gaps (gaps[i] added to the
// running signed distance), one shell per entry.
SEM_API int SEM_ComputeOffsetsAt(const double* gaps, int count);

enum SEM_SteinerMethod {
    SEM_STEINER_GRID = 0
};

struct SEM_MeshParams {
    int    method;   // only SEM_STEINER_GRID is implemented.
    double param;    // interior Steiner-point spacing. < 0 = auto (source avg edge length).
    double margin;   // min spacing from offset lines, in multiples of `param`. <= 0 = auto (0.45).
};

// Triangulate (CDT) the band between the source and the outermost offset.
SEM_API int SEM_BuildMesh(const SEM_MeshParams* params);

// Reload previously serialized 2D stages into the cache instead of recomputing.
// A source must already be loaded (SEM_LoadCSV3D) to fix the stem/closedness.
// SEM_LoadOffsets reads <stem>_offset_<i>.csv3d (i = 0..) from `dir`
// (null/empty = working dir); SEM_LoadMesh reads one <..>_mesh.csv3d.
SEM_API int SEM_LoadOffsets(const char* dir);
SEM_API int SEM_LoadMesh(const char* path);

// Solve the steady-state thermal field on the band mesh. Dirichlet BCs are set
// by distance to the source: nodes on the source -> T=1, nodes on the outermost
// offset -> T=0. Overwrites <stem>_mesh.csv3d with the solved field.
SEM_API int SEM_SolveThermal(void);

// Extract the iso line at the given level set of the normalized field
// (value in [0,1]). Writes <stem>_isoline.csv3d.
SEM_API int SEM_ExtractIsoline(double value);

// Read-only access to the cached 2D results (see SEM_MeshView for the lifetime
// and layout contract). SEM_GetMesh fills the band triangle mesh (coords/tris,
// plus T after a solve); SEM_GetIsoline fills the extracted iso line
// (coords/edges). Negative if the result has not been computed/loaded yet.
SEM_API int SEM_GetMesh(SEM_MeshView* out);
SEM_API int SEM_GetIsoline(SEM_MeshView* out);

// ===========================================================================
//  3D pipeline
// ===========================================================================

// --- Pipeline: load source -> ... -> offset-remeshed isosurface ------------
//  These run in this order to build the target remeshed isosurface from a
//  freshly loaded source surface.

// Load a 3D source surface from a .csv3d file (validated triangle manifold).
// Closedness is detected from the topology (a watertight manifold with no
// boundary edges encloses a volume); for a closed surface the enclosed volume
// is computed on import (divergence theorem) and cached for
// SEM_GetSourceVolume3D. Clears the 3D cache.
SEM_API int SEM_LoadSurface3D(const char* path);

// Subdivide the source surface. Same n semantics as SEM_SubdivideContour.
SEM_API int SEM_SubdivideSurface3D(int n);

// Set clip half-spaces applied to the 3D pipeline. `planes` is `count` planes of
// 4 doubles each (nx, ny, nz, d); the kept half-space is nx*x+ny*y+nz*z+d >= 0
// (normal points into the retained region; need not be unit). Geometry beyond a
// plane is dropped from the tet band (removing stray offset artifacts, realizing
// symmetry planes whose cut face is naturally zero-flux), and the extracted
// isosurface is cut flush to each plane. Vertices within `on_plane_rel_tol` of a
// plane -- as a fraction of the local average edge length -- are snapped exactly onto
// it, so a nearly-flat contour is treated as planar rather than sliced through its
// wobble; the same relative tolerance widens the band-mesher half-space test and
// rectifies offset normals on the plane. Pass 0 for exact (no snapping); a typical
// value is 1e-2. Because the snap/cut bakes into the source geometry, the source is
// reloaded from its file and re-restricted on every clip change, and the cached
// offset shells are rebuilt against the new clip. Order-independent w.r.t.
// SEM_ComputeOffsets3D. count = 0 clears all planes (restoring the pristine source).
SEM_API int SEM_SetClipPlanes3D(const double* planes, int count,
    double on_plane_rel_tol);
SEM_API int SEM_ClearClipPlanes3D(void);

// Compute graded offset shells. Same semantics as the 2D SEM_ComputeOffsets[At]
// (first_gap in multiples of SEM_GetSurfaceAvgEdgeLen3D; offset 0 is the source).
SEM_API int SEM_ComputeOffsets3D(double first_gap, int num_offsets, double grading);
SEM_API int SEM_ComputeOffsetsAt3D(const double* gaps, int count);

// Tetrahedralization variant for SEM_BuildMesh3D:
//   SEM_TET_BAND    - Delaunay of (source verts + outermost-offset verts + an
//                     interior Steiner grid), carved to the band by the source
//                     signed distance. `param` is the Steiner grid cell size.
//   SEM_TET_LAYERED - Delaunay of a point cloud layered across ALL offset
//                     shells: even shells (0 = source) contribute their vertices,
//                     odd shells their triangle centroids. `param` is the use_sdf
//                     flag (0/1): 0 keeps every tet (trim the convex bulge with
//                     max_edge_len), 1 carves to the band by the source SDF.
enum SEM_TetMethod {
    SEM_TET_BAND = 0,
    SEM_TET_LAYERED = 1
};

struct SEM_MeshParams3D {
    int    method;
    // SEM_TET_BAND:    Steiner grid cell size. < 0 = auto (source avg edge length).
    // SEM_TET_LAYERED: use_sdf flag (0/1). < 0 = auto (0 = SDF-free).
    double param;
    // Max tet edge length, in multiples of SEM_GetSurfaceAvgEdgeLen3D. After
    // meshing, any tet with a longer edge is removed, along with any vertex left
    // referenced by no remaining tet (the mesh is compacted). <= 0 = off.
    double max_edge_len;
};

// Tetrahedralize the band (see SEM_TetMethod / SEM_MeshParams3D).
SEM_API int SEM_BuildMesh3D(const SEM_MeshParams3D* params);

// Solve the steady-state thermal field on the tet band. Dirichlet BCs by shell:
// source-shell nodes -> T=1, outermost-offset nodes -> T=0. The T=0 set is
// filtered by each node's cached signed distance to the source: max_inward in
// [0,1] is the deepest (as a fraction of the outer extent) an outermost node may
// sit inward and still be kept, dropping self-intersection artifacts that
// penetrate further. max_inward < 0 disables the filter (keep all T=0 nodes).
SEM_API int SEM_SolveThermal3D(float max_inward);

// Extract the iso surface at the given level set of the normalized field
// (value in [0,1]). Clip planes are applied. The extracted iso is kept as a
// Surface (read it back with SEM_GetSourceIsosurface3D, including its vertex
// normals) and becomes the input to SEM_OffsetRemeshIsosurface3D. Writes
// <stem>_isosurface3d.csv3d.
SEM_API int SEM_ExtractIsosurface3D(double value);

// Offset-and-remesh the extracted isosurface (SEM_ExtractIsosurface3D), as the final
// pipeline stage producing the target remeshed isosurface. Operates on the cached
// iso Surface (SEM_GetSourceIsosurface3D) directly: it is shifted along its
// angle-weighted pseudonormals by offset_value * (the iso's own avg edge length) — a
// signed multiple, positive inward against the outward normals, negative outward —
// concave folds nearer than the min_offset_value clearance are dropped (same cull
// semantics as the standalone SEM_OffsetRemeshInPlaneSurface3D below), and the
// survivors are re-meshed as a height field over the base plane perpendicular to
// `axis` (1/2/3 = X/Y/Z). The current clip planes are applied. The remeshed result
// is cached (read it back with SEM_GetIsosurface3D, the flat projection with
// SEM_GetIsosurfaceProjection3D), written to surface_remesh3d.csv3d, and returned in
// `out` (see SEM_MeshView). Requires an extracted iso surface; negative if absent or
// on failure.
SEM_API int SEM_OffsetRemeshIsosurface3D(int axis, double offset_value,
    double min_offset_value);

// --- Reload previously serialized stages into the cache (skip recomputation) ---

// A source must already be loaded (SEM_LoadSurface3D). SEM_LoadOffsets3D reads
// <stem>_offset3d_<i>.csv3d (i = 0..) from `dir` (null/empty = working dir);
// SEM_LoadMesh3D reads one <..>_mesh3d.csv3d (must contain a #tets section).
SEM_API int SEM_LoadOffsets3D(const char* dir);
SEM_API int SEM_LoadMesh3D(const char* path);

// Reload the cache-state sidecar (<stem>_state3d.txt) written alongside the 3D
// geometry: the exact signed offset distances, clip planes, subdivision level,
// and thermal-BC origin tags (src/far node ids). These cannot be recovered from
// the stage files at all (clip planes, subdivision, BC tags) or only via a lossy
// recompute (SEM_LoadOffsets3D re-derives unsigned, averaged offset distances).
// Call LAST in the reload sequence — after SEM_LoadOffsets3D and SEM_LoadMesh3D —
// so the restored distances/tags line up with the loaded shells and mesh.
// `dir` null/empty = working dir. Negative if missing/unparseable or mismatched.
SEM_API int SEM_LoadState3D(const char* dir);

// --- Helpers and read-only accessors ---------------------------------------

// Offset-and-remesh an OPEN surface passed in directly, as a standalone helper
// independent of the loaded pipeline source — it does NOT modify the 3D cache (the
// loaded source/iso/offsets are untouched; the result lives in its own buffer and
// the returned view is valid until the next standalone call). The surface is given
// as raw arrays (same layout as the Mesh fields): `xyz` is 3 * num_nodes doubles
// (x,y,z interleaved) and `tris` is 3 * num_tris ints (zero-based vertex indices).
// The sheet is shifted along its angle-weighted pseudonormals by
// offset_value * avg_edge_len, where avg_edge_len is the surface's own average edge
// length and offset_value is a signed multiple of it (no range limit): positive
// shifts inward (against the outward normals), negative outward. Vertices that end
// up closer to the surface than the shift are dropped (concave folds clean up); the
// survivors are projected onto the base plane perpendicular to `axis` (1/2/3 =
// X/Y/Z; that coordinate is preserved), constrained-CDT re-triangulated with the
// boundary loops as constraints, and lifted back. Requires an open sheet; a closed
// surface fails. The current clip planes (SEM_SetClipPlanes3D) are applied to the
// result. Writes surface_remesh3d.csv3d to the working dir and fills `out` (see
// SEM_MeshView for the lifetime/layout contract).
// `min_offset_value` sets the minimum clearance a shifted vertex must keep from the
// iso before it is pruned, in the SAME avg-edge-length multiples as `offset_value`.
// Vertices that end up closer to the iso than this are dropped; the fraction
// c = min_offset_value / offset_value (expected in [0, 1]) interpolates between the
// two cull modes:
//   min_offset_value == offset_value (c = 1): fold cull — demand the full |shift| of
//     clearance, dropping every concave fold that collapsed the sheet onto itself.
//   min_offset_value == 0 (c = 0): signed-crossing cull — keep the folds and drop
//     only vertices that crossed THROUGH the iso to the wrong side (their signed
//     distance ended up opposite in sign to the shift direction).
//   intermediate: drop vertices nearer than |shift| * c.
SEM_API int SEM_OffsetRemeshInPlaneSurface3D(int axis, double offset_value,
    const double* xyz, int num_nodes,
    const int* tris, int num_tris,
    double min_offset_value,
    SEM_MeshView* out);

// Average edge length of the loaded source surface (the unit for first_gap).
SEM_API double SEM_GetSurfaceAvgEdgeLen3D();

// Cached enclosed volume of the source surface. Returns the volume (>= 0) if the
// loaded surface is closed (watertight), else -1.0 (with SEM_GetLastError set).
SEM_API double SEM_GetSourceVolume3D();

// Read-only view of the current source surface — the active source as
// SEM_ComputeOffsets3D/SEM_BuildMesh3D see it: subdivided (SEM_SubdivideSurface3D)
// and clip-snapped (SEM_SetClipPlanes3D) in place. Fills coords/tris AND `normals`
// (the angle-weighted unit vertex pseudonormals, one per vertex). See SEM_MeshView
// for the lifetime/layout contract. Negative if no source is loaded.
SEM_API int SEM_GetSourceSurface3D(SEM_MeshView* out);

// Band tet mesh (coords/tets, plus T after a solve). Negative if the mesh has not
// been built/loaded yet. See SEM_MeshView for the lifetime/layout contract.
SEM_API int SEM_GetMesh3D(SEM_MeshView* out);

// The source isosurface consumed by SEM_OffsetRemeshIsosurface3D: the extracted iso
// (SEM_ExtractIsosurface3D), oriented outward. Fills coords/tris AND `normals` (the
// angle-weighted unit vertex pseudonormals that drive the offset). Empty until
// SEM_ExtractIsosurface3D has run; negative if not yet computed.
SEM_API int SEM_GetSourceIsosurface3D(SEM_MeshView* out);

// The final remeshed isosurface produced by SEM_OffsetRemeshIsosurface3D
// (coords/tris). SEM_GetIsosurfaceProjection3D fills its flat base-plane projection
// (coords/tris plus wireframe edges). Both are empty until
// SEM_OffsetRemeshIsosurface3D has run; negative if not yet computed.
SEM_API int SEM_GetIsosurface3D(SEM_MeshView* out);
SEM_API int SEM_GetIsosurfaceProjection3D(SEM_MeshView* out);

// The CDT constraint loops traced for the offset-remesh (SEM_OffsetRemeshIsosurface3D):
// the closed boundary loops of the source isosurface used as the re-triangulation
// constraints. Fills `coords` with the source isosurface vertices
// (SEM_GetSourceIsosurface3D) and `edges` with the loop wireframe (each loop closed
// last->first), so the indices index straight into `coords`. Empty until
// SEM_OffsetRemeshIsosurface3D has run; negative if not yet computed. See SEM_MeshView
// for the lifetime/layout contract.
SEM_API int SEM_GetIsosurfaceLoops3D(SEM_MeshView* out);