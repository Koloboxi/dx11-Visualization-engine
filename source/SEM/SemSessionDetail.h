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
int SafeExtractIsosurface3D(double value, int axis, double offset_value);
int SafeFlipIsosurface3D();

// Verbose detail for the most recent SEM_* failure (SEM_GetLastError), formatted
// as " — <text>" for appending to a status message, or "" when none. MUST be
// read immediately after the failing call — the library overwrites it on the next.
std::string SemDetail();

} // namespace detail
} // namespace SemSessionNS
