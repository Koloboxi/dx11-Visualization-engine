#pragma once
#include "primitives\primitive.h"
#include "..\..\external\json.hpp"
using json = nlohmann::json;

namespace jsonSaver {
    inline void to_json(json& j, const Primitive& p) {
        j["id"] = p.id;
        if (!p.name.empty()) j["name"] = p.name;
        j["scale"] = p.GetScale();
        j["dimension"] = p.GetDimension();
        j["primitiveTopology"] = p.GetPrimitiveTopology();
        j["illuminationCapability"] = p.GetIlluminationCapability();

        XMFLOAT3 pos = p.GetPosition();
        XMFLOAT4 rot = p.GetRotation();
        XMFLOAT4 col = p.GetColor();
        j["position"] = { pos.x, pos.y, pos.z };
        j["rotation"] = { rot.x, rot.y, rot.z, rot.w };
        j["color"] = { col.x, col.y, col.z, col.w };

        if (!p.luaScript.empty()) j["luaScript"] = p.luaScript;
        if (p.mass != 1.0f) j["mass"] = p.mass;

        std::vector<Vertex> vertices = p.GetVertexData();
        std::vector<DWORD> indices = p.GetIndexData();

        for (size_t i = 0; i < vertices.size(); ++i) {
            j["vertices"][i] = {
                vertices[i].pos.x, vertices[i].pos.y, vertices[i].pos.z,
                vertices[i].normal.x, vertices[i].normal.y, vertices[i].normal.z
            };
        }
        for (size_t i = 0; i < indices.size(); ++i) {
            j["indices"][i] = static_cast<UINT>(indices[i]);
        }
    }

    inline bool from_json(const json& j, Primitive& p) {
        if (j.contains("id")) p.id = j["id"].get<UINT>();
        if (j.contains("name")) p.name = j["name"].get<std::string>();
        if (j.contains("luaScript")) p.luaScript = j["luaScript"].get<std::string>();
        p.mass = j.value("mass", 1.0f);

        p.SetScale(j.value("scale", 1.0f));
        p.SetPrimitiveTopology(j["primitiveTopology"].get<D3D10_PRIMITIVE_TOPOLOGY>());
        p.SetIlluminationCapability(j["illuminationCapability"].get<bool>());

        XMFLOAT3 pos(j["position"][0], j["position"][1], j["position"][2]);
        XMFLOAT4 rot(j["rotation"][0], j["rotation"][1], j["rotation"][2], j["rotation"][3]);
        XMFLOAT4 col(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
        p.SetPosition(pos);
        p.SetRotation(rot);
        p.SetColor(col);

        if (!j.contains("vertices") || !j["vertices"].is_array()) return false;

        std::vector<Vertex> vertices(j["vertices"].size());
        for (size_t i = 0; i < j["vertices"].size(); ++i) {
            vertices[i] = Vertex(
                XMFLOAT3(j["vertices"][i][0], j["vertices"][i][1], j["vertices"][i][2]),
                XMFLOAT3(j["vertices"][i][3], j["vertices"][i][4], j["vertices"][i][5])
            );
        }

        std::vector<DWORD> indices;
        if (j.contains("indices") && j["indices"].is_array()) {
            indices.resize(j["indices"].size());
            for (size_t i = 0; i < j["indices"].size(); ++i)
                indices[i] = j["indices"][i].get<DWORD>();
        }

        p.SetVertexIndexBuffers(
            vertices.data(), static_cast<UINT>(vertices.size()),
            indices.empty() ? nullptr : indices.data(), static_cast<UINT>(indices.size()),
            j["dimension"].get<UCHAR>()
        );

        return true;
    }
}
