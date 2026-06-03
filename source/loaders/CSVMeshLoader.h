#pragma once
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <DirectXMath.h>
using namespace DirectX;

// CSV mesh format (.csv)
//
// Each row is:  type , val , val , ...
// Lines starting with '#' are comments and are ignored.
//
//   v , x , y , z          — vertex position (floats)
//   f , i , j , k , ...    — polygon face; vertex indices are 1-based
//   l , i , j , k , ...    — line strip;   vertex indices are 1-based
//
// Negative indices are not supported. Whitespace around commas is ignored.
// Empty rows are skipped.
//
// Example:
//   # triangle in XY plane
//   v, 0.0, 0.0, 0.0
//   v, 1.0, 0.0, 0.0
//   v, 0.5, 1.0, 0.0
//   f, 1, 2, 3
//   l, 1, 2, 3, 1

namespace CSVMeshLoader {

struct CSVMeshData {
    std::vector<XMFLOAT3>         vertices;
    std::vector<std::vector<int>> faces;    // 0-based indices
    std::vector<std::vector<int>> lines;    // 0-based indices
};

namespace detail {
    inline std::vector<std::string> SplitCSV(const std::string& row) {
        std::vector<std::string> tokens;
        std::string tok;
        for (char c : row) {
            if (c == ',') { tokens.push_back(tok); tok.clear(); }
            else if (c != ' ' && c != '\t' && c != '\r') tok += c;
        }
        if (!tok.empty()) tokens.push_back(tok);
        return tokens;
    }
}

inline bool Load(const std::string& path, CSVMeshData& out) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto tok = detail::SplitCSV(line);
        if (tok.empty()) continue;

        const std::string& type = tok[0];

        if (type == "v" && tok.size() >= 4) {
            float x = std::stof(tok[1]);
            float y = std::stof(tok[2]);
            float z = std::stof(tok[3]);
            out.vertices.push_back({x, y, z});
        }
        else if (type == "f" && tok.size() >= 3) {
            std::vector<int> face;
            for (size_t i = 1; i < tok.size(); ++i)
                face.push_back(std::stoi(tok[i]) - 1); // 1-based → 0-based
            if (face.size() >= 2) out.faces.push_back(std::move(face));
        }
        else if (type == "l" && tok.size() >= 3) {
            std::vector<int> strip;
            for (size_t i = 1; i < tok.size(); ++i)
                strip.push_back(std::stoi(tok[i]) - 1);
            if (strip.size() >= 2) out.lines.push_back(std::move(strip));
        }
    }

    return !out.vertices.empty();
}

} // namespace CSVMeshLoader
