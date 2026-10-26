#ifndef N3BASE_PLATFORM_INPUT_H
#define N3BASE_PLATFORM_INPUT_H

#pragma once

#include "Types.h"

#include <cstdint>
#include <memory>

// Keyboard, mouse and cursor. Polled state keeps its Win32 meaning: GetAsyncKeyState() takes
// virtual-key codes (VK_*), KeyboardDevice reports DirectInput scan codes (DIK_*, KeyCodes.h).
namespace Platform
{

/// \brief The state of a key or mouse button (VK_*) right now: bit 15 set while it is down,
/// bit 0 set if it was pressed since the previous call.
SHORT GetAsyncKeyState(int vKey);

/// \brief The cursor position in screen coordinates.
BOOL GetCursorPos(POINT* lpPoint);

/// \brief Moves the cursor to a position in screen coordinates.
BOOL SetCursorPos(int x, int y);

/// \brief Converts a point from screen coordinates to the window's client coordinates.
BOOL ScreenToClient(HWND hWnd, POINT* lpPoint);

/// \brief Converts a point from the window's client coordinates to screen coordinates.
BOOL ClientToScreen(HWND hWnd, POINT* lpPoint);

/// \brief The maximum time between the clicks of a double click, in milliseconds.
UINT GetDoubleClickTime();

/// \brief Loads a cursor from the executable's resources (by resource ID).
HCURSOR LoadCursorResource(HINSTANCE hInstance, int resourceId);

/// \brief Sets the cursor shown over the game window (nullptr hides it); returns the previous one.
HCURSOR SetCursor(HCURSOR hCursor);

/// \brief The cursor currently set.
HCURSOR GetCursor();

/// \brief Increments (bShow) or decrements the cursor display counter; the cursor shows while it
/// is >= 0. Returns the new counter.
int ShowCursor(BOOL bShow);

/// \brief The keyboard as a polled device (DirectInput's system keyboard on Windows): a 256-byte
/// state indexed by DIK_* scan code, non-zero while the key is down.
class KeyboardDevice
{
public:
	KeyboardDevice();
	~KeyboardDevice();

	KeyboardDevice(const KeyboardDevice&)            = delete;
	KeyboardDevice& operator=(const KeyboardDevice&) = delete;

	/// \brief Creates the device for the window. Returns false if there is no keyboard device.
	bool Init(HINSTANCE hInstance, HWND hWnd);

	/// \brief Starts (or resumes) receiving input. Returns false if the device can't be acquired.
	bool Acquire();

	/// \brief Stops receiving input.
	void Unacquire();

	/// \brief Reads the current key state into keys[0..255]. Returns false if the device was lost
	/// (it then needs Acquire()).
	bool GetState(uint8_t (&keys)[256]);

	/// \brief Releases the device.
	void Release();

	bool IsCreated() const
	{
		return _impl != nullptr;
	}

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

} // namespace Platform

#endif // N3BASE_PLATFORM_INPUT_H
