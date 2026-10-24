#include "../Settings.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace Platform
{

// Each value is a file: ~/Library/Application Support/KnightOnline/Settings/<key>/<value name>,
// with the key's '\' separators as directories (the registry layout the client uses on Windows).

namespace
{

std::filesystem::path SettingPath(const std::string& szKey, const char* szValueName)
{
	const char* home           = std::getenv("HOME");
	std::filesystem::path path = std::filesystem::path(home != nullptr ? home : ".") / "Library"
								 / "Application Support" / "KnightOnline" / "Settings";

	std::string key = szKey;
	std::replace(key.begin(), key.end(), '\\', '/');
	return path / std::filesystem::path(key).relative_path() / szValueName;
}

} // namespace

bool WriteUserSetting(
	const std::string& szKey, const char* szValueName, const void* pData, size_t size)
{
	std::filesystem::path path = SettingPath(szKey, szValueName);

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(static_cast<const char*>(pData), static_cast<std::streamsize>(size));
	return file.good();
}

bool ReadUserSetting(const std::string& szKey, const char* szValueName, void* pData, size_t size)
{
	std::ifstream file(SettingPath(szKey, szValueName), std::ios::binary);
	if (!file)
		return false;

	file.read(static_cast<char*>(pData), static_cast<std::streamsize>(size));
	return !file.bad();
}

} // namespace Platform
