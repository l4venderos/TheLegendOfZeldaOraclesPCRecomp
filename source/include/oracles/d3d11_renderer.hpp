#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <string>

namespace oracles {

enum class DisplayAspect {
    Original10x9,
    Aspect4x3,
    Widescreen16x9
};

// D3D11 frame presenter. The live cartridge runner submits the PPU's RGBA
// framebuffer through submit_frame() and presents it through the selected DXGI adapter.
class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    bool initialize(HWND window, IDXGIAdapter1* selected_adapter,
                    DisplayAspect aspect, std::wstring& error);
    void shutdown();
    bool resize(unsigned width, unsigned height, std::wstring& error);
    void set_aspect(DisplayAspect aspect) noexcept { aspect_ = aspect; }

    // Pixels are packed as 0xAABBGGRR in memory on little-endian Windows,
    // matching DXGI_FORMAT_R8G8B8A8_UNORM byte order.
    bool submit_frame(const std::uint32_t* rgba_pixels,
                      unsigned width, unsigned height,
                      std::wstring& error);
    bool render(std::wstring& error);

    const std::wstring& adapter_description() const noexcept { return adapter_description_; }
    const std::wstring& feature_level_description() const noexcept { return feature_level_description_; }
    unsigned frame_width() const noexcept { return frame_width_; }
    unsigned frame_height() const noexcept { return frame_height_; }

private:
    bool create_render_target(std::wstring& error);
    bool create_pipeline(std::wstring& error);
    bool create_empty_frame(std::wstring& error);
    void release_frame_resources();

    HWND window_{};
    ID3D11Device* device_{};
    ID3D11DeviceContext* context_{};
    IDXGISwapChain1* swap_chain_{};
    ID3D11RenderTargetView* render_target_{};
    ID3D11VertexShader* vertex_shader_{};
    ID3D11PixelShader* pixel_shader_{};
    ID3D11SamplerState* sampler_{};
    ID3D11RasterizerState* rasterizer_{};
    ID3D11Texture2D* frame_texture_{};
    ID3D11ShaderResourceView* frame_view_{};
    unsigned frame_width_{};
    unsigned frame_height_{};
    unsigned target_width_{};
    unsigned target_height_{};
    DisplayAspect aspect_{DisplayAspect::Aspect4x3};
    std::wstring adapter_description_;
    std::wstring feature_level_description_;
};

} // namespace oracles

#endif // _WIN32
