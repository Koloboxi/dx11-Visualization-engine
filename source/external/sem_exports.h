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

// ===========================================================================
//  3D pipeline
// ===========================================================================

// Load a 3D source surface from a .csv3d file (validated triangle manifold).
// `closed` (non-zero) declares it a closed manifold enclosing a volume; when
// closed, the enclosed volume is computed on import (divergence theorem) and
// cached for SEM_GetSourceVolume3D. Clears the 3D cache.
SEM_API int SEM_LoadSurface3D(const char* path, int closed);

// Subdivide the source surface. Same n semantics as SEM_SubdivideContour.
SEM_API int SEM_SubdivideSurface3D(int n);

// Average edge length of the loaded source surface (the unit for first_gap).
SEM_API double SEM_GetSurfaceAvgEdgeLen3D();

// Cached enclosed volume of the source surface. Returns the volume (>= 0) if it
// was loaded as closed, else -1.0 (with SEM_GetLastError set).
SEM_API double SEM_GetSourceVolume3D();

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
    // meshing, any tet with a longer edge is removed (vertices stay). <= 0 = off.
    double max_edge_len;
};

// Set clip half-spaces applied to the 3D pipeline. `planes` is `count` planes of
// 4 doubles each (nx, ny, nz, d); the kept half-space is nx*x+ny*y+nz*z+d >= 0
// (normal points into the retained region; need not be unit). Geometry beyond a
// plane is dropped from the tet band (removing stray offset artifacts, realizing
// symmetry planes whose cut face is naturally zero-flux), and the extracted
// isosurface is cut flush to each plane. Set before SEM_BuildMesh3D; invalidates
// the cached mesh/isosurface. count = 0 clears all planes.
SEM_API int SEM_SetClipPlanes3D(const double* planes, int count);
SEM_API int SEM_ClearClipPlanes3D(void);

// Tetrahedralize the band (see SEM_TetMethod / SEM_MeshParams3D).
SEM_API int SEM_BuildMesh3D(const SEM_MeshParams3D* params);

// Reload previously serialized 3D stages into the cache instead of recomputing.
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

// Solve the steady-state thermal field on the tet band. Dirichlet BCs by shell:
// source-shell nodes -> T=1, outermost-offset nodes -> T=0. The T=0 set is
// filtered by each node's cached signed distance to the source: max_inward in
// [0,1] is the deepest (as a fraction of the outer extent) an outermost node may
// sit inward and still be kept, dropping self-intersection artifacts that
// penetrate further. max_inward < 0 disables the filter (keep all T=0 nodes).
SEM_API int SEM_SolveThermal3D(float max_inward);

// Extract the iso surface at the given level set of the normalized field
// (value in [0,1]). Writes <stem>_isosurface3d.csv3d.
SEM_API int SEM_ExtractIsosurface3D(double value);
