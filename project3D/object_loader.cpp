#include "object_loader.h"

#include <iostream>
#include <fstream>
#include <utility>

using Position = DirectX::XMFLOAT3;
using UV = DirectX::XMFLOAT2;

std::vector<std::string> ObjectLoader::split(const std::string& str, const std::string& delimiter) {
    std::size_t start_position = 0;
    std::size_t end_position;
    std::size_t delimiter_length = delimiter.length();
    std::string token;
    std::vector<std::string> result;

    while ((end_position = str.find(delimiter, start_position)) != std::string::npos) {
        token = str.substr(start_position, end_position - start_position);
        start_position = end_position + delimiter_length;
        result.push_back(token);
    }

    result.push_back(str.substr(start_position));
    return result;
}

ObjectLoader::ObjectLoader(std::string uri, DirectX::XMFLOAT4 color) : uri(std::move(uri)), color(color) {}

HRESULT ObjectLoader::load() {
    HRESULT hr = S_OK;
    std::string line;
    std::vector<Position> vertices;
    std::vector<Position> normals;
    std::vector<UV> texture_coordinates;

    std::ifstream obj_file(uri + ".obj");

    if (obj_file.is_open()) {
        while (obj_file.good()) {
            std::getline(obj_file, line);
            auto line_split = split(line, " ");

            if (line_split[0] == "v") {
                vertices.emplace_back(
                        std::stof(line_split[1]) * -1.0f,
                        std::stof(line_split[2]),
                        std::stof(line_split[3]) * -1.0f
                );
            }
            else if (line_split[0] == "vt") {
                texture_coordinates.emplace_back(
                        std::stof(line_split[1]),
                        std::stof(line_split[2]) * -1.0f
                );
            }
            else if (line_split[0] == "vn") {
                normals.emplace_back(
                        std::stof(line_split[1]) * -1.0f,
                        std::stof(line_split[2]),
                        std::stof(line_split[3])
                );
            }
            else if (line_split[0] == "f") {
                for (std::size_t i = 1; i < line_split.size(); i++) {
                    auto vertex = split(line_split[i], "/");

                    mesh.push_back({
                            vertices[std::stoi(vertex[0]) - 1],
                            normals[std::stoi(vertex[2]) - 1],
                            color,
                            texture_coordinates[std::stoi(vertex[1]) - 1]
                    });
                }
            }
        }
    }
    else {
        hr = E_FAIL;
    }

    obj_file.close();

    if (SUCCEEDED(hr)) {
        std::ifstream mtl_file(uri + ".mtl");

        if (mtl_file.is_open()) {
            while (mtl_file.good()) {
                std::getline(mtl_file, line);
                auto line_split = split(line, " ");

                if (line_split[0] == "map_Kd") {
                    texture_name = line_split[1];
                }
            }
        }
        else {
            hr = E_FAIL;
        }

        mtl_file.close();
    }

    return hr;
}

std::vector<Vertex> ObjectLoader::get_vertices() {
    return mesh;
}

std::wstring ObjectLoader::get_texture_uri() {
    auto path = split(uri, "\\");
    std::string result;

    for (std::size_t i = 0; i < path.size() - 1; i++) {
        result += (path[i] + "\\");
    }

    result += texture_name;

    return {result.begin(), result.end()};
}

std::size_t ObjectLoader::get_number_of_vertices() {
    return mesh.size();
}
