#pragma once
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <DirectXMath.h>
using namespace DirectX;

// Loads a subset of the Wavefront OBJ format:
//   v  x y z          — vertex position
//   f  v[/vt[/vn]]... — polygon face (vertex indices 1-based, /vt and /vn suffixes ignored)
//   l  v v ...        — line strip (vertex indices 1-based)
// Lines starting with '#' and unrecognised tokens are ignored.

namespace OBJLoader {

struct OBJData {
    std::vector<XMFLOAT3>              vertices; // all declared vertices
    std::vector<std::vector<int>>      faces;    // each face: 0-based vertex indices
    std::vector<std::vector<int>>      lines;    // each l-element: 0-based vertex indices
};

namespace detail {
    // Parse the vertex index from "v", "v/vt", "v/vt/vn", "v//vn"
    inline int ParseVI(const std::string& token, int numVerts) {
        int vi = std::stoi(token.substr(0, token.find('/')));
        return vi > 0 ? vi - 1 : numVerts + vi; // 1-based → 0-based; negative = relative
    }
}

inline bool Load(const std::string& path, OBJData& out) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "v") {
            float x = 0, y = 0, z = 0;
            iss >> x >> y >> z;
            out.vertices.push_back({x, y, z});
        }
        else if (token == "f") {
            std::vector<int> face;
            std::string idx;
            while (iss >> idx)
                face.push_back(detail::ParseVI(idx, (int)out.vertices.size()));
            if (face.size() >= 2) out.faces.push_back(std::move(face));
        }
        else if (token == "l") {
            std::vector<int> strip;
            std::string idx;
            while (iss >> idx)
                strip.push_back(detail::ParseVI(idx, (int)out.vertices.size()));
            if (strip.size() >= 2) out.lines.push_back(std::move(strip));
        }
    }

    return !out.vertices.empty();
}

} // namespace OBJLoader
