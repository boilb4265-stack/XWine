#include "xwine/launcher/ui_renderer.hpp"
#include "xwine/platform/logger.hpp"
#include "xwine/platform/xbox_capabilities.hpp"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

namespace xwine::launcher {

namespace {

// Quiet Obsidian-Slate Neutral Ramp (Unslop + Tasteskill App-Screen discipline)
// Surfaces stay neutral so game cover art and telemetry instruments carry the color.
constexpr ColorRGBA kGround         { 12,  15,  20, 255}; // #0C0F14
constexpr ColorRGBA kSurface        { 19,  23,  31, 255}; // #13171F
constexpr ColorRGBA kSurfaceElev    { 27,  32,  43, 255}; // #1B202B
constexpr ColorRGBA kSurfaceActive  { 36,  43,  56, 255}; // #242B38
constexpr ColorRGBA kChromeBar      {  9,  12,  16, 255}; // #090C10
constexpr ColorRGBA kHairline       {255, 255, 255,  20}; // ~8% white quiet hairline
constexpr ColorRGBA kHairlineStrong {255, 255, 255,  40}; // ~16% white active hairline

// Strictly 3 neutral text roles
constexpr ColorRGBA kTextPrimary    {242, 245, 250, 255}; // #F2F5FA
constexpr ColorRGBA kTextSecondary  {154, 164, 178, 255}; // #9AA4B2
constexpr ColorRGBA kTextMuted      {102, 113, 130, 255}; // #667182

// Disciplined Blue/Cyan Brand Accent (used sparingly: selection, data line, primary action)
constexpr ColorRGBA kAccentBlue     { 14, 165, 233, 255}; // #0EA5E9
constexpr ColorRGBA kAccentCyan     { 34, 211, 238, 255}; // #22D3EE

// Desaturated Semantic Signals (data/status meaning only, never chrome decoration)
constexpr ColorRGBA kSemInfo        { 56, 189, 248, 255}; // Implemented / Inspect Only
constexpr ColorRGBA kSemPlanned     {129, 140, 248, 255}; // Planned
constexpr ColorRGBA kSemWarn        {234, 179,   8, 255}; // Caution / DRM / App Mode
constexpr ColorRGBA kSemDanger      {248, 113, 113, 255}; // Unsupported / Anti-Cheat
constexpr ColorRGBA kSemVerified    { 74, 222, 128, 255}; // Reserved strictly for verified hardware

// Strict 4px Layout Grid Constants (48px equal outer margins -> 1824px inner canvas)
constexpr float kMarginX = 48.0f;
constexpr float kCanvasW = 1824.0f; // 1920 - 2 * 48

SDL_FColor to_fcolor(ColorRGBA c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

ColorRGBA rgb_hex(uint32_t rgb, uint8_t alpha = 255) {
    return {
        static_cast<uint8_t>((rgb >> 16) & 0xFF),
        static_cast<uint8_t>((rgb >> 8) & 0xFF),
        static_cast<uint8_t>(rgb & 0xFF),
        alpha
    };
}

ColorRGBA scale_rgb(ColorRGBA c, float factor, uint8_t alpha = 255) {
    return {
        static_cast<uint8_t>(std::clamp(c.r * factor, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * factor, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * factor, 0.0f, 255.0f)),
        alpha
    };
}

ColorRGBA compat_color(runtime::CompatibilityRating r) {
    switch (r) {
        case runtime::CompatibilityRating::InspectOnly:            return kSemInfo;
        case runtime::CompatibilityRating::Experimental_HostOnly:  return kAccentBlue;
        case runtime::CompatibilityRating::Blocked_32Bit:          return kSemPlanned;
        case runtime::CompatibilityRating::Blocked_DRM:            return kSemWarn;
        case runtime::CompatibilityRating::Blocked_AntiCheat:
        case runtime::CompatibilityRating::Incompatible:           return kSemDanger;
        case runtime::CompatibilityRating::Blocked_MissingRuntime: return kTextSecondary;
        case runtime::CompatibilityRating::Verified_Xbox:          return kSemVerified;
        case runtime::CompatibilityRating::Untested:               return kTextMuted;
    }
    return kTextMuted;
}

const char* compat_badge_code(runtime::CompatibilityRating r) {
    switch (r) {
        case runtime::CompatibilityRating::InspectOnly:            return "PE";
        case runtime::CompatibilityRating::Experimental_HostOnly:  return "EX";
        case runtime::CompatibilityRating::Blocked_32Bit:          return "32";
        case runtime::CompatibilityRating::Blocked_DRM:            return "DM";
        case runtime::CompatibilityRating::Blocked_AntiCheat:      return "AC";
        case runtime::CompatibilityRating::Blocked_MissingRuntime: return "--";
        case runtime::CompatibilityRating::Incompatible:           return "X";
        case runtime::CompatibilityRating::Verified_Xbox:          return "OK";
        case runtime::CompatibilityRating::Untested:               return "?";
    }
    return "?";
}

ColorRGBA status_color(platform::CapabilityStatus s) {
    switch (s) {
        case platform::CapabilityStatus::Implemented:            return kAccentCyan;
        case platform::CapabilityStatus::ExperimentallyVerified: return kSemVerified;
        case platform::CapabilityStatus::Planned:                return kSemPlanned;
        case platform::CapabilityStatus::TechnicallyUnsupported: return kSemDanger;
    }
    return kTextMuted;
}

std::string format_bytes(uint64_t bytes) {
    char buf[64];
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    if (bytes >= static_cast<uint64_t>(kGB)) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / kGB);
    } else if (bytes >= static_cast<uint64_t>(kMB)) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", static_cast<double>(bytes) / kMB);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu KB", static_cast<unsigned long long>(bytes / 1024ULL));
    }
    return buf;
}

std::string format_clock_now() {
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char buf[32];
    int hour = tm_buf.tm_hour % 12;
    if (hour == 0) hour = 12;
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour, tm_buf.tm_min,
                  tm_buf.tm_hour >= 12 ? "PM" : "AM");
    return buf;
}

std::string truncate_str(const std::string& s, size_t max_chars) {
    if (s.size() <= max_chars) return s;
    if (max_chars <= 3) return s.substr(0, max_chars);
    return s.substr(0, max_chars - 3) + "...";
}

} // namespace

UiRenderer::UiRenderer() = default;

UiRenderer::~UiRenderer() {
    shutdown();
}

bool UiRenderer::initialize(SDL_Window* window, SDL_Renderer* renderer) {
    window_ = window;
    renderer_ = renderer;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderLogicalPresentation(renderer_, kLogicalWidth, kLogicalHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    if (FT_Init_FreeType(&ft_lib_) != 0) {
        platform::Logger::instance().error("UI", "FT_Init_FreeType failed; text rendering degraded");
        ft_lib_ = nullptr;
    } else {
        load_fonts();
    }
    return true;
}

void UiRenderer::load_fonts() {
    if (!ft_lib_) return;

    const std::vector<const char*> ui_candidates = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Avenir Next.ttc",
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    for (const char* path : ui_candidates) {
        if (FT_New_Face(ft_lib_, path, 0, &ui_face_) == 0) {
            platform::Logger::instance().info("UI", std::string("Loaded UI font: ") + path);
            break;
        }
    }

    const std::vector<const char*> mono_candidates = {
        "/System/Library/Fonts/SFNSMono.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
    };
    for (const char* path : mono_candidates) {
        if (FT_New_Face(ft_lib_, path, 0, &mono_face_) == 0) {
            platform::Logger::instance().info("UI", std::string("Loaded Mono font: ") + path);
            break;
        }
    }
    if (!mono_face_) mono_face_ = ui_face_;
}

void UiRenderer::shutdown() {
    for (auto& [_, entry] : text_cache_) {
        if (entry.texture) SDL_DestroyTexture(entry.texture);
    }
    text_cache_.clear();

    for (auto& [_, tex] : image_cache_) {
        if (tex) SDL_DestroyTexture(tex);
    }
    image_cache_.clear();

    if (mono_face_ && mono_face_ != ui_face_) {
        FT_Done_Face(mono_face_);
    }
    mono_face_ = nullptr;
    if (ui_face_) {
        FT_Done_Face(ui_face_);
        ui_face_ = nullptr;
    }
    if (ft_lib_) {
        FT_Done_FreeType(ft_lib_);
        ft_lib_ = nullptr;
    }
}

void UiRenderer::evict_stale_text_cache() {
    if (text_cache_.size() < 480) return;
    for (auto it = text_cache_.begin(); it != text_cache_.end();) {
        if (frame_counter_ > it->second.last_used_frame + 120) {
            if (it->second.texture) SDL_DestroyTexture(it->second.texture);
            it = text_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

UiRenderer::CachedText* UiRenderer::get_or_render_text(const std::string& text, int px_size,
                                                       ColorRGBA color, bool mono) {
    if (text.empty() || !renderer_) return nullptr;
    FT_Face face = mono ? mono_face_ : ui_face_;
    if (!face) return nullptr;

    std::string key;
    key.reserve(text.size() + 24);
    key.push_back(mono ? 'M' : 'U');
    key += std::to_string(px_size);
    key.push_back(':');
    key += std::to_string(color.r);
    key.push_back(',');
    key += std::to_string(color.g);
    key.push_back(',');
    key += std::to_string(color.b);
    key.push_back(',');
    key += std::to_string(color.a);
    key.push_back('|');
    key += text;

    auto it = text_cache_.find(key);
    if (it != text_cache_.end()) {
        it->second.last_used_frame = frame_counter_;
        return &it->second;
    }

    constexpr int kSuper = 2;
    const FT_UInt raster_px = static_cast<FT_UInt>(std::max(10, px_size * kSuper));
    FT_Set_Pixel_Sizes(face, 0, raster_px);

    int total_w = 0;
    int max_asc = static_cast<int>(face->size->metrics.ascender >> 6);
    int max_desc = static_cast<int>(-(face->size->metrics.descender >> 6));
    if (max_asc <= 0) max_asc = static_cast<int>(raster_px);
    if (max_desc < 0) max_desc = static_cast<int>(raster_px / 4);

    struct GlyphBmp {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        std::vector<uint8_t> alpha;
    };
    std::vector<GlyphBmp> glyphs;
    glyphs.reserve(text.size());

    int pen_x = 0;
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = 0;
        const unsigned char b0 = static_cast<unsigned char>(text[i]);
        if (b0 < 0x80) {
            cp = b0;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((b0 & 0x0F) << 12) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((b0 & 0x07) << 18) |
                 ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }
        if (cp < 0x20) continue;
        if (FT_Load_Char(face, static_cast<FT_ULong>(cp), FT_LOAD_RENDER) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        GlyphBmp g;
        g.x = pen_x + slot->bitmap_left;
        g.y = max_asc - slot->bitmap_top;
        g.w = static_cast<int>(slot->bitmap.width);
        g.h = static_cast<int>(slot->bitmap.rows);
        if (g.w > 0 && g.h > 0) {
            g.alpha.resize(static_cast<size_t>(g.w * g.h));
            for (int row = 0; row < g.h; ++row) {
                std::memcpy(g.alpha.data() + static_cast<size_t>(row * g.w),
                            slot->bitmap.buffer + static_cast<size_t>(row * std::abs(slot->bitmap.pitch)),
                            static_cast<size_t>(g.w));
            }
        }
        pen_x += static_cast<int>(slot->advance.x >> 6);
        total_w = std::max(total_w, std::max(pen_x, g.x + g.w));
        glyphs.push_back(std::move(g));
    }

    const int tex_w = std::max(1, total_w + 4);
    const int tex_h = std::max(1, max_asc + max_desc + 4);
    std::vector<uint8_t> rgba(static_cast<size_t>(tex_w * tex_h * 4), 0);

    for (const auto& g : glyphs) {
        for (int row = 0; row < g.h; ++row) {
            const int dst_y = g.y + row;
            if (dst_y < 0 || dst_y >= tex_h) continue;
            for (int col = 0; col < g.w; ++col) {
                const int dst_x = g.x + col;
                if (dst_x < 0 || dst_x >= tex_w) continue;
                const uint8_t cov = g.alpha[static_cast<size_t>(row * g.w + col)];
                if (cov == 0) continue;
                const size_t idx = static_cast<size_t>((dst_y * tex_w + dst_x) * 4);
                const uint8_t out_a = static_cast<uint8_t>((static_cast<uint32_t>(cov) * color.a) / 255U);
                rgba[idx + 0] = color.r;
                rgba[idx + 1] = color.g;
                rgba[idx + 2] = color.b;
                rgba[idx + 3] = std::max(rgba[idx + 3], out_a);
            }
        }
    }

    SDL_Surface* surf = SDL_CreateSurfaceFrom(tex_w, tex_h, SDL_PIXELFORMAT_RGBA32,
                                              rgba.data(), tex_w * 4);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_DestroySurface(surf);
    if (!tex) return nullptr;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    CachedText entry;
    entry.texture = tex;
    entry.width = static_cast<float>(tex_w) / static_cast<float>(kSuper);
    entry.height = static_cast<float>(tex_h) / static_cast<float>(kSuper);
    entry.last_used_frame = frame_counter_;
    auto [inserted, _] = text_cache_.emplace(std::move(key), entry);
    return &inserted->second;
}

float UiRenderer::measure_text_width(const std::string& text, int px_size, bool mono) {
    CachedText* ct = get_or_render_text(text, px_size, kTextPrimary, mono);
    return ct ? ct->width : 0.0f;
}

void UiRenderer::draw_rect(SDL_FRect r, ColorRGBA fill) {
    draw_gradient_rect(r, fill, fill, fill, fill);
}

void UiRenderer::draw_gradient_rect(SDL_FRect r, ColorRGBA tl, ColorRGBA tr, ColorRGBA bl, ColorRGBA br) {
    SDL_Vertex verts[4];
    verts[0] = {{r.x, r.y}, to_fcolor(tl), {0, 0}};
    verts[1] = {{r.x + r.w, r.y}, to_fcolor(tr), {1, 0}};
    verts[2] = {{r.x + r.w, r.y + r.h}, to_fcolor(br), {1, 1}};
    verts[3] = {{r.x, r.y + r.h}, to_fcolor(bl), {0, 1}};
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, verts, 4, indices, 6);
}

void UiRenderer::draw_rounded_rect(SDL_FRect r, float radius, ColorRGBA fill) {
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    if (radius <= 1.0f) {
        draw_rect(r, fill);
        return;
    }

    constexpr int kSegs = 6;
    const SDL_FColor fc = to_fcolor(fill);
    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;
    verts.reserve(1 + 4 * (kSegs + 1));
    verts.push_back({{r.x + r.w * 0.5f, r.y + r.h * 0.5f}, fc, {0.5f, 0.5f}});

    const float corners[4][3] = {
        {r.x + r.w - radius, r.y + radius,       -1.5707963f},
        {r.x + r.w - radius, r.y + r.h - radius,  0.0f},
        {r.x + radius,       r.y + r.h - radius,  1.5707963f},
        {r.x + radius,       r.y + radius,        3.1415926f}
    };

    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= kSegs; ++s) {
            const float ang = corners[c][2] + (1.5707963f * static_cast<float>(s) / static_cast<float>(kSegs));
            verts.push_back({{corners[c][0] + std::cos(ang) * radius,
                              corners[c][1] + std::sin(ang) * radius},
                             fc, {0, 0}});
        }
    }

    const int ring_count = static_cast<int>(verts.size()) - 1;
    indices.reserve(static_cast<size_t>(ring_count * 3));
    for (int i = 0; i < ring_count; ++i) {
        indices.push_back(0);
        indices.push_back(1 + i);
        indices.push_back(1 + ((i + 1) % ring_count));
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), static_cast<int>(verts.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void UiRenderer::draw_rounded_border(SDL_FRect r, float radius, float thickness, ColorRGBA color) {
    radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
    constexpr int kSegs = 6;
    const SDL_FColor fc = to_fcolor(color);

    const float outer_corners[4][3] = {
        {r.x + r.w - radius, r.y + radius,       -1.5707963f},
        {r.x + r.w - radius, r.y + r.h - radius,  0.0f},
        {r.x + radius,       r.y + r.h - radius,  1.5707963f},
        {r.x + radius,       r.y + radius,        3.1415926f}
    };
    const float inner_r = std::max(0.5f, radius - thickness);

    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;
    const int ring_pts = 4 * (kSegs + 1);
    verts.reserve(static_cast<size_t>(ring_pts * 2));

    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= kSegs; ++s) {
            const float ang = outer_corners[c][2] + (1.5707963f * static_cast<float>(s) / static_cast<float>(kSegs));
            const float ca = std::cos(ang);
            const float sa = std::sin(ang);
            verts.push_back({{outer_corners[c][0] + ca * radius,
                              outer_corners[c][1] + sa * radius}, fc, {0, 0}});
            verts.push_back({{outer_corners[c][0] + ca * inner_r,
                              outer_corners[c][1] + sa * inner_r}, fc, {0, 0}});
        }
    }

    indices.reserve(static_cast<size_t>(ring_pts * 6));
    for (int i = 0; i < ring_pts; ++i) {
        const int next = (i + 1) % ring_pts;
        const int o0 = i * 2;
        const int i0 = i * 2 + 1;
        const int o1 = next * 2;
        const int i1 = next * 2 + 1;
        indices.push_back(o0); indices.push_back(o1); indices.push_back(i1);
        indices.push_back(o0); indices.push_back(i1); indices.push_back(i0);
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), static_cast<int>(verts.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void UiRenderer::draw_circle(float cx, float cy, float radius, ColorRGBA color) {
    constexpr int kSegs = 20;
    const SDL_FColor fc = to_fcolor(color);
    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;
    verts.reserve(kSegs + 1);
    verts.push_back({{cx, cy}, fc, {0.5f, 0.5f}});
    for (int i = 0; i < kSegs; ++i) {
        const float ang = 6.2831853f * static_cast<float>(i) / static_cast<float>(kSegs);
        verts.push_back({{cx + std::cos(ang) * radius, cy + std::sin(ang) * radius}, fc, {0, 0}});
    }
    for (int i = 0; i < kSegs; ++i) {
        indices.push_back(0);
        indices.push_back(1 + i);
        indices.push_back(1 + ((i + 1) % kSegs));
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), static_cast<int>(verts.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void UiRenderer::draw_line_segment(float x0, float y0, float x1, float y1, float thickness, ColorRGBA color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const float nx = (-dy / len) * (thickness * 0.5f);
    const float ny = (dx / len) * (thickness * 0.5f);
    const SDL_FColor fc = to_fcolor(color);
    SDL_Vertex v[4] = {
        {{x0 + nx, y0 + ny}, fc, {0, 0}},
        {{x1 + nx, y1 + ny}, fc, {1, 0}},
        {{x1 - nx, y1 - ny}, fc, {1, 1}},
        {{x0 - nx, y0 - ny}, fc, {0, 1}}
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
}

void UiRenderer::draw_text(const std::string& text, float x, float y, int px_size, ColorRGBA color,
                           bool mono, float max_width) {
    if (text.empty()) return;
    std::string display = text;
    CachedText* ct = get_or_render_text(display, px_size, color, mono);
    if (!ct || !ct->texture) return;

    if (max_width > 24.0f && ct->width > max_width && display.size() > 6) {
        const double ratio = max_width / ct->width;
        const size_t approx = std::max<size_t>(4, static_cast<size_t>(display.size() * ratio) - 2);
        display = truncate_str(display, approx);
        ct = get_or_render_text(display, px_size, color, mono);
        if (!ct || !ct->texture) return;
    }

    SDL_FRect dst{x, y, ct->width, ct->height};
    SDL_RenderTexture(renderer_, ct->texture, nullptr, &dst);
}

void UiRenderer::draw_text_right(const std::string& text, float right_x, float y, int px_size,
                                 ColorRGBA color, bool mono) {
    CachedText* ct = get_or_render_text(text, px_size, color, mono);
    if (!ct || !ct->texture) return;
    SDL_FRect dst{right_x - ct->width, y, ct->width, ct->height};
    SDL_RenderTexture(renderer_, ct->texture, nullptr, &dst);
}

void UiRenderer::draw_text_centered(const std::string& text, SDL_FRect box, int px_size,
                                    ColorRGBA color, bool mono) {
    CachedText* ct = get_or_render_text(text, px_size, color, mono);
    if (!ct || !ct->texture) return;
    SDL_FRect dst{
        box.x + (box.w - ct->width) * 0.5f,
        box.y + (box.h - ct->height) * 0.5f,
        ct->width, ct->height
    };
    SDL_RenderTexture(renderer_, ct->texture, nullptr, &dst);
}

void UiRenderer::draw_wrapped_text(const std::string& text, float x, float y, float max_width,
                                   int px_size, ColorRGBA color, int max_lines) {
    const float avg_char_w = static_cast<float>(px_size) * 0.50f;
    const size_t chars_per_line = std::max<size_t>(16, static_cast<size_t>(max_width / avg_char_w));
    std::istringstream iss(text);
    std::string word, line;
    int line_num = 0;
    const float line_h = static_cast<float>(px_size) * 1.44f;

    while (iss >> word) {
        if (!line.empty() && line.size() + 1 + word.size() > chars_per_line) {
            if (line_num + 1 >= max_lines) {
                draw_text(truncate_str(line + " " + word, chars_per_line), x, y + line_num * line_h, px_size, color);
                return;
            }
            draw_text(line, x, y + line_num * line_h, px_size, color);
            ++line_num;
            line = word;
        } else {
            if (!line.empty()) line.push_back(' ');
            line += word;
        }
    }
    if (!line.empty() && line_num < max_lines) {
        draw_text(line, x, y + line_num * line_h, px_size, color);
    }
}

void UiRenderer::draw_status_indicator(float x, float y, const std::string& label,
                                       ColorRGBA dot_color, ColorRGBA text_color, int px_size) {
    draw_circle(x + 4.0f, y + static_cast<float>(px_size) * 0.62f, 4.0f, dot_color);
    draw_text(label, x + 16.0f, y, px_size, text_color);
}

void UiRenderer::draw_keycap(SDL_FRect r, const std::string& label, ColorRGBA bg, ColorRGBA fg) {
    draw_rounded_rect(r, 6.0f, bg);
    draw_text_centered(label, r, 15, fg, false);
}

void UiRenderer::draw_xbox_button(float cx, float cy, float radius, const std::string& label,
                                  ColorRGBA fill, ColorRGBA fg) {
    draw_circle(cx, cy, radius, fill);
    draw_text_centered(label, {cx - radius, cy - radius, radius * 2.0f, radius * 2.0f},
                       static_cast<int>(radius * 1.15f), fg, false);
}

void UiRenderer::draw_xwine_mark(float x, float y, float size) {
    // Minimal flat architectural X monogram — no glowing tile or gradient ball
    const float t = size * 0.14f;
    draw_line_segment(x + 2, y + 2, x + size - 2, y + size - 2, t, kTextPrimary);
    draw_line_segment(x + size - 2, y + 2, x + 2, y + size - 2, t, kAccentCyan);
}

SDL_Texture* UiRenderer::load_png_cover_cached(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = image_cache_.find(path);
    if (it != image_cache_.end()) return it->second;

    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        image_cache_[path] = nullptr;
        return nullptr;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        if (png) png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
        std::fclose(fp);
        image_cache_[path] = nullptr;
        return nullptr;
    }

    png_init_io(png, fp);
    png_read_info(png, info);
    const uint32_t w = png_get_image_width(png, info);
    const uint32_t h = png_get_image_height(png, info);
    const png_byte color_type = png_get_color_type(png, info);
    const png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    std::vector<uint8_t> pixels(static_cast<size_t>(w * h * 4));
    std::vector<png_bytep> rows(h);
    for (uint32_t y = 0; y < h; ++y) {
        rows[y] = pixels.data() + static_cast<size_t>(y * w * 4);
    }
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);

    SDL_Surface* surf = SDL_CreateSurfaceFrom(static_cast<int>(w), static_cast<int>(h),
                                              SDL_PIXELFORMAT_RGBA32, pixels.data(), static_cast<int>(w * 4));
    SDL_Texture* tex = surf ? SDL_CreateTextureFromSurface(renderer_, surf) : nullptr;
    if (surf) SDL_DestroySurface(surf);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    image_cache_[path] = tex;
    return tex;
}

void UiRenderer::draw_background() {
    // Flat obsidian ground — zero corner glows, blobs, or hue gradients
    draw_rect({0, 0, static_cast<float>(kLogicalWidth), static_cast<float>(kLogicalHeight)}, kGround);
}

void UiRenderer::draw_top_bar(LauncherState& state, const input::InputManager& input, float display_scale) {
    draw_rect({0, 0, 1920, 60}, kChromeBar);
    draw_rect({0, 59, 1920, 1}, kHairline);

    // Product mark + quiet scope breadcrumb
    draw_xwine_mark(kMarginX, 18, 24);
    draw_text("XWine", kMarginX + 36, 16, 22, kTextPrimary);
    draw_rect({174, 20, 1, 20}, kHairlineStrong);
    draw_status_indicator(192, 20, "Experimental · macOS dev host · Xbox hardware unverified",
                          kSemWarn, kTextSecondary, 14);

    // Right telemetry cluster: quiet plain tabular text, no pill clutter
    const auto fstats = state.context().benchmark.frame_stats();
    const bool pad = input.has_connected_controller();
    const float right_edge = 1920.0f - kMarginX;

    draw_text_right(format_clock_now(), right_edge, 19, 18, kTextPrimary);

    char perf_buf[64];
    std::snprintf(perf_buf, sizeof(perf_buf), "%.0f fps  ·  1080p %.1fx",
                  fstats.current_fps > 0.1 ? fstats.current_fps : 60.0, display_scale);
    draw_text_right(perf_buf, right_edge - 116, 21, 14, kTextSecondary, true);

    draw_rect({right_edge - 340, 20, 1, 20}, kHairlineStrong);
    draw_status_indicator(right_edge - 580, 20,
                          pad ? truncate_str(input.active_controller_name(), 24) : "Keyboard / Mouse",
                          pad ? kAccentCyan : kTextMuted,
                          pad ? kTextPrimary : kTextSecondary, 14);
}

void UiRenderer::draw_nav_bar(LauncherState& state) {
    const float y = 76.0f;
    const float h = 44.0f;

    // Matte physical Xbox shoulder keycaps (LB / RB)
    draw_keycap({kMarginX, y + 6, 48, 32}, "LB", kTextPrimary, kChromeBar);
    hit_regions_.push_back({{kMarginX, y + 6, 48, 32}, [&state]() {
        state.handle_action(input::UiAction::TabPrev);
    }});

    draw_keycap({1920.0f - kMarginX - 48, y + 6, 48, 32}, "RB", kTextPrimary, kChromeBar);
    hit_regions_.push_back({{1920.0f - kMarginX - 48, y + 6, 48, 32}, [&state]() {
        state.handle_action(input::UiAction::TabNext);
    }});

    const float start_x = kMarginX + 72.0f;
    const float total_w = kCanvasW - 144.0f; // 1680 px exact -> 7 * 240 px!
    const int count = static_cast<int>(NavTab::Count);
    const float slot_w = total_w / static_cast<float>(count);

    // Quiet elevated neutral pill for active tab + crisp 2px cyan bottom indicator (no neon glow)
    const float anim_x = start_x + state.animated_tab_index() * slot_w + 8.0f;
    const float pill_w = slot_w - 16.0f;
    draw_rounded_rect({anim_x, y + 2, pill_w, h - 4}, 20.0f, kSurfaceActive);
    draw_rect({anim_x + 28.0f, y + h - 4, pill_w - 56.0f, 2.0f}, kAccentCyan);

    for (int i = 0; i < count; ++i) {
        const auto tab = static_cast<NavTab>(i);
        const bool active = (tab == state.active_tab());
        SDL_FRect tab_r{start_x + i * slot_w + 8.0f, y, pill_w, h};
        draw_text_centered(nav_tab_label(tab), tab_r, 15, active ? kTextPrimary : kTextSecondary);
        hit_regions_.push_back({tab_r, [&state, tab]() { state.set_active_tab(tab); }});
    }

    draw_rect({0, 132, 1920, 1}, kHairline);
}

void UiRenderer::draw_game_cover_card(SDL_FRect r, const runtime::GameEntry& game, bool focused,
                                      const std::function<void()>& on_click) {
    // Rich poster cover art — the one bright moment on the screen, while chrome stays quiet.
    // Focus state: crisp solid white outer frame + subtle cyan hairline, matching the Deck reference (zero breathing glow!).
    const float radius = 8.0f;
    if (focused) {
        draw_rounded_border({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, radius + 3.0f, 3.0f, kTextPrimary);
        draw_rounded_border({r.x - 6, r.y - 6, r.w + 12, r.h + 12}, radius + 5.0f, 1.5f, kAccentCyan);
    }

    draw_rounded_rect(r, radius, kSurface);

    SDL_Texture* png_tex = load_png_cover_cached(game.cover_image_path);
    if (png_tex) {
        SDL_RenderTexture(renderer_, png_tex, nullptr, &r);
    } else {
        const ColorRGBA accent = rgb_hex(game.accent_color_rgb);
        const ColorRGBA top_l = scale_rgb(accent, 0.56f);
        const ColorRGBA top_r = scale_rgb(accent, 0.24f);
        const ColorRGBA bot_l = {14, 18, 26, 255};
        const ColorRGBA bot_r = scale_rgb(accent, 0.16f);

        draw_gradient_rect({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, top_l, top_r, bot_l, bot_r);

        // Distinct architectural graphic motif seeded from game ID
        const float cx = r.x + r.w * 0.5f;
        const float cy = r.y + r.h * 0.37f;
        uint32_t h = 2166136261U;
        for (unsigned char ch : game.id) { h ^= ch; h *= 16777619U; }
        const uint32_t pattern = h % 4;

        if (pattern == 0) {
            draw_circle(cx, cy, r.w * 0.27f, {accent.r, accent.g, accent.b, 55});
            draw_circle(cx, cy, r.w * 0.16f, {242, 245, 250, 42});
            draw_rect({r.x + 28, cy - 2, r.w - 56, 3}, {242, 245, 250, 90});
        } else if (pattern == 1) {
            const float rad = r.w * 0.26f;
            SDL_Vertex dv[4] = {
                {{cx, cy - rad}, to_fcolor({242, 245, 250, 70}), {0, 0}},
                {{cx + rad, cy}, to_fcolor({accent.r, accent.g, accent.b, 85}), {1, 0}},
                {{cx, cy + rad}, to_fcolor({accent.r, accent.g, accent.b, 45}), {1, 1}},
                {{cx - rad, cy}, to_fcolor({242, 245, 250, 35}), {0, 1}}
            };
            const int di[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, dv, 4, di, 6);
            draw_circle(cx, cy, r.w * 0.09f, {12, 15, 20, 185});
        } else if (pattern == 2) {
            draw_circle(cx, cy - 12, r.w * 0.22f, {accent.r, accent.g, accent.b, 90});
            for (int b = 0; b < 5; ++b) {
                draw_rect({r.x + 32, cy + 12 + b * 14.0f, r.w - 64, 5.0f},
                          {12, 15, 20, static_cast<uint8_t>(120 + b * 24)});
            }
        } else {
            draw_rounded_border({cx - 52, cy - 52, 104, 104}, 52.0f, 3.0f, {242, 245, 250, 105});
            draw_circle(cx - 24, cy, r.w * 0.16f, {accent.r, accent.g, accent.b, 70});
            draw_circle(cx + 24, cy, r.w * 0.16f, {34, 211, 238, 45});
        }

        // Top subtle API tag (quiet translucent neutral surface, no neon border)
        const std::string tag = game.pe_graphics_api.empty() ? game.genre : game.pe_graphics_api;
        const float tag_w = std::min(r.w - 64.0f, measure_text_width(tag, 12, true) + 20.0f);
        draw_rounded_rect({r.x + 16, r.y + 16, tag_w, 24}, 4.0f, {12, 15, 20, 185});
        draw_text(tag, r.x + 26, r.y + 21, 12, kTextPrimary, true);

        // Dark neutral bottom scrim for crisp typography contrast
        draw_gradient_rect({r.x + 1, r.y + r.h * 0.50f, r.w - 2, r.h * 0.50f - 1},
                           {12, 15, 20, 0}, {12, 15, 20, 0},
                           {12, 15, 20, 242}, {12, 15, 20, 250});

        draw_wrapped_text(game.title, r.x + 16, r.y + r.h - 112, r.w - 32, 20, kTextPrimary, 2);
        draw_text(truncate_str(game.developer, 26), r.x + 16, r.y + r.h - 48, 13, kTextSecondary);
        draw_text(format_bytes(game.size_on_disk_bytes), r.x + 16, r.y + r.h - 28, 12, kTextMuted, true);
    }

    // Favorite mark (quiet neutral backing)
    if (game.favorite) {
        draw_rounded_rect({r.x + r.w - 36, r.y + 16, 20, 24}, 4.0f, {12, 15, 20, 185});
        draw_text_centered("★", {r.x + r.w - 36, r.y + 15, 20, 24}, 14, kAccentCyan);
    }

    // Bottom-right honest compatibility corner badge (Deck reference position, honest semantics)
    const ColorRGBA badge_col = compat_color(game.compatibility);
    draw_circle(r.x + r.w - 26, r.y + r.h - 26, 17, {12, 15, 20, 235});
    draw_circle(r.x + r.w - 26, r.y + r.h - 26, 14, badge_col);
    draw_text_centered(compat_badge_code(game.compatibility),
                       {r.x + r.w - 40, r.y + r.h - 40, 28, 28}, 11, kChromeBar, true);

    if (!focused) {
        draw_rounded_border(r, radius, 1.0f, kHairline);
    }

    if (on_click) {
        hit_regions_.push_back({r, on_click});
    }
}

void UiRenderer::draw_home_view(LauncherState& state, const input::InputManager& input) {
    const auto& ctx = state.context();
    const bool game_mode = ctx.config.config().runtime.xbox_target_mode == "GameMode_5GB";
    const auto host = ctx.platform->host_hardware();
    const auto xbox = ctx.platform->target_xbox_series_s_profile(game_mode);
    const auto storage = ctx.platform->query_storage(ctx.data_dir);
    const auto metrics = ctx.platform->query_metrics(xbox.app_usable_ram_bytes);

    // Clean flat title row — no hero box wrapper
    draw_text("Overview", kMarginX, 156, 28, kTextPrimary);
    draw_text("Windows PE inspection and Xbox Series S Developer Mode compatibility workspace",
              kMarginX, 192, 15, kTextSecondary);
    draw_text_right("Target: " + xbox.device_name, kMarginX + kCanvasW, 166, 14, kTextSecondary, true);
    draw_text_right("Hardware execution verified: No (Phase 6 pending)",
                    kMarginX + kCanvasW, 190, 14, kSemWarn);

    // One flat hairline-divided KPI strip (app-screens.md §5: no tile-in-tile, no icons in circles)
    SDL_FRect strip{kMarginX, 232, kCanvasW, 92};
    draw_rounded_rect(strip, 10.0f, kSurface);
    draw_rounded_border(strip, 10.0f, 1.0f, kHairline);

    const size_t total_cnt = ctx.library.all_games().size();
    const size_t fav_cnt = ctx.library.query(runtime::LibraryFilter::Favorites, runtime::LibrarySort::TitleAsc).size();
    const size_t inspected_cnt = ctx.library.query(runtime::LibraryFilter::Inspected, runtime::LibrarySort::TitleAsc).size();

    const float col_w = kCanvasW / 4.0f; // 456 px exact
    struct KpiItem { std::string label; std::string value; std::string sub; ColorRGBA accent; };
    const KpiItem kpis[4] = {
        {"Catalog titles", std::to_string(total_cnt),
         std::to_string(inspected_cnt) + " inspected · " + std::to_string(fav_cnt) + " favorites", kTextPrimary},
        {"Xbox UWP RAM ceiling", format_bytes(xbox.app_usable_ram_bytes),
         game_mode ? "Game mode (50% of 10 GB GDDR6)" : "App mode (switch to Game mode)", game_mode ? kAccentCyan : kSemWarn},
        {"Host storage free", format_bytes(storage.available_bytes),
         format_bytes(storage.used_by_xwine_bytes) + " used by XWine data", kTextPrimary},
        {"Process memory & input", format_bytes(metrics.process_rss_bytes),
         input.has_connected_controller() ? "Xbox gamepad active" : "Keyboard and mouse active", kTextPrimary}
    };

    for (int i = 0; i < 4; ++i) {
        const float cx = strip.x + i * col_w;
        if (i > 0) {
            draw_rect({cx, strip.y + 16, 1, strip.h - 32}, kHairline);
        }
        draw_text(kpis[i].label, cx + 24, strip.y + 14, 13, kTextMuted);
        draw_text(kpis[i].value, cx + 24, strip.y + 34, 26, kpis[i].accent);
        draw_text(kpis[i].sub, cx + 24, strip.y + 66, 12, kTextSecondary);
    }

    // Dominant visual moment: 5-card 2:3 game cover grid (5 * 344 + 4 * 26 = 1824 px exact)
    draw_text("Recently inspected", kMarginX, 356, 20, kTextPrimary);
    draw_text_right("Select a title to inspect PE headers and Xbox blockers",
                    kMarginX + kCanvasW, 360, 14, kTextMuted);

    auto featured = state.home_featured_games();
    const float card_w = 344.0f;
    const float card_h = 516.0f; // Exact 2:3 aspect ratio!
    const float gap = 26.0f;

    for (size_t i = 0; i < featured.size(); ++i) {
        const auto* g = featured[i];
        SDL_FRect cr{kMarginX + static_cast<float>(i) * (card_w + gap), 400, card_w, card_h};
        const bool focused = (static_cast<int>(i) == state.home_selected_index());
        std::string gid = g->id;
        draw_game_cover_card(cr, *g, focused, [&state, gid]() {
            state.open_game_detail(gid);
        });
    }

    // Quiet single-line host footer metadata
    draw_text("Host: " + host.cpu_model + " (" + host.cpu_architecture + ")  ·  " +
                  std::to_string(host.logical_cores) + " logical cores  ·  " +
                  format_bytes(host.physical_ram_bytes) + " unified memory",
              kMarginX, 948, 13, kTextMuted, true);
}

void UiRenderer::draw_library_view(LauncherState& state) {
    const auto& lib = state.context().library;

    // Clean segmented filter row (Deck reference style: neutral pill for active, quiet counts)
    const struct { runtime::LibraryFilter f; const char* name; } filters[] = {
        {runtime::LibraryFilter::All,            "All games"},
        {runtime::LibraryFilter::Installed,      "Installed"},
        {runtime::LibraryFilter::Favorites,      "Favorites"},
        {runtime::LibraryFilter::Inspected,      "Inspected"},
        {runtime::LibraryFilter::DrmFree,        "DRM-free"},
        {runtime::LibraryFilter::RecentlyPlayed, "Recent"}
    };

    float fx = kMarginX;
    for (const auto& item : filters) {
        const size_t cnt = lib.query(item.f, runtime::LibrarySort::TitleAsc, "").size();
        const std::string label = std::string(item.name) + "  " + std::to_string(cnt);
        const bool active = (state.library_filter() == item.f);
        const float w = measure_text_width(label, 14, false) + 32.0f;
        SDL_FRect pr{fx, 152, w, 36};
        if (active) {
            draw_rounded_rect(pr, 18.0f, kSurfaceActive);
            draw_rounded_border(pr, 18.0f, 1.0f, kHairlineStrong);
        }
        draw_text_centered(label, pr, 14, active ? kTextPrimary : kTextSecondary);
        const auto f_val = item.f;
        hit_regions_.push_back({pr, [&state, f_val]() { state.set_library_filter(f_val); }});
        fx += w + 8.0f;
    }

    // Search input (short label: "Search", radius 8px — not everything is a pill)
    const float right_edge = kMarginX + kCanvasW;
    const char* sort_names[] = {"Sort: Title", "Sort: Added", "Sort: Recent", "Sort: Size"};
    SDL_FRect sort_r{right_edge - 160, 152, 160, 36};
    draw_rounded_rect(sort_r, 8.0f, kSurface);
    draw_rounded_border(sort_r, 8.0f, 1.0f, kHairline);
    draw_text_centered(sort_names[static_cast<int>(state.library_sort())], sort_r, 14, kTextSecondary);
    hit_regions_.push_back({sort_r, [&state]() { state.cycle_library_sort(); }});

    SDL_FRect search_r{right_edge - 160 - 12 - 340, 152, 340, 36};
    draw_rounded_rect(search_r, 8.0f, kSurface);
    draw_rounded_border(search_r, 8.0f, state.search_focused() ? 1.5f : 1.0f,
                        state.search_focused() ? kAccentCyan : kHairline);
    std::string search_disp = state.search_query().empty()
                                  ? (state.search_focused() ? "Type to filter" : "Search  (F)")
                                  : state.search_query() + (state.search_focused() ? "_" : "");
    draw_text(search_disp, search_r.x + 14, search_r.y + 8, 14,
              state.search_query().empty() ? kTextMuted : kTextPrimary);
    hit_regions_.push_back({search_r, [&state]() { state.set_search_focused(true); }});

    // 5-Column Vertical 2:3 Cover Grid (5 * 344 + 4 * 26 = 1824 px exact!)
    auto visible = state.visible_library_games();
    if (visible.empty()) {
        SDL_FRect empty_box{kMarginX, 212, kCanvasW, 240};
        draw_rounded_rect(empty_box, 12.0f, kSurface);
        draw_rounded_border(empty_box, 12.0f, 1.0f, kHairline);
        draw_text_centered("No titles match this filter", {kMarginX, 272, kCanvasW, 36}, 22, kTextPrimary);
        draw_text_centered("Press X to cycle filters or import Steam .acf manifests in Steam Library",
                           {kMarginX, 316, kCanvasW, 32}, 15, kTextSecondary);
        return;
    }

    constexpr int kCols = 5;
    const float card_w = 344.0f;
    const float card_h = 500.0f;
    const float gap_x = 26.0f;
    const float gap_y = 28.0f;
    const int start_row = state.library_scroll_row();

    SDL_Rect clip{0, 200, 1920, 796};
    SDL_SetRenderClipRect(renderer_, &clip);

    for (size_t i = 0; i < visible.size(); ++i) {
        const int row = static_cast<int>(i) / kCols - start_row;
        const int col = static_cast<int>(i) % kCols;
        if (row < 0 || row > 2) continue;

        SDL_FRect cr{
            kMarginX + col * (card_w + gap_x),
            212.0f + row * (card_h + gap_y),
            card_w, card_h
        };
        const bool focused = (static_cast<int>(i) == state.library_selected_index());
        std::string gid = visible[i]->id;
        int idx_copy = static_cast<int>(i);
        draw_game_cover_card(cr, *visible[i], focused, [&state, gid, idx_copy]() {
            state.select_library_index(idx_copy);
            state.open_game_detail(gid);
        });
    }

    SDL_SetRenderClipRect(renderer_, nullptr);
}

void UiRenderer::draw_game_detail_view(LauncherState& state) {
    const auto* game = state.detail_game();
    if (!game) return;

    // Emil Design-Eng: modal enters from scale(0.97) + subtle vertical settle, never scale(0)
    const float p = std::clamp(state.detail_enter_progress(), 0.0f, 1.0f);
    const float slide_y = (1.0f - p) * 12.0f;

    draw_rect({0, 133, 1920, 871}, {10, 13, 18, 248});

    // Left column: Cover art (344 x 516) + crisp action controls (aligned to 48px left margin)
    draw_game_cover_card({kMarginX, 164 + slide_y, 344, 516}, *game, true, nullptr);

    const std::string action_labels[4] = {
        "Inspect PE & run feasibility check",
        game->favorite ? "Remove from favorites" : "Add to favorites",
        "Graphics: " + truncate_str(game->launch_options.graphics_backend, 20),
        "Back to library"
    };

    const float press_s = state.press_feedback_scale();
    for (int i = 0; i < 4; ++i) {
        const bool sel = (state.detail_action_index() == i);
        const bool is_primary = (i == 0);
        float bw = 344.0f;
        float bh = 44.0f;
        float bx = kMarginX;
        float by = 704.0f + slide_y + i * 56.0f;
        if (sel && press_s < 0.999f) {
            const float dw = bw * (1.0f - press_s);
            const float dh = bh * (1.0f - press_s);
            bx += dw * 0.5f; by += dh * 0.5f; bw -= dw; bh -= dh;
        }
        SDL_FRect br{bx, by, bw, bh};

        // Unslop §2: On dark theme, primary button is crisp off-white with dark text; selection has cyan edge
        ColorRGBA bg = is_primary ? (sel ? kTextPrimary : kSurfaceElev)
                                  : (sel ? kSurfaceActive : kSurface);
        ColorRGBA fg = (is_primary && sel) ? kChromeBar : kTextPrimary;
        draw_rounded_rect(br, 8.0f, bg);
        draw_rounded_border(br, 8.0f, sel ? 1.5f : 1.0f, sel ? kAccentCyan : kHairline);
        draw_text_centered(action_labels[i], br, 14, fg);
        hit_regions_.push_back({br, [&state, i]() { state.execute_detail_action(i); }});
    }

    // Right column: exact canvas fill (48 + 344 + 32 = 424; width = 1872 - 424 = 1448)
    SDL_FRect panel{424, 164 + slide_y, 1448, 752};
    draw_rounded_rect(panel, 12.0f, kSurface);
    draw_rounded_border(panel, 12.0f, 1.0f, kHairline);

    draw_text(game->title, 456, 192 + slide_y, 30, kTextPrimary);
    draw_text(game->developer + "  ·  " + game->genre + "  ·  " + game->source + "  ·  " +
                  format_bytes(game->size_on_disk_bytes),
              456, 232 + slide_y, 14, kTextSecondary);

    // Right-aligned honest status indicator (dot + text, no neon border capsule)
    const ColorRGBA cc = compat_color(game->compatibility);
    const std::string rating_str = runtime::to_string(game->compatibility);
    const float rating_w = measure_text_width(rating_str, 15, false) + 20.0f;
    draw_status_indicator(panel.x + panel.w - 32.0f - rating_w, 200 + slide_y,
                          rating_str, cc, cc, 15);

    draw_wrapped_text(game->description, 456, 268 + slide_y, 1380, 16, kTextPrimary, 3);
    draw_rect({456, 336 + slide_y, 1384, 1}, kHairline);

    // Two balanced sub-columns (680 + 24 + 680 = 1384 exact)
    const float sub_y = 356.0f + slide_y;
    const float sub_h = 240.0f;

    // Sub-column 1: Static PE Metadata
    SDL_FRect pe_box{456, sub_y, 680, sub_h};
    draw_rounded_rect(pe_box, 8.0f, kGround);
    draw_rounded_border(pe_box, 8.0f, 1.0f, kHairline);
    draw_text("Static PE executable metadata", pe_box.x + 20, pe_box.y + 16, 15, kTextPrimary);

    auto draw_kv = [&](float y_off, const std::string& k, const std::string& v, ColorRGBA vc = kTextPrimary) {
        draw_text(k, pe_box.x + 20, pe_box.y + y_off, 14, kTextMuted);
        draw_text(v, pe_box.x + 170, pe_box.y + y_off, 14, vc, true);
    };
    draw_kv(50,  "Architecture", game->pe_architecture.empty() ? "—" : game->pe_architecture);
    draw_kv(78,  "PE format",    (game->pe_format.empty() ? "—" : game->pe_format) +
                                     (game->pe_subsystem.empty() ? "" : " · " + game->pe_subsystem));
    draw_kv(106, "Graphics API", game->pe_graphics_api.empty() ? "None detected" : game->pe_graphics_api, kAccentCyan);
    draw_kv(134, "DRM-free",     game->drm_free_declared ? "Declared yes" : "No / unverified",
            game->drm_free_declared ? kTextPrimary : kSemWarn);

    std::string dlls_line;
    for (size_t i = 0; i < game->pe_imported_dlls.size() && i < 6; ++i) {
        if (i > 0) dlls_line += ", ";
        dlls_line += game->pe_imported_dlls[i];
    }
    draw_text("Imports (" + std::to_string(game->pe_imported_dlls.size()) + ")", pe_box.x + 20, pe_box.y + 166, 14, kTextMuted);
    draw_wrapped_text(dlls_line.empty() ? "None parsed" : dlls_line, pe_box.x + 170, pe_box.y + 166, 490, 13, kTextSecondary, 2);

    // Sub-column 2: Honest Xbox Blockers
    SDL_FRect blk_box{456 + 680 + 24, sub_y, 680, sub_h};
    draw_rounded_rect(blk_box, 8.0f, kGround);
    draw_rounded_border(blk_box, 8.0f, 1.0f, kHairline);
    draw_text("Xbox Series S Dev Mode assessment", blk_box.x + 20, blk_box.y + 16, 15, kTextPrimary);
    draw_wrapped_text(game->compatibility_summary, blk_box.x + 20, blk_box.y + 46, 640, 14, kTextSecondary, 2);

    float by = blk_box.y + 100.0f;
    for (size_t i = 0; i < game->blockers.size() && i < 4; ++i) {
        draw_status_indicator(blk_box.x + 20, by, truncate_str(game->blockers[i], 70),
                              kSemDanger, kTextPrimary, 13);
        by += 32.0f;
    }

    // Bottom section: Live Dry-Run Report or Launch Config
    SDL_FRect diag_box{456, 616 + slide_y, 1384, 276};
    draw_rounded_rect(diag_box, 8.0f, kGround);
    draw_rounded_border(diag_box, 8.0f, 1.0f, kHairline);

    if (const auto& rep = state.last_launch_report()) {
        draw_status_indicator(diag_box.x + 20, diag_box.y + 18,
                              "Feasibility check result — executed: false", kSemWarn, kSemWarn, 15);
        draw_text(rep->summary, diag_box.x + 20, diag_box.y + 48, 15, kTextPrimary);
        float ry = diag_box.y + 84.0f;
        for (size_t i = 0; i < rep->blockers.size() && i < 5; ++i) {
            draw_text(truncate_str(rep->blockers[i], 122), diag_box.x + 36, ry, 13, kTextSecondary, true);
            draw_circle(diag_box.x + 24, ry + 8, 2.5f, kTextMuted);
            ry += 34.0f;
        }
    } else {
        draw_text("Launch configuration", diag_box.x + 20, diag_box.y + 18, 15, kTextPrimary);
        draw_text("Runtime profile", diag_box.x + 20, diag_box.y + 56, 14, kTextMuted);
        draw_text(game->launch_options.runtime_profile, diag_box.x + 200, diag_box.y + 56, 14, kTextPrimary, true);
        draw_text("Graphics strategy", diag_box.x + 20, diag_box.y + 86, 14, kTextMuted);
        draw_text(game->launch_options.graphics_backend, diag_box.x + 200, diag_box.y + 86, 14, kAccentCyan, true);
        draw_text("Controller map", diag_box.x + 20, diag_box.y + 116, 14, kTextMuted);
        draw_text(game->launch_options.controller_profile, diag_box.x + 200, diag_box.y + 116, 14, kTextPrimary, true);
        draw_wrapped_text(
            "Select 'Inspect PE & run feasibility check' to run live static PE verification and record all Xbox Series S "
            "blockers to the diagnostic log. XWine never fakes game execution.",
            diag_box.x + 20, diag_box.y + 164, 1340, 14, kTextSecondary, 2);
    }
}

void UiRenderer::draw_steam_view(LauncherState& state) {
    const auto& steam_path = state.context().config.config().storage.steam_library_path;

    // Clean flat title + primary/secondary actions (no nested boxes)
    draw_text("Steam library metadata", kMarginX, 156, 28, kTextPrimary);
    draw_text("Parses libraryfolders.vdf and appmanifest_*.acf for owned metadata only — never bypasses Steam DRM or anti-cheat",
              kMarginX, 192, 14, kTextSecondary);

    const float right_edge = kMarginX + kCanvasW;
    SDL_FRect btn_scan{right_edge - 436, 160, 212, 42};
    draw_rounded_rect(btn_scan, 8.0f, kTextPrimary);
    draw_text_centered("Scan local Steam", btn_scan, 14, kChromeBar);
    hit_regions_.push_back({btn_scan, [&state]() { state.scan_configured_steam_library(); }});

    SDL_FRect btn_sample{right_edge - 212, 160, 212, 42};
    draw_rounded_rect(btn_sample, 8.0f, kSurfaceElev);
    draw_rounded_border(btn_sample, 8.0f, 1.0f, kHairlineStrong);
    draw_text_centered("Parse sample ACF", btn_sample, 14, kTextPrimary);
    hit_regions_.push_back({btn_sample, [&state]() { state.load_sample_steam_manifests(); }});

    // Path bar
    SDL_FRect path_bar{kMarginX, 232, kCanvasW, 48};
    draw_rounded_rect(path_bar, 8.0f, kSurface);
    draw_rounded_border(path_bar, 8.0f, 1.0f, kHairline);
    draw_text("Configured steamapps path", kMarginX + 20, 248, 13, kTextMuted);
    draw_text(steam_path.empty() ? "Not configured" : steam_path, kMarginX + 228, 247, 14, kTextPrimary, true);

    // Table panel (app-screens.md §7: hairline rows, aligned columns, right-aligned numbers)
    SDL_FRect table{kMarginX, 304, kCanvasW, 672};
    draw_rounded_rect(table, 12.0f, kSurface);
    draw_rounded_border(table, 12.0f, 1.0f, kHairline);

    const auto& scan = state.last_steam_scan();
    draw_text("Imported manifests (" + std::to_string(scan.manifests.size()) + ")",
              kMarginX + 24, 326, 16, kTextPrimary);

    if (scan.manifests.empty()) {
        draw_text_centered("No Steam manifests scanned in this session", {kMarginX, 540, kCanvasW, 36}, 20, kTextPrimary);
        draw_text_centered("Use Scan local Steam to inspect an existing Steam folder, or Parse sample ACF to verify the Valve VDF parser",
                           {kMarginX, 582, kCanvasW, 32}, 14, kTextSecondary);
        if (!scan.warnings.empty()) {
            draw_text_centered(scan.warnings.front(), {kMarginX, 626, kCanvasW, 28}, 13, kSemWarn, true);
        }
        return;
    }

    draw_text("App ID",       kMarginX + 24,  372, 13, kTextMuted);
    draw_text("Title",        kMarginX + 160, 372, 13, kTextMuted);
    draw_text("Install folder", kMarginX + 760, 372, 13, kTextMuted);
    draw_text_right("Disk size", kMarginX + 1320, 372, 13, kTextMuted);
    draw_text("Xbox status",  kMarginX + 1380, 372, 13, kTextMuted);
    draw_rect({kMarginX + 24, 400, kCanvasW - 48, 1}, kHairline);

    float ry = 408.0f;
    for (size_t i = 0; i < scan.manifests.size() && i < 10; ++i) {
        const auto& m = scan.manifests[i];
        const bool sel = (static_cast<int>(i) == state.steam_selected_index());
        SDL_FRect rr{kMarginX + 16, ry, kCanvasW - 32, 52};
        if (sel) {
            draw_rounded_rect(rr, 6.0f, kSurfaceActive);
        }
        draw_text(std::to_string(m.app_id), kMarginX + 24, ry + 16, 14, kTextSecondary, true);
        draw_text(truncate_str(m.name, 46), kMarginX + 160, ry + 15, 16, kTextPrimary);
        draw_text(truncate_str(m.install_dir, 34), kMarginX + 760, ry + 16, 14, kTextSecondary, true);
        draw_text_right(format_bytes(m.size_on_disk_bytes), kMarginX + 1320, ry + 16, 14, kTextPrimary, true);
        draw_status_indicator(kMarginX + 1380, ry + 16, "Metadata only · Not Xbox executable",
                              kSemWarn, kTextSecondary, 14);
        draw_rect({kMarginX + 24, ry + 52, kCanvasW - 48, 1}, kHairline);

        std::string gid = "steam-" + std::to_string(m.app_id);
        hit_regions_.push_back({rr, [&state, gid]() { state.open_game_detail(gid); }});
        ry += 54.0f;
    }
}

void UiRenderer::draw_compatibility_view(LauncherState& state) {
    const auto caps = state.context().platform->capability_matrix();
    const auto& dlls = state.context().runtime.api_catalog().entries();

    draw_text("Compatibility matrix", kMarginX, 156, 28, kTextPrimary);
    draw_text("Strict separation between implemented components, planned Phase 4–7 work, and unsupported Xbox AppContainer limits",
              kMarginX, 192, 14, kTextSecondary);

    // Two equal balanced columns: 900 + 24 + 900 = 1824 px exact, sharing bottom edge at y = 976
    const float col_w = 900.0f;
    const float panel_y = 232.0f;
    const float panel_h = 744.0f;

    SDL_FRect left{kMarginX, panel_y, col_w, panel_h};
    draw_rounded_rect(left, 12.0f, kSurface);
    draw_rounded_border(left, 12.0f, 1.0f, kHairline);
    draw_text("Xbox Series S Dev Mode capabilities", left.x + 24, left.y + 20, 16, kTextPrimary);
    draw_rect({left.x + 24, left.y + 52, col_w - 48, 1}, kHairline);

    float y = left.y + 60.0f;
    for (size_t i = 0; i < caps.size() && i < 12; ++i) {
        const auto& c = caps[i];
        const ColorRGBA sc = status_color(c.status);
        draw_text(truncate_str(c.name, 48), left.x + 24, y + 4, 14, kTextPrimary);
        draw_text(truncate_str(c.details, 74), left.x + 24, y + 26, 12, kTextMuted);
        draw_status_indicator(left.x + col_w - 208, y + 14, platform::to_string(c.status), sc, sc, 13);
        if (i + 1 < 12) {
            draw_rect({left.x + 24, y + 54, col_w - 48, 1}, kHairline);
        }
        y += 56.0f;
    }

    SDL_FRect right{kMarginX + col_w + 24.0f, panel_y, col_w, panel_h};
    draw_rounded_rect(right, 12.0f, kSurface);
    draw_rounded_border(right, 12.0f, 1.0f, kHairline);
    draw_text("Win32 DLL to OneCoreUAP map", right.x + 24, right.y + 20, 16, kTextPrimary);
    draw_text_right("Up / Down to scroll", right.x + col_w - 24, right.y + 22, 13, kTextMuted);
    draw_rect({right.x + 24, right.y + 52, col_w - 48, 1}, kHairline);

    const int offset = state.compat_scroll_offset();
    float dy = right.y + 60.0f;
    int shown = 0;
    for (size_t idx = static_cast<size_t>(offset); idx < dlls.size() && shown < 11; ++idx, ++shown) {
        const auto& d = dlls[idx];
        ColorRGBA col = kSemInfo;
        if (d.xbox_uwp_support == runtime::Win32DllSupport::NativeOneCoreUAP) col = kAccentCyan;
        else if (d.xbox_uwp_support == runtime::Win32DllSupport::TechnicallyUnsupported) col = kSemDanger;
        else if (d.xbox_uwp_support == runtime::Win32DllSupport::RequiresTranslation) col = kSemWarn;

        draw_text(d.dll_name, right.x + 24, dy + 4, 14, kTextPrimary, true);
        draw_text(d.category, right.x + 196, dy + 5, 13, kTextMuted);
        draw_status_indicator(right.x + col_w - 216, dy + 4, runtime::to_string(d.xbox_uwp_support), col, col, 13);
        draw_text(truncate_str(d.xbox_strategy_note, 92), right.x + 24, dy + 28, 12, kTextSecondary);
        if (shown + 1 < 11) {
            draw_rect({right.x + 24, dy + 58, col_w - 48, 1}, kHairline);
        }
        dy += 61.0f;
    }
}

void UiRenderer::draw_runtimes_view(LauncherState& state) {
    const auto& backends = state.context().runtime.backends();
    const auto routes = state.context().graphics.all_xbox_routes();
    const bool game_mode = state.context().config.config().runtime.xbox_target_mode == "GameMode_5GB";

    draw_text("Runtimes & graphics architecture", kMarginX, 156, 28, kTextPrimary);
    draw_text("Xbox Series S runs Windows NT with native Direct3D 12 and no Vulkan ICD — graphics translation is decoupled from Wine",
              kMarginX, 192, 14, kTextSecondary);

    // Memory Budget Stacked Bar (app-screens.md §6.1: 100% stacked bar with 2px seams)
    SDL_FRect mem_card{kMarginX, 232, kCanvasW, 132};
    draw_rounded_rect(mem_card, 12.0f, kSurface);
    draw_rounded_border(mem_card, 12.0f, 1.0f, kHairline);

    draw_text("Xbox Series S memory allocation (10 GB physical GDDR6)", kMarginX + 24, 250, 15, kTextPrimary);
    draw_text_right(game_mode ? "Active target: UWP Game Mode (5,120 MiB)" : "Active target: UWP App Mode (1,024 MiB)",
                    kMarginX + kCanvasW - 24, 251, 14, game_mode ? kAccentCyan : kSemWarn, true);

    const float bar_x = kMarginX + 24;
    const float bar_y = 286.0f;
    const float bar_w = kCanvasW - 48; // 1776 px
    const float bar_h = 28.0f;
    draw_rounded_rect({bar_x, bar_y, bar_w * 0.10f - 2, bar_h}, 4.0f, kSemWarn);
    draw_rounded_rect({bar_x + bar_w * 0.10f + 2, bar_y, bar_w * 0.40f - 4, bar_h}, 4.0f, kAccentBlue);
    draw_rounded_rect({bar_x + bar_w * 0.50f + 2, bar_y, bar_w * 0.50f - 2, bar_h}, 4.0f, kSurfaceActive);

    draw_text("1.0 GB App cap", bar_x, 326, 13, kSemWarn, true);
    draw_text("5.0 GB Game Mode ceiling (shared CPU + GPU)", bar_x + bar_w * 0.10f + 8, 326, 13, kTextPrimary, true);
    draw_text("5.0 GB Hypervisor / System OS reserved", bar_x + bar_w * 0.50f + 8, 326, 13, kTextMuted, true);

    // Balanced Bottom 2-Column Split: 900 + 24 + 900 = 1824 px, height = 588 px (y = 388..976)
    const float col_w = 900.0f;
    const float bot_y = 388.0f;
    const float bot_h = 588.0f;

    SDL_FRect rt_card{kMarginX, bot_y, col_w, bot_h};
    draw_rounded_rect(rt_card, 12.0f, kSurface);
    draw_rounded_border(rt_card, 12.0f, 1.0f, kHairline);
    draw_text("Runtime backends", rt_card.x + 24, rt_card.y + 20, 16, kTextPrimary);
    draw_rect({rt_card.x + 24, rt_card.y + 52, col_w - 48, 1}, kHairline);

    float ry = rt_card.y + 64.0f;
    for (size_t i = 0; i < backends.size(); ++i) {
        const auto& b = backends[i];
        const ColorRGBA sc = status_color(b.status);
        draw_text(b.name, rt_card.x + 24, ry, 15, kTextPrimary);
        draw_text(b.version, rt_card.x + 24, ry + 22, 12, kTextMuted, true);
        draw_status_indicator(rt_card.x + col_w - 208, ry + 2, platform::to_string(b.status), sc, sc, 13);
        draw_wrapped_text(b.summary, rt_card.x + 24, ry + 44, col_w - 48, 13, kTextSecondary, 2);
        if (!b.blocker.empty()) {
            draw_text("Blocker: " + truncate_str(b.blocker, 86), rt_card.x + 24, ry + 88, 12, kSemDanger);
        }
        if (i + 1 < backends.size()) {
            draw_rect({rt_card.x + 24, ry + 118, col_w - 48, 1}, kHairline);
        }
        ry += 128.0f;
    }

    SDL_FRect gfx_card{kMarginX + col_w + 24.0f, bot_y, col_w, bot_h};
    draw_rounded_rect(gfx_card, 12.0f, kSurface);
    draw_rounded_border(gfx_card, 12.0f, 1.0f, kHairline);
    draw_text("Graphics API routing (Xbox Series S)", gfx_card.x + 24, gfx_card.y + 20, 16, kTextPrimary);
    draw_rect({gfx_card.x + 24, gfx_card.y + 52, col_w - 48, 1}, kHairline);

    float gy = gfx_card.y + 62.0f;
    for (size_t i = 0; i < routes.size(); ++i) {
        const auto& r = routes[i];
        const ColorRGBA sc = status_color(r.status);
        draw_text(graphics::to_string(r.guest_api) + "  →  " + r.xbox_target_backend,
                  gfx_card.x + 24, gy, 15, kTextPrimary);
        draw_status_indicator(gfx_card.x + col_w - 208, gy + 2, platform::to_string(r.status), sc, sc, 13);
        draw_text("Shaders: " + r.shader_plan.source_ir + " → " + r.shader_plan.target_ir,
                  gfx_card.x + 24, gy + 26, 13, kAccentCyan, true);
        draw_text(truncate_str(r.dxvk_vkd3d_note, 94), gfx_card.x + 24, gy + 50, 13, kTextSecondary);
        if (i + 1 < routes.size()) {
            draw_rect({gfx_card.x + 24, gy + 86, col_w - 48, 1}, kHairline);
        }
        gy += 102.0f;
    }
}

void UiRenderer::draw_settings_view(LauncherState& state) {
    const auto& cfg = state.context().config.config();

    draw_text("Settings", kMarginX, 156, 28, kTextPrimary);
    draw_text("Controller calibration, target hardware cap simulation, and display preferences saved to config.json",
              kMarginX, 192, 14, kTextSecondary);

    SDL_FRect panel{kMarginX, 232, kCanvasW, 744};
    draw_rounded_rect(panel, 12.0f, kSurface);
    draw_rounded_border(panel, 12.0f, 1.0f, kHairline);

    auto fmt_pct = [](float v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.0f%%", v * 100.0f);
        return std::string(b);
    };
    auto fmt_f2 = [](float v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.2fx", v);
        return std::string(b);
    };

    const std::pair<std::string, std::string> rows[LauncherState::kSettingsItemCount] = {
        {"Xbox Series S target mode",   cfg.runtime.xbox_target_mode + " (" + std::to_string(cfg.runtime.simulated_memory_cap_mib) + " MiB)"},
        {"Left stick radial deadzone",  fmt_pct(cfg.input.left_stick_deadzone)},
        {"Right stick radial deadzone", fmt_pct(cfg.input.right_stick_deadzone)},
        {"Analog stick sensitivity",    fmt_f2(cfg.input.stick_sensitivity)},
        {"Analog trigger threshold",    fmt_pct(cfg.input.trigger_threshold)},
        {"Controller button profile",   cfg.input.controller_profile},
        {"Invert stick Y-axis",         cfg.input.invert_y_axis ? "Enabled" : "Disabled"},
        {"Smooth UI transitions",       cfg.display.smooth_animations ? "Enabled" : "Disabled"},
        {"4K / Retina HiDPI scaling",   cfg.display.uhd_4k_aware ? "Enabled" : "Disabled"},
        {"Diagnostic log verbosity",    platform::log_level_name(cfg.log_level)},
        {"Save configuration",          "Write config.json and library.json"},
        {"Reset to defaults",           "Restore baseline configuration"}
    };

    float y = 248.0f;
    for (int i = 0; i < LauncherState::kSettingsItemCount; ++i) {
        const bool sel = (i == state.settings_selected_index());
        SDL_FRect rr{kMarginX + 16, y, kCanvasW - 32, 54};
        if (sel) {
            draw_rounded_rect(rr, 8.0f, kSurfaceActive);
            draw_rect({rr.x, rr.y + 10, 3.0f, rr.h - 20}, kAccentCyan);
        }
        draw_text(rows[i].first, rr.x + 20, y + 16, 16, sel ? kTextPrimary : kTextSecondary);
        draw_text_right(rows[i].second, rr.x + rr.w - 24, y + 16, 15,
                        sel ? kAccentCyan : kTextPrimary, true);
        if (!sel && i + 1 < LauncherState::kSettingsItemCount) {
            draw_rect({kMarginX + 24, y + 54, kCanvasW - 48, 1}, kHairline);
        }

        hit_regions_.push_back({rr, [&state, i]() { state.adjust_setting(i, +1); }});
        y += 59.0f;
    }
}

void UiRenderer::draw_diagnostics_view(LauncherState& state, const input::InputManager& input) {
    const auto& ctx = state.context();
    const bool game_mode = ctx.config.config().runtime.xbox_target_mode == "GameMode_5GB";
    const auto xbox = ctx.platform->target_xbox_series_s_profile(game_mode);
    const auto metrics = ctx.platform->query_metrics(xbox.app_usable_ram_bytes);
    const auto fstats = ctx.benchmark.frame_stats();

    draw_text("Diagnostics & telemetry", kMarginX, 156, 28, kTextPrimary);
    draw_text("Measured host frame timing, resident memory, live SDL3-to-XInput controller state, and structured subsystem logs",
              kMarginX, 192, 14, kTextSecondary);

    const float right_edge = kMarginX + kCanvasW;
    SDL_FRect btn_exp{right_edge - 240, 160, 240, 42};
    draw_rounded_rect(btn_exp, 8.0f, kTextPrimary);
    draw_text_centered("Export diagnostic bundle", btn_exp, 14, kChromeBar);
    hit_regions_.push_back({btn_exp, [&state]() { state.export_diagnostic_bundle(); }});

    // Top row: Wide Frame-Time Chart (1200 px) + Narrow Input Instrument (600 px), gap 24 -> 1824 exact
    const float top_y = 232.0f;
    const float top_h = 300.0f;

    SDL_FRect graph_card{kMarginX, top_y, 1200, top_h};
    draw_rounded_rect(graph_card, 12.0f, kSurface);
    draw_rounded_border(graph_card, 12.0f, 1.0f, kHairline);

    draw_text("Frame time (ms)", graph_card.x + 24, graph_card.y + 18, 16, kTextPrimary);

    char stat_line[256];
    std::snprintf(stat_line, sizeof(stat_line),
                  "%.1f fps  ·  avg %.2f ms  ·  p95 %.2f ms  ·  p99 %.2f ms  ·  RSS %s  ·  CPU %s",
                  fstats.current_fps, fstats.avg_frame_ms, fstats.p95_frame_ms, fstats.p99_frame_ms,
                  format_bytes(metrics.process_rss_bytes).c_str(),
                  metrics.process_cpu_percent ? ([&]() {
                      char b[24];
                      std::snprintf(b, sizeof(b), "%.1f%%", *metrics.process_cpu_percent);
                      return std::string(b);
                  })().c_str() : "—");
    draw_text_right(stat_line, graph_card.x + graph_card.w - 24, graph_card.y + 20, 13, kTextSecondary, true);

    // Calibrated chart plot with real Y-axis ticks & muted horizontal gridlines (app-screens.md §6.3)
    SDL_FRect plot{graph_card.x + 76, graph_card.y + 58, graph_card.w - 104, 212};
    const float ticks[5] = {33.3f, 25.0f, 16.7f, 8.3f, 0.0f};
    for (int t = 0; t < 5; ++t) {
        const float ty = plot.y + (static_cast<float>(t) / 4.0f) * plot.h;
        char tb[16];
        std::snprintf(tb, sizeof(tb), "%.1f", ticks[t]);
        draw_text_right(tb, plot.x - 12, ty - 7, 12, kTextMuted, true);
        draw_rect({plot.x, ty, plot.w, (t == 4) ? 1.5f : 1.0f},
                  (t == 2) ? ColorRGBA{34, 211, 238, 60} : kHairline);
    }
    draw_text("60 fps target", plot.x + plot.w - 92, plot.y + plot.h * 0.5f - 18, 11, kAccentCyan, true);

    const auto& hist = ctx.benchmark.frame_history_ms();
    if (hist.size() >= 2) {
        const size_t n = hist.size();
        const float dx = plot.w / static_cast<float>(std::max<size_t>(1, n - 1));
        for (size_t i = 0; i + 1 < n; ++i) {
            const float v0 = std::clamp(static_cast<float>(hist[i]) / 33.33f, 0.0f, 1.0f);
            const float v1 = std::clamp(static_cast<float>(hist[i + 1]) / 33.33f, 0.0f, 1.0f);
            const float x0 = plot.x + static_cast<float>(i) * dx;
            const float x1 = plot.x + static_cast<float>(i + 1) * dx;
            const float py0 = plot.y + plot.h - v0 * plot.h;
            const float py1 = plot.y + plot.h - v1 * plot.h;

            // Whisper-light 10% fill beneath a crisp 2px data line
            SDL_Vertex quad[4] = {
                {{x0, py0}, to_fcolor({34, 211, 238, 28}), {0, 0}},
                {{x1, py1}, to_fcolor({34, 211, 238, 28}), {1, 0}},
                {{x1, plot.y + plot.h}, to_fcolor({34, 211, 238, 4}), {1, 1}},
                {{x0, plot.y + plot.h}, to_fcolor({34, 211, 238, 4}), {0, 1}}
            };
            const int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, quad, 4, idx, 6);
            draw_line_segment(x0, py0, x1, py1, 2.0f, kAccentCyan);
        }
    }

    // Top Right: Live Controller & XInput Bridge Instrument
    SDL_FRect pad_card{kMarginX + 1200 + 24, top_y, 600, top_h};
    draw_rounded_rect(pad_card, 12.0f, kSurface);
    draw_rounded_border(pad_card, 12.0f, 1.0f, kHairline);

    draw_text("Controller & XInput bridge", pad_card.x + 24, pad_card.y + 18, 16, kTextPrimary);
    draw_text(truncate_str(input.active_controller_name(), 44), pad_card.x + 24, pad_card.y + 46, 13, kTextSecondary);

    const auto xi = input.xinput_snapshot();
    char xi_buf[128];
    std::snprintf(xi_buf, sizeof(xi_buf), "wButtons  0x%04X     LT %3u     RT %3u",
                  xi.wButtons, xi.bLeftTrigger, xi.bRightTrigger);
    draw_text(xi_buf, pad_card.x + 24, pad_card.y + 80, 14, kTextPrimary, true);
    std::snprintf(xi_buf, sizeof(xi_buf), "LS (%6d, %6d)   RS (%6d, %6d)",
                  xi.sThumbLX, xi.sThumbLY, xi.sThumbRX, xi.sThumbRY);
    draw_text(xi_buf, pad_card.x + 24, pad_card.y + 106, 14, kTextSecondary, true);

    const auto& gs = input.gamepad_state();
    draw_circle(pad_card.x + 84, pad_card.y + 190, 38, kGround);
    draw_rounded_border({pad_card.x + 46, pad_card.y + 152, 76, 76}, 38.0f, 1.0f, kHairlineStrong);
    draw_circle(pad_card.x + 84 + gs.left_stick.x * 26.0f, pad_card.y + 190 + gs.left_stick.y * 26.0f, 7, kAccentCyan);
    draw_text_centered("LS", {pad_card.x + 46, pad_card.y + 234, 76, 20}, 12, kTextMuted, true);

    draw_circle(pad_card.x + 194, pad_card.y + 190, 38, kGround);
    draw_rounded_border({pad_card.x + 156, pad_card.y + 152, 76, 76}, 38.0f, 1.0f, kHairlineStrong);
    draw_circle(pad_card.x + 194 + gs.right_stick.x * 26.0f, pad_card.y + 190 + gs.right_stick.y * 26.0f, 7, kAccentBlue);
    draw_text_centered("RS", {pad_card.x + 156, pad_card.y + 234, 76, 20}, 12, kTextMuted, true);

    draw_text("GPU utilization: —", pad_card.x + 276, pad_card.y + 172, 14, kTextSecondary, true);
    draw_wrapped_text("Public macOS user APIs do not expose per-process GPU load; omitted instead of estimated.",
                      pad_card.x + 276, pad_card.y + 198, 296, 12, kTextMuted, 3);

    // Bottom: Structured Diagnostic Logs
    SDL_FRect log_card{kMarginX, 556, kCanvasW, 420};
    draw_rounded_rect(log_card, 12.0f, kSurface);
    draw_rounded_border(log_card, 12.0f, 1.0f, kHairline);

    draw_text("Structured diagnostic log", log_card.x + 24, log_card.y + 18, 16, kTextPrimary);
    draw_text("Warnings: " + std::to_string(platform::Logger::instance().warn_count()) +
                  "  ·  Errors: " + std::to_string(platform::Logger::instance().error_count()),
              log_card.x + 244, log_card.y + 20, 13, kTextSecondary, true);

    SDL_FRect filter_btn{log_card.x + log_card.w - 208, log_card.y + 12, 184, 32};
    draw_rounded_rect(filter_btn, 6.0f, kSurfaceElev);
    draw_rounded_border(filter_btn, 6.0f, 1.0f, kHairline);
    draw_text_centered(std::string("Level >= ") + platform::log_level_name(state.diag_log_filter()),
                       filter_btn, 13, kTextPrimary);
    hit_regions_.push_back({filter_btn, [&state]() { state.cycle_diag_log_filter(); }});

    draw_rect({log_card.x + 24, log_card.y + 52, log_card.w - 48, 1}, kHairline);

    const auto logs = platform::Logger::instance().recent_entries(12, state.diag_log_filter());
    float ly = log_card.y + 64.0f;
    for (const auto& entry : logs) {
        ColorRGBA lcol = kTextSecondary;
        if (entry.level == platform::LogLevel::Warn) lcol = kSemWarn;
        else if (entry.level >= platform::LogLevel::Error) lcol = kSemDanger;
        else if (entry.level == platform::LogLevel::Info) lcol = kTextPrimary;

        std::string line = entry.timestamp_iso.substr(11) + "  " +
                           platform::log_level_name(entry.level) + "  [" + entry.subsystem + "]  " +
                           entry.message;
        draw_text(truncate_str(line, 146), log_card.x + 24, ly, 13, lcol, true);
        ly += 28.0f;
    }
}

void UiRenderer::draw_bottom_prompt_bar(const LauncherState& state) {
    draw_rect({0, 1008, 1920, 72}, kChromeBar);
    draw_rect({0, 1008, 1920, 1}, kHairline);

    // Left keycap anchor matching Deck reference footer
    draw_keycap({kMarginX, 1028, 76, 32}, "XWINE", kTextPrimary, kChromeBar);
    draw_text("Menu", kMarginX + 88, 1034, 15, kTextSecondary);

    // Right-aligned physical Xbox glyph prompts
    float x = 1090.0f;
    auto prompt = [&](const std::string& btn, const std::string& label, ColorRGBA fill) {
        draw_xbox_button(x, 1044, 14, btn, fill, kChromeBar);
        draw_text(label, x + 22, 1034, 15, kTextPrimary);
        x += 44.0f + measure_text_width(label, 15, false) + 28.0f;
    };

    if (state.detail_open()) {
        prompt("X", "Inspect PE", kAccentBlue);
        prompt("Y", "Favorite", kSemWarn);
        prompt("A", "Select", kTextPrimary);
        prompt("B", "Back", kTextPrimary);
    } else if (state.active_tab() == NavTab::Library) {
        prompt("X", "Filter", kAccentBlue);
        prompt("Y", "Sort by", kSemWarn);
        prompt("A", "Details", kTextPrimary);
        prompt("B", "Back", kTextPrimary);
    } else if (state.active_tab() == NavTab::SteamLibrary) {
        prompt("X", "Sample ACF", kAccentBlue);
        prompt("A", "Scan Steam", kTextPrimary);
        prompt("B", "Back", kTextPrimary);
    } else if (state.active_tab() == NavTab::Diagnostics) {
        prompt("X", "Log level", kAccentBlue);
        prompt("A", "Export JSON", kTextPrimary);
        prompt("B", "Back", kTextPrimary);
    } else {
        prompt("Y", "Favorite", kSemWarn);
        prompt("F", "Search", kAccentCyan);
        prompt("A", "Select", kTextPrimary);
        prompt("B", "Back", kTextPrimary);
    }
}

void UiRenderer::draw_toast(const LauncherState& state) {
    const auto& t = state.toast();
    if (t.remaining_seconds <= 0.0f || t.message.empty()) return;

    const float alpha = std::min(1.0f, t.remaining_seconds * 2.0f);
    const uint8_t a = static_cast<uint8_t>(245.0f * alpha);
    const ColorRGBA dot = t.is_warning ? kSemWarn : kAccentCyan;
    const float tw = std::min(760.0f, measure_text_width(t.message, 14, false) + 56.0f);
    SDL_FRect r{(1920.0f - tw) * 0.5f, 944, tw, 44};
    draw_rounded_rect(r, 8.0f, {24, 29, 39, a});
    draw_rounded_border(r, 8.0f, 1.0f, {255, 255, 255, static_cast<uint8_t>(38.0f * alpha)});
    draw_status_indicator(r.x + 20, r.y + 13, t.message,
                          {dot.r, dot.g, dot.b, a}, {242, 245, 250, a}, 14);
}

void UiRenderer::render(LauncherState& state, const input::InputManager& input, float display_scale) {
    ++frame_counter_;
    hit_regions_.clear();

    SDL_SetRenderDrawColor(renderer_, kGround.r, kGround.g, kGround.b, 255);
    SDL_RenderClear(renderer_);

    draw_background();
    draw_top_bar(state, input, display_scale);
    draw_nav_bar(state);

    if (state.detail_open()) {
        draw_game_detail_view(state);
    } else {
        switch (state.active_tab()) {
            case NavTab::Home:          draw_home_view(state, input); break;
            case NavTab::Library:       draw_library_view(state); break;
            case NavTab::SteamLibrary:  draw_steam_view(state); break;
            case NavTab::Compatibility: draw_compatibility_view(state); break;
            case NavTab::Runtimes:      draw_runtimes_view(state); break;
            case NavTab::Settings:      draw_settings_view(state); break;
            case NavTab::Diagnostics:   draw_diagnostics_view(state, input); break;
            case NavTab::Count:         break;
        }
    }

    draw_toast(state);
    draw_bottom_prompt_bar(state);
    evict_stale_text_cache();
}

bool UiRenderer::handle_mouse_click(float logical_x, float logical_y) {
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
        const auto& r = it->rect;
        if (logical_x >= r.x && logical_x <= r.x + r.w &&
            logical_y >= r.y && logical_y <= r.y + r.h) {
            if (it->on_click) it->on_click();
            return true;
        }
    }
    return false;
}

bool UiRenderer::save_screenshot_png(const std::filesystem::path& output_path) const {
    if (!renderer_) return false;
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, nullptr);
    if (!surf) return false;

    SDL_Surface* rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!rgba) return false;

    std::error_code ec;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), ec);
    }

    FILE* fp = std::fopen(output_path.string().c_str(), "wb");
    if (!fp) {
        SDL_DestroySurface(rgba);
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        if (png) png_destroy_write_struct(&png, info ? &info : nullptr);
        std::fclose(fp);
        SDL_DestroySurface(rgba);
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, static_cast<png_uint_32>(rgba->w), static_cast<png_uint_32>(rgba->h),
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(static_cast<size_t>(rgba->h));
    auto* base = static_cast<png_bytep>(rgba->pixels);
    for (int y = 0; y < rgba->h; ++y) {
        rows[y] = base + static_cast<size_t>(y * rgba->pitch);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    SDL_DestroySurface(rgba);
    return true;
}

} // namespace xwine::launcher
