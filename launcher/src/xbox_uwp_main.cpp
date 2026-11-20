// Native Xbox Series S Developer Mode (UWP CoreWindow + Direct3D 11 + DXGI + XInput)
// Launcher Entry Point & Software-Rasterized 1920x1080 Canvas Bridge.
//
// Why this exists:
//   Upstream SDL3 removed WinRT/UWP CoreWindow support (`"supports": "!uwp"` in vcpkg),
//   and Xbox OS kills any MSIX package on launch with 0x8027025b (APPMODEL_ERROR_NO_PACKAGE /
//   PLM activation timeout) if the executable does not call CoreApplication::Run(IFrameworkViewSource)
//   or if it imports desktop Win32 DLLs absent on Xbox OS (GDI32, IMM32, SETUPAPI, SHELL32).
//
// This translation unit implements a genuine C++/WinRT IFrameworkView + IFrameworkViewSource,
// creates a Direct3D 11 + DXGI 1920x1080 SwapChain For CoreWindow, polls Xbox Gamepad input via
// XInputGetState (xinput1_4.dll), and renders the exact same 7-tab XWine Launcher UI using
// FreeType + libpng + a fast 1920x1080 RGBA software canvas uploaded per frame to the D3D11 backbuffer.

#if defined(_WIN32) && defined(XWINE_PLATFORM_XBOX)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <xinput.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Storage.h>

#include "xwine/input/input_manager.hpp"
#include "xwine/launcher/launcher_state.hpp"
#include "xwine/launcher/ui_renderer.hpp"
#include "xwine/platform/logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;

// =============================================================================
// Lightweight In-Process Software 2D Canvas Implementation of SDL3 2D Primitives
// Used ONLY inside the Xbox UWP CoreWindow executable so UiRenderer compiles and
// renders pixel-identically without linking desktop SDL3.dll (USER32/GDI32/IMM32).
// =============================================================================
struct SDL_Window {
    int w = 1920;
    int h = 1080;
};

struct XWineSoftTexture {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
};

struct SDL_Renderer {
    int w = 1920;
    int h = 1080;
    std::vector<uint8_t> pixels; // w * h * 4 RGBA32
    uint8_t draw_r = 12, draw_g = 15, draw_b = 20, draw_a = 255;
    bool has_clip = false;
    SDL_Rect clip{0, 0, 1920, 1080};

    SDL_Renderer() : pixels(static_cast<size_t>(1920 * 1080 * 4), 0) {}
};

namespace {

inline void blend_pixel(uint8_t* dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
    if (sa == 0) return;
    if (sa == 255) {
        dst[0] = sr;
        dst[1] = sg;
        dst[2] = sb;
        dst[3] = 255;
        return;
    }
    const uint32_t inv = 255U - sa;
    dst[0] = static_cast<uint8_t>((sr * sa + dst[0] * inv) / 255U);
    dst[1] = static_cast<uint8_t>((sg * sa + dst[1] * inv) / 255U);
    dst[2] = static_cast<uint8_t>((sb * sa + dst[2] * inv) / 255U);
    dst[3] = 255;
}

void rasterize_triangle(SDL_Renderer* r, const SDL_Vertex& v0, const SDL_Vertex& v1, const SDL_Vertex& v2) {
    const int clip_x0 = r->has_clip ? std::max(0, r->clip.x) : 0;
    const int clip_y0 = r->has_clip ? std::max(0, r->clip.y) : 0;
    const int clip_x1 = r->has_clip ? std::min(r->w - 1, r->clip.x + r->clip.w - 1) : (r->w - 1);
    const int clip_y1 = r->has_clip ? std::min(r->h - 1, r->clip.y + r->clip.h - 1) : (r->h - 1);

    const float min_xf = std::min({v0.position.x, v1.position.x, v2.position.x});
    const float max_xf = std::max({v0.position.x, v1.position.x, v2.position.x});
    const float min_yf = std::min({v0.position.y, v1.position.y, v2.position.y});
    const float max_yf = std::max({v0.position.y, v1.position.y, v2.position.y});

    const int x0 = std::max(clip_x0, static_cast<int>(std::floor(min_xf)));
    const int x1 = std::min(clip_x1, static_cast<int>(std::ceil(max_xf)));
    const int y0 = std::max(clip_y0, static_cast<int>(std::floor(min_yf)));
    const int y1 = std::min(clip_y1, static_cast<int>(std::ceil(max_yf)));
    if (x0 > x1 || y0 > y1) return;

    // Fast path: axis-aligned solid rectangle check
    const bool same_col =
        (std::fabs(v0.color.r - v1.color.r) < 0.002f && std::fabs(v0.color.r - v2.color.r) < 0.002f &&
         std::fabs(v0.color.g - v1.color.g) < 0.002f && std::fabs(v0.color.g - v2.color.g) < 0.002f &&
         std::fabs(v0.color.b - v1.color.b) < 0.002f && std::fabs(v0.color.b - v2.color.b) < 0.002f &&
         std::fabs(v0.color.a - v1.color.a) < 0.002f && std::fabs(v0.color.a - v2.color.a) < 0.002f);

    const float area = (v1.position.x - v0.position.x) * (v2.position.y - v0.position.y) -
                       (v1.position.y - v0.position.y) * (v2.position.x - v0.position.x);
    if (std::fabs(area) < 1e-4f) return;
    const float inv_area = 1.0f / area;

    const uint8_t cr = static_cast<uint8_t>(std::clamp(v0.color.r * 255.0f, 0.0f, 255.0f));
    const uint8_t cg = static_cast<uint8_t>(std::clamp(v0.color.g * 255.0f, 0.0f, 255.0f));
    const uint8_t cb = static_cast<uint8_t>(std::clamp(v0.color.b * 255.0f, 0.0f, 255.0f));
    const uint8_t ca = static_cast<uint8_t>(std::clamp(v0.color.a * 255.0f, 0.0f, 255.0f));

    for (int y = y0; y <= y1; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        uint8_t* row = r->pixels.data() + static_cast<size_t>(y * r->w * 4);
        for (int x = x0; x <= x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            float w0 = ((v1.position.x - px) * (v2.position.y - py) -
                        (v1.position.y - py) * (v2.position.x - px)) * inv_area;
            float w1 = ((v2.position.x - px) * (v0.position.y - py) -
                        (v2.position.y - py) * (v0.position.x - px)) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f) {
                if (same_col) {
                    blend_pixel(row + x * 4, cr, cg, cb, ca);
                } else {
                    const uint8_t ir = static_cast<uint8_t>(std::clamp((w0 * v0.color.r + w1 * v1.color.r + w2 * v2.color.r) * 255.0f, 0.0f, 255.0f));
                    const uint8_t ig = static_cast<uint8_t>(std::clamp((w0 * v0.color.g + w1 * v1.color.g + w2 * v2.color.g) * 255.0f, 0.0f, 255.0f));
                    const uint8_t ib = static_cast<uint8_t>(std::clamp((w0 * v0.color.b + w1 * v1.color.b + w2 * v2.color.b) * 255.0f, 0.0f, 255.0f));
                    const uint8_t ia = static_cast<uint8_t>(std::clamp((w0 * v0.color.a + w1 * v1.color.a + w2 * v2.color.a) * 255.0f, 0.0f, 255.0f));
                    blend_pixel(row + x * 4, ir, ig, ib, ia);
                }
            }
        }
    }
}

} // namespace

// Provide exact C linkage implementations of the SDL3 functions called by UiRenderer & InputManager
extern "C" {

bool SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode) { return true; }
bool SDL_SetRenderLogicalPresentation(SDL_Renderer*, int, int, SDL_RendererLogicalPresentation) { return true; }
void SDL_DestroyTexture(SDL_Texture* t) { delete reinterpret_cast<XWineSoftTexture*>(t); }

SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void* pixels, int pitch) {
    auto* s = new SDL_Surface{};
    s->w = width;
    s->h = height;
    s->format = format;
    s->pixels = pixels;
    s->pitch = pitch;
    return s;
}

void SDL_DestroySurface(SDL_Surface* surface) { delete surface; }

SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer*, SDL_Surface* surface) {
    if (!surface || !surface->pixels) return nullptr;
    auto* t = new XWineSoftTexture{};
    t->w = surface->w;
    t->h = surface->h;
    t->rgba.resize(static_cast<size_t>(t->w * t->h * 4));
    for (int y = 0; y < t->h; ++y) {
        std::memcpy(t->rgba.data() + static_cast<size_t>(y * t->w * 4),
                    static_cast<const uint8_t*>(surface->pixels) + static_cast<size_t>(y * surface->pitch),
                    static_cast<size_t>(t->w * 4));
    }
    return reinterpret_cast<SDL_Texture*>(t);
}

bool SDL_SetTextureScaleMode(SDL_Texture*, SDL_ScaleMode) { return true; }
bool SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return true; }

bool SDL_RenderGeometry(SDL_Renderer* r, SDL_Texture*, const SDL_Vertex* vertices, int num_vertices,
                        const int* indices, int num_indices) {
    if (!r || !vertices) return false;
    // Fast axis-aligned quad detection (6 indices: 0,1,2, 0,2,3)
    if (num_vertices == 4 && num_indices == 6 &&
        std::fabs(vertices[0].position.y - vertices[1].position.y) < 0.01f &&
        std::fabs(vertices[2].position.y - vertices[3].position.y) < 0.01f &&
        std::fabs(vertices[0].position.x - vertices[3].position.x) < 0.01f &&
        std::fabs(vertices[1].position.x - vertices[2].position.x) < 0.01f) {
        const int clip_x0 = r->has_clip ? std::max(0, r->clip.x) : 0;
        const int clip_y0 = r->has_clip ? std::max(0, r->clip.y) : 0;
        const int clip_x1 = r->has_clip ? std::min(r->w, r->clip.x + r->clip.w) : r->w;
        const int clip_y1 = r->has_clip ? std::min(r->h, r->clip.y + r->clip.h) : r->h;
        const int x0 = std::max(clip_x0, static_cast<int>(std::round(vertices[0].position.x)));
        const int y0 = std::max(clip_y0, static_cast<int>(std::round(vertices[0].position.y)));
        const int x1 = std::min(clip_x1, static_cast<int>(std::round(vertices[2].position.x)));
        const int y1 = std::min(clip_y1, static_cast<int>(std::round(vertices[2].position.y)));
        const uint8_t cr = static_cast<uint8_t>(std::clamp(vertices[0].color.r * 255.0f, 0.0f, 255.0f));
        const uint8_t cg = static_cast<uint8_t>(std::clamp(vertices[0].color.g * 255.0f, 0.0f, 255.0f));
        const uint8_t cb = static_cast<uint8_t>(std::clamp(vertices[0].color.b * 255.0f, 0.0f, 255.0f));
        const uint8_t ca = static_cast<uint8_t>(std::clamp(vertices[0].color.a * 255.0f, 0.0f, 255.0f));
        const bool solid =
            (std::fabs(vertices[0].color.r - vertices[2].color.r) < 0.002f &&
             std::fabs(vertices[0].color.g - vertices[2].color.g) < 0.002f &&
             std::fabs(vertices[0].color.b - vertices[2].color.b) < 0.002f &&
             std::fabs(vertices[0].color.a - vertices[2].color.a) < 0.002f);
        if (solid) {
            for (int y = y0; y < y1; ++y) {
                uint8_t* row = r->pixels.data() + static_cast<size_t>(y * r->w * 4);
                for (int x = x0; x < x1; ++x) {
                    blend_pixel(row + x * 4, cr, cg, cb, ca);
                }
            }
            return true;
        }
    }

    if (indices && num_indices >= 3) {
        for (int i = 0; i + 2 < num_indices; i += 3) {
            rasterize_triangle(r, vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]);
        }
    }
    return true;
}

bool SDL_RenderTexture(SDL_Renderer* r, SDL_Texture* raw_tex, const SDL_FRect*, const SDL_FRect* dst) {
    auto* t = reinterpret_cast<XWineSoftTexture*>(raw_tex);
    if (!r || !t || !dst || dst->w <= 0.5f || dst->h <= 0.5f) return false;
    const int clip_x0 = r->has_clip ? std::max(0, r->clip.x) : 0;
    const int clip_y0 = r->has_clip ? std::max(0, r->clip.y) : 0;
    const int clip_x1 = r->has_clip ? std::min(r->w, r->clip.x + r->clip.w) : r->w;
    const int clip_y1 = r->has_clip ? std::min(r->h, r->clip.y + r->clip.h) : r->h;

    const int x0 = std::max(clip_x0, static_cast<int>(std::round(dst->x)));
    const int y0 = std::max(clip_y0, static_cast<int>(std::round(dst->y)));
    const int x1 = std::min(clip_x1, static_cast<int>(std::round(dst->x + dst->w)));
    const int y1 = std::min(clip_y1, static_cast<int>(std::round(dst->y + dst->h)));
    if (x0 >= x1 || y0 >= y1) return true;

    for (int y = y0; y < y1; ++y) {
        const float v = (static_cast<float>(y - dst->y) + 0.5f) / dst->h;
        const int sy = std::clamp(static_cast<int>(v * static_cast<float>(t->h)), 0, t->h - 1);
        const uint8_t* srow = t->rgba.data() + static_cast<size_t>(sy * t->w * 4);
        uint8_t* drow = r->pixels.data() + static_cast<size_t>(y * r->w * 4);
        for (int x = x0; x < x1; ++x) {
            const float u = (static_cast<float>(x - dst->x) + 0.5f) / dst->w;
            const int sx = std::clamp(static_cast<int>(u * static_cast<float>(t->w)), 0, t->w - 1);
            const uint8_t* sp = srow + sx * 4;
            blend_pixel(drow + x * 4, sp[0], sp[1], sp[2], sp[3]);
        }
    }
    return true;
}

bool SDL_SetRenderClipRect(SDL_Renderer* r, const SDL_Rect* rect) {
    if (!r) return false;
    if (!rect) {
        r->has_clip = false;
    } else {
        r->has_clip = true;
        r->clip = *rect;
    }
    return true;
}

bool SDL_SetRenderDrawColor(SDL_Renderer* r, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha) {
    if (!r) return false;
    r->draw_r = red; r->draw_g = green; r->draw_b = blue; r->draw_a = alpha;
    return true;
}

bool SDL_RenderClear(SDL_Renderer* r) {
    if (!r) return false;
    uint32_t packed = static_cast<uint32_t>(r->draw_r) |
                      (static_cast<uint32_t>(r->draw_g) << 8) |
                      (static_cast<uint32_t>(r->draw_b) << 16) |
                      (0xFF000000U);
    auto* p32 = reinterpret_cast<uint32_t*>(r->pixels.data());
    std::fill(p32, p32 + (r->w * r->h), packed);
    return true;
}

SDL_Surface* SDL_RenderReadPixels(SDL_Renderer*, const SDL_Rect*) { return nullptr; }
SDL_Surface* SDL_ConvertSurface(SDL_Surface*, SDL_PixelFormat) { return nullptr; }

// Stubs for InputManager's SDL gamepad symbols (Xbox UWP polls XInputGetState directly below)
SDL_JoystickID* SDL_GetGamepads(int* count) { if (count) *count = 0; return nullptr; }
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID) { return nullptr; }
void SDL_CloseGamepad(SDL_Gamepad*) {}
const char* SDL_GetGamepadName(SDL_Gamepad*) { return "Xbox Series S Controller (XInput)"; }
Sint16 SDL_GetGamepadAxis(SDL_Gamepad*, SDL_GamepadAxis) { return 0; }
void SDL_free(void*) {}

} // extern "C"

// =============================================================================
// C++/WinRT CoreApplication View + Direct3D 11 SwapChain + XInput Controller Loop
// =============================================================================
struct XWineXboxFrameworkView : implements<XWineXboxFrameworkView, IFrameworkView> {
    CoreWindow window_{nullptr};
    bool window_closed_ = false;

    ComPtr<ID3D11Device1> d3d_device_;
    ComPtr<ID3D11DeviceContext1> d3d_ctx_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<ID3D11Texture2D> staging_tex_;

    void Initialize(CoreApplicationView const&) {}

    void SetWindow(CoreWindow const& window) {
        window_ = window;
        window_.Closed([this](auto&&, auto&&) { window_closed_ = true; });
        init_d3d11_for_corewindow();
    }

    void Load(hstring const&) {}

    void init_d3d11_for_corewindow() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        ComPtr<ID3D11Device> base_dev;
        ComPtr<ID3D11DeviceContext> base_ctx;
        D3D_FEATURE_LEVEL achieved{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       fls, 2, D3D11_SDK_VERSION, &base_dev, &achieved, &base_ctx);
        if (FAILED(hr)) {
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                              fls, 2, D3D11_SDK_VERSION, &base_dev, &achieved, &base_ctx);
        }
        base_dev.As(&d3d_device_);
        base_ctx.As(&d3d_ctx_);

        ComPtr<IDXGIDevice1> dxgi_dev;
        d3d_device_.As(&dxgi_dev);
        ComPtr<IDXGIAdapter> adapter;
        dxgi_dev->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> factory;
        adapter->GetParent(IID_PPV_ARGS(&factory));

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = 1920;
        sd.Height = 1080;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Stereo = FALSE;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        factory->CreateSwapChainForCoreWindow(
            d3d_device_.Get(),
            reinterpret_cast<::IUnknown*>(winrt::get_abi(window_)),
            &sd, nullptr, &swapchain_);

        D3D11_TEXTURE2D_DESC td{};
        td.Width = 1920;
        td.Height = 1080;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d3d_device_->CreateTexture2D(&td, nullptr, &staging_tex_);
    }

    void Run() {
        window_.Activate();

        xwine::launcher::LauncherAppContext ctx;
        ctx.platform = xwine::platform::create_host_platform();
        try {
            auto local_folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
            ctx.data_dir = std::filesystem::path(std::wstring(local_folder)) / "XWine";
        } catch (...) {
            ctx.data_dir = ctx.platform->default_data_directory();
        }

        auto& logger = xwine::platform::Logger::instance();
        logger.enable_file_sink(ctx.data_dir / "logs" / "xwine.log");
        logger.set_stderr_echo(false);
        logger.info("Boot", "Project XWine UWP CoreWindow activated on Xbox Series S!");
        logger.info("Boot", "Host Platform: " + xwine::platform::to_string(ctx.platform->platform_id()));

        ctx.config.reset_to_defaults(ctx.data_dir);
        std::string cfg_err;
        if (!ctx.config.load_from_file(ctx.data_dir / "config.json", &cfg_err)) {
            ctx.config.save_to_file(ctx.data_dir / "config.json");
        }
        ctx.library.set_sandbox_root(ctx.data_dir / "games");
        ctx.runtime.configure_prefix(ctx.data_dir / "prefix");
        std::string lib_err;
        if (!ctx.library.load_from_file(ctx.data_dir / "library.json", &lib_err) ||
            ctx.library.all_games().empty()) {
            ctx.library.seed_demo_fixtures();
            ctx.library.save_to_file(ctx.data_dir / "library.json");
        }

        SDL_Window fake_win{};
        SDL_Renderer sw_renderer{};
        xwine::input::InputManager input;
        xwine::launcher::UiRenderer ui;
        ui.initialize(&fake_win, &sw_renderer);
        xwine::launcher::LauncherState state(ctx);

        WORD prev_buttons = 0;
        float stick_timer = 0.0f;
        auto last_tick = std::chrono::steady_clock::now();

        while (!window_closed_) {
            window_.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

            const auto frame_start = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(frame_start - last_tick).count();
            last_tick = frame_start;

            // Poll Xbox Series S controller via XInputGetState (xinput1_4.dll)
            XINPUT_STATE xs{};
            if (XInputGetState(0, &xs) == ERROR_SUCCESS) {
                const WORD cur = xs.Gamepad.wButtons;
                const WORD pressed = static_cast<WORD>(cur & ~prev_buttons);
                prev_buttons = cur;

                xwine::input::XInputStateSnapshot snap{};
                snap.wButtons = xs.Gamepad.wButtons;
                snap.bLeftTrigger = xs.Gamepad.bLeftTrigger;
                snap.bRightTrigger = xs.Gamepad.bRightTrigger;
                snap.sThumbLX = xs.Gamepad.sThumbLX;
                snap.sThumbLY = xs.Gamepad.sThumbLY;
                snap.sThumbRX = xs.Gamepad.sThumbRX;
                snap.sThumbRY = xs.Gamepad.sThumbRY;
                ctx.runtime.shim_environment().set_gamepad_snapshot(snap, true);

                xwine::input::NormalizedGamepadState gs{};
                gs.connected = true;
                gs.device_name = "Xbox Series S Controller (XInput #0)";
                gs.left_stick.x = static_cast<float>(xs.Gamepad.sThumbLX) / 32767.0f;
                gs.left_stick.y = -static_cast<float>(xs.Gamepad.sThumbLY) / 32767.0f;
                gs.right_stick.x = static_cast<float>(xs.Gamepad.sThumbRX) / 32767.0f;
                gs.right_stick.y = -static_cast<float>(xs.Gamepad.sThumbRY) / 32767.0f;
                gs.left_trigger = static_cast<float>(xs.Gamepad.bLeftTrigger) / 255.0f;
                gs.right_trigger = static_cast<float>(xs.Gamepad.bRightTrigger) / 255.0f;
                input.set_external_gamepad_state(gs);

                auto fire_if = [&](WORD mask, xwine::input::UiAction act) {
                    if ((pressed & mask) != 0) state.handle_action(act);
                };
                fire_if(XINPUT_GAMEPAD_DPAD_UP,        xwine::input::UiAction::NavUp);
                fire_if(XINPUT_GAMEPAD_DPAD_DOWN,      xwine::input::UiAction::NavDown);
                fire_if(XINPUT_GAMEPAD_DPAD_LEFT,      xwine::input::UiAction::NavLeft);
                fire_if(XINPUT_GAMEPAD_DPAD_RIGHT,     xwine::input::UiAction::NavRight);
                fire_if(XINPUT_GAMEPAD_A,              xwine::input::UiAction::Confirm);
                fire_if(XINPUT_GAMEPAD_B,              xwine::input::UiAction::Cancel);
                fire_if(XINPUT_GAMEPAD_X,              xwine::input::UiAction::SecondaryX);
                fire_if(XINPUT_GAMEPAD_Y,              xwine::input::UiAction::TertiaryY);
                fire_if(XINPUT_GAMEPAD_LEFT_SHOULDER,  xwine::input::UiAction::TabPrev);
                fire_if(XINPUT_GAMEPAD_RIGHT_SHOULDER, xwine::input::UiAction::TabNext);
                fire_if(XINPUT_GAMEPAD_START,          xwine::input::UiAction::Menu);
                fire_if(XINPUT_GAMEPAD_BACK,           xwine::input::UiAction::View);

                // Left thumbstick directional repeat
                stick_timer += dt;
                if (stick_timer >= 0.18f) {
                    if (xs.Gamepad.sThumbLX < -16000) { state.handle_action(xwine::input::UiAction::NavLeft); stick_timer = 0.0f; }
                    else if (xs.Gamepad.sThumbLX > 16000) { state.handle_action(xwine::input::UiAction::NavRight); stick_timer = 0.0f; }
                    else if (xs.Gamepad.sThumbLY > 16000) { state.handle_action(xwine::input::UiAction::NavUp); stick_timer = 0.0f; }
                    else if (xs.Gamepad.sThumbLY < -16000) { state.handle_action(xwine::input::UiAction::NavDown); stick_timer = 0.0f; }
                }
            }

            state.update_animations(dt);
            ui.render(state, input, 1.0f);

            if (swapchain_ && d3d_ctx_ && staging_tex_) {
                d3d_ctx_->UpdateSubresource(staging_tex_.Get(), 0, nullptr,
                                            sw_renderer.pixels.data(), 1920 * 4, 1920 * 1080 * 4);
                ComPtr<ID3D11Texture2D> backbuffer;
                if (SUCCEEDED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
                    d3d_ctx_->CopyResource(backbuffer.Get(), staging_tex_.Get());
                }
                swapchain_->Present(1, 0);
            }

            const auto frame_end = std::chrono::steady_clock::now();
            const double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
            ctx.benchmark.record_frame_ms(frame_ms);
        }

        ui.shutdown();
    }

    void Uninitialize() {}
};

struct XWineXboxViewSource : implements<XWineXboxViewSource, IFrameworkViewSource> {
    IFrameworkView CreateView() {
        return make<XWineXboxFrameworkView>();
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    winrt::init_apartment();
    CoreApplication::Run(make<XWineXboxViewSource>());
    return 0;
}

#endif // _WIN32 && XWINE_PLATFORM_XBOX
