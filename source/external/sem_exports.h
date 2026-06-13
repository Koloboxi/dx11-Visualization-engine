#pragma once

#ifdef SURFACEEXTENDERMESHER_EXPORTS
#  define SEM_API extern "C" __declspec(dllexport)
#else
#  define SEM_API extern "C" __declspec(dllimport)
#endif

// Human-readable description of the most recent SEM_* failure (a call that
// returned a negative code or NULL). Returns a verbose message — including the
// underlying geometry/meshing-library text where available — to make coded
// errors diagnosable. Never NULL: returns "" when nothing has failed. The
// pointer is owned by the library and stays valid only until the next SEM_*
// call; copy it immediately.
SEM_API const char* SEM_GetLastError(void);

// Load a CSV3D edge-only file into the internal cache (0 ok, <0 error).
SEM_API int SEM_LoadCSV3D(const char* path);

// Densify the cached source contour, used afterwards as the meshing source.
//   n == -1  clear subdivision (use the original contour)
//   n == 0   adaptive: split each edge to ~mean edge length
//   n  > 0   split every edge into n equal parts
// Call after SEM_LoadCSV3D, before SEM_ComputeOffsets. 0 ok, <0 error.
SEM_API int SEM_SubdivideContour(int n);

// Mean edge length of the ORIGINAL (un-subdivided) source contour, in model
// units. Lets the caller express mesh parameters (max triangle area, max edge
// length, ...) as multiples of the contour's edge length. Returns <= 0 when no
// source is loaded.
SEM_API double SEM_GetAvgEdgeLen();

// Enable/disable surface-of-revolution mode for the thermal solve and pick the
// revolution axis: 1 = X, 2 = Y, 3 = Z. When enabled, SEM_SolveThermal weights
// every element by its centroid distance to the axis (Pappus), turning the 2D
// section solve into an axisymmetric one. Requires a profile whose two end
// points lie on the axis (their two perpendicular coordinates are zero) and
// whose interior never touches or crosses the axis. Pass enable=0 to turn it
// off (axis ignored). Call after SEM_LoadCSV3D.
//   0 ok, -1 no source, -2 bad axis, -3 endpoints off axis, -4 contour crosses
//   the axis.
SEM_API int SEM_SetRevolution(int enable, int axis);

// Compute a family of offset lines from the source contour. The source is
// stored as line 0, so N offsets yield N+1 lines. Gaps grow geometrically from
// a fixed first step (no absolute total distance).
//   first_gap    size of the first gap, in units of the source's mean edge
//                length (1 = one mean edge length); sign selects the side
//   num_offsets  number of offset lines beyond the source (>= 1)
//   grading      geometric ratio between successive gaps (>0; 1 = uniform)
// 0 ok, <0 error.
SEM_API int SEM_ComputeOffsets(double first_gap, int num_offsets, double grading);

// Like SEM_ComputeOffsets but with explicit per-line signed gaps; gaps are
// accumulated from the source (line 0). 0 ok, <0 error.
SEM_API int SEM_ComputeOffsetsAt(const double* gaps, int count);

// Serialize source contour + all offset lines to a CSV3D file.
// output_dir NULL/"" → system temp dir. Returns the written path (static
// buffer, copy immediately) or NULL on error.
SEM_API const char* SEM_SerializeOffsets(const char* output_dir);

// Tuning parameters for SEM_BuildMesh (pass NULL for defaults).
struct SEM_MeshParams {
    // Interior Steiner grid spacing: <0 auto (avg edge length), 0 disabled.
    double steiner_spacing;
    // Steiner keep-out from constraints, fraction of spacing (<=0 → 0.45).
    double steiner_margin;
};

// Build and cache the band mesh between the source contour (T=0) and the
// farthest offset line (T=1); T is each vertex's normalized distance to the
// source. 0 ok, <0 error.
SEM_API int SEM_BuildMesh(const SEM_MeshParams* params);

// Interior point-insertion strategy for SEM_BuildMeshEx.
enum SEM_SteinerMethod {
    SEM_STEINER_GRID = 0, // regular grid + CDT (default)
    SEM_STEINER_NONE = 1, // constrained Delaunay, no interior points
    SEM_STEINER_MIN_ANGLE = 2, // Triangle -q: minimum angle (deg)
    SEM_STEINER_MAX_AREA = 3, // Triangle -a: maximum triangle area
    SEM_STEINER_CONFORMING = 4, // Triangle -D: conforming Delaunay
    SEM_STEINER_SIZING = 5  // Triangle -u: maximum edge length
};

// Tuning parameters for SEM_BuildMeshEx (pass NULL for defaults).
struct SEM_MeshParamsEx {
    int    method;  // one of SEM_SteinerMethod
    double param;   // method's primary knob (spacing / angle / area / length);
    // negative → automatic per-method default
    double margin;  // GRID only: keep-out fraction (<=0 → 0.45)
};

// Build and cache the band mesh using the chosen strategy. Same T handling as
// SEM_BuildMesh. 0 ok, <0 error.
SEM_API int SEM_BuildMeshEx(const SEM_MeshParamsEx* params);

// Write the cached mesh to "<stem>_mesh.csv3d". output_dir NULL/"" → system
// temp dir. Returns the written path (static buffer, copy immediately) or NULL.
SEM_API const char* SEM_SerializeMesh(const char* output_dir);

// Solve steady-state heat conduction on the cached band mesh. The first offset
// line (source contour) is held at T=1 and the last (farthest) offset line at
// T=0; conductivity sets the material constant (>0). Overwrites each mesh
// node's T with the computed temperature. Boundary nodes are taken from the
// normalized distance field stored in each node's T by SEM_BuildMesh[Ex]
// (T~0 -> hot source, T~1 -> cold far line). If SEM_SetRevolution is active the
// solve is axisymmetric about the chosen axis. Run after SEM_BuildMesh[Ex].
// 0 ok, <0 error.
SEM_API int SEM_SolveThermal(double conductivity);

// Extract the isotherm at the given temperature (0..1) from the cached mesh's
// nodal field and store it in the cache. The line is built by linear
// interpolation of the per-element temperature along crossed triangle edges
// (marching triangles); output points carry T = value. Run after
// SEM_SolveThermal. 0 ok, <0 error.
SEM_API int SEM_ExtractIsoline(double value);

// Write the cached isotherm to "<stem>_isoline.csv3d". output_dir NULL/"" →
// system temp dir. Returns the written path (static buffer, copy immediately)
// or NULL on error.
SEM_API const char* SEM_SerializeIsoline(const char* output_dir);

// ---------------------------------------------------------------------------
// 3D pipeline. Parallel to the 2D API above but the source is a triangle
// surface mesh (CSV3D #triangles). Offset shells are isosurfaces of the
// source's one-sided signed distance field, extracted by marching cubes;
// for open surfaces each shell is trimmed at the boundary by planes through
// the boundary edges (spanned by the edge and its triangle normal).
// Kept in a separate cache; the 2D entry points are unaffected.
// ---------------------------------------------------------------------------

// Load a CSV3D triangle-surface file into the 3D cache. The surface must be a
// consistently oriented 2-manifold (each edge shared by 1 or 2 triangles).
//   0 ok, -1 bad path, -2 load failed, -3 no triangles / invalid surface.
SEM_API int SEM_LoadSurface3D(const char* path);

// Densify the cached source surface, used afterwards as the offset source.
//   n == -1  clear subdivision (use the original surface)
//   n == 0   adaptive: halve edges until ~mean edge length
//   n  > 1   split every edge into n equal parts (n*n sub-triangles per face)
// Call after SEM_LoadSurface3D, before SEM_ComputeOffsets3D. 0 ok, <0 error.
SEM_API int SEM_SubdivideSurface3D(int n);

// Mean edge length of the ORIGINAL (un-subdivided) source surface, in model
// units. Returns <= 0 when no surface is loaded.
SEM_API double SEM_GetSurfaceAvgEdgeLen3D();

// Compute a family of offset shells from the source surface as signed-distance
// isosurfaces (marching cubes), one per cumulative offset distance. The source
// is stored as shell 0, so N offsets yield N+1 shells. Gaps grow geometrically
// from a fixed first step.
//   first_gap    size of the first gap, in units of the source's mean edge
//                length; sign selects the side
//   num_offsets  number of offset shells beyond the source (>= 1)
//   grading      geometric ratio between successive gaps (>0; 1 = uniform)
// 0 ok, <0 error.
SEM_API int SEM_ComputeOffsets3D(double first_gap, int num_offsets, double grading);

// Like SEM_ComputeOffsets3D but with explicit per-shell signed gaps; gaps are
// accumulated from the source (shell 0). 0 ok, <0 error.
SEM_API int SEM_ComputeOffsetsAt3D(const double* gaps, int count);

// Serialize source surface + all offset shells to a CSV3D file. Each node's T
// carries its shell's normalized offset distance (source 0 .. farthest 1).
// output_dir NULL/"" → system temp dir. Returns the written path (static
// buffer, copy immediately) or NULL on error.
SEM_API const char* SEM_SerializeOffsets3D(const char* output_dir);

// Tuning parameters for SEM_BuildMesh3D (pass NULL for defaults).
struct SEM_MeshParams3D {
    // TetGen max tetrahedron volume (-a): <0 auto (~avg_edge^3/6), 0 unconstrained.
    double max_volume;
    // TetGen radius-edge quality bound (-q): >0 enables quality refinement (lower
    // = stricter, but <~1.4 over-refines near sharp boundary features); <=0
    // disables -q for a volume-only mesh (analogous to Triangle's max-area mode).
    // When params is NULL the default is 1.4.
    double radius_edge;
};

// Build and cache the tetrahedral band mesh between the source surface and the
// farthest offset shell, closed by side walls along the surface boundary. Each
// node's T is its normalized distance to the source surface (source 0 .. far 1)
// for use as thermal boundary conditions. Run after SEM_ComputeOffsets3D.
// 0 ok, <0 error.
SEM_API int SEM_BuildMesh3D(const SEM_MeshParams3D* params);

// Interior refinement strategy for SEM_BuildMesh3DEx (TetGen). Parallel to the
// 2D SEM_SteinerMethod.
enum SEM_TetMethod {
    SEM_TET_QUALITY = 0, // -q radius-edge quality (param = ratio; ~2.0 default,
    //    lower = stricter, <~1.4 over-refines near features)
    SEM_TET_NONE = 1, // -p only: conforming Delaunay, no size/quality refine
    SEM_TET_MAX_VOL = 2, // -a maximum tet volume (param = volume); Triangle -a analog
    SEM_TET_SIZING = 3  // -a derived from a target edge length (param = length)
};

// Tuning parameters for SEM_BuildMesh3DEx (pass NULL for defaults).
struct SEM_MeshParams3DEx {
    int    method; // one of SEM_TetMethod (default SEM_TET_MAX_VOL)
    double param;  // method's primary knob (ratio / volume / edge length);
    // negative → automatic per-method default
};

// Build and cache the tet band mesh using the chosen refinement strategy. Same
// caps + rim-wall PLC and T handling as SEM_BuildMesh3D. Run after
// SEM_ComputeOffsets3D. 0 ok, <0 error (-5 bad method).
SEM_API int SEM_BuildMesh3DEx(const SEM_MeshParams3DEx* params);

// Write the cached tet mesh to "<stem>_mesh3d.csv3d". output_dir NULL/"" →
// system temp dir. Returns the written path (static buffer, copy immediately)
// or NULL on error.
SEM_API const char* SEM_SerializeMesh3D(const char* output_dir);

// Solve steady-state heat conduction on the cached tet mesh. Source surface
// nodes are held at T=1 and farthest-shell nodes at T=0 (taken from the
// normalized distance field stored by SEM_BuildMesh3D); side-wall nodes are
// free (insulated). conductivity sets the material constant (>0). Overwrites
// each node's T with the computed temperature. Run after SEM_BuildMesh3D.
// 0 ok, <0 error.
SEM_API int SEM_SolveThermal3D(double conductivity);

// Extract the isosurface at the given temperature (0..1) from the cached tet
// mesh's nodal field (marching tetrahedra) and store it in the cache. Output
// vertices carry T = value. Run after SEM_SolveThermal3D. 0 ok, <0 error.
SEM_API int SEM_ExtractIsosurface3D(double value);

// Write the cached isosurface to "<stem>_isosurface3d.csv3d". output_dir
// NULL/"" → system temp dir. Returns the written path (static buffer, copy
// immediately) or NULL on error.
SEM_API const char* SEM_SerializeIsosurface3D(const char* output_dir);

// Current progress of the active 3D computation stage (0..1). Updated during
// SEM_ComputeOffsets3D, SEM_BuildMesh3D[Ex], and SEM_SolveThermal3D; reset to
// 0 when each call completes (success or failure). Safe to call from any thread.
SEM_API float SEM_GetProgress3D(void);

