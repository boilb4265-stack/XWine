#include "xwine/input/input_manager.hpp"
#include "xwine/platform/logger.hpp"

#include <cmath>

namespace xwine::input {

InputManager::InputManager() = default;

InputManager::~InputManager() {
    shutdown();
}

bool InputManager::initialize() {
    open_first_available_gamepad();
    return true;
}

void InputManager::shutdown() {
    if (gamepad_) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
        gamepad_id_ = 0;
    }
    state_ = NormalizedGamepadState{};
}

void InputManager::set_controller_settings(const ControllerSettings& settings) {
    mapper_.set_settings(settings);
}

void InputManager::open_first_available_gamepad() {
    if (gamepad_) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
        gamepad_ = SDL_OpenGamepad(ids[0]);
        if (gamepad_) {
            gamepad_id_ = ids[0];
            const char* name = SDL_GetGamepadName(gamepad_);
            state_.connected = true;
            state_.device_name = name ? name : "SDL3 Gamepad";
            platform::Logger::instance().info("Input", "Connected gamepad: " + state_.device_name);
        }
    }
    if (ids) {
        SDL_free(ids);
    }
}

GamepadButton InputManager::from_sdl_button(Uint8 sdl_btn) {
    switch (sdl_btn) {
        case SDL_GAMEPAD_BUTTON_SOUTH:          return GamepadButton::A;
        case SDL_GAMEPAD_BUTTON_EAST:           return GamepadButton::B;
        case SDL_GAMEPAD_BUTTON_WEST:           return GamepadButton::X;
        case SDL_GAMEPAD_BUTTON_NORTH:          return GamepadButton::Y;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return GamepadButton::LeftBumper;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadButton::RightBumper;
        case SDL_GAMEPAD_BUTTON_BACK:           return GamepadButton::View;
        case SDL_GAMEPAD_BUTTON_START:          return GamepadButton::Menu;
        case SDL_GAMEPAD_BUTTON_GUIDE:          return GamepadButton::Guide;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return GamepadButton::LeftStickClick;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return GamepadButton::RightStickClick;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:        return GamepadButton::DPadUp;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return GamepadButton::DPadDown;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return GamepadButton::DPadLeft;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return GamepadButton::DPadRight;
        default: return GamepadButton::Count;
    }
}

void InputManager::process_sdl_event(const SDL_Event& ev, bool text_input_active, InputFrameEvents& events) {
    switch (ev.type) {
        case SDL_EVENT_QUIT:
            events.quit_requested = true;
            break;

        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_) {
                gamepad_ = SDL_OpenGamepad(ev.gdevice.which);
                if (gamepad_) {
                    gamepad_id_ = ev.gdevice.which;
                    const char* name = SDL_GetGamepadName(gamepad_);
                    state_.connected = true;
                    state_.device_name = name ? name : "SDL3 Gamepad";
                    platform::Logger::instance().info("Input", "Gamepad hotplugged: " + state_.device_name);
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad_ && ev.gdevice.which == gamepad_id_) {
                platform::Logger::instance().info("Input", "Gamepad disconnected: " + state_.device_name);
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
                gamepad_id_ = 0;
                state_ = NormalizedGamepadState{};
                open_first_available_gamepad();
            }
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            GamepadButton phys = from_sdl_button(ev.gbutton.button);
            if (phys != GamepadButton::Count) {
                GamepadButton logical = mapper_.map_button(phys);
                state_.buttons[static_cast<size_t>(logical)] = true;
                UiAction act = mapper_.button_to_ui_action(logical);
                if (act != UiAction::None) {
                    events.actions.push_back(act);
                }
            }
            break;
        }

        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            GamepadButton phys = from_sdl_button(ev.gbutton.button);
            if (phys != GamepadButton::Count) {
                GamepadButton logical = mapper_.map_button(phys);
                state_.buttons[static_cast<size_t>(logical)] = false;
            }
            break;
        }

        case SDL_EVENT_KEY_DOWN: {
            if (text_input_active && ev.key.key == SDLK_BACKSPACE) {
                events.backspace_pressed = true;
            }
            UiAction act = KeyboardInputAdapter::translate_key(ev.key.key, ev.key.mod, text_input_active);
            if (act != UiAction::None) {
                events.actions.push_back(act);
            }
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
            if (text_input_active && ev.text.text) {
                events.text_input += ev.text.text;
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            events.mouse_moved = true;
            events.mouse_x = ev.motion.x;
            events.mouse_y = ev.motion.y;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                events.mouse_clicked = true;
                events.mouse_x = ev.button.x;
                events.mouse_y = ev.button.y;
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            events.mouse_wheel_y += ev.wheel.y;
            break;

        default:
            break;
    }
}

void InputManager::update(float dt_seconds, bool text_input_active, InputFrameEvents& events) {
    if (!gamepad_) {
        held_stick_dir_ = UiAction::None;
        stick_repeat_timer_ = 0.0f;
        stick_initial_fired_ = false;
        return;
    }

    const auto& cfg = mapper_.settings();
    auto norm_axis = [&](SDL_GamepadAxis axis) -> float {
        return static_cast<float>(SDL_GetGamepadAxis(gamepad_, axis)) / 32767.0f;
    };

    state_.left_stick = apply_radial_deadzone_and_curve(
        norm_axis(SDL_GAMEPAD_AXIS_LEFTX),
        norm_axis(SDL_GAMEPAD_AXIS_LEFTY),
        cfg.left_stick_deadzone, cfg.sensitivity, cfg.invert_y_axis);

    state_.right_stick = apply_radial_deadzone_and_curve(
        norm_axis(SDL_GAMEPAD_AXIS_RIGHTX),
        norm_axis(SDL_GAMEPAD_AXIS_RIGHTY),
        cfg.right_stick_deadzone, cfg.sensitivity, cfg.invert_y_axis);

    state_.left_trigger = apply_trigger_threshold(
        norm_axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), cfg.trigger_threshold);
    state_.right_trigger = apply_trigger_threshold(
        norm_axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), cfg.trigger_threshold);

    if (text_input_active) return;

    // Emit crisp directional repeat navigation from left analog stick
    constexpr float kNavThreshold = 0.55f;
    UiAction dir = UiAction::None;
    if (std::fabs(state_.left_stick.x) > std::fabs(state_.left_stick.y)) {
        if (state_.left_stick.x < -kNavThreshold) dir = UiAction::NavLeft;
        else if (state_.left_stick.x > kNavThreshold) dir = UiAction::NavRight;
    } else {
        if (state_.left_stick.y < -kNavThreshold) dir = UiAction::NavUp;
        else if (state_.left_stick.y > kNavThreshold) dir = UiAction::NavDown;
    }

    if (dir == UiAction::None) {
        held_stick_dir_ = UiAction::None;
        stick_repeat_timer_ = 0.0f;
        stick_initial_fired_ = false;
    } else if (dir != held_stick_dir_) {
        held_stick_dir_ = dir;
        stick_repeat_timer_ = 0.0f;
        stick_initial_fired_ = false;
        events.actions.push_back(dir);
    } else {
        stick_repeat_timer_ += dt_seconds;
        const float delay = stick_initial_fired_ ? 0.14f : 0.36f;
        if (stick_repeat_timer_ >= delay) {
            stick_repeat_timer_ = 0.0f;
            stick_initial_fired_ = true;
            events.actions.push_back(dir);
        }
    }
}

std::string InputManager::active_controller_name() const {
    return state_.connected ? state_.device_name : "No Controller (Keyboard/Mouse Active)";
}

} // namespace xwine::input
