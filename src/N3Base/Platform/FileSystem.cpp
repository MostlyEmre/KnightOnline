#include "FileSystem.h"

#include <FileIO/PathResolver.h>

#include <algorithm>
#include <cctype>
#include <cstring>
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
	std::vector<std::string> names;

	std::error_code ec;
	for (const auto& entry :
		std::filesystem::directory_iterator(PathResolver::Resolve(szDirectory), ec))
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

FILE* OpenFile(const std::string& szPath, const char* szMode)
{
#if defined(_WIN32)
	return std::fopen(szPath.c_str(), szMode);
#else
	// Anything that may write ("w", "a", "r+") must not land in read-only game data.
	bool bWrites = std::strpbrk(szMode, "wa+") != nullptr;
	return std::fopen(
		(bWrites ? PathResolver::ResolveForWriting(szPath) : PathResolver::Resolve(szPath)).c_str(),
		szMode);
#endif
}

} // namespace Platform
