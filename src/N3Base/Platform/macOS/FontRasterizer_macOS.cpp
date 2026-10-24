#include "../FontRasterizer.h"
#include "Cocoa/CoreTextFont.h"
#include "CodePage.h"

#include <algorithm>
#include <cctype>

namespace Platform
{

namespace
{

// The face to ask CoreText for: Windows faces macOS doesn't ship get a close stand-in.
std::string MacFaceFor(const std::string& szFace)
{
	std::string lower = szFace;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

	// "굴림" (Gulim) and "돋움" (Dotum), the Korean Windows UI faces, spelt in CP949 or UTF-8.
	if (lower == "gulim" || lower == "gulimche" || lower == "dotum" || lower == "\xb1\xbc\xb8\xb2"
		|| lower == "\xb5\xb8\xbf\xf2" || lower == "\xea\xb5\xb4\xeb\xa6\xbc"
		|| lower == "\xeb\x8f\x8b\xec\x9b\x80")
		return "Apple SD Gothic Neo";

	if (lower.empty() || lower == "ms sans serif" || lower == "ms shell dlg")
		return "Tahoma";

	return macOS::AnsiToUtf8(szFace);
}

class CoreTextFontRasterizer : public FontRasterizer
{
public:
	explicit CoreTextFontRasterizer(std::unique_ptr<macOS::Cocoa::CoreTextFont> font) :
		_font(std::move(font))
	{
	}

	SIZE MeasureText(std::string_view text) override
	{
		int iWidth = 0, iHeight = 0;
		_font->Measure(macOS::AnsiToUtf8(text), iWidth, iHeight);
		return { iWidth, iHeight };
	}

	bool BeginCanvas(uint32_t width, uint32_t height) override
	{
		return _font->BeginCanvas(width, height);
	}

	void DrawString(int x, int y, std::string_view text) override
	{
		_font->Draw(x, y, macOS::AnsiToUtf8(text));
	}

	const uint32_t* CanvasPixels() const override
	{
		return _font->Pixels();
	}

	void EndCanvas() override
	{
		_font->EndCanvas();
	}

private:
	std::unique_ptr<macOS::Cocoa::CoreTextFont> _font;
};

} // namespace

int FontRasterizer::GetLogPixelsY()
{
	// What Windows reports at 100% scaling; the game's pixels are what's rasterized.
	return 96;
}

std::unique_ptr<FontRasterizer> FontRasterizer::Create(
	const std::string& szFaceName, int iHeight, bool bBold, bool bItalic)
{
	auto font = macOS::Cocoa::CoreTextFont::Create(MacFaceFor(szFaceName), iHeight, bBold, bItalic);
	if (font == nullptr)
		return nullptr;

	return std::make_unique<CoreTextFontRasterizer>(std::move(font));
}

} // namespace Platform
