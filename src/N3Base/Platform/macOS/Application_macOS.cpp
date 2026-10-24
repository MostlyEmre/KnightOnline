#include "../Application.h"
#include "NotImplemented.h"

namespace Platform
{

HWND CreateMainWindow(
	HINSTANCE /*hInstance*/, const MainWindowDesc& /*desc*/, const EventHandlers& /*handlers*/)
{
	macOS::NotImplemented("CreateMainWindow");
	return nullptr;
}

int RunEventLoop(HWND /*hWnd*/, int /*iAcceleratorResourceId*/)
{
	macOS::NotImplemented("RunEventLoop");
	return -1;
}

} // namespace Platform
