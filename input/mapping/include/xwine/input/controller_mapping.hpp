#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace xwine::input {

enum class GamepadButton : uint8_t {
    A = 0,          // South (Xbox A)
    B,              // East  (Xbox B)
    X,              // West  (Xbox X)
    Y,              // North (Xbox Y)
    LeftBumper,     // LB
    RightBumper,    // RB
    View,           // Back / Select
    Menu,           // Start
    Guide,          // Xbox Guide
    LeftStickClick, // L3
    RightStickClick,// R3
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count
};

enum class UiAction {
    None,
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    Confirm,      // Xbox A / Enter
    Cancel,       // Xbox B / Esc
    SecondaryX,   // Xbox X / Filter / Inspect
    TertiaryY,    // Xbox Y / Sort / Favorite
    TabPrev,      // Xbox LB / Q / Shift+Tab
    TabNext,      // Xbox RB / E / Tab
    Menu,         // Xbox Menu / Search
    View          // Xbox View / Diagnostics
};

struct ControllerSettings {
    float left_stick_deadzone = 0.15f;
    float right_stick_deadzone = 0.15f;
    float trigger_threshold = 0.10f;
    float sensitivity = 1.0f;
    bool invert_y_axis = false;
    std::string profile_name = "XboxStandard";
};

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct NormalizedGamepadState {
    bool connected = false;
    std::string device_name;
    std::array<bool, static_cast<size_t>(GamepadButton::Count)> buttons{};
    Vec2f left_stick;
    Vec2f right_stick;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
};

// Mirrors Windows XINPUT_GAMEPAD layout for Phase 4/7 XInput shim compatibility.
struct XInputStateSnapshot {
    uint16_t wButtons = 0;
    uint8_t bLeftTrigger = 0;
    uint8_t bRightTrigger = 0;
    int16_t sThumbLX = 0;
    int16_t sThumbLY = 0;
    int16_t sThumbRX = 0;
    int16_t sThumbRY = 0;
};

// Exact Windows XInput bitmasks
constexpr uint16_t kXInputDPadUp        = 0x0001;
constexpr uint16_t kXInputDPadDown      = 0x0002;
constexpr uint16_t kXInputDPadLeft      = 0x0004;
constexpr uint16_t kXInputDPadRight     = 0x0008;
constexpr uint16_t kXInputStart         = 0x0010;
constexpr uint16_t kXInputBack          = 0x0020;
constexpr uint16_t kXInputLeftThumb     = 0x0040;
constexpr uint16_t kXInputRightThumb    = 0x0080;
constexpr uint16_t kXInputLeftShoulder  = 0x0100;
constexpr uint16_t kXInputRightShoulder = 0x0200;
constexpr uint16_t kXInputA             = 0x1000;
constexpr uint16_t kXInputB             = 0x2000;
constexpr uint16_t kXInputX             = 0x4000;
constexpr uint16_t kXInputY             = 0x8000;

Vec2f apply_radial_deadzone_and_curve(float raw_x, float raw_y, float deadzone,
                                      float sensitivity, bool invert_y);
float apply_trigger_threshold(float raw_value, float threshold);

class ControllerMapper {
public:
    ControllerMapper();

    void apply_preset(const std::string& profile_name);
    void set_remap(GamepadButton physical, GamepadButton logical);
    GamepadButton map_button(GamepadButton physical) const;

    void set_settings(const ControllerSettings& settings);
    const ControllerSettings& settings() const { return settings_; }

    XInputStateSnapshot to_xinput(const NormalizedGamepadState& state) const;
    UiAction button_to_ui_action(GamepadButton logical_button) const;

private:
    ControllerSettings settings_;
    std::array<GamepadButton, static_cast<size_t>(GamepadButton::Count)> button_map_{};
};

} // namespace xwine::input
