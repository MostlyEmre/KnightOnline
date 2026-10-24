#include "Win32Types.h"

#include <span>
#include <string_view>

namespace
{

bool IsPathSeparator(char ch)
{
	return ch == '\\' || ch == '/';
}

void CopyPart(char* out, std::string_view part, size_t capacity)
{
	if (out == nullptr)
		return;

	std::span<char> buffer(out, capacity);
	size_t len = std::min(part.size(), capacity - 1);
	part.copy(buffer.data(), len);
	buffer[len] = '\0';
}

} // namespace

// Same splitting rules as the MSVC CRT: an optional "X:" drive, the directory up to and including
// the last separator, then the file name and the extension from its last '.'.
void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
	std::string_view rest = path != nullptr ? path : "";

	std::string_view drivePart;
	if (rest.size() >= 2 && rest[1] == ':')
	{
		drivePart = rest.substr(0, 2);
		rest.remove_prefix(2);
	}

	size_t lastSeparator = std::string_view::npos;
	for (size_t i = 0; i < rest.size(); i++)
	{
		if (IsPathSeparator(rest[i]))
			lastSeparator = i;
	}

	std::string_view dirPart;
	if (lastSeparator != std::string_view::npos)
	{
		dirPart = rest.substr(0, lastSeparator + 1);
		rest.remove_prefix(lastSeparator + 1);
	}

	std::string_view extPart;
	size_t lastDot = rest.rfind('.');
	if (lastDot != std::string_view::npos)
	{
		extPart = rest.substr(lastDot);
		rest    = rest.substr(0, lastDot);
	}

	CopyPart(drive, drivePart, _MAX_DRIVE);
	CopyPart(dir, dirPart, _MAX_DIR);
	CopyPart(fname, rest, _MAX_FNAME);
	CopyPart(ext, extPart, _MAX_EXT);
}

// Same joining rules as the MSVC CRT, including the '\' it adds after a directory without one.
void _makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
	if (path == nullptr)
		return;

	std::string result;
	if (drive != nullptr && *drive != '\0')
	{
		result += *drive;
		result += ':';
	}

	if (dir != nullptr && *dir != '\0')
	{
		std::string_view dirPart  = dir;
		result                   += dirPart;
		if (!IsPathSeparator(dirPart.back()))
			result += '\\';
	}

	if (fname != nullptr)
		result += fname;

	if (ext != nullptr && *ext != '\0')
	{
		if (*ext != '.')
			result += '.';
		result += ext;
	}

	CopyPart(path, result, _MAX_PATH);
}
