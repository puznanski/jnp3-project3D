#include "app.h"

#include <cstdlib>
#include <malloc.h>
#include <memory.h>
#include <cwchar>
#include <cmath>
#include <windowsx.h>
#include <numbers>
#include <string>
#include <utility>
#include <wincodec.h>

#include "pixel_shader.h"
#include "vertex_shader.h"

LRESULT CALLBACK App::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;

    if (msg == WM_CREATE) {
        LPCREATESTRUCT pcs = (LPCREATESTRUCT) lParam;
        App *app = (App *) pcs->lpCreateParams;
        SetTimer(hwnd, app->timer, 15, nullptr);

        ::SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(app)
        );

        result = 1;
    } else {
        App *app = reinterpret_cast<App *>(static_cast<LONG_PTR>(
                ::GetWindowLongPtrW(
                        hwnd,
                        GWLP_USERDATA
                )));

        bool wasHandled = false;

        if (app) {
            switch (msg) {
                case WM_SIZE: {
                    UINT width = LOWORD(lParam);
                    UINT height = HIWORD(lParam);
                    app->OnResize(width, height);
                }
                    result = 0;
                    wasHandled = true;
                    break;

                case WM_DISPLAYCHANGE: {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                    result = 0;
                    wasHandled = true;
                    break;

                case WM_PAINT: {
                    app->OnUpdate();
                    app->OnRender();
                    ValidateRect(hwnd, nullptr);
                }
                    result = 0;
                    wasHandled = true;
                    break;

                case WM_DESTROY: {
                    KillTimer(hwnd, app->timer);
                    PostQuitMessage(0);
                }
                    result = 1;
                    wasHandled = true;
                    break;

                case WM_MOUSEMOVE: {
                    app->mouse_x = GET_X_LPARAM(lParam);
                    app->mouse_y = GET_Y_LPARAM(lParam);
                }
                    result = 0;
                    wasHandled = true;
                    break;

                case WM_TIMER: {
                    app->OnAnimationStep();
                }
                    result = 0;
                    wasHandled = true;
                    break;
            }
        }

        if (!wasHandled) {
            result = DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    return result;
}

App::App(UINT width, UINT height, std::wstring name) :
        hwnd(nullptr),
        width(width),
        height(height),
        aspect_ratio(static_cast<float>(width) / static_cast<float>(height)),
        title(std::move(name)),
        use_warp_device(false),
        frame_index(0),
        rtv_descriptor_size(0),
        viewport(0.0f, 0.0f, static_cast<FLOAT>(width), static_cast<FLOAT>(height)),
        scissor_rect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
        constant_buffer_data_begin(nullptr) {
    DirectX::XMStoreFloat4x4(&constant_buffer_data.mat_world_view_proj, DirectX::XMMatrixIdentity());
}

App::~App() {
    OnDestroy();
}

void App::RunMessageLoop() {
    MSG msg;

    do {
        mouse_pressed = (GetAsyncKeyState(VK_LBUTTON) < 0);

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_QUIT) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        } else {
            OnUpdate();
            OnRender();
        }
    } while (msg.message != WM_QUIT);
}

HRESULT App::Initialize(HINSTANCE instance, INT cmd_show) {
    HRESULT hr = S_OK;

    // Register the window class.
    WNDCLASSEX wcex = {sizeof(WNDCLASSEX)};
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = App::WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(LONG_PTR);
    wcex.hInstance = instance;
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.hCursor = LoadCursor(nullptr, IDI_APPLICATION);
    wcex.lpszClassName = L"D3DApp";

    ATOM register_result = RegisterClassEx(&wcex);
    if (register_result == 0) {
        return 1;
    }

    hwnd = CreateWindowEx(
            0,                              // Optional window styles.
            L"D3DApp",                      // Window class
            title.c_str(),                  // Window text
            WS_OVERLAPPEDWINDOW,            // Window style

            // Size and position
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

            nullptr,       // Parent window
            nullptr,       // Menu
            instance,  // Instance handle
            this        // Additional application data
    );

    hr = OnInit();

    if (SUCCEEDED(hr)) {
        if (hwnd) {
            SetWindowPos(
                    hwnd,
                    NULL,
                    NULL,
                    NULL,
                    static_cast<FLOAT>(height),
                    static_cast<FLOAT>(width),
                    SWP_NOMOVE);
            ShowWindow(hwnd, SW_SHOWNORMAL);
            UpdateWindow(hwnd);
        } else {
            return 1;
        }
    }

    return hr;
}

void App::GetHardwareAdapter(
        IDXGIFactory1* factory,
        IDXGIAdapter1** adapter,
        bool request_high_performance_adapter) {
    *adapter = nullptr;

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_ptr;
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_ptr;

    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory_ptr)))) {
        for (
                UINT adapter_index = 0;
                SUCCEEDED(factory_ptr->EnumAdapterByGpuPreference(
                        adapter_index,
                        request_high_performance_adapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                        IID_PPV_ARGS(&adapter_ptr)));
                ++adapter_index) {
            DXGI_ADAPTER_DESC1 desc;
            adapter_ptr->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter_ptr.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }
    }

    if(adapter_ptr.Get() == nullptr) {
        for (UINT adapter_index = 0; SUCCEEDED(factory->EnumAdapters1(adapter_index, &adapter_ptr)); ++adapter_index) {
            DXGI_ADAPTER_DESC1 desc;
            adapter_ptr->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter_ptr.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                break;
            }
        }
    }

    *adapter = adapter_ptr.Detach();
}

HRESULT App::OnRender() {
    HRESULT hr = S_OK;

    hr = PopulateCommandList();

    if (SUCCEEDED(hr)) {
        ID3D12CommandList* command_lists[] = {command_list.Get() };
        command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

        hr = swap_chain->Present(1, 0);
        WaitForPreviousFrame();
    }

    return hr;
}

void App::OnResize(UINT width, UINT height) {

}

HRESULT App::LoadPipeline() {
    HRESULT hr = S_OK;
    UINT dxgi_factory_flags = 2;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;

    hr = CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory));

    if (SUCCEEDED(hr)) {
        if (use_warp_device) {
            Microsoft::WRL::ComPtr<IDXGIAdapter> warp_adapter;
            hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter));

            if (SUCCEEDED(hr)) {
                hr = D3D12CreateDevice(
                        warp_adapter.Get(),
                        D3D_FEATURE_LEVEL_11_0,
                        IID_PPV_ARGS(&device)
                );
            }
        } else {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> hardware_adapter;
            GetHardwareAdapter(factory.Get(), &hardware_adapter);

            if (SUCCEEDED(hr)) {
                hr = D3D12CreateDevice(
                        hardware_adapter.Get(),
                        D3D_FEATURE_LEVEL_11_0,
                        IID_PPV_ARGS(&device)
                );
            }
        }
    }

    if (SUCCEEDED(hr)) {
        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain1> loc_swap_chain;

    if (SUCCEEDED(hr)) {
        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.BufferCount = FRAME_COUNT;
        swap_chain_desc.Width = width;
        swap_chain_desc.Height = height;
        swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.SampleDesc.Count = 1;

        hr = factory->CreateSwapChainForHwnd(
                command_queue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
                hwnd,
                &swap_chain_desc,
                nullptr,
                nullptr,
                &loc_swap_chain
        );
    }

    if (SUCCEEDED(hr)) {
        hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }

    if (SUCCEEDED(hr)) {
        hr = loc_swap_chain.As(&swap_chain);
    }

    if (SUCCEEDED(hr)) {
        frame_index = swap_chain->GetCurrentBackBufferIndex();

        // Create descriptor heaps.
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
        rtv_heap_desc.NumDescriptors = FRAME_COUNT;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap));

        if (SUCCEEDED(hr)) {
            rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
    }

    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc = {};
        cbv_heap_desc.NumDescriptors = 1;
        cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbv_heap_desc.NodeMask = 0;

        hr = device->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap));
    }

    if (SUCCEEDED(hr)) {
        // Create frame resources.
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());

        // Create an RTV for each frame.
        for (UINT n = 0; n < FRAME_COUNT; n++) {
            hr = swap_chain->GetBuffer(n, IID_PPV_ARGS(&render_targets[n]));

            if (SUCCEEDED(hr)) {
                device->CreateRenderTargetView(render_targets[n].Get(), nullptr, rtv_handle);
                rtv_handle.Offset(1, rtv_descriptor_size);
            }
        }
    }

    if (SUCCEEDED(hr)) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));
    }

    return hr;
}

HRESULT App::LoadAssets() {
    HRESULT hr = S_OK;

    D3D12_DESCRIPTOR_RANGE descriptor_range = {};
    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_parameters[] = {{
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable = { 1, &descriptor_range },
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
    }};

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc = {};
    root_signature_desc.NumParameters = _countof(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = nullptr;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    hr = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if (SUCCEEDED(hr)) {
        hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature));
    }

    if (SUCCEEDED(hr)) {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.InputLayout = {input_element_desc, _countof(input_element_desc) };
        pso_desc.pRootSignature = root_signature.Get();
        pso_desc.VS = CD3DX12_SHADER_BYTECODE(vs_main, sizeof(vs_main));
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(ps_main, sizeof(ps_main));
        pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso_desc.DepthStencilState.DepthEnable = FALSE;
        pso_desc.DepthStencilState.StencilEnable = FALSE;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso_desc.SampleDesc.Count = 1;

        hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state));
    }

    if (SUCCEEDED(hr)) {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list));
    }

    if (SUCCEEDED(hr)) {
        hr = command_list->Close();
    }

    // Create the vertex buffer.
    // Define the geometry for a triangle.
    Vertex triangle[] =
            {
                    { { 0.0f, 1.0f, 0.5f }, color1 },
                    { { 1.0f, 0.0f, 0.5f }, color2 },
                    { { -1.0f, -1.0f, 0.5f }, color3 }
            };

    Vertex cube[] =
            {
                    {{-0.5f, 0.5f, -0.5}, color1},
                    {{0.5f, 0.5f, -0.5}, color2},
                    {{-0.5f, -0.5f, -0.5}, color3},
                    {{-0.5f, -0.5f, -0.5}, color3},
                    {{0.5f, 0.5f, -0.5}, color2},
                    {{0.5f, -0.5f, -0.5}, color4},
                    {{-0.5f, 0.5f, 0.5}, color5},
                    {{-0.5f, 0.5f, -0.5}, color1},
                    {{-0.5f, -0.5f, 0.5}, color7},
                    {{-0.5f, -0.5f, 0.5}, color7},
                    {{-0.5f, 0.5f, -0.5}, color1},
                    {{-0.5f, -0.5f, -0.5}, color3},
                    {{0.5f, -0.5f, 0.5}, color8},
                    {{0.5f, 0.5f, 0.5}, color6},
                    {{-0.5f, -0.5f, 0.5}, color7},
                    {{-0.5f, -0.5f, 0.5}, color7},
                    {{0.5f, 0.5f, 0.5}, color6},
                    {{-0.5f, 0.5f, 0.5}, color5},
                    {{0.5f, -0.5f, -0.5}, color4},
                    {{0.5f, 0.5f, -0.5}, color2},
                    {{0.5f, -0.5f, 0.5}, color8},
                    {{0.5f, -0.5f, 0.5}, color8},
                    {{0.5f, 0.5f, -0.5}, color2},
                    {{0.5f, 0.5f, 0.5}, color6},
                    {{-0.5f, 0.5f, 0.5}, color5},
                    {{0.5f, 0.5f, 0.5}, color6},
                    {{-0.5f, 0.5f, -0.5}, color1},
                    {{-0.5f, 0.5f, -0.5}, color1},
                    {{0.5f, 0.5f, 0.5}, color6},
                    {{0.5f, 0.5f, -0.5}, color2},
                    {{0.5f, -0.5f, -0.5}, color4},
                    {{0.5f, -0.5f, 0.5}, color8},
                    {{-0.5f, -0.5f, -0.5}, color3},
                    {{-0.5f, -0.5f, -0.5}, color3},
                    {{0.5f, -0.5f, 0.5}, color8},
                    {{-0.5f, -0.5f, 0.5}, color7}
            };

    const UINT vertex_buffer_size = sizeof(cube);

    if (SUCCEEDED(hr)) {
        // Note: using upload heaps to transfer static data like vert buffers is not
        // recommended. Every time the GPU needs it, the upload heap will be marshalled
        // over. Please read up on Default Heap usage. An upload heap is used here for
        // code simplicity and because there are very few verts to actually transfer.
        const auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size);

        hr = device->CreateCommittedResource(
                &heap_properties,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&vertex_buffer)
        );
    }

    // Copy the triangle data to the vertex buffer.
    UINT8* vertex_data_begin;

    if (SUCCEEDED(hr)) {
        CD3DX12_RANGE read_range(0, 0);        // We do not intend to read from this resource on the CPU.
        hr = vertex_buffer->Map(0, &read_range, reinterpret_cast<void**>(&vertex_data_begin));
    }

    if (SUCCEEDED(hr)) {
        memcpy(vertex_data_begin, cube, sizeof(cube));
        vertex_buffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
        vertex_buffer_view.StrideInBytes = sizeof(Vertex);
        vertex_buffer_view.SizeInBytes = vertex_buffer_size;
    }

    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Alignment = 0;
        resource_desc.Width = sizeof(ConstantBuffer);
        resource_desc.Height = 1;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_UNKNOWN;
        resource_desc.SampleDesc = { .Count = 1, .Quality = 0 };
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = device->CreateCommittedResource(
                &heap_properties,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&constant_buffer)
        );
    }

    if (SUCCEEDED(hr)) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC constant_buffer_view_desc = {};
        constant_buffer_view_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
        constant_buffer_view_desc.SizeInBytes = sizeof(ConstantBuffer);
        device->CreateConstantBufferView(&constant_buffer_view_desc, cbv_heap->GetCPUDescriptorHandleForHeapStart());

        CD3DX12_RANGE read_range(0, 0);
        hr = constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&constant_buffer_data_begin));
    }

    if (SUCCEEDED(hr)) {
        memcpy(constant_buffer_data_begin, &constant_buffer_data, sizeof(constant_buffer_data));

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    }

    if (SUCCEEDED(hr)) {
        fence_value = 1;

        // Create an event handle to use for frame synchronization.
        fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fence_event == nullptr) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    return hr;
}


HRESULT App::PopulateCommandList() {
    HRESULT hr = S_OK;

    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    hr = command_allocator->Reset();

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    if (SUCCEEDED(hr)) {
        hr = command_list->Reset(command_allocator.Get(), pipeline_state.Get());
    }

    if (SUCCEEDED(hr)) {
        // Set necessary state.
        command_list->SetGraphicsRootSignature(root_signature.Get());
        ID3D12DescriptorHeap* heaps[] = { cbv_heap.Get() };
        command_list->SetDescriptorHeaps(_countof(heaps), heaps);
        command_list->SetGraphicsRootDescriptorTable(0, cbv_heap->GetGPUDescriptorHandleForHeapStart());
        command_list->RSSetViewports(1, &viewport);
        command_list->RSSetScissorRects(1, &scissor_rect);

        // Indicate that the back buffer will be used as a render target.
        auto transition1 = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        command_list->ResourceBarrier(1, &transition1);
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtv_heap->GetCPUDescriptorHandleForHeapStart(), frame_index, rtv_descriptor_size);
        command_list->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // Record commands.
        const float color[] = {background_color.x, background_color.y, background_color.z, background_color.w};
        command_list->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
        command_list->DrawInstanced(36, 1, 0, 0);

        // Indicate that the back buffer will now be used to present.
        auto transition2 = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        command_list->ResourceBarrier(1, &transition2);

        hr = command_list->Close();
    }

    return hr;
}

HRESULT App::WaitForPreviousFrame() {
    HRESULT hr = S_OK;

    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence_value_copy = fence_value;
    hr = command_queue->Signal(fence.Get(), fence_value_copy);

    if (SUCCEEDED(hr)) {
        fence_value++;

        // Wait until the previous frame is finished.
        if (fence->GetCompletedValue() < fence_value_copy) {
            hr = fence->SetEventOnCompletion(fence_value_copy, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }

        frame_index = swap_chain->GetCurrentBackBufferIndex();
    }

    return hr;
}

HRESULT App::OnInit() {
    HRESULT hr = LoadPipeline();

    if (SUCCEEDED(hr)) {
        hr = LoadAssets();
    }

    return hr;
}

HRESULT App::OnUpdate() {
    DirectX::XMMATRIX wvp_matrix;

    wvp_matrix = XMMatrixMultiply(
            DirectX::XMMatrixRotationY(2.5f * angle),
            DirectX::XMMatrixRotationX(static_cast<FLOAT>(sin(angle)) / 2.0f)
    );

    wvp_matrix = XMMatrixMultiply(
            wvp_matrix,
            DirectX::XMMatrixTranslation(0.0f, 0.0f, 4.0f)
    );

    wvp_matrix = XMMatrixMultiply(
            wvp_matrix,
            DirectX::XMMatrixPerspectiveFovLH(
                    45.0f, viewport.Width / viewport.Height, 1.0f, 100.0f
            )
    );

    wvp_matrix = XMMatrixTranspose(wvp_matrix);
    DirectX::XMStoreFloat4x4(&constant_buffer_data.mat_world_view_proj, wvp_matrix);
    memcpy(constant_buffer_data_begin, &constant_buffer_data, sizeof(constant_buffer_data));

    return S_OK;
}

HRESULT App::OnDestroy() {
    HRESULT hr = WaitForPreviousFrame();

    if (SUCCEEDED(hr)) {
        CloseHandle(fence_event);
    }

    return hr;
}

HRESULT App::OnAnimationStep() {
    if (angle >= 360.0f) {
        angle -= 360.0f;
    }

    angle += angle_speed;

    return S_OK;
}

