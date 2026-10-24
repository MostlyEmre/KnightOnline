#include "../Input.h"

// DirectInput lives in its own file so that only users of KeyboardDevice link dinput8.
#include <dinput.h>

namespace Platform
{

struct KeyboardDevice::Impl
{
	LPDIRECTINPUT8 lpDI                = nullptr;
	LPDIRECTINPUTDEVICE8 lpDIDKeyboard = nullptr;
};

KeyboardDevice::KeyboardDevice() = default;

KeyboardDevice::~KeyboardDevice()
{
	Release();
}

bool KeyboardDevice::Init(HINSTANCE hInstance, HWND hWnd)
{
	Release();

	auto impl    = std::make_unique<Impl>();

	HRESULT rval = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
		reinterpret_cast<void**>(&impl->lpDI), nullptr);
	if (rval != DI_OK)
		return false;

	// Create the keyboard device
	rval = impl->lpDI->CreateDevice(GUID_SysKeyboard, &impl->lpDIDKeyboard, nullptr);
	if (rval != DI_OK)
	{
		impl->lpDI->Release();
		return false;
	}

	impl->lpDIDKeyboard->SetDataFormat(&c_dfDIKeyboard);
	impl->lpDIDKeyboard->SetCooperativeLevel(
		hWnd, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY);

	_impl = std::move(impl);
	return true;
}

bool KeyboardDevice::Acquire()
{
	if (_impl == nullptr)
		return false;

	HRESULT rval = _impl->lpDIDKeyboard->Acquire();
	return rval == DI_OK || rval == S_FALSE;
}

void KeyboardDevice::Unacquire()
{
	if (_impl != nullptr)
		_impl->lpDIDKeyboard->Unacquire();
}

bool KeyboardDevice::GetState(uint8_t (&keys)[256])
{
	if (_impl == nullptr)
		return false;

	return _impl->lpDIDKeyboard->GetDeviceState(sizeof(keys), keys) == DI_OK;
}

void KeyboardDevice::Release()
{
	if (_impl == nullptr)
		return;

	_impl->lpDIDKeyboard->Unacquire();
	_impl->lpDIDKeyboard->Release();
	_impl->lpDI->Release();
	_impl.reset();
}

} // namespace Platform
