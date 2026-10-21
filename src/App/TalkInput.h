#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

namespace Ascension {

// Shared by the keyboard and the gamepad. The talk menu is the only place a pad button
// is read. Confirm is the same press as E on the shop screen.
enum class TalkCommand : u8 { None, Up, Down, Confirm };

inline TalkCommand TalkCommandFromKey(SDL_Scancode key)
{
    if (key == SDL_SCANCODE_UP)
        return TalkCommand::Up;
    if (key == SDL_SCANCODE_DOWN)
        return TalkCommand::Down;
    if (key == SDL_SCANCODE_E || key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
        return TalkCommand::Confirm;
    return TalkCommand::None;
}

inline TalkCommand TalkCommandFromPad(SDL_GamepadButton button)
{
    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP)
        return TalkCommand::Up;
    if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
        return TalkCommand::Down;
    if (button == SDL_GAMEPAD_BUTTON_SOUTH)
        return TalkCommand::Confirm;
    return TalkCommand::None;
}

} // namespace Ascension
