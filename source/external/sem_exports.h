#pragma once

#ifdef SURFACEEXTENDERMESHER_EXPORTS
#  define SEM_API extern "C" __declspec(dllexport)
#else
#  define SEM_API extern "C" __declspec(dllimport)
#endif

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
// node's T with the computed temperature. Run after SEM_BuildMesh[Ex].
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

