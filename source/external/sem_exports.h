#pragma once

#ifdef SURFACEEXTENDERMESHER_EXPORTS
#  define SEM_API extern "C" __declspec(dllexport)
#else
#  define SEM_API extern "C" __declspec(dllimport)
#endif

SEM_API int SEM_SetWorkingDir(const char* path);

SEM_API const char* SEM_GetLastError(void);

SEM_API int SEM_LoadCSV3D(const char* path);

SEM_API int SEM_SubdivideContour(int n);

SEM_API double SEM_GetAvgEdgeLen();

SEM_API int SEM_SetRevolution(int enable, int axis);

SEM_API int SEM_ComputeOffsets(double first_gap, int num_offsets, double grading);

SEM_API int SEM_ComputeOffsetsAt(const double* gaps, int count);

// 2D meshing variant. Only the free constrained-Delaunay grid mesher remains
// (the Triangle-backed quality variants were removed with their non-free
// dependency). The enum is kept as the variant selector for future methods.
enum SEM_SteinerMethod {
    SEM_STEINER_GRID = 0
};

struct SEM_MeshParams {
    int    method;
    // GridCDT: interior Steiner-point spacing. < 0 = auto (source avg edge length).
    double param;
    // GridCDT: minimum spacing from offset lines, in multiples of `param`.
    // <= 0 = auto (0.45).
    double margin;
};

SEM_API int SEM_BuildMesh(const SEM_MeshParams* params);

// Reload previously serialized stages into the cache (skip recomputation).
// A source contour must already be loaded (SEM_LoadCSV3D) so the stem/closedness
// are known. SEM_LoadOffsets reads <stem>_offset_<i>.csv3d (i = 0..) from `dir`
// (null/empty = current working dir); SEM_LoadMesh reads one <..>_mesh.csv3d.
SEM_API int SEM_LoadOffsets(const char* dir);

SEM_API int SEM_LoadMesh(const char* path);

SEM_API int SEM_SolveThermal(void);

SEM_API int SEM_ExtractIsoline(double value);

SEM_API int SEM_LoadSurface3D(const char* path);

SEM_API int SEM_SubdivideSurface3D(int n);

SEM_API double SEM_GetSurfaceAvgEdgeLen3D();

SEM_API int SEM_ComputeOffsets3D(double first_gap, int num_offsets, double grading);

SEM_API int SEM_ComputeOffsetsAt3D(const double* gaps, int count);

// 3D tetrahedralization variant.
//   SEM_TET_BAND    - Delaunay of (source verts + outermost-offset verts + an
//                     interior Steiner grid), carved to the band by the source
//                     signed distance. `param` is the Steiner grid cell size.
//   SEM_TET_LAYERED - Delaunay of a layered point cloud built across ALL offset
//                     shells: even shells (0 = source) contribute their vertices,
//                     odd shells their triangle centroids. `param` is the boolean
//                     use_sdf (treated as 0/1). use_sdf=0 keeps the build fully
//                     SDF-free: each node's distance is the offset value of its
//                     layer, and tets are carved by layer span (kept when the
//                     spread of their vertices' layer indices is <= layer_span;
//                     see below). use_sdf=1 carves and colours by the source
//                     signed distance instead, and layer_span is ignored.
enum SEM_TetMethod {
    SEM_TET_BAND = 0,
    SEM_TET_LAYERED = 1
};

struct SEM_MeshParams3D {
    int    method;
    // SEM_TET_BAND:    Steiner grid cell size. < 0 = auto (source avg edge length).
    // SEM_TET_LAYERED: use_sdf flag (0/1). < 0 = auto (0 = SDF-free).
    double param;

    // Maximum tet edge length, in multiples of the source surface's average edge
    // length (SEM_GetSurfaceAvgEdgeLen3D). After meshing, any tet with an edge
    // longer than this is removed; its vertices remain. <= 0 disables the filter.
    double max_edge_len;

    // SEM_TET_LAYERED + use_sdf=0 only: carving layer-span. A tet is kept when the
    // spread of its vertices' layer indices (max - min) is <= layer_span. 1 keeps
    // only strictly adjacent layers; larger values keep vertex-centroid-vertex
    // tets that bridge two layer gaps, closing holes at the cost of admitting some
    // longer bridging tets. <= 0 = 1. Ignored by SEM_TET_BAND and use_sdf=1.
    int    layer_span;
};

// Set clip half-spaces applied to the 3D pipeline. `planes` is `count` planes of
// 4 doubles each: (nx, ny, nz, d); the kept half-space is nx*x+ny*y+nz*z+d >= 0,
// i.e. the normal points into the region that is retained. Normals need not be
// unit (normalized internally). Geometry beyond a plane is dropped from the tet
// band (removing stray offset artifacts and realizing symmetry planes, whose cut
// face is naturally zero-flux), and the extracted isosurface is cut flush to each
// plane. Set before SEM_BuildMesh3D; invalidates the cached mesh/isosurface.
// count=0 clears all planes (same as SEM_ClearClipPlanes3D).
SEM_API int SEM_SetClipPlanes3D(const double* planes, int count);

SEM_API int SEM_ClearClipPlanes3D(void);

SEM_API int SEM_BuildMesh3D(const SEM_MeshParams3D* params);

// Reload previously serialized 3D stages into the cache (skip recomputation).
// A source surface must already be loaded (SEM_LoadSurface3D). SEM_LoadOffsets3D
// reads <stem>_offset3d_<i>.csv3d (i = 0..) from `dir` (null/empty = current
// working dir); SEM_LoadMesh3D reads one <..>_mesh3d.csv3d (must contain #tets).
SEM_API int SEM_LoadOffsets3D(const char* dir);

SEM_API int SEM_LoadMesh3D(const char* path);

// Solve the steady-state thermal field on the tet band. Dirichlet BCs are set by
// shell membership: source-shell nodes -> T=1, outermost-offset nodes -> T=0.
//
// use_source_sdf selects how the outermost-offset (T=0) nodes are filtered:
//   0 - every outermost-offset node is kept as a T=0 BC by tag alone; max_inward
//       is ignored (the SDF-free path, robust on surfaces where the signed
//       distance misbehaves).
//   1 - the source signed distance is evaluated ONLY on the outermost-offset
//       nodes (not the whole mesh) and used to drop self-intersecting ones:
//       max_inward in [0,1] is the maximum relative depth (fraction of the outer
//       extent) an outermost node may sit inward of the true outer extent and
//       still be kept; nodes that penetrate deeper are dropped. max_inward < 0
//       disables the filter (keep all). This is independent of the meshing
//       use_sdf flag.
SEM_API int SEM_SolveThermal3D(int use_source_sdf, float max_inward);

SEM_API int SEM_ExtractIsosurface3D(double value);

SEM_API float SEM_GetProgress(void);
