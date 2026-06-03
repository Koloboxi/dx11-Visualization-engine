#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <DirectXMath.h>
using namespace DirectX;

// Binary 2D grid format (.grid2d)
//
// Offset  Size  Field
// 0       4     magic = "G2DB"
// 4       4     uint32  rows   (>= 2)
// 8       4     uint32  cols   (>= 2)
// 12      12*N  float32 x,y,z per vertex, row-major (N = rows*cols)
//
// Vertices are ordered [row0col0, row0col1, ..., row(R-1)col(C-1)].
// Lines are implied between adjacent vertices (horizontal and vertical edges).

namespace Grid2DLoader {

struct GridData {
    uint32_t rows = 0;
    uint32_t cols = 0;
    std::vector<XMFLOAT3> vertices; // size = rows * cols
};

inline bool Load(const std::string& path, GridData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4] = {};
    f.read(magic, 4);
    if (std::strncmp(magic, "G2DB", 4) != 0) return false;

    uint32_t rows = 0, cols = 0;
    f.read(reinterpret_cast<char*>(&rows), 4);
    f.read(reinterpret_cast<char*>(&cols), 4);
    if (!f || rows < 2 || cols < 2) return false;

    out.vertices.resize(rows * cols);
    for (uint32_t i = 0; i < rows * cols; ++i) {
        f.read(reinterpret_cast<char*>(&out.vertices[i].x), 4);
        f.read(reinterpret_cast<char*>(&out.vertices[i].y), 4);
        f.read(reinterpret_cast<char*>(&out.vertices[i].z), 4);
    }
    if (!f) return false;

    out.rows = rows;
    out.cols = cols;
    return true;
}

} // namespace Grid2DLoader
