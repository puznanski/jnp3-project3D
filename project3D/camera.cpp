#include <algorithm>
#include "camera.h"

DirectX::XMMATRIX Camera::get_projection_matrix() {
    DirectX::XMVECTOR base_vector = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    auto look_vector = DirectX::XMVector3Transform(
            base_vector,
            DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f)
    );
    auto camera_position = DirectX::XMLoadFloat3(&position);
    auto camera_target = DirectX::XMVectorAdd(camera_position, look_vector);
    return DirectX::XMMatrixLookAtLH(camera_position, camera_target, DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

void Camera::rotate(float delta_mouse_x, float delta_mouse_y) {
    pitch = std::clamp(pitch + delta_mouse_y * ROTATION_SPEED, -DirectX::XM_PI * 0.995f / 2.0f, DirectX::XM_PI * 0.995f / 2.0f);
    yaw += delta_mouse_x * ROTATION_SPEED;

    while (yaw >= 2 * DirectX::XM_PI) {
        yaw -= 2 * DirectX::XM_PI;
    }

    while (yaw < 0.0f) {
        yaw += 2 * DirectX::XM_PI;
    }
}

void Camera::move(DirectX::XMFLOAT3 translation) {
    DirectX::XMFLOAT3 movement = {};

    DirectX::XMStoreFloat3(&movement, DirectX::XMVector3Transform(
            DirectX::XMLoadFloat3(&translation),
            DirectX::XMMatrixMultiply(
                    DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f),
                    DirectX::XMMatrixScaling(MOVE_SPEED, MOVE_SPEED, MOVE_SPEED)
            )
    ));

    position.x += movement.x;
    position.y += movement.y;
    position.z += movement.z;
}

void Camera::reset() {
    yaw = DEF_YAW;
    pitch = DEF_PITCH;
    position = DEF_POSITION;
}
