#include "../Settings.h"
#include "../Types.h"

namespace Platform
{

bool WriteUserSetting(
	const std::string& szKey, const char* szValueName, const void* pData, size_t size)
{
	HKEY hKey = nullptr;

	if (RegOpenKeyA(HKEY_CURRENT_USER, szKey.c_str(), &hKey) != ERROR_SUCCESS)
	{
		if (RegCreateKeyA(HKEY_CURRENT_USER, szKey.c_str(), &hKey) != ERROR_SUCCESS)
			return false;

		if (RegOpenKeyA(HKEY_CURRENT_USER, szKey.c_str(), &hKey) != ERROR_SUCCESS)
			return false;
	}

	// set the value
	if (RegSetValueExA(hKey, szValueName, 0, REG_BINARY, static_cast<const BYTE*>(pData),
			static_cast<DWORD>(size))
		!= ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}

	return RegCloseKey(hKey) == ERROR_SUCCESS;
}

bool ReadUserSetting(const std::string& szKey, const char* szValueName, void* pData, size_t size)
{
	HKEY hKey  = nullptr;
	DWORD Type = 0;
	DWORD len  = static_cast<DWORD>(size);

	if (RegOpenKeyA(HKEY_CURRENT_USER, szKey.c_str(), &hKey) != ERROR_SUCCESS)
		return false;

	// get the value
	if (RegQueryValueExA(hKey, szValueName, nullptr, &Type, static_cast<BYTE*>(pData), &len)
		!= ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}

	return RegCloseKey(hKey) == ERROR_SUCCESS;
}

} // namespace Platform
