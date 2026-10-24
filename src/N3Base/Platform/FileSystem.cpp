#include "FileSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace Platform
{

namespace
{

bool EqualsIgnoreCase(const std::string& a, const std::string& b)
{
	return a.size() == b.size()
		   && std::equal(a.begin(), a.end(), b.begin(),
			   [](char x, char y)
			   {
				   return std::tolower(static_cast<unsigned char>(x))
						  == std::tolower(static_cast<unsigned char>(y));
			   });
}

} // namespace

std::vector<std::string> FindFiles(const std::string& szDirectory, const std::string& szExtension)
{
	std::string szDir = szDirectory;
	std::replace(szDir.begin(), szDir.end(), '\\', '/');

	std::vector<std::string> names;

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(szDir, ec))
	{
		if (!entry.is_regular_file(ec))
			continue;

		std::string szName = entry.path().filename().string();
		std::string szExt  = entry.path().extension().string();
		if (EqualsIgnoreCase(szExt, szExtension))
			names.push_back(std::move(szName));
	}

	return names;
}

} // namespace Platform
