#include "xwine/input/controller_mapping.hpp"

#include <algorithm>
#include <cmath>

namespace xwine::input {

Vec2f apply_radial_deadzone_and_curve(float raw_x, float raw_y, float deadzone,
                                      float sensitivity, bool invert_y) {
    deadzone = std::clamp(deadzone, 0.0f, 0.9f);
    sensitivity = std::clamp(sensitivity, 0.25f, 3.0f);

    const float clamped_x = std::clamp(raw_x, -1.0f, 1.0f);
    const float clamped_y = std::clamp(invert_y ? -raw_y : raw_y, -1.0f, 1.0f);
    const float mag = std::sqrt(clamped_x * clamped_x + clamped_y * clamped_y);
    if (mag <= deadzone || mag < 1e-6f) {
        return {0.0f, 0.0f};
    }

    const float normalized_mag = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone));
    // Smooth sensitivity exponent: sensitivity > 1 boosts response, < 1 softens center
    const float curved_mag = std::clamp(std::pow(normalized_mag, 1.0f / sensitivity), 0.0f, 1.0f);
    const float scale = curved_mag / mag;
    return {std::clamp(clamped_x * scale, -1.0f, 1.0f),
            std::clamp(clamped_y * scale, -1.0f, 1.0f)};
}

float apply_trigger_threshold(float raw_value, float threshold) {
    threshold = std::clamp(threshold, 0.0f, 0.95f);
    const float clamped = std::clamp(raw_value, 0.0f, 1.0f);
    if (clamped < threshold) return 0.0f;
    return std::clamp((clamped - threshold) / (1.0f - threshold), 0.0f, 1.0f);
}

ControllerMapper::ControllerMapper() {
    apply_preset("XboxStandard");
}

void ControllerMapper::apply_preset(const std::string& profile_name) {
    settings_.profile_name = profile_name;
    for (size_t i = 0; i < button_map_.size(); ++i) {
        button_map_[i] = static_cast<GamepadButton>(i);
    }

    if (profile_name == "SwappedConfirmCancel") {
        set_remap(GamepadButton::A, GamepadButton::B);
        set_remap(GamepadButton::B, GamepadButton::A);
    } else if (profile_name == "SwapXY") {
        set_remap(GamepadButton::X, GamepadButton::Y);
        set_remap(GamepadButton::Y, GamepadButton::X);
    }
}

void ControllerMapper::set_remap(GamepadButton physical, GamepadButton logical) {
    const auto idx = static_cast<size_t>(physical);
    if (idx < button_map_.size()) {
        button_map_[idx] = logical;
    }
}

GamepadButton ControllerMapper::map_button(GamepadButton physical) const {
    const auto idx = static_cast<size_t>(physical);
    return idx < button_map_.size() ? button_map_[idx] : physical;
}

void ControllerMapper::set_settings(const ControllerSettings& settings) {
    const bool profile_changed = settings.profile_name != settings_.profile_name;
    settings_ = settings;
    if (profile_changed) {
        apply_preset(settings.profile_name);
    }
}

XInputStateSnapshot ControllerMapper::to_xinput(const NormalizedGamepadState& state) const {
    XInputStateSnapshot snap{};
    auto pressed = [&](GamepadButton b) {
        return state.buttons[static_cast<size_t>(b)];
    };

    if (pressed(GamepadButton::DPadUp))          snap.wButtons |= kXInputDPadUp;
    if (pressed(GamepadButton::DPadDown))        snap.wButtons |= kXInputDPadDown;
    if (pressed(GamepadButton::DPadLeft))        snap.wButtons |= kXInputDPadLeft;
    if (pressed(GamepadButton::DPadRight))       snap.wButtons |= kXInputDPadRight;
    if (pressed(GamepadButton::Menu))            snap.wButtons |= kXInputStart;
    if (pressed(GamepadButton::View))            snap.wButtons |= kXInputBack;
    if (pressed(GamepadButton::LeftStickClick))  snap.wButtons |= kXInputLeftThumb;
    if (pressed(GamepadButton::RightStickClick)) snap.wButtons |= kXInputRightThumb;
    if (pressed(GamepadButton::LeftBumper))      snap.wButtons |= kXInputLeftShoulder;
    if (pressed(GamepadButton::RightBumper))     snap.wButtons |= kXInputRightShoulder;
    if (pressed(GamepadButton::A))               snap.wButtons |= kXInputA;
    if (pressed(GamepadButton::B))               snap.wButtons |= kXInputB;
    if (pressed(GamepadButton::X))               snap.wButtons |= kXInputX;
    if (pressed(GamepadButton::Y))               snap.wButtons |= kXInputY;

    snap.bLeftTrigger  = static_cast<uint8_t>(std::round(std::clamp(state.left_trigger, 0.0f, 1.0f) * 255.0f));
    snap.bRightTrigger = static_cast<uint8_t>(std::round(std::clamp(state.right_trigger, 0.0f, 1.0f) * 255.0f));

    // XInput thumbstick Y is +up (opposite of SDL screen-down Y)
    auto to_s16 = [](float v) -> int16_t {
        const float c = std::clamp(v, -1.0f, 1.0f);
        return static_cast<int16_t>(std::round(c * 32767.0f));
    };
    snap.sThumbLX = to_s16(state.left_stick.x);
    snap.sThumbLY = to_s16(-state.left_stick.y);
    snap.sThumbRX = to_s16(state.right_stick.x);
    snap.sThumbRY = to_s16(-state.right_stick.y);
    return snap;
}

UiAction ControllerMapper::button_to_ui_action(GamepadButton logical_button) const {
    switch (logical_button) {
        case GamepadButton::DPadUp:      return UiAction::NavUp;
        case GamepadButton::DPadDown:    return UiAction::NavDown;
        case GamepadButton::DPadLeft:    return UiAction::NavLeft;
        case GamepadButton::DPadRight:   return UiAction::NavRight;
        case GamepadButton::A:           return UiAction::Confirm;
        case GamepadButton::B:           return UiAction::Cancel;
        case GamepadButton::X:           return UiAction::SecondaryX;
        case GamepadButton::Y:           return UiAction::TertiaryY;
        case GamepadButton::LeftBumper:  return UiAction::TabPrev;
        case GamepadButton::RightBumper: return UiAction::TabNext;
        case GamepadButton::Menu:        return UiAction::Menu;
        case GamepadButton::View:        return UiAction::View;
        default: return UiAction::None;
    }
}

} // namespace xwine::input
