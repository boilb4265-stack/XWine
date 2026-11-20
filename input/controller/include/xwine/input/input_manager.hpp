#pragma once

#include "xwine/input/controller_mapping.hpp"
#include "xwine/input/keyboard_input.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace xwine::input {

struct InputFrameEvents {
    std::vector<UiAction> actions;
    std::string text_input;
    bool backspace_pressed = false;
    bool mouse_moved = false;
    bool mouse_clicked = false;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float mouse_wheel_y = 0.0f;
    bool quit_requested = false;
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    bool initialize();
    void shutdown();

    void set_controller_settings(const ControllerSettings& settings);
    const ControllerSettings& controller_settings() const { return mapper_.settings(); }
    ControllerMapper& mapper() { return mapper_; }

    // Processes an SDL3 event and appends any resulting actions to `events`.
    void process_sdl_event(const SDL_Event& ev, bool text_input_active, InputFrameEvents& events);

    // Polls continuous analog axes and emits smooth stick navigation repeat actions.
    void update(float dt_seconds, bool text_input_active, InputFrameEvents& events);

    const NormalizedGamepadState& gamepad_state() const { return state_; }
    void set_external_gamepad_state(const NormalizedGamepadState& s) { state_ = s; }
    XInputStateSnapshot xinput_snapshot() const { return mapper_.to_xinput(state_); }
    bool has_connected_controller() const { return gamepad_ != nullptr || state_.connected; }
    std::string active_controller_name() const;

private:
    void open_first_available_gamepad();
    static GamepadButton from_sdl_button(Uint8 sdl_btn);

    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
    ControllerMapper mapper_;
    NormalizedGamepadState state_;

    // Analog stick UI navigation repeat timer
    UiAction held_stick_dir_ = UiAction::None;
    float stick_repeat_timer_ = 0.0f;
    bool stick_initial_fired_ = false;
};

} // namespace xwine::input
