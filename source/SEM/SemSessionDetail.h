#pragma once
#include "SemSession.h"
#include "../loaders/CSV3DLoader.h"
#include "../external/sem_exports.h"

// Internal free helpers shared across the SemSession translation units. They
// carry no SemSession state and operate purely on files / SEM views.
namespace SemSessionNS {
namespace detail {

std::string BaseName(const std::string& path);
std::string Stem(const std::string& path);

Stats ComputeStatsData(const CSV3DLoader::CSV3DData& d);
Stats ComputeStats(const std::string& path);

// Marshal a read-only SEM_MeshView (cache-owned flat arrays) into a CSV3DData so
// the scene loaders can build a primitive from memory. The view's pointers are
// valid only until the next SEM_* call, so everything is copied out.
CSV3DLoader::CSV3DData ViewToData(const SEM_MeshView& v);

// Copy the per-vertex normals (3 * num_nodes interleaved doubles) of a
// SEM_MeshView into one unit vector per node. Returns empty when the view has no
// normals (v.normals == null) — only the Surface-backed views (source surface /
// source isosurface) carry them. Valid only until the next SEM_* call, like the view.
std::vector<XMFLOAT3> ViewNormals(const SEM_MeshView& v);

// Angle-weighted ("pseudo") vertex normals of a triangle surface, one unit
// vector per node (Thürmer-Wüthrich: each incident face contributes its unit
// normal weighted by the interior angle at the vertex). Nodes with no incident
// triangle (or a degenerate fan) get a zero vector.
std::vector<XMFLOAT3> VertexPseudonormals(const CSV3DLoader::CSV3DData& d);

// Mean length of the triangle edges of a surface; 1.0 when it has no triangles.
// Used to size the drawn pseudonormal segments to the model.
float MeanTriEdgeLen(const CSV3DLoader::CSV3DData& d);

// Per-triangle "minority winding" flags. A consistent orientation is propagated
// across the triangle adjacency graph (two triangles sharing an edge agree when
// that edge runs opposite ways in them); within each connected component the
// smaller orientation class is the minority. Returns true for every triangle
// that disagrees with its component's majority — i.e. all false when the surface
// is uniformly wound. data.nodes is unused (topology only).
std::vector<char> WindingMinorityTris(const CSV3DLoader::CSV3DData& d);

bool SourceBBox(const std::string& path, XMFLOAT3& lo, XMFLOAT3& hi);
bool OrderedContourFromCSV3D(const std::string& path, std::vector<XMFLOAT3>& out);
bool IsOpenContourOnYAxis(const std::string& path);
int  DetectSemDim(const std::string& path);
bool MeshFileHasTField(const std::string& path);

// SEH guards around the meshing/solver entry points: the bundled Triangle and
// TetGen libraries can raise access violations C++ try/catch cannot intercept.
// The wrappers return -100 so the host app survives.
int SafeBuildMesh(const SEM_MeshParams* params);
int SafeBuildMesh3D(const SEM_MeshParams3D* params);
int SafeSolveThermal();
int SafeSolveThermal3D(float max_inward);
int SafeExtractIsoline(double value);
int SafeExtractIsosurface3D(double value, int axis, double offset_value, double min_offset_value,
                            double target_len_mult, int iterations);
// Standalone offset-and-remesh of an open surface passed in as raw arrays (does NOT
// touch the loaded pipeline cache); the result comes back through `out` (valid until
// the next standalone call). See SEM_OffsetRemeshInPlaneSurface3D.
int SafeOffsetRemeshInPlaneSurface3D(int axis, double offset_value,
                                     const double* xyz, int num_nodes,
                                     const int* tris, int num_tris,
                                     double min_offset_value,
                                     double target_len_mult, int iterations,
                                     SEM_MeshView* out);

// Verbose detail for the most recent SEM_* failure (SEM_GetLastError), formatted
// as " — <text>" for appending to a status message, or "" when none. MUST be
// read immediately after the failing call — the library overwrites it on the next.
std::string SemDetail();

} // namespace detail
} // namespace SemSessionNS
