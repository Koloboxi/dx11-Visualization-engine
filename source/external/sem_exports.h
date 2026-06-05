#pragma once

// Use this header in the DLL project AND in any consumer project.
// Consumers must NOT define SURFACEEXTENDERMESHER_EXPORTS.

#ifdef SURFACEEXTENDERMESHER_EXPORTS
#  define SEM_API extern "C" __declspec(dllexport)
#else
#  define SEM_API extern "C" __declspec(dllimport)
#endif

// ---------------------------------------------------------------------------
// Task 1 — Load a CSV3D edge-only file into the internal cache.
//
// Returns:
//   0   success
//  -1   cannot open / parse error
//  -2   file contains no edges
SEM_API int SEM_LoadCSV3D(const char* path);

// ---------------------------------------------------------------------------
// Task 1b — Subdivide the cached source contour.
//
// Adds points along the contour's edges so that long straight segments do not
// later get meshed into long, thin triangles. The original contour stays in the
// cache untouched; the densified copy is stored separately. When a subdivided
// contour is present it is used as the source (line 0) by SEM_ComputeOffsets /
// SEM_ComputeOffsetsAt and hence by the mesh. The original contour is still
// used as the reference for the T-coordinate.
//
// Call this AFTER SEM_LoadCSV3D and BEFORE SEM_ComputeOffsets — it invalidates
// any previously computed offsets and mesh.
//
//  n   < -1  invalid
//      == -1 clear subdivision (mesh from the original contour as-is)
//      == 0  adaptive: split each edge into round(len / L) equal parts, where
//            L is the contour's mean edge length (evens out edge density so
//            long straight runs are broken up, short edges are left alone)
//      >  0  split every edge into exactly n equal parts
//
// Returns:
//   0   success
//  -1   no source loaded
//  -2   invalid argument (n < -1)
SEM_API int SEM_SubdivideContour(int n);

// ---------------------------------------------------------------------------
// Task 2 — Compute the family of offset lines from the cached source contour.
//
// The source contour itself is always stored as line 0 (distance 0); the
// computed offsets follow it. So a call producing N offsets yields N+1 lines.
//
//  d_max         distance from the source contour to the farthest (last)
//                offset line. The SIGN selects the side: positive offsets to
//                the left of travel direction (outward for a closed contour),
//                negative to the right (inward). Must be non-zero.
//  num_offsets   number of offset lines beyond the source (>= 1).
//  grading       geometric ratio between successive gaps:
//                  > 1  -> gaps grow outward (denser near the source),
//                  < 1  -> gaps shrink outward,
//                  == 1 -> uniform spacing.
//                Must be > 0.
//
// Returns:
//   0   success
//  -1   no source loaded
//  -2   invalid parameters
SEM_API int SEM_ComputeOffsets(double d_max, int num_offsets, double grading);

// ---------------------------------------------------------------------------
// Task 2b — Compute offset lines from an explicit list of per-line gaps.
//
// Like SEM_ComputeOffsets but the spacing is given directly: gaps[i] is the
// signed distance between line i and line i+1, with line 0 being the source
// contour. Gaps are accumulated, so line k sits at distance
// gaps[0] + ... + gaps[k-1] from the source. Use a consistent sign for a
// well-formed band.
//
//  gaps    array of `count` signed gaps (must be non-null).
//  count   number of gaps / offset lines beyond the source (>= 1).
//
// Returns:
//   0   success
//  -1   no source loaded
//  -2   invalid parameters
SEM_API int SEM_ComputeOffsetsAt(const double* gaps, int count);

// ---------------------------------------------------------------------------
// Task 3 — Serialize the source contour + all offset lines to a CSV3D file.
//
//  output_dir   directory for the output file (NULL or "" → system temp dir).
//
// Returns the absolute path to the written file (pointer to a static buffer —
// copy immediately; invalidated by the next call that returns a path).
// Returns NULL on error.
SEM_API const char* SEM_SerializeOffsets(const char* output_dir);

// ---------------------------------------------------------------------------
// Task 4 — Constrained-Delaunay meshing of the band between the source contour
// and the farthest offset line.
//
// Split into two steps:
//   SEM_BuildMesh     builds + caches the triangulation (the expensive part);
//   SEM_SerializeMesh writes the cached mesh to disk.
// The cached mesh is reused until offsets are recomputed.

// Tunable meshing parameters. Pass NULL to SEM_BuildMesh for the defaults.
struct SEM_MeshParams {
    // Grid spacing for interior Steiner points.
    //   < 0  → auto: use the source contour's average edge length,
    //   == 0 → disable densification (boundary-conforming triangulation only),
    //   > 0  → use this spacing.
    double steiner_spacing;

    // Keep-out distance from constraint lines for Steiner points, as a fraction
    // of the spacing (typical 0.3..0.5). <= 0 → default 0.45.
    double steiner_margin;
};

// Build and cache the band mesh. The band runs from the source contour (its
// vertices map to T = 0) to the farthest offset line (T = 1), with T being each
// vertex's minimum distance to the source contour, linearly normalized.
//
//  params   tuning parameters, or NULL for defaults
//           (steiner_spacing = avg edge length, steiner_margin = 0.45).
//
// Returns:
//   0   success
//  -1   no source loaded
//  -2   fewer than 2 lines available (call SEM_ComputeOffsets first)
//  -3   not enough valid lines to form a band
//  -4   triangulation produced no triangles
SEM_API int SEM_BuildMesh(const SEM_MeshParams* params);

// ---------------------------------------------------------------------------
// Task 4b — Band meshing with a selectable Steiner-point insertion strategy.
//
// SEM_BuildMesh (above) always uses the original regular-grid strategy.
// SEM_BuildMeshEx exposes every backend strategy: the grid one plus the
// Shewchuk Triangle refinement methods (-q / -a / -D / -u).

// Interior point-insertion strategy.
enum SEM_SteinerMethod {
    SEM_STEINER_GRID = 0, // regular grid of interior points + CDT (default)
    SEM_STEINER_NONE = 1, // Triangle: constrained Delaunay, no interior pts
    SEM_STEINER_MIN_ANGLE = 2, // Triangle -q: Ruppert refinement, min angle (deg)
    SEM_STEINER_MAX_AREA = 3, // Triangle -a: bounded triangle area
    SEM_STEINER_CONFORMING = 4, // Triangle -D: conforming Delaunay (+opt min angle)
    SEM_STEINER_SIZING = 5  // Triangle -u: bounded longest edge length
};

struct SEM_MeshParamsEx {
    // One of SEM_SteinerMethod.
    int method;

    // Primary tuning knob, interpreted per method:
    //   GRID / NONE     -> interior grid spacing (NONE ignores it),
    //   MIN_ANGLE       -> minimum angle in degrees (typical 20..33),
    //   MAX_AREA        -> maximum triangle area,
    //   CONFORMING      -> minimum angle in degrees (0 => conforming only),
    //   SIZING          -> maximum edge length.
    // Pass a negative value for an automatic per-method default derived from
    // the source contour's average edge length.
    double param;

    // GRID only: Steiner keep-out distance from constraint lines, as a fraction
    // of the spacing (typical 0.3..0.5). <= 0 => default 0.45. Ignored by the
    // Triangle methods.
    double margin;
};

// Build and cache the band mesh using the chosen strategy. Pass NULL for the
// defaults (method = GRID, automatic spacing, margin = 0.45). T-coordinate
// handling is identical to SEM_BuildMesh.
//
// Returns:
//   0   success
//  -1   no source loaded
//  -2   fewer than 2 lines available (call SEM_ComputeOffsets first)
//  -3   not enough valid lines to form a band
//  -4   triangulation failed / produced no triangles
//  -5   invalid method
SEM_API int SEM_BuildMeshEx(const SEM_MeshParamsEx* params);

// Write the cached mesh to "<stem>_mesh.csv3d" with #face triangle rows.
//
//  output_dir   directory for the output file (NULL or "" → system temp dir).
//
// Returns the absolute path to the written file (pointer to a static buffer —
// copy immediately; invalidated by the next call that returns a path).
// Returns NULL on error (no mesh built yet, or write failure).
SEM_API const char* SEM_SerializeMesh(const char* output_dir);
