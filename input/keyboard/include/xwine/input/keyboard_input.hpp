#pragma once

#include "xwine/input/controller_mapping.hpp"

#include <SDL3/SDL_keycode.h>

namespace xwine::input {

class KeyboardInputAdapter {
public:
    static UiAction translate_key(SDL_Keycode key, SDL_Keymod mod, bool text_input_active);
};

} // namespace xwine::input
