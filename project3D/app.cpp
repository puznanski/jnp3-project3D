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
#include "object_loader.h"

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
                    app->prev_mouse_x = app->mouse_x;
                    app->prev_mouse_y = app->mouse_y;
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

                case WM_KEYDOWN: {
                    app->OnKeyDown(static_cast<UINT8>(wParam));
                }
                    result = 0;
                    wasHandled = true;
                    break;

                case WM_KEYUP: {
                    app->OnKeyUp(static_cast<UINT8>(wParam));
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
        aspect_ratio(static_cast<float>(height) / static_cast<float>(width)),
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
        ID3D12CommandList* command_lists[] = { command_list.Get() };
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
        cbv_heap_desc.NumDescriptors = 2;
        cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbv_heap_desc.NodeMask = 0;

        hr = device->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap));
    }

    if (SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
        dsv_heap_desc.NumDescriptors = 1;
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dsv_heap_desc.NodeMask = 0;

        hr = device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap));
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

    // Create a WIC factory.
    if (SUCCEEDED(hr)) {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }

    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                __uuidof(IWICImagingFactory),
                &wic_factory
        );
    }

    return hr;
}

HRESULT App::LoadAssets() {
    HRESULT hr = S_OK;

    D3D12_DESCRIPTOR_RANGE descriptor_ranges[] = {
            {
                    .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                    .NumDescriptors = 1,
                    .BaseShaderRegister = 0,
                    .RegisterSpace = 0,
                    .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
            },
            {
                    .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                    .NumDescriptors = 1,
                    .BaseShaderRegister = 0,
                    .RegisterSpace = 0,
                    .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
            }
    };

    D3D12_ROOT_PARAMETER root_parameters[] = {
            {
                    .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                    .DescriptorTable = { 1, &descriptor_ranges[0] },
                    .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
            },
            {
                    .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                    .DescriptorTable = { 1, &descriptor_ranges[1] },
                    .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
            },
    };

    D3D12_STATIC_SAMPLER_DESC texture_sampler_desc = {};
    texture_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    texture_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    texture_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    texture_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    texture_sampler_desc.MipLODBias = 0;
    texture_sampler_desc.MaxAnisotropy = 0;
    texture_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    texture_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    texture_sampler_desc.MinLOD = 0.f;
    texture_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    texture_sampler_desc.ShaderRegister = 0;
    texture_sampler_desc.RegisterSpace = 0;
    texture_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc = {};
    root_signature_desc.NumParameters = _countof(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 1;
    root_signature_desc.pStaticSamplers = &texture_sampler_desc;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
                                //D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS; //TODO: ??

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    hr = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if (SUCCEEDED(hr)) {
        hr = device->CreateRootSignature(
                0,
                signature->GetBufferPointer(),
                signature->GetBufferSize(),
                IID_PPV_ARGS(&root_signature)
        );
    }

    if (SUCCEEDED(hr)) {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC input_element_desc[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        D3D12_DEPTH_STENCIL_DESC depth_stencil_desc = {};
        depth_stencil_desc.DepthEnable = TRUE;
        depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        depth_stencil_desc.StencilEnable = FALSE;
        depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK; //TODO: ??
        depth_stencil_desc.FrontFace = {
                .StencilFailOp = D3D12_STENCIL_OP_KEEP,
                .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
                .StencilPassOp = D3D12_STENCIL_OP_KEEP,
                .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        };
        depth_stencil_desc.BackFace = {
                .StencilFailOp = D3D12_STENCIL_OP_KEEP,
                .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
                .StencilPassOp = D3D12_STENCIL_OP_KEEP,
                .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.InputLayout = {input_element_desc, _countof(input_element_desc) };
        pso_desc.pRootSignature = root_signature.Get();
        pso_desc.VS = CD3DX12_SHADER_BYTECODE(vs_main, sizeof(vs_main));
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(ps_main, sizeof(ps_main));
        pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso_desc.DepthStencilState = depth_stencil_desc;
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

    ObjectLoader object_loader("assets\\untitled", color);

    if (SUCCEEDED(hr)) {
        hr = object_loader.load();
    }

    UINT vertex_buffer_size = 0;

    if (SUCCEEDED(hr)) {
        object = object_loader.get_vertices();
        number_of_vertices = object_loader.get_number_of_vertices();
        vertex_buffer_size = object.size() * sizeof(Vertex);

        hr = LoadBitmapFromFile(object_loader.get_texture_uri().c_str(), bitmap_width, bitmap_height, &bitmap);
    }

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
        Vertex* object_ptr = &object[0];
        memcpy(vertex_data_begin, object_ptr, number_of_vertices * sizeof(Vertex));
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
    }

    D3D12_CLEAR_VALUE clear_value = {};
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil = { .Depth = 1.0f, .Stencil = 0 };

    D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc = {};
    depth_stencil_view_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_stencil_view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depth_stencil_view_desc.Flags = D3D12_DSV_FLAG_NONE;
    depth_stencil_view_desc.Texture2D = {};

    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Alignment = 0;
        resource_desc.Width = width;
        resource_desc.Height = height;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 0;
        resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
        resource_desc.SampleDesc = { .Count = 1, .Quality = 0 };
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        hr = device->CreateCommittedResource(
                &heap_properties,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clear_value,
                IID_PPV_ARGS(&depth_buffer)
        );
    }

    if (SUCCEEDED(hr)) {
        device->CreateDepthStencilView(
                depth_buffer.Get(),
                &depth_stencil_view_desc,
                dsv_heap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Alignment = 0;
        resource_desc.Width = bitmap_width;
        resource_desc.Height = bitmap_height;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resource_desc.SampleDesc = { .Count = 1, .Quality = 0 };
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = device->CreateCommittedResource(
                &heap_properties,
                D3D12_HEAP_FLAG_NONE,
                &resource_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&texture)
        );
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> tex_upload_buffer;
    UINT64 tex_upload_buffer_size = 0;

    if (SUCCEEDED(hr)) {
        tex_upload_buffer_size = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    }

    D3D12_RESOURCE_DESC texture_resource_desc = {};
    texture_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    texture_resource_desc.Alignment = 0;
    texture_resource_desc.Width = tex_upload_buffer_size;
    texture_resource_desc.Height = 1;
    texture_resource_desc.DepthOrArraySize = 1;
    texture_resource_desc.MipLevels = 1;
    texture_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    texture_resource_desc.SampleDesc = { .Count = 1, .Quality = 0 };
    texture_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    texture_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (SUCCEEDED(hr)) {
        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1;
        heap_properties.VisibleNodeMask = 1;

        hr = device->CreateCommittedResource(
                &heap_properties,
                D3D12_HEAP_FLAG_NONE,
                &texture_resource_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&tex_upload_buffer)
        );
    }

    if (SUCCEEDED(hr)) {
        hr = command_list->Reset(command_allocator.Get(), pipeline_state.Get());
    }

    if (SUCCEEDED(hr)) {
        D3D12_SUBRESOURCE_DATA texture_data = {};
        texture_data.RowPitch = bitmap_width * BITMAP_PIXEL_SIZE;
        texture_data.SlicePitch = bitmap_width * bitmap_height * BITMAP_PIXEL_SIZE;
        texture_data.pData = bitmap;

        D3D12_RESOURCE_BARRIER tex_upload_resource_barrier = {};
        tex_upload_resource_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        tex_upload_resource_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        tex_upload_resource_barrier.Transition = {
                .pResource = texture.Get(),
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        };

        UpdateSubresources(command_list.Get(), texture.Get(), tex_upload_buffer.Get(), 0, 0, 1, &texture_data);
        command_list->ResourceBarrier(1, &tex_upload_resource_barrier);
    }

    if (SUCCEEDED(hr)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc = {};
        shader_resource_view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shader_resource_view_desc.Format = texture_resource_desc.Format;
        shader_resource_view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shader_resource_view_desc.Texture2D = {
                .MostDetailedMip = 0,
                .MipLevels = 1,
                .PlaneSlice = 0,
                .ResourceMinLODClamp = 0.f
        };

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor_handle = cbv_heap->GetCPUDescriptorHandleForHeapStart();
        cpu_descriptor_handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateShaderResourceView(texture.Get(), &shader_resource_view_desc, cpu_descriptor_handle);

        hr = command_list->Close();
    }

    if (SUCCEEDED(hr)) {
        ID3D12CommandList* command_lists[] = { command_list.Get() };
        command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    }

    if (SUCCEEDED(hr)) {
        fence_value = 1;

        // Create an event handle to use for frame synchronization.
        fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fence_event == nullptr) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }

        WaitForPreviousFrame();
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

        D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor_handle = cbv_heap->GetGPUDescriptorHandleForHeapStart();
        command_list->SetGraphicsRootDescriptorTable(0, gpu_descriptor_handle);
        gpu_descriptor_handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        command_list->SetGraphicsRootDescriptorTable(1, gpu_descriptor_handle);
        command_list->RSSetViewports(1, &viewport);
        command_list->RSSetScissorRects(1, &scissor_rect);

        // Indicate that the back buffer will be used as a render target.
        auto transition1 = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        command_list->ResourceBarrier(1, &transition1);
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart(), frame_index, rtv_descriptor_size);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_heap->GetCPUDescriptorHandleForHeapStart());
        command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);

        // Record commands.
        const float color_arr[] = {background_color.x, background_color.y, background_color.z, background_color.w};
        command_list->ClearRenderTargetView(rtv_handle, color_arr, 0, nullptr);
        command_list->ClearDepthStencilView(dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
        command_list->DrawInstanced(number_of_vertices, 1, 0, 0);

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
    DirectX::XMMATRIX wvp_matrix = {};

    wvp_matrix = XMMatrixMultiply(
            DirectX::XMMatrixRotationY(2.5f * angle),
            DirectX::XMMatrixRotationX(-0.5f)
    );

    wvp_matrix = XMMatrixMultiply(
            wvp_matrix,
            DirectX::XMMatrixTranslation(position.x, position.y, position.z)
    );
    //DirectX::XMFLOAT3 up = {0, 1, 0};
    //wvp_matrix = DirectX::XMMatrixLookToLH(XMLoadFloat3(&position), XMLoadFloat3(&camera_direction), XMLoadFloat3(&up));

    DirectX::XMStoreFloat4x4(&constant_buffer_data.mat_world_view, XMMatrixTranspose(wvp_matrix));

    wvp_matrix = XMMatrixMultiply(
            wvp_matrix,
            DirectX::XMMatrixPerspectiveFovLH(
                    45.0f, aspect_ratio, 1.0f, 100.0f
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
    ProcessMove();

    if (angle >= 360.0f) {
        angle -= 360.0f;
    }

    angle += angle_speed;

    return S_OK;
}

HRESULT App::LoadBitmapFromFile(PCWSTR uri, UINT &width, UINT &height, BYTE **bits) {
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *source = nullptr;
    IWICFormatConverter *converter = nullptr;

    HRESULT hr = wic_factory->CreateDecoderFromFilename(
            uri,
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder
    );

    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrame(0, &source);
    }

    if (SUCCEEDED(hr)) {
        hr = wic_factory->CreateFormatConverter(&converter);
    }

    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
                source,
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.f,
                WICBitmapPaletteTypeMedianCut
        );
    }

    if (SUCCEEDED(hr)) {
        hr = converter->GetSize(&width, &height);
    }

    if (SUCCEEDED(hr)) {
        *bits = new BYTE[4 * width * height];
        hr = converter->CopyPixels(
                nullptr,
                4 * width,
                4 * width * height,
                *bits
        );
    }

    SafeRelease(&decoder);
    SafeRelease(&source);
    SafeRelease(&converter);

    return hr;
}

void App::OnKeyDown(UINT8 key) {
    switch (key) {
        case 'W': {
            keyboard.w = true;
            break;
        }
        case 'A': {
            keyboard.a = true;
            break;
        }
        case 'S': {
            keyboard.s = true;
            break;
        }
        case 'D': {
            keyboard.d = true;
            break;
        }
        default: ;
    }
}

void App::OnKeyUp(UINT8 key) {
    switch (key) {
        case 'W': {
            keyboard.w = false;
            break;
        }
        case 'A': {
            keyboard.a = false;
            break;
        }
        case 'S': {
            keyboard.s = false;
            break;
        }
        case 'D': {
            keyboard.d = false;
            break;
        }
        default: ;
    }
}

void App::ProcessMove() {
    move = { 0.0f, 0.0f, 0.0f };

    if (keyboard.w) {
        move.z -= MOVE_SPEED;
    }
    if (keyboard.a) {
        move.x += MOVE_SPEED;
    }
    if (keyboard.s) {
        move.z += MOVE_SPEED;
    }
    if (keyboard.d) {
        move.x -= MOVE_SPEED;
    }
    /*if (prev_mouse_x - mouse_x != 0) {
        camera_rotation.second += (ROTATION_SPEED * (prev_mouse_x - mouse_x));
    }
    if (prev_mouse_y - mouse_y != 0) {
        camera_rotation.first += (ROTATION_SPEED * (prev_mouse_y - mouse_y));
    }

    if (fabs(move.x) > 0.1f && fabs(move.z) > 0.1f) {
        DirectX::XMVECTOR vector = DirectX::XMVector3Normalize(XMLoadFloat3(&move));
        move.x = DirectX::XMVectorGetX(vector);
        move.z = DirectX::XMVectorGetZ(vector);
    }

    camera_rotation.first = min(camera_rotation.first, DirectX::XM_PIDIV4);
    camera_rotation.first = max(-DirectX::XM_PIDIV4, camera_rotation.first);

    position.x += move.x * -cosf(camera_rotation.second) - move.z * sinf(camera_rotation.second);
    position.z += move.z * sinf(camera_rotation.second) - move.z * cosf(camera_rotation.second);

    camera_direction.x = cosf(camera_rotation.first) * sinf(camera_rotation.second);
    camera_direction.y = sinf(camera_rotation.first);
    camera_direction.z = cosf(camera_rotation.first) * cosf(camera_rotation.second);*/

    position.x += move.x;
    position.z += move.z;
}

