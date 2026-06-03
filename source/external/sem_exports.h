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
// Task 2 — Compute offset polylines from the cached source line.
//
//  scale_coeff   step multiplier; sign sets side.
//                Positive → left of travel direction.
//                Negative → right of travel direction.
//                Each line is |scale_coeff| * avg_edge_len further from the previous.
//                offset[i] = avg_edge_len * (1 + i * |scale_coeff|) from source.
//                scale_coeff = 1  → equal spacing equal to avg_edge_len.
//                Must be non-zero.
//  num_offsets   total number of offset lines to produce (>= 1).
//
// Returns:
//   0   success
//  -1   no source loaded
//  -2   invalid parameters
//  -3   cannot order edges into a polyline
//  -4   path extraction failed
SEM_API int SEM_ComputeOffsets(double scale_coeff, int num_offsets);

// ---------------------------------------------------------------------------
// Task 3 — Serialize original line + all offsets to a CSV3D file.
//
//  output_dir   directory for the output file (NULL or "" → system temp dir).
//
// Returns the absolute path to the written file (pointer to a static buffer —
// copy immediately; invalidated by the next call that returns a path).
// Returns NULL on error.
SEM_API const char* SEM_SerializeOffsets(const char* output_dir);

// ---------------------------------------------------------------------------
// Task 4 — Constrained-Delaunay triangulation of the band spanned by the
// computed offset lines.
//
// Triangulates the region between the two extreme offset lines, conforming to
// every intermediate line, and inserts Steiner points (grid spacing =
// avg_edge_len) so the band is filled with well-shaped triangles instead of
// long slivers. For a closed source contour the band is the ring between the
// innermost and outermost loop (inner hole excluded); for an open source it is
// the strip between the first and last polyline.
//
// Each vertex's T-coordinate is its minimum distance to the original source
// contour, linearly normalized to [0, 1] across the mesh.
//
// The mesh is cached (reused on repeated calls until offsets are recomputed)
// and written to "<stem>_mesh.csv3d" with #face triangle rows.
//
//  output_dir   directory for the output file (NULL or "" → system temp dir).
//
// Returns the absolute path to the written file (pointer to a static buffer —
// copy immediately; invalidated by the next call that returns a path).
// Returns NULL on error (no source loaded, fewer than 2 offset lines,
// triangulation or write failure).
SEM_API const char* SEM_Triangulate(const char* output_dir);
