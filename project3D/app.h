#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <wrl.h>
#include <shellapi.h>
#include "d3dx12.h"

template<class Interface>
inline void SafeRelease(
        Interface **ppInterfaceToRelease) {
    if (*ppInterfaceToRelease != NULL) {
        (*ppInterfaceToRelease)->Release();
        (*ppInterfaceToRelease) = NULL;
    }
}

class App {
public:
    App(UINT width, UINT height, std::wstring name);

    ~App();

    HRESULT Initialize(HINSTANCE instance, INT cmd_show);

    void RunMessageLoop();

private:
    static const UINT FRAME_COUNT = 2;

    struct Vertex {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
    };

    struct ConstantBuffer {
        DirectX::XMFLOAT4X4 mat_world_view_proj;
        DirectX::XMFLOAT4 padding[(256 - sizeof(DirectX::XMFLOAT4X4)) / sizeof(DirectX::XMFLOAT4)];
    };

    HWND hwnd;

    UINT width;
    UINT height;
    FLOAT aspect_ratio;
    bool use_warp_device;
    std::wstring title;

    // Pipeline objects
    CD3DX12_VIEWPORT viewport;
    CD3DX12_RECT scissor_rect;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Resource> render_targets[FRAME_COUNT];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbv_heap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    UINT rtv_descriptor_size;

    // App resources
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
    Microsoft::WRL::ComPtr<ID3D12Resource> constant_buffer;
    ConstantBuffer constant_buffer_data;
    UINT8* constant_buffer_data_begin;


    // Synchronization objects
    UINT frame_index;
    HANDLE fence_event;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fence_value;

    HRESULT LoadPipeline();
    HRESULT LoadAssets();
    HRESULT PopulateCommandList();
    HRESULT WaitForPreviousFrame();

    void GetHardwareAdapter(
            _In_ IDXGIFactory1* factory,
            _Outptr_result_maybenull_ IDXGIAdapter1** adapter,
            bool request_high_performance_adapter = false
    );

    HRESULT OnInit();
    HRESULT OnUpdate();
    HRESULT OnRender();
    HRESULT OnDestroy();
    HRESULT OnAnimationStep();

    void OnResize(
            UINT width,
            UINT height
    );

    static LRESULT CALLBACK WindowProc(
            HWND hwnd,
            UINT msg,
            WPARAM wParam,
            LPARAM lParam
    );

    INT mouse_x = 0;
    INT mouse_y = 0;
    bool mouse_pressed = false;

    FLOAT angle = 0.0f;
    FLOAT angle_speed = 1.0f / 64.0f;
    UINT_PTR timer;

    static constexpr DirectX::XMFLOAT4 background_color = { 0.0f, 0.2f, 0.4f, 1.0f };

    static constexpr DirectX::XMFLOAT4 color1 = { 0.0f, 0.8f, 0.8f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color2 = { 0.8f, 0.8f, 0.4f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color3 = { 0.8f, 0.2f, 0.4f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color4 = { 0.0f, 0.2f, 0.8f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color5 = { 0.3f, 0.2f, 0.4f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color6 = { 1.0f, 0.2f, 0.4f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color7 = { 0.8f, 0.2f, 0.8f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color8 = { 1.0f, 0.2f, 1.0f, 1.0f };
};