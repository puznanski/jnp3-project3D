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
#include <wincodec.h>
#include <queue>

#include "d3dx12.h"
#include "common.h"
#include "camera.h"

template<class Interface>
inline void SafeRelease(
        Interface **interface_to_release) {
    if (*interface_to_release != NULL) {
        (*interface_to_release)->Release();
        (*interface_to_release) = NULL;
    }
}

class App {
public:
    explicit App(std::wstring name);

    ~App();

    HRESULT Initialize(HINSTANCE instance, INT cmd_show);

    void RunMessageLoop();

private:
    static const UINT FRAME_COUNT = 2;
    static const UINT BITMAP_PIXEL_SIZE = 4;
    std::string MODEL_URI = "assets\\untitled";

    struct ConstantBuffer {
        DirectX::XMFLOAT4X4 mat_world_view_proj;
        DirectX::XMFLOAT4X4 mat_world_view;
        DirectX::XMFLOAT4 padding[(256 - (2 * sizeof(DirectX::XMFLOAT4X4))) / sizeof(DirectX::XMFLOAT4)];
    };

    struct Keyboard {
        bool w = false;
        bool a = false;
        bool s = false;
        bool d = false;
        bool space = false;
        bool shift = false;
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
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory;
    UINT rtv_descriptor_size;

    // App resources
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    Microsoft::WRL::ComPtr<ID3D12Resource> constant_buffer;
    ConstantBuffer constant_buffer_data{};
    UINT8* constant_buffer_data_begin;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_buffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;

    // Synchronization objects
    UINT frame_index;
    HANDLE fence_event{};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fence_value{};

    HRESULT LoadPipeline();
    HRESULT LoadAssets();
    HRESULT PopulateCommandList();
    HRESULT WaitForPreviousFrame();

    void GetHardwareAdapter(
            _In_ IDXGIFactory1* factory,
            _Outptr_result_maybenull_ IDXGIAdapter1** adapter,
            bool request_high_performance_adapter = false
    );

    HRESULT LoadBitmapFromFile(PCWSTR uri, UINT &width, UINT &height, BYTE **bits);
    HRESULT OnInit();
    HRESULT OnUpdate();
    HRESULT OnRender();
    HRESULT OnDestroy();

    void OnKeyDown(UINT8 key);
    void OnKeyUp(UINT8 key);
    void ProcessMove();

    static LRESULT CALLBACK WindowProc(
            HWND hwnd,
            UINT msg,
            WPARAM wParam,
            LPARAM lParam
    );

    std::queue<std::pair<LONG, LONG>> mouse_position_queue;
    bool mouse_pressed = false;
    Keyboard keyboard;
    bool post_quit = false;

    Camera camera;

    std::vector<Vertex> object;
    std::size_t number_of_vertices{};

    UINT bitmap_width = 0;
    UINT bitmap_height = 0;
    BYTE* bitmap = nullptr;

    static constexpr DirectX::XMFLOAT4 background_color = { 0.15f, 0.56f, 0.96f, 1.0f };
    static constexpr DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};
