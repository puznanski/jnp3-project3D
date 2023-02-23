#ifndef PROJECT3D_OBJECT_LOADER_H
#define PROJECT3D_OBJECT_LOADER_H

#include <string>
#include <vector>
#include <winerror.h>
#include <DirectXMath.h>
#include "common.h"

class ObjectLoader {
public:
    ObjectLoader(std::string uri, DirectX::XMFLOAT4 color);
    HRESULT load();
    std::vector<Vertex> get_vertices();
    std::wstring get_texture_uri();
    std::size_t get_number_of_vertices();

private:
    const std::string uri;
    const DirectX::XMFLOAT4 color;
    std::vector<Vertex> mesh;
    std::string texture_name;

    static std::vector<std::string> split(const std::string& str, const std::string& delimiter);
};

#endif //PROJECT3D_OBJECT_LOADER_H
