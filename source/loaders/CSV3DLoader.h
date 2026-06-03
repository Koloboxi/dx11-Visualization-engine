#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <utility>
#include <DirectXMath.h>
using namespace DirectX;

// CSV3D mesh format (.csv3d)
//
// Section-based, semicolon-separated. Sections are introduced by a line
// starting with '#'. Recognised sections:
//
//   #nodes
//   node_id;x;y;z;T          — one node per row; a header row
//                              "node_id;x;y;z;T" may precede the data and is
//                              skipped. node_id is 0-based. T is a scalar
//                              temperature, expected in [0, 1].
//   #triangles
//   i;j;k                    — triangle by node ids (0-based)
//   #face[;count]
//   i;j;k;l;...              — polygon face by node ids (0-based, any arity)
//   #edge[;count]
//   i;j                     — explicit edge by node ids (0-based)
//
// The optional number after a section tag (e.g. "#face;4") is an element
// count hint and is ignored — rows are read until the next section header.
//
// The loader stores nodes indexed by node_id, triangles as index triples,
// faces as variable-length index lists, and edges as index pairs.

namespace CSV3DLoader {

struct Node {
    XMFLOAT3 pos;
    float    T;
};

struct CSV3DData {
    std::vector<Node>                       nodes;        // indexed by node_id (0-based)
    std::vector<XMUINT3>                    triangles;    // node-id triples (0-based)
    std::vector<std::vector<unsigned>>      faces;        // polygon faces (0-based)
    std::vector<std::pair<unsigned, unsigned>> edges;     // explicit edges (0-based)
};

namespace detail {
    inline std::vector<std::string> SplitSemi(const std::string& row) {
        std::vector<std::string> tokens;
        std::string tok;
        for (char c : row) {
            if (c == ';') { tokens.push_back(tok); tok.clear(); }
            else if (c != ' ' && c != '\t' && c != '\r') tok += c;
        }
        tokens.push_back(tok);
        return tokens;
    }

    inline bool IsNumber(const std::string& s) {
        if (s.empty()) return false;
        char c = s[0];
        return (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9'));
    }
}

inline bool Load(const std::string& path, CSV3DData& out) {
    std::ifstream f(path);
    if (!f) return false;

    enum class Section { None, Nodes, Triangles, Faces, Edges, Other };
    Section section = Section::None;

    std::string line;
    while (std::getline(f, line)) {
        // Trim leading whitespace for the '#' check.
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;     // blank line

        if (line[start] == '#') {
            std::string tag = line.substr(start + 1);
            // keep only up to first ';' or space so "#face;4" → "face"
            size_t cut = tag.find_first_of("; \t\r");
            if (cut != std::string::npos) tag = tag.substr(0, cut);
            if (tag == "nodes")          section = Section::Nodes;
            else if (tag == "triangles") section = Section::Triangles;
            else if (tag == "face")      section = Section::Faces;
            else if (tag == "edge")      section = Section::Edges;
            else                          section = Section::Other; // ignore unknown sections
            continue;
        }

        if (section == Section::Nodes) {
            auto tok = detail::SplitSemi(line);
            if (tok.size() < 5) continue;
            if (!detail::IsNumber(tok[1])) continue;   // skip header row
            Node n;
            n.pos = { std::stof(tok[1]), std::stof(tok[2]), std::stof(tok[3]) };
            n.T   = std::stof(tok[4]);
            out.nodes.push_back(n);
        }
        else if (section == Section::Triangles) {
            auto tok = detail::SplitSemi(line);
            if (tok.size() < 3) continue;
            if (!detail::IsNumber(tok[0])) continue;
            unsigned a = (unsigned)std::stoul(tok[0]);
            unsigned b = (unsigned)std::stoul(tok[1]);
            unsigned c = (unsigned)std::stoul(tok[2]);
            out.triangles.push_back({ a, b, c });
        }
        else if (section == Section::Faces) {
            auto tok = detail::SplitSemi(line);
            if (tok.size() < 2) continue;
            if (!detail::IsNumber(tok[0])) continue;
            std::vector<unsigned> face;
            face.reserve(tok.size());
            for (const auto& t : tok)
                if (detail::IsNumber(t)) face.push_back((unsigned)std::stoul(t));
            if (face.size() >= 2) out.faces.push_back(std::move(face));
        }
        else if (section == Section::Edges) {
            auto tok = detail::SplitSemi(line);
            if (tok.size() < 2) continue;
            if (!detail::IsNumber(tok[0])) continue;
            unsigned a = (unsigned)std::stoul(tok[0]);
            unsigned b = (unsigned)std::stoul(tok[1]);
            out.edges.push_back({ a, b });
        }
        // Section::Other / None → ignore
    }

    return !out.nodes.empty();
}

} // namespace CSV3DLoader
