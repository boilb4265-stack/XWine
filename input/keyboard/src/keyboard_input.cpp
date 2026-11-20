#include "xwine/input/keyboard_input.hpp"

namespace xwine::input {

UiAction KeyboardInputAdapter::translate_key(SDL_Keycode key, SDL_Keymod mod, bool text_input_active) {
    if (text_input_active) {
        switch (key) {
            case SDLK_ESCAPE: return UiAction::Cancel;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: return UiAction::Confirm;
            default: return UiAction::None;
        }
    }

    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    switch (key) {
        case SDLK_UP:
        case SDLK_W: return UiAction::NavUp;
        case SDLK_DOWN:
        case SDLK_S: return UiAction::NavDown;
        case SDLK_LEFT:
        case SDLK_A: return UiAction::NavLeft;
        case SDLK_RIGHT:
        case SDLK_D: return UiAction::NavRight;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: return UiAction::Confirm;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: return UiAction::Cancel;
        case SDLK_X: return UiAction::SecondaryX;
        case SDLK_Y: return UiAction::TertiaryY;
        case SDLK_Q: return UiAction::TabPrev;
        case SDLK_E: return UiAction::TabNext;
        case SDLK_TAB: return shift ? UiAction::TabPrev : UiAction::TabNext;
        case SDLK_F:
        case SDLK_SLASH: return UiAction::Menu;
        case SDLK_F1: return UiAction::View;
        default: return UiAction::None;
    }
}

} // namespace xwine::input
