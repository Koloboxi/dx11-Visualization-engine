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

enum SEM_SteinerMethod {
    SEM_STEINER_GRID = 0,
    SEM_STEINER_NONE = 1,
    SEM_STEINER_MIN_ANGLE = 2,
    SEM_STEINER_MAX_AREA = 3,
    SEM_STEINER_CONFORMING = 4,
    SEM_STEINER_SIZING = 5
};

struct SEM_MeshParams {
    int    method;
    double param;

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

enum SEM_TetMethod {
    SEM_TET_QUALITY = 0,
    SEM_TET_NONE = 1,
    SEM_TET_MAX_VOL = 2,
    SEM_TET_SIZING = 3
};

struct SEM_MeshParams3D {
    int    method;
    double param;

};

SEM_API int SEM_BuildMesh3D(const SEM_MeshParams3D* params);

// Reload previously serialized 3D stages into the cache (skip recomputation).
// A source surface must already be loaded (SEM_LoadSurface3D). SEM_LoadOffsets3D
// reads <stem>_offset3d_<i>.csv3d (i = 0..) from `dir` (null/empty = current
// working dir); SEM_LoadMesh3D reads one <..>_mesh3d.csv3d (must contain #tets).
SEM_API int SEM_LoadOffsets3D(const char* dir);

SEM_API int SEM_LoadMesh3D(const char* path);

SEM_API int SEM_SolveThermal3D(void);

SEM_API int SEM_ExtractIsosurface3D(double value);

SEM_API float SEM_GetProgress(void);
