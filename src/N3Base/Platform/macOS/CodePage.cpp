#include "CodePage.h"

#include <array>
#include <cstdint>

namespace Platform::macOS
{

namespace
{

// Windows-1252 0x80..0x9F; the rest of 0x80..0xFF is Latin-1. 0 = undefined in 1252.
constexpr std::array<char32_t, 32> Cp1252High = {
	0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, //
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,      //
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, //
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178, //
};

char32_t AnsiToCodePoint(unsigned char ch)
{
	if (ch >= 0x80 && ch < 0xA0)
		return Cp1252High[ch - 0x80] != 0 ? Cp1252High[ch - 0x80] : U'?';
	return ch;
}

char CodePointToAnsi(char32_t cp)
{
	if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF))
		return static_cast<char>(cp);

	for (size_t i = 0; i < Cp1252High.size(); i++)
	{
		if (Cp1252High[i] == cp)
			return static_cast<char>(0x80 + i);
	}

	return '?';
}

void AppendUtf8(std::string& out, char32_t cp)
{
	if (cp < 0x80)
	{
		out += static_cast<char>(cp);
	}
	else if (cp < 0x800)
	{
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

} // namespace

std::string AnsiToUtf8(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (char ch : text)
		AppendUtf8(out, AnsiToCodePoint(static_cast<unsigned char>(ch)));
	return out;
}

std::string Utf8ToAnsi(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size();)
	{
		auto lead     = static_cast<unsigned char>(text[i]);
		size_t length = lead < 0x80           ? 1
						: (lead >> 5) == 0x6  ? 2
						: (lead >> 4) == 0xE  ? 3
						: (lead >> 3) == 0x1E ? 4
											  : 0;
		if (length == 0 || i + length > text.size())
		{
			out += '?';
			i++;
			continue;
		}

		char32_t cp = length == 1 ? lead : (lead & (0xFF >> (length + 1)));
		for (size_t k = 1; k < length; k++)
			cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);

		out += CodePointToAnsi(cp);
		i   += length;
	}
	return out;
}

} // namespace Platform::macOS
