#pragma once
#include "primitives\primitive.h"
#include "..\..\external\json.hpp"
using json = nlohmann::json;

namespace jsonSaver {
    inline void to_json(json& j, const Primitive& p) {
        j["scalable"] = p.GetScalable();
        j["scale"] = p.GetScale();
        j["dimension"] = p.GetDimension();
        j["primitiveTopology"] = p.GetPrimitiveTopology();
        j["illuminationCapability"] = p.GetIlluminationCapability();

        XMFLOAT3 pos = p.GetPosition();
        XMFLOAT3 rot = p.GetRotation();
        XMFLOAT4 col = p.GetColor();
        j["position"] = { pos.x, pos.y, pos.z };
        j["rotation"] = { rot.x, rot.y, rot.z };
        j["color"] = { col.x, col.y, col.z, col.w };

        std::vector<Vertex> vertices = p.GetVertexData();
        std::vector<DWORD> indices = p.GetIndexData();
        std::vector<UINT> uintIndices(
            reinterpret_cast<const UINT*>(indices.data()),
            reinterpret_cast<const UINT*>(indices.data()) + indices.size()
        );

        for (size_t i = 0; i < vertices.size(); ++i) {
            j["vertices"][i] = { vertices[i].pos.x, vertices[i].pos.y, vertices[i].pos.z, 
                                 vertices[i].normal.x, vertices[i].normal.y, vertices[i].normal.z };
        }
        for (size_t i = 0; i < uintIndices.size(); ++i) {
            j["indices"][i] = uintIndices[i];
        }
    }

    inline bool from_json(const json& j, Primitive& p) {
        p.SetScalable(j["scalable"]);
        p.SetScale(j["scale"]);
        p.SetPrimitiveTopology(j["primitiveTopology"]);
        p.SetIlluminationCapability(j["illuminationCapability"]);

        XMFLOAT3 pos = XMFLOAT3(j["position"][0], j["position"][1], j["position"][2]);
        XMFLOAT3 rot = XMFLOAT3(j["rotation"][0], j["rotation"][1], j["rotation"][2]);
        XMFLOAT4 col = XMFLOAT4(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
        p.SetPosition(pos);
        p.SetRotation(rot);
        p.SetColor(col);

        if (!j["vertices"].is_array()) {
            return false;
        }
        std::vector<Vertex> vertices(j["vertices"].size());
        for (size_t i = 0; i < j["vertices"].size(); ++i) {
            vertices[i] = Vertex(XMFLOAT3(j["vertices"][i][0], j["vertices"][i][1], j["vertices"][i][2]), XMFLOAT3(j["vertices"][i][3], j["vertices"][i][4], j["vertices"][i][5]));
        }

        std::vector<DWORD> indices(j["indices"].size());
        if (j["indices"].is_array()) {
            for (size_t i = 0; i < j["indices"].size(); ++i) {
                indices[i] = j["indices"][i];
            }
        }

        p.SetVertexIndexBuffers(vertices.data(), vertices.size(), indices.data(), indices.size(), j["dimension"]);

        return true;
    }
}
