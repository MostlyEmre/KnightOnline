#include "../TextInput.h"
#include "NotImplemented.h"

namespace Platform::TextInput
{

bool Create(HWND /*hWndParent*/, const RECT& /*rc*/, const Handlers& /*handlers*/)
{
	macOS::NotImplemented("TextInput::Create");
	return false;
}

bool IsCreated()
{
	return false;
}

void Focus(const RECT& /*rcRegion*/, const std::string& /*szText*/)
{
}

void Unfocus()
{
}

std::string GetText()
{
	return {};
}

int GetCaretPos()
{
	return 0;
}

void SetImeStatus(POINT /*ptPos*/, bool /*bOpen*/)
{
}

} // namespace Platform::TextInput
