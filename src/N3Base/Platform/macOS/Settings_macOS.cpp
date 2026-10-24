#include "../Settings.h"
#include "NotImplemented.h"

namespace Platform
{

bool WriteUserSetting(const std::string& /*szKey*/, const char* /*szValueName*/,
	const void* /*pData*/, size_t /*size*/)
{
	macOS::NotImplemented("WriteUserSetting");
	return false;
}

bool ReadUserSetting(
	const std::string& /*szKey*/, const char* /*szValueName*/, void* /*pData*/, size_t /*size*/)
{
	macOS::NotImplemented("ReadUserSetting");
	return false;
}

} // namespace Platform
