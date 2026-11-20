#pragma once

#include "xwine/input/input_manager.hpp"
#include "xwine/launcher/launcher_state.hpp"

#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xwine::launcher {

struct ColorRGBA {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct HitRegion {
    SDL_FRect rect{};
    std::function<void()> on_click;
};

class UiRenderer {
public:
    static constexpr int kLogicalWidth  = 1920;
    static constexpr int kLogicalHeight = 1080;

    UiRenderer();
    ~UiRenderer();

    bool initialize(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    void render(LauncherState& state, const input::InputManager& input, float display_scale);

    // Dispatches mouse click against hit regions recorded during the last frame.
    bool handle_mouse_click(float logical_x, float logical_y);

    // Captures current backbuffer as PNG using libpng for visual QA.
    bool save_screenshot_png(const std::filesystem::path& output_path) const;

private:
    struct CachedText {
        SDL_Texture* texture = nullptr;
        float width = 0.0f;
        float height = 0.0f;
        uint64_t last_used_frame = 0;
    };

    void load_fonts();
    void evict_stale_text_cache();
    CachedText* get_or_render_text(const std::string& text, int px_size, ColorRGBA color, bool mono);
    float measure_text_width(const std::string& text, int px_size, bool mono);

    // Low-level vector/UI primitives
    void draw_rect(SDL_FRect r, ColorRGBA fill);
    void draw_gradient_rect(SDL_FRect r, ColorRGBA top_left, ColorRGBA top_right,
                            ColorRGBA bottom_left, ColorRGBA bottom_right);
    void draw_rounded_rect(SDL_FRect r, float radius, ColorRGBA fill);
    void draw_rounded_border(SDL_FRect r, float radius, float thickness, ColorRGBA color);
    void draw_circle(float cx, float cy, float radius, ColorRGBA color);
    void draw_line_segment(float x0, float y0, float x1, float y1, float thickness, ColorRGBA color);
    void draw_text(const std::string& text, float x, float y, int px_size, ColorRGBA color,
                   bool mono = false, float max_width = 0.0f);
    void draw_text_right(const std::string& text, float right_x, float y, int px_size, ColorRGBA color,
                         bool mono = false);
    void draw_text_centered(const std::string& text, SDL_FRect box, int px_size, ColorRGBA color,
                            bool mono = false);
    void draw_wrapped_text(const std::string& text, float x, float y, float max_width, int px_size,
                           ColorRGBA color, int max_lines = 4);
    void draw_status_indicator(float x, float y, const std::string& label, ColorRGBA dot_color,
                               ColorRGBA text_color, int px_size = 14);
    void draw_keycap(SDL_FRect r, const std::string& label, ColorRGBA bg, ColorRGBA fg);
    void draw_xbox_button(float cx, float cy, float radius, const std::string& label,
                          ColorRGBA fill, ColorRGBA fg);
    void draw_xwine_mark(float x, float y, float size);

    // High-level launcher sections
    void draw_background();
    void draw_top_bar(LauncherState& state, const input::InputManager& input, float display_scale);
    void draw_nav_bar(LauncherState& state);
    void draw_bottom_prompt_bar(const LauncherState& state);
    void draw_toast(const LauncherState& state);

    void draw_game_cover_card(SDL_FRect r, const runtime::GameEntry& game, bool focused,
                              const std::function<void()>& on_click);
    void draw_home_view(LauncherState& state, const input::InputManager& input);
    void draw_library_view(LauncherState& state);
    void draw_game_detail_view(LauncherState& state);
    void draw_steam_view(LauncherState& state);
    void draw_compatibility_view(LauncherState& state);
    void draw_runtimes_view(LauncherState& state);
    void draw_settings_view(LauncherState& state);
    void draw_diagnostics_view(LauncherState& state, const input::InputManager& input);

    SDL_Texture* load_png_cover_cached(const std::string& path);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    FT_Library ft_lib_ = nullptr;
    FT_Face ui_face_ = nullptr;
    FT_Face mono_face_ = nullptr;
    uint64_t frame_counter_ = 0;

    std::unordered_map<std::string, CachedText> text_cache_;
    std::unordered_map<std::string, SDL_Texture*> image_cache_;
    std::vector<HitRegion> hit_regions_;
};

} // namespace xwine::launcher
