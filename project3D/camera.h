#ifndef PROJECT3D_CAMERA_H
#define PROJECT3D_CAMERA_H

#include <DirectXMath.h>

class Camera {
public:
    DirectX::XMMATRIX get_projection_matrix();
    void rotate(float delta_mouse_x, float delta_mouse_y);
    void move(DirectX::XMFLOAT3 translation);
    void reset();

private:
    static constexpr float DEF_PITCH = 0.0f;
    static constexpr float DEF_YAW = 0.0f;
    static constexpr DirectX::XMFLOAT3 DEF_POSITION = {0.0f, 0.0f, 0.0f};
    static constexpr float ROTATION_SPEED = 0.01f;
    static constexpr float MOVE_SPEED = 0.1f;

    float pitch = DEF_PITCH;
    float yaw = DEF_YAW;
    DirectX::XMFLOAT3 position = DEF_POSITION;
};

#endif //PROJECT3D_CAMERA_H
