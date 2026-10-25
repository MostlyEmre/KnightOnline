#ifndef N3BASE_PLATFORM_MACOS_INPUTSCRIPT_H
#define N3BASE_PLATFORM_MACOS_INPUTSCRIPT_H

#pragma once

#include <cstdint>

// Scripted input for testing: with OPENKO_INPUT_SCRIPT=<file>, the platform layer plays the file's
// mouse, keyboard and text commands back as if a user made them, and can save frames as PNGs.
// Times are in 60ths of a second (real time, whatever the frame rate). Commands, one per line
// ('#' starts a comment):
//
//   wait <ticks>              do nothing for that long
//   move <x> <y>              put the cursor at client coordinates
//   click <x> <y> [right]     move there and click (button down for 2 frames, then up)
//   down / up                 press the left button and keep it down / let it go (a drag is
//                             move, down, wait, move, wait, up)
//   key <name>                press a key for 2 ticks (DirectInput names: RETURN, ESCAPE, W, F1...)
//   hold <name> <ticks>       hold a key down
//   text <characters>         type into the focused edit box
//   snapshot <file.png>       save the next frame
//   quit                      end the program like PostQuitMessage(0)
//   options                   open the game's Options window (as the app menu's Settings... does)
//   handoff                   stop the script and give the window and real input to the user
namespace Platform::macOS::InputScript
{

/// \brief Whether a script is playing (input then comes from it only).
bool IsActive();

/// \brief Advances the script by one frame.
void Tick();

/// \brief The scripted cursor, in client coordinates.
void CursorClient(int& x, int& y);

/// \brief Scripted mouse buttons (bit 0 left, bit 1 right).
uint32_t MouseButtons();

/// \brief Whether the script holds this DirectInput key down.
bool IsKeyDown(int iScanCode);

} // namespace Platform::macOS::InputScript

#endif // N3BASE_PLATFORM_MACOS_INPUTSCRIPT_H
