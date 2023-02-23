#ifndef PROJECT3D_COMMON_H
#define PROJECT3D_COMMON_H

#include <DirectXMath.h>

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT2 texture_coordinates;
};

#endif //PROJECT3D_COMMON_H
