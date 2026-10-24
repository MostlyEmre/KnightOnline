#include "../Input.h"
#include "Cocoa/CocoaApp.h"
#include "InputScript.h"

namespace Platform
{

// The keyboard state comes from the game window's key events (Cocoa/CocoaApp.mm). Like
// DirectInput's foreground cooperative level, it's only available while the window has focus.

struct KeyboardDevice::Impl
{
};

KeyboardDevice::KeyboardDevice()  = default;

KeyboardDevice::~KeyboardDevice() = default;

bool KeyboardDevice::Init(HINSTANCE /*hInstance*/, HWND /*hWnd*/)
{
	_impl = std::make_unique<Impl>();
	return true;
}

bool KeyboardDevice::Acquire()
{
	return _impl != nullptr && (macOS::Cocoa::IsActive() || macOS::InputScript::IsActive());
}

void KeyboardDevice::Unacquire()
{
}

bool KeyboardDevice::GetState(uint8_t (&keys)[256])
{
	if (_impl == nullptr)
		return false;

	if (macOS::InputScript::IsActive())
	{
		for (int i = 0; i < 256; i++)
			keys[i] = macOS::InputScript::IsKeyDown(i) ? 0x80 : 0;
		return true;
	}

	if (!macOS::Cocoa::IsActive())
		return false;

	macOS::Cocoa::KeyboardState(keys);
	return true;
}

void KeyboardDevice::Release()
{
	_impl.reset();
}

} // namespace Platform
