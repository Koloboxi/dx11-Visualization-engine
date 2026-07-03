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
    const int*    edges;
    const int*    tris;
    const int*    tets;
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

// Average edge length of the loaded source contour.
SEM_API double SEM_GetAvgEdgeLen();

// Mark the contour as an axisymmetric profile so the thermal solve uses
// revolution (cylindrical) weighting. axis: 1 = X, 2 = Y, 3 = Z. enable = 0
// turns it off. The contour endpoints must lie on the axis and must not cross
// it; validated here.
SEM_API int SEM_SetRevolution(int enable, int axis);

// Compute graded offset shells. first_gap is a distance in world units
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
SEM_API int SEM_SetClipPlanes3D(const double* planes, int count, double on_plane_rel_tol);
SEM_API int SEM_ClearClipPlanes3D(void);

// Compute graded offset shells. Same semantics as the 2D SEM_ComputeOffsets[At]
// (first_gap is a distance in world units; offset 0 is the source).
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
    SEM_TET_BAND    = 0,
    SEM_TET_LAYERED = 1
};

struct SEM_MeshParams3D {
    int    method;
    // SEM_TET_BAND:    Steiner grid cell size. < 0 = auto (source avg edge length).
    // SEM_TET_LAYERED: use_sdf flag (0/1). < 0 = auto (0 = SDF-free).
    double param;
    // Max tet edge length, in world units. After meshing, any tet with a longer
    // edge is removed, along with any vertex left referenced by no remaining tet
    // (the mesh is compacted). <= 0 = off.
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
// (value in [0,1]), then optionally offset-and-remesh it in one call. Clip planes are
// applied. The extracted iso is kept as a Surface (read it back with
// SEM_GetSourceIsosurface3D, including its vertex normals) and written to
// <stem>_isosurface3d.csv3d.
//
// axis == 0: extract only — no offset-remesh runs (SEM_GetIsosurface3D stays empty).
// axis 1/2/3 (X/Y/Z): the offset-remesh stage runs as the final step, folded in from the
// former SEM_OffsetRemeshIsosurface3D. The cached iso Surface is shifted along its
// angle-weighted pseudonormals by offset_value world units — a signed distance,
// positive inward against the outward normals, negative outward —
// concave folds nearer than the min_offset_value clearance are dropped (same cull
// semantics as the standalone SEM_OffsetRemeshInPlaneSurface3D below), and the
// survivors are re-meshed over the base plane perpendicular to `axis` as a single
// boundary-constrained Delaunay of the projected point cloud (a single-valued height
// field over the axis; a folded projection is rejected) — the offset-remesh base.
//
// A final isotropic remesh (folded in from the former SEM_RemeshIsosurface3D) then runs
// over that base as an optional cleanup: the height-field re-mesh leaves tall, narrow,
// near-degenerate triangles where a near-vertical wall projects to a thin sliver over the
// base plane; `iterations` sweeps of short-edge collapse, long-edge split, and tangential
// relaxation break those slivers into well-shaped triangles at a target edge length of
// target_len_mult * the sheet's average edge length (target_len_mult <= 0 uses the median
// edge length). iterations <= 0 skips this pass and publishes the base as-is. Boundary
// vertices lying in a clip plane slide flat within it; every other boundary vertex is frozen.
//
// The remeshed result is cached (read it back with SEM_GetIsosurface3D, the flat projection
// with SEM_GetIsosurfaceProjection3D) and written to surface_remesh3d.csv3d. Clip planes are
// applied throughout. As an optimization, a repeat call that changes only target_len_mult /
// iterations (value, axis, offset_value, min_offset_value unchanged) re-runs just the final
// remesh on the cached base, skipping extraction and offset-remesh. A clip plane change is
// not tracked here — it rebuilds the whole pipeline upstream, invalidating the cached base.
// Negative on failure.
SEM_API int SEM_ExtractIsosurface3D(double value, int axis, double offset_value,
                                    double min_offset_value,
                                    double target_len_mult = 1.0, int iterations = 3);

// --- Reload a whole serialized session into the cache -----------------------

// Restore an entire 3D session previously written into `dir` by the pipeline
// stages: the source surface (from the copy the pipeline kept in `dir`), every
// computed stage's geometry, and every stage's call arguments and cache tags.
// This replaces the former piecemeal SEM_LoadOffsets3D / SEM_LoadMesh3D /
// SEM_LoadState3D reload sequence — no source need be loaded first; the session
// manifest (session3d.txt) names the source copy and drives the reload. Loads
// exactly as far as the pipeline had progressed. `dir` must be a session folder
// (not null/empty); it also becomes the working dir. Negative if the manifest is
// missing/unparseable or a stage file is inconsistent with it. Read the restored
// arguments back with SEM_GetPipelineArgs3D / SEM_GetOffsetGaps3D /
// SEM_GetClipPlanes3D.
SEM_API int SEM_LoadSession3D(const char* dir);

// Which pipeline stages the 3D cache currently holds (bitmask in
// SEM_PipelineArgs3D::stages_present).
enum SEM_StageFlags {
    SEM_STAGE_OFFSETS = 1 << 0,
    SEM_STAGE_MESH    = 1 << 1,
    SEM_STAGE_THERMAL = 1 << 2,
    SEM_STAGE_ISO     = 1 << 3
};

// Offset schedule kind (SEM_PipelineArgs3D::offset_mode).
enum SEM_OffsetMode { SEM_OFFSET_GRADED = 0, SEM_OFFSET_GAPS = 1 };

// The pipeline call arguments last used (or restored by SEM_LoadSession3D), so a
// host can repopulate its UI to match the loaded session. The variable-length
// offset gaps and the clip planes are read separately (below). Negative if no
// source is loaded.
struct SEM_PipelineArgs3D {
    int    subdiv_n;              // SEM_SubdivideSurface3D (-1 = disabled)
    double clip_rel_tol;          // SEM_SetClipPlanes3D on_plane_rel_tol
    int    clip_count;            // number of clip planes (read via SEM_GetClipPlanes3D)
    int    offset_mode;           // SEM_OffsetMode: GRADED = first_gap/num_offsets/grading,
                                  //                 GAPS  = the SEM_GetOffsetGaps3D list
    double first_gap;             // SEM_ComputeOffsets3D (GRADED)
    int    num_offsets;
    double grading;
    int    offset_gap_count;      // number of explicit gaps (GAPS; read via SEM_GetOffsetGaps3D)
    int    tet_method;            // SEM_MeshParams3D.method
    double tet_param;             // SEM_MeshParams3D.param
    double tet_max_edge_len;      // SEM_MeshParams3D.max_edge_len
    float  max_inward;            // SEM_SolveThermal3D
    double iso_value;             // SEM_ExtractIsosurface3D
    int    iso_axis;
    double iso_offset_value;
    double iso_min_offset_value;
    double iso_target_len_mult;
    int    iso_iterations;
    int    stages_present;        // bitmask of SEM_StageFlags
};
SEM_API int SEM_GetPipelineArgs3D(SEM_PipelineArgs3D* out);

// Copy the explicit cumulative offset gaps (SEM_OFFSET_GAPS mode) into `out`
// (room for `max` doubles); *count receives the true number regardless of `max`.
// Empty in GRADED mode. Negative if no source is loaded.
SEM_API int SEM_GetOffsetGaps3D(double* out, int max, int* count);

// Copy the active clip planes into `out` as `count` planes of 4 doubles each
// (nx, ny, nz, d), room for `max` planes; *count receives the true number
// regardless of `max`. Negative if no source is loaded.
SEM_API int SEM_GetClipPlanes3D(double* out, int max, int* count);

// --- Helpers and read-only accessors ---------------------------------------

// Offset-and-remesh an OPEN surface passed in directly, as a standalone helper
// independent of the loaded pipeline source — it leaves the loaded source/iso/offsets
// untouched (the result lives in its own buffer and the returned view is valid until
// the next standalone call). As the one exception it publishes the flat base-plane
// projection into the shared projection cache, so SEM_GetIsosurfaceProjection3D
// returns it after a standalone call too. Repeated calls that pass identical geometry,
// clip planes and on-plane tolerance reuse the prepared input Surface. The surface is given
// as raw arrays (same layout as the Mesh fields): `xyz` is 3 * num_nodes doubles
// (x,y,z interleaved) and `tris` is 3 * num_tris ints (zero-based vertex indices).
// The sheet is shifted along its angle-weighted pseudonormals by offset_value world
// units, a signed distance (no range limit): positive
// shifts inward (against the outward normals), negative outward. Vertices that end
// up closer to the surface than the shift are dropped (concave folds clean up); the
// survivors are projected onto the base plane perpendicular to `axis` (1/2/3 =
// X/Y/Z; that coordinate is preserved), re-triangulated as a single
// boundary-constrained Delaunay (a single-valued height field over the axis; a folded
// projection is rejected), and lifted back. Requires an open sheet; a closed surface
// fails. The current clip planes (SEM_SetClipPlanes3D) are applied to the result.
// Writes surface_remesh3d.csv3d to the working dir and fills `out` (see SEM_MeshView
// for the lifetime/layout contract).
// `min_offset_value` sets the minimum clearance a shifted vertex must keep from the
// iso before it is pruned, in the SAME world units as `offset_value`.
// Vertices that end up closer to the iso than this are dropped; the fraction
// c = min_offset_value / offset_value (expected in [0, 1]) interpolates between the
// two cull modes:
//   min_offset_value == offset_value (c = 1): fold cull — demand the full |shift| of
//     clearance, dropping every concave fold that collapsed the sheet onto itself.
//   min_offset_value == 0 (c = 0): signed-crossing cull — keep the folds and drop
//     only vertices that crossed THROUGH the iso to the wrong side (their signed
//     distance ended up opposite in sign to the shift direction).
//   intermediate: drop vertices nearer than |shift| * c.
// Finally, when `iterations` > 0, the offset-remeshed sheet is run through the same
// isotropic cleanup as SEM_ExtractIsosurface3D's final-remesh pass (short-edge collapse /
// long-edge split / tangential relaxation), `iterations` sweeps at a target edge length of
// `target_len_mult` * the sheet's own average edge length (target_len_mult <= 0 uses
// the median). iterations <= 0 skips this final remesh.
SEM_API int SEM_OffsetRemeshInPlaneSurface3D(int axis, double offset_value,
                                      const double* xyz, int num_nodes,
                                      const int* tris, int num_tris,
                                      double min_offset_value,
                                      double target_len_mult, int iterations,
                                      SEM_MeshView* out);

// Average edge length of the loaded source surface.
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

// The source isosurface consumed by the offset-remesh: the extracted iso
// (SEM_ExtractIsosurface3D), oriented outward. Fills coords/tris AND `normals` (the
// angle-weighted unit vertex pseudonormals that drive the offset). Empty until
// SEM_ExtractIsosurface3D has run; negative if not yet computed.
SEM_API int SEM_GetSourceIsosurface3D(SEM_MeshView* out);

// The final remeshed isosurface produced by SEM_ExtractIsosurface3D with an offset axis
// (coords/tris). SEM_GetIsosurfaceProjection3D fills its flat base-plane projection
// (coords/tris plus wireframe edges). Both are empty until an offset-remesh has run;
// negative if not yet computed.
SEM_API int SEM_GetIsosurface3D(SEM_MeshView* out);
SEM_API int SEM_GetIsosurfaceProjection3D(SEM_MeshView* out);

// The CDT constraint loops traced for the offset-remesh (SEM_ExtractIsosurface3D with an offset axis):
// the closed boundary loops of the source isosurface used as the re-triangulation
// constraints. Fills `coords` with the source isosurface vertices
// (SEM_GetSourceIsosurface3D) and `edges` with the loop wireframe (each loop closed
// last->first), so the indices index straight into `coords`. Empty until the
// offset-remesh has run; negative if not yet computed. See SEM_MeshView
// for the lifetime/layout contract.
SEM_API int SEM_GetIsosurfaceLoops3D(SEM_MeshView* out);

// What one clip-plane operation of the 3D pipeline did to the geometry it ran on:
//   stage     : "<pipeline stage>/<operation>" label. Stages, in pipeline order:
//               "source" (the source restriction on load / clip change), "offset<i>"
//               (the cut of offset shell i), "isosurface" (the extracted-iso
//               restriction), "remesh" (the final cut of the remeshed sheet).
//               Operations: "snap" (near-plane vertices projected onto the planes),
//               "cut<j>" (the half-space cut by plane j), "drop" (coplanar caps
//               removed).
//   moved     : vertices the snap moved, 6 doubles each — original x,y,z then
//               snapped x,y,z. num_moved is the vertex (pair) count. Null/0 for cuts.
//   removed   : the geometry the operation removed (the clipped-away sheet of a cut,
//               the dropped coplanar caps), as a compact SEM_MeshView. Empty for
//               snaps and for calls that removed nothing.
struct SEM_ClipChangeView {
    const char*   stage;
    const double* moved;
    int           num_moved;
    SEM_MeshView  removed;
};

// The log of every clip-plane operation the 3D pipeline stages ran, one
// SEM_ClipChangeView per call, in pipeline order (source, offsets, isosurface,
// remesh). Each stage's entries are replaced when that stage reruns and dropped when
// its geometry is invalidated. Empty (count 0, out null) when no clip planes are set
// or nothing has run. `*out` points into the cache and follows the SEM_MeshView
// lifetime contract. The standalone SEM_OffsetRemeshInPlaneSurface3D is not logged
// (it does not touch the pipeline cache).
SEM_API int SEM_GetClipChanges3D(const SEM_ClipChangeView** out, int* count);