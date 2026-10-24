#include "../Input.h"
#include "NotImplemented.h"

namespace Platform
{

struct KeyboardDevice::Impl
{
};

KeyboardDevice::KeyboardDevice()  = default;

KeyboardDevice::~KeyboardDevice() = default;

bool KeyboardDevice::Init(HINSTANCE /*hInstance*/, HWND /*hWnd*/)
{
	macOS::NotImplemented("KeyboardDevice::Init");
	return false;
}

bool KeyboardDevice::Acquire()
{
	return false;
}

void KeyboardDevice::Unacquire()
{
}

bool KeyboardDevice::GetState(uint8_t (& /*keys*/)[256])
{
	return false;
}

void KeyboardDevice::Release()
{
	_impl.reset();
}

} // namespace Platform
