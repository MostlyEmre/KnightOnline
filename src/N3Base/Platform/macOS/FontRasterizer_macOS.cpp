#include "../FontRasterizer.h"
#include "NotImplemented.h"

namespace Platform
{

int FontRasterizer::GetLogPixelsY()
{
	return 96;
}

std::unique_ptr<FontRasterizer> FontRasterizer::Create(
	const std::string& /*szFaceName*/, int /*iHeight*/, bool /*bBold*/, bool /*bItalic*/)
{
	macOS::NotImplemented("FontRasterizer::Create");
	return nullptr;
}

} // namespace Platform
