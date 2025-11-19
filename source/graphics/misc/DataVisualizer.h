#pragma once
#include "..\..\external\json.hpp"
#include <filesystem>
#include <fstream>

const std::string filePath = "Data/Datasets/";


namespace AdministrativeBoundaries{
    inline nlohmann::json GetJsonData(const std::string & fileName) {
        auto fullPath = std::filesystem::path(filePath) / fileName;

        if (!std::filesystem::exists(fullPath)) {
            return {};
        }

        std::ifstream file(fullPath);
        if (!file.is_open()) {
            return {};
        }

        try {
            return nlohmann::json::parse(file);
        }
        catch (const nlohmann::json::parse_error& e) {
            return {};
        }
    }

    inline std::vector<std::pair<float, float>> ExtractCoords(nlohmann::json jsonData) {
        std::vector<std::pair<float, float>> result{};

        if (!jsonData.is_array()) {
            return result;
        }

        for (const auto& point : jsonData) {
            if (point.size() != 2 || !point[0].is_number() || !point[1].is_number()) {
                return result;
            }
            result.emplace_back(point[0].get<float>(), point[1].get<float>());
        }

        return result;
    }

    inline std::vector<XMFLOAT3> GetLinestripFromCoords(std::vector<std::pair<float, float>>&coords) {
        std::vector<XMFLOAT3> poses;
        for (auto point : coords) {
            poses.push_back(XMFLOAT3(point.first, point.second, 0));
        }
        return poses;
    }

    inline void ToSphere(std::vector<XMFLOAT3>&coords, float radius) {
        for (size_t i = 0; i < coords.size(); ++i) {
            float longitude = coords[i].x * XM_2PI / 360.f;
            float latitude = coords[i].y * XM_2PI / 360.f;

            coords[i].x = radius * cos(latitude) * cos(longitude);
            coords[i].y = radius * cos(latitude) * sin(longitude);
            coords[i].z = radius * sin(latitude);
        }
    }

    inline std::vector<std::vector<XMFLOAT3>> GetBoudaries() {
        nlohmann::json data = GetJsonData("world-administrative-boundaries.json");
        std::vector<std::vector<XMFLOAT3>> lineStrips{};

        for (size_t i = 0; i < data.size(); ++i) {
            nlohmann::json coordinatesData = data[i]["geo_shape"]["geometry"]["coordinates"];
            if (coordinatesData.size() > 1) {
                for (size_t j = 0; j < coordinatesData.size(); ++j) {
                    std::vector<std::pair<float, float>> coords = ExtractCoords(coordinatesData[j][0]);
                    std::vector<XMFLOAT3> poses = GetLinestripFromCoords(coords);
                    ToSphere(poses, 100);
                    lineStrips.push_back(poses);
                    coords = ExtractCoords(coordinatesData[j]);
                    poses = GetLinestripFromCoords(coords);
                    ToSphere(poses, 100);
                    lineStrips.push_back(poses);
                }
            }
            else {
                std::vector<std::pair<float, float>> coords = ExtractCoords(coordinatesData[0]);
                std::vector<XMFLOAT3> poses = GetLinestripFromCoords(coords);
                ToSphere(poses, 100);
                lineStrips.push_back(poses);
            }
        }
        return lineStrips;
    }
}
