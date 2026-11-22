#ifdef _WIN32

#include "oracles/d3d11_renderer.hpp"
#include "render_layout.hpp"

#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace oracles {
namespace {

constexpr std::uint32_t rgba8(std::uint8_t r, std::uint8_t g,
                              std::uint8_t b, std::uint8_t a = 255) {
    return static_cast<std::uint32_t>(r) |
           (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
}

template <typename T>
void release_com(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

std::wstring widen_ascii(const char* value) {
    std::wstring result;
    if (!value) return result;
    while (*value) result.push_back(static_cast<unsigned char>(*value++));
    return result;
}

std::wstring hr_text(const wchar_t* operation, HRESULT hr) {
    std::wstringstream stream;
    stream << operation << L" failed (HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(hr) << L")";
    return stream.str();
}

const char* vertex_shader_source = R"hlsl(
struct VertexOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
VertexOutput main(uint vertex_id : SV_VertexID) {
    VertexOutput output;
    float2 positions[4] = {
        float2(-1.0, -1.0), float2(-1.0, 1.0),
        float2( 1.0, -1.0), float2( 1.0, 1.0)
    };
    float2 coordinates[4] = {
        float2(0.0, 1.0), float2(0.0, 0.0),
        float2(1.0, 1.0), float2(1.0, 0.0)
    };
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = coordinates[vertex_id];
    return output;
}
)hlsl";

const char* pixel_shader_source = R"hlsl(
Texture2D frame_texture : register(t0);
SamplerState frame_sampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    return frame_texture.Sample(frame_sampler, uv);
}
)hlsl";

} // namespace

D3D11Renderer::~D3D11Renderer() {
    shutdown();
}

void D3D11Renderer::release_frame_resources() {
    release_com(frame_view_);
    release_com(frame_texture_);
}

void D3D11Renderer::shutdown() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    release_frame_resources();
    release_com(render_target_);
    release_com(sampler_);
    release_com(rasterizer_);
    release_com(pixel_shader_);
    release_com(vertex_shader_);
    release_com(swap_chain_);
    release_com(context_);
    release_com(device_);
    window_ = nullptr;
    frame_width_ = 0;
    frame_height_ = 0;
    target_width_ = 0;
    target_height_ = 0;
    adapter_description_.clear();
    feature_level_description_.clear();
}

bool D3D11Renderer::initialize(HWND window, IDXGIAdapter1* selected_adapter,
                               DisplayAspect aspect, std::wstring& error) {
    shutdown();
    error.clear();
    if (!window || !IsWindow(window)) {
        error = L"A valid Direct3D child window is required.";
        return false;
    }
    window_ = window;
    aspect_ = aspect;

    const std::array<D3D_FEATURE_LEVEL, 4> feature_levels = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL obtained{};
    HRESULT hr = D3D11CreateDevice(
        selected_adapter,
        selected_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        feature_levels.data(), static_cast<UINT>(feature_levels.size()),
        D3D11_SDK_VERSION, &device_, &obtained, &context_);

    if (hr == E_INVALIDARG) {
        // Older Windows 10 runtimes may not recognize the 11.1 feature level.
        release_com(context_);
        release_com(device_);
        hr = D3D11CreateDevice(
            selected_adapter,
            selected_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels.data() + 1,
            static_cast<UINT>(feature_levels.size() - 1),
            D3D11_SDK_VERSION, &device_, &obtained, &context_);
    }
    if (FAILED(hr)) {
        error = hr_text(L"D3D11CreateDevice", hr);
        shutdown();
        return false;
    }

    switch (obtained) {
    case D3D_FEATURE_LEVEL_11_1: feature_level_description_ = L"11.1"; break;
    case D3D_FEATURE_LEVEL_11_0: feature_level_description_ = L"11.0"; break;
    case D3D_FEATURE_LEVEL_10_1: feature_level_description_ = L"10.1"; break;
    case D3D_FEATURE_LEVEL_10_0: feature_level_description_ = L"10.0"; break;
    default: feature_level_description_ = L"unknown"; break;
    }

    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* actual_adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&actual_adapter);
    if (SUCCEEDED(hr)) hr = actual_adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        error = hr_text(L"DXGI factory acquisition", hr);
        release_com(factory);
        release_com(actual_adapter);
        release_com(dxgi_device);
        shutdown();
        return false;
    }

    DXGI_ADAPTER_DESC adapter_desc{};
    if (SUCCEEDED(actual_adapter->GetDesc(&adapter_desc))) {
        adapter_description_ = adapter_desc.Description;
    } else {
        adapter_description_ = L"Selected Direct3D adapter";
    }

    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = 0;
    swap_desc.Height = 0;
    swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.Stereo = FALSE;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.SampleDesc.Quality = 0;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;
    swap_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swap_desc.Flags = 0;

    hr = factory->CreateSwapChainForHwnd(device_, window_, &swap_desc, nullptr, nullptr, &swap_chain_);
    if (FAILED(hr)) {
        release_com(swap_chain_);
        // FLIP_SEQUENTIAL is a compatibility fallback for drivers that do not
        // accept FLIP_DISCARD on otherwise supported DXGI 1.2 systems.
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChainForHwnd(device_, window_, &swap_desc, nullptr, nullptr, &swap_chain_);
    }
    if (SUCCEEDED(hr)) factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);

    release_com(factory);
    release_com(actual_adapter);
    release_com(dxgi_device);

    if (FAILED(hr)) {
        error = hr_text(L"IDXGIFactory2::CreateSwapChainForHwnd", hr);
        shutdown();
        return false;
    }

    if (!create_render_target(error) || !create_pipeline(error) ||
        !create_empty_frame(error)) {
        shutdown();
        return false;
    }
    return true;
}

bool D3D11Renderer::create_render_target(std::wstring& error) {
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
    if (SUCCEEDED(hr)) hr = device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
    release_com(back_buffer);
    if (FAILED(hr)) {
        error = hr_text(L"Create back-buffer render target", hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    if (SUCCEEDED(swap_chain_->GetDesc1(&desc))) {
        target_width_ = desc.Width;
        target_height_ = desc.Height;
    }
    return true;
}

bool D3D11Renderer::create_pipeline(std::wstring& error) {
    ID3DBlob* vertex_blob = nullptr;
    ID3DBlob* pixel_blob = nullptr;
    ID3DBlob* diagnostics = nullptr;

    HRESULT hr = D3DCompile(vertex_shader_source, std::strlen(vertex_shader_source),
        "oracles_vertex_shader", nullptr, nullptr, "main", "vs_4_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertex_blob, &diagnostics);
    if (FAILED(hr)) {
        if (diagnostics && diagnostics->GetBufferPointer()) {
            const char* message = static_cast<const char*>(diagnostics->GetBufferPointer());
            error = widen_ascii(message);
        } else error = hr_text(L"Compile vertex shader", hr);
        release_com(diagnostics);
        release_com(vertex_blob);
        return false;
    }
    release_com(diagnostics);

    hr = D3DCompile(pixel_shader_source, std::strlen(pixel_shader_source),
        "oracles_pixel_shader", nullptr, nullptr, "main", "ps_4_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixel_blob, &diagnostics);
    if (FAILED(hr)) {
        if (diagnostics && diagnostics->GetBufferPointer()) {
            const char* message = static_cast<const char*>(diagnostics->GetBufferPointer());
            error = widen_ascii(message);
        } else error = hr_text(L"Compile pixel shader", hr);
        release_com(diagnostics);
        release_com(vertex_blob);
        release_com(pixel_blob);
        return false;
    }
    release_com(diagnostics);

    hr = device_->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, &vertex_shader_);
    if (SUCCEEDED(hr)) hr = device_->CreatePixelShader(pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(), nullptr, &pixel_shader_);
    release_com(vertex_blob);
    release_com(pixel_blob);
    if (FAILED(hr)) {
        error = hr_text(L"Create D3D11 shaders", hr);
        return false;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sampler_desc, &sampler_);
    if (FAILED(hr)) {
        error = hr_text(L"Create pixel-art sampler", hr);
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizer_desc{};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    hr = device_->CreateRasterizerState(&rasterizer_desc, &rasterizer_);
    if (FAILED(hr)) {
        error = hr_text(L"Create renderer rasterizer state", hr);
        return false;
    }
    return true;
}

bool D3D11Renderer::create_empty_frame(std::wstring& error) {
    // A blank startup surface is replaced by the first frame produced by the
    // live cartridge runtime. No synthetic diagnostic image is drawn.
    constexpr unsigned width = 160;
    constexpr unsigned height = 144;
    std::vector<std::uint32_t> black(width * height, 0xFF000000u);
    return submit_frame(black.data(), width, height, error);
}

bool D3D11Renderer::submit_frame(const std::uint32_t* rgba_pixels,
                                unsigned width, unsigned height,
                                std::wstring& error) {
    error.clear();
    if (!device_ || !context_ || !rgba_pixels || width == 0 || height == 0) {
        error = L"Cannot submit an empty frame or submit before D3D11 initialization.";
        return false;
    }

    if (!frame_texture_ || width != frame_width_ || height != frame_height_) {
        release_frame_resources();
        D3D11_TEXTURE2D_DESC texture_desc{};
        texture_desc.Width = width;
        texture_desc.Height = height;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device_->CreateTexture2D(&texture_desc, nullptr, &frame_texture_);
        if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(frame_texture_, nullptr, &frame_view_);
        if (FAILED(hr)) {
            error = hr_text(L"Create game frame texture", hr);
            release_frame_resources();
            return false;
        }
        frame_width_ = width;
        frame_height_ = height;
    }

    context_->UpdateSubresource(frame_texture_, 0, nullptr, rgba_pixels,
                                width * static_cast<UINT>(sizeof(std::uint32_t)), 0);
    return true;
}

bool D3D11Renderer::resize(unsigned width, unsigned height, std::wstring& error) {
    error.clear();
    if (!swap_chain_ || width == 0 || height == 0) return true;
    if (width == target_width_ && height == target_height_) return true;

    context_->OMSetRenderTargets(0, nullptr, nullptr);
    release_com(render_target_);
    HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        error = hr_text(L"Resize DXGI swap chain", hr);
        return false;
    }
    return create_render_target(error);
}

bool D3D11Renderer::render(std::wstring& error) {
    error.clear();
    if (!device_ || !context_ || !swap_chain_ || !render_target_ || !frame_view_) {
        error = L"D3D11 renderer is not fully initialized.";
        return false;
    }

    RECT client{};
    if (!GetClientRect(window_, &client)) {
        error = L"GetClientRect failed for the renderer window.";
        return false;
    }
    const unsigned width = static_cast<unsigned>(std::max<LONG>(0, client.right - client.left));
    const unsigned height = static_cast<unsigned>(std::max<LONG>(0, client.bottom - client.top));
    if (width == 0 || height == 0) return true;
    if (!resize(width, height, error)) return false;

    const float clear_color[4] = {0.012f, 0.020f, 0.035f, 1.0f};
    context_->OMSetRenderTargets(1, &render_target_, nullptr);
    context_->ClearRenderTargetView(render_target_, clear_color);

    std::uint32_t canvas_w = 4u;
    std::uint32_t canvas_h = 3u;
    if (aspect_ == DisplayAspect::Original10x9) {
        canvas_w = 10u;
        canvas_h = 9u;
    } else if (aspect_ == DisplayAspect::Widescreen16x9) {
        canvas_w = 16u;
        canvas_h = 9u;
    }
    const auto canvas = oracles::render::fit_aspect(static_cast<int>(width), static_cast<int>(height), canvas_w, canvas_h);
    const auto image = oracles::render::fit_aspect(canvas.width, canvas.height, frame_width_, frame_height_);

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = static_cast<float>(canvas.x + image.x);
    viewport.TopLeftY = static_cast<float>(canvas.y + image.y);
    viewport.Width = static_cast<float>(image.width);
    viewport.Height = static_cast<float>(image.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context_->RSSetState(rasterizer_);
    context_->RSSetViewports(1, &viewport);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vertex_shader_, nullptr, 0);
    context_->PSSetShader(pixel_shader_, nullptr, 0);
    context_->PSSetSamplers(0, 1, &sampler_);
    context_->PSSetShaderResources(0, 1, &frame_view_);
    context_->Draw(4, 0);

    ID3D11ShaderResourceView* null_view = nullptr;
    context_->PSSetShaderResources(0, 1, &null_view);
    const HRESULT hr = swap_chain_->Present(1, 0);
    if (FAILED(hr)) {
        error = hr_text(L"DXGI Present", hr);
        return false;
    }
    return true;
}

} // namespace oracles

#endif // _WIN32
