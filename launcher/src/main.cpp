#include "xwine/input/input_manager.hpp"
#include "xwine/launcher/launcher_state.hpp"
#include "xwine/launcher/ui_renderer.hpp"
#include "xwine/platform/logger.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct CliOptions {
    int width = 1280;
    int height = 720;
    std::filesystem::path data_dir;
    std::filesystem::path capture_dir;
    bool seed_demo = true;
};

CliOptions parse_cli(int argc, char** argv) {
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            opts.width = std::max(640, std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            opts.height = std::max(360, std::atoi(argv[++i]));
        } else if (arg == "--data-dir" && i + 1 < argc) {
            opts.data_dir = argv[++i];
        } else if (arg == "--capture-screenshots" && i + 1 < argc) {
            opts.capture_dir = argv[++i];
        } else if (arg == "--no-demo") {
            opts.seed_demo = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "XWine Native Launcher (Experimental — macOS Dev Workstation)\n"
                      << "Usage: xwine-launcher [options]\n"
                      << "  --width <px>                  Window width (default 1280)\n"
                      << "  --height <px>                 Window height (default 720)\n"
                      << "  --data-dir <path>             Override XWine storage directory\n"
                      << "  --capture-screenshots <dir>   Render UI views to PNGs and exit\n"
                      << "  --no-demo                     Do not seed sample catalog fixtures\n";
            std::exit(0);
        }
    }
    return opts;
}

} // namespace

int main(int argc, char** argv) {
    const CliOptions cli = parse_cli(argc, argv);

    xwine::launcher::LauncherAppContext ctx;
    ctx.platform = xwine::platform::create_host_platform();
    ctx.data_dir = cli.data_dir.empty() ? ctx.platform->default_data_directory() : cli.data_dir;

    auto& logger = xwine::platform::Logger::instance();
    logger.enable_file_sink(ctx.data_dir / "logs" / "xwine.log");
    logger.set_stderr_echo(false);
    logger.info("Boot", "Starting Project XWine Launcher (Experimental — Pre-Hardware Verification)");
    logger.info("Boot", "Host Platform: " + xwine::platform::to_string(ctx.platform->platform_id()));
    logger.info("Boot", "Data Directory: " + ctx.data_dir.string());

    {
        xwine::platform::ScopedTimer init_timer(ctx.benchmark, "Config & Library Load");
        ctx.config.reset_to_defaults(ctx.data_dir);
        std::string cfg_err;
        if (!ctx.config.load_from_file(ctx.data_dir / "config.json", &cfg_err)) {
            logger.info("Config", "Creating default config.json (" + cfg_err + ")");
            ctx.config.save_to_file(ctx.data_dir / "config.json");
        }
        logger.set_min_level(ctx.config.config().log_level);

        ctx.library.set_sandbox_root(ctx.data_dir / "games");
        std::string lib_err;
        if (!ctx.library.load_from_file(ctx.data_dir / "library.json", &lib_err) ||
            ctx.library.all_games().empty()) {
            if (cli.seed_demo) {
                logger.info("Library", "Seeding inspection-only sample catalog fixtures.");
                ctx.library.seed_demo_fixtures();
                ctx.library.save_to_file(ctx.data_dir / "library.json");
            }
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        logger.fatal("SDL", std::string("SDL_Init failed: ") + SDL_GetError());
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!cli.capture_dir.empty()) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (ctx.config.config().display.fullscreen && cli.capture_dir.empty()) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Project XWine — Experimental Xbox Series S Compatibility Launcher",
        cli.width, cli.height, flags);
    if (!window) {
        logger.fatal("SDL", std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        logger.fatal("SDL", std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (ctx.config.config().display.vsync) {
        SDL_SetRenderVSync(renderer, 1);
    }

    xwine::input::InputManager input;
    input.initialize();
    auto sync_input_cfg = [&]() {
        const auto& ic = ctx.config.config().input;
        xwine::input::ControllerSettings cs;
        cs.left_stick_deadzone = ic.left_stick_deadzone;
        cs.right_stick_deadzone = ic.right_stick_deadzone;
        cs.trigger_threshold = ic.trigger_threshold;
        cs.sensitivity = ic.stick_sensitivity;
        cs.invert_y_axis = ic.invert_y_axis;
        cs.profile_name = ic.controller_profile;
        input.set_controller_settings(cs);
    };
    sync_input_cfg();

    xwine::launcher::UiRenderer ui;
    ui.initialize(window, renderer);

    xwine::launcher::LauncherState state(ctx);
    const float display_scale = SDL_GetWindowDisplayScale(window);

    // Headless / Automated Visual QA Capture Mode
    if (!cli.capture_dir.empty()) {
        for (int i = 0; i < 60; ++i) {
            ctx.benchmark.record_frame_ms(16.4 + (i % 5) * 0.15);
        }
        auto capture_view = [&](const std::string& filename) {
            for (int f = 0; f < 10; ++f) state.update_animations(0.05f);
            ui.render(state, input, display_scale);
            SDL_RenderPresent(renderer);
            ui.render(state, input, display_scale);
            const auto out = cli.capture_dir / filename;
            if (ui.save_screenshot_png(out)) {
                logger.info("Capture", "Saved screenshot: " + out.string());
            } else {
                logger.error("Capture", "Failed to save screenshot: " + out.string());
            }
        };

        state.set_active_tab(xwine::launcher::NavTab::Home);
        capture_view("01_home.png");

        state.set_active_tab(xwine::launcher::NavTab::Library);
        capture_view("02_library.png");

        auto vis = state.visible_library_games();
        if (!vis.empty()) {
            state.open_game_detail(vis.front()->id);
            state.execute_detail_action(0); // Populate honest dry-run report
            capture_view("03_game_detail.png");
            state.close_game_detail();
            for (int t = 0; t < 60; ++t) state.update_animations(0.1f); // Clear toast before next views
        }

        state.set_active_tab(xwine::launcher::NavTab::SteamLibrary);
        state.load_sample_steam_manifests();
        for (int t = 0; t < 60; ++t) state.update_animations(0.1f);
        capture_view("04_steam_library.png");

        state.set_active_tab(xwine::launcher::NavTab::Compatibility);
        capture_view("05_compatibility.png");

        state.set_active_tab(xwine::launcher::NavTab::Runtimes);
        capture_view("06_runtimes.png");

        state.set_active_tab(xwine::launcher::NavTab::Settings);
        capture_view("07_settings.png");

        state.set_active_tab(xwine::launcher::NavTab::Diagnostics);
        capture_view("08_diagnostics.png");

        ui.shutdown();
        input.shutdown();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    // Main Interactive 60 FPS Render & Event Loop
    bool running = true;
    bool prev_text_input = false;
    auto last_tick = std::chrono::steady_clock::now();

    while (running) {
        const auto frame_start = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(frame_start - last_tick).count();
        last_tick = frame_start;

        if (state.search_focused() != prev_text_input) {
            prev_text_input = state.search_focused();
            if (prev_text_input) SDL_StartTextInput(window);
            else SDL_StopTextInput(window);
        }

        xwine::input::InputFrameEvents frame_events;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_MOTION) {
                SDL_ConvertEventToRenderCoordinates(renderer, &ev);
            }
            input.process_sdl_event(ev, state.search_focused(), frame_events);
        }
        input.update(dt, state.search_focused(), frame_events);

        if (frame_events.quit_requested) {
            running = false;
            break;
        }
        if (!frame_events.text_input.empty()) {
            state.handle_text_input(frame_events.text_input);
        }
        if (frame_events.backspace_pressed) {
            state.handle_backspace();
        }
        for (auto act : frame_events.actions) {
            state.handle_action(act);
        }
        if (frame_events.mouse_clicked) {
            ui.handle_mouse_click(frame_events.mouse_x, frame_events.mouse_y);
        }
        if (state.config_dirty()) {
            sync_input_cfg();
            state.clear_config_dirty();
        }

        state.update_animations(dt);
        ui.render(state, input, SDL_GetWindowDisplayScale(window));
        SDL_RenderPresent(renderer);

        const auto frame_end = std::chrono::steady_clock::now();
        const double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
        ctx.benchmark.record_frame_ms(frame_ms);
    }

    ctx.config.save_to_file(ctx.data_dir / "config.json");
    ctx.library.save_to_file(ctx.data_dir / "library.json");
    logger.info("Boot", "Shutting down Project XWine Launcher cleanly.");

    ui.shutdown();
    input.shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
