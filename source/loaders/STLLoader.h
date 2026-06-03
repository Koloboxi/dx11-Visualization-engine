#pragma once
#include "..\graphics\scene\primitives\primitive.h"
#include <fstream>
#include <sstream>
#include <cstring>

namespace STLLoader {

    struct Triangle {
        XMFLOAT3 normal;
        XMFLOAT3 v[3];
    };

    inline bool LoadBinary(const std::string& path, std::vector<Triangle>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

        char header[80];
        f.read(header, 80);

        uint32_t count = 0;
        f.read(reinterpret_cast<char*>(&count), 4);
        if (!f || count == 0) return false;

        out.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            f.read(reinterpret_cast<char*>(&out[i].normal), 12);
            f.read(reinterpret_cast<char*>(&out[i].v[0]), 12);
            f.read(reinterpret_cast<char*>(&out[i].v[1]), 12);
            f.read(reinterpret_cast<char*>(&out[i].v[2]), 12);
            uint16_t attr; f.read(reinterpret_cast<char*>(&attr), 2);
        }
        return !out.empty();
    }

    inline bool LoadASCII(const std::string& path, std::vector<Triangle>& out) {
        std::ifstream f(path);
        if (!f) return false;

        std::string line, token;
        Triangle tri{};
        int vIdx = 0;
        bool inFacet = false;

        while (std::getline(f, line)) {
            std::istringstream iss(line);
            iss >> token;
            if (token == "facet") {
                std::string _normal;
                iss >> _normal >> tri.normal.x >> tri.normal.y >> tri.normal.z;
                inFacet = true; vIdx = 0;
            }
            else if (token == "vertex" && inFacet && vIdx < 3) {
                iss >> tri.v[vIdx].x >> tri.v[vIdx].y >> tri.v[vIdx].z;
                ++vIdx;
            }
            else if (token == "endfacet" && inFacet) {
                out.push_back(tri);
                inFacet = false;
            }
        }
        return !out.empty();
    }

    inline Primitive* Load(const std::string& path, const XMFLOAT4& col, UINT id) {
        std::ifstream test(path, std::ios::binary);
        if (!test) return nullptr;

        char header[6] = {};
        test.read(header, 5);
        test.close();

        std::vector<Triangle> tris;
        bool ascii = (strncmp(header, "solid", 5) == 0);

        if (ascii) {
            if (!LoadASCII(path, tris) || tris.empty()) {
                tris.clear();
                LoadBinary(path, tris);
            }
        }
        else {
            LoadBinary(path, tris);
        }

        if (tris.empty()) return nullptr;

        std::vector<Vertex> vertices;
        std::vector<DWORD> indices;
        vertices.reserve(tris.size() * 3);
        indices.reserve(tris.size() * 3);

        for (const Triangle& tri : tris) {
            XMFLOAT3 n = tri.normal;
            XMVECTOR nv = XMLoadFloat3(&n);
            if (XMVectorGetX(XMVector3LengthSq(nv)) < 1e-6f) {
                XMVECTOR v0 = XMLoadFloat3(&tri.v[0]);
                XMVECTOR v1 = XMLoadFloat3(&tri.v[1]);
                XMVECTOR v2 = XMLoadFloat3(&tri.v[2]);
                nv = XMVector3Normalize(XMVector3Cross(v1 - v0, v2 - v0));
                XMStoreFloat3(&n, nv);
            }
            for (int i = 0; i < 3; ++i) {
                indices.push_back(static_cast<DWORD>(vertices.size()));
                vertices.push_back(Vertex(tri.v[i], n));
            }
        }

        Primitive* prim = new Primitive(PrimitiveConstructor::device, PrimitiveConstructor::deviceContext);
        prim->SetVertexIndexBuffers(vertices.data(), static_cast<UINT>(vertices.size()),
            indices.data(), static_cast<UINT>(indices.size()), 2);
        prim->SetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        prim->SetPosition(BaseVectors::ORIGIN);
        prim->SetColor(col);
        prim->SetIlluminationCapability(true);
        prim->id = id;
        return prim;
    }
}
