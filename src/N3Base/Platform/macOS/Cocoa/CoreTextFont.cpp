#include "CoreTextFont.h"

#include <CoreText/CoreText.h>

#include <cmath>

namespace Platform::macOS::Cocoa
{

namespace
{

CFStringRef CreateCFString(const std::string& text)
{
	CFStringRef s = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()),
		static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
	return s != nullptr ? s : CFStringCreateWithCString(nullptr, "", kCFStringEncodingUTF8);
}

} // namespace

struct CoreTextFont::Impl
{
	CTFontRef font       = nullptr;
	CGFloat ascent       = 0;
	CGFloat descent      = 0;
	CGContextRef context = nullptr;
	std::vector<uint32_t> pixels;
	uint32_t width  = 0;
	uint32_t height = 0;

	CTLineRef CreateLine(const std::string& text, CGColorRef color) const
	{
		CFStringRef string         = CreateCFString(text);
		const void* keys[]         = { kCTFontAttributeName, kCTForegroundColorAttributeName };
		const void* values[]       = { font, color };
		CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values,
			color != nullptr ? 2 : 1, &kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks);
		CFAttributedStringRef attributed = CFAttributedStringCreate(nullptr, string, attributes);
		CTLineRef line                   = CTLineCreateWithAttributedString(attributed);
		CFRelease(attributed);
		CFRelease(attributes);
		CFRelease(string);
		return line;
	}
};

std::unique_ptr<CoreTextFont> CoreTextFont::Create(
	const std::string& szFace, int iPixelHeight, bool bBold, bool bItalic)
{
	CGFloat size = std::abs(iPixelHeight) > 0 ? static_cast<CGFloat>(std::abs(iPixelHeight)) : 12.0;

	CFStringRef name = CreateCFString(szFace);
	CTFontRef font   = CTFontCreateWithName(name, size, nullptr);
	CFRelease(name);
	if (font == nullptr)
		return nullptr;

	CTFontSymbolicTraits traits = (bBold ? kCTFontTraitBold : 0)
								  | (bItalic ? kCTFontTraitItalic : 0);
	if (traits != 0)
	{
		CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(font, size, nullptr, traits, traits);
		if (styled != nullptr)
		{
			CFRelease(font);
			font = styled;
		}
	}

	auto impl     = std::make_unique<Impl>();
	impl->font    = font;
	impl->ascent  = CTFontGetAscent(font);
	impl->descent = CTFontGetDescent(font);
	return std::unique_ptr<CoreTextFont>(new CoreTextFont(std::move(impl)));
}

CoreTextFont::CoreTextFont(std::unique_ptr<Impl> impl) : _impl(std::move(impl))
{
}

CoreTextFont::~CoreTextFont()
{
	EndCanvas();
	if (_impl != nullptr && _impl->font != nullptr)
		CFRelease(_impl->font);
}

void CoreTextFont::Measure(const std::string& szText, int& iWidth, int& iHeight) const
{
	CTLineRef line = _impl->CreateLine(szText, nullptr);
	double width   = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
	CFRelease(line);

	iWidth  = static_cast<int>(std::lround(width));
	iHeight = static_cast<int>(std::ceil(_impl->ascent + _impl->descent));
}

bool CoreTextFont::BeginCanvas(uint32_t width, uint32_t height)
{
	EndCanvas();
	if (width == 0 || height == 0)
		return false;

	_impl->pixels.assign(static_cast<size_t>(width) * height, 0);
	_impl->width        = width;
	_impl->height       = height;

	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	// Little-endian 32-bit with the unused byte first: each pixel reads as 0x00RRGGBB.
	_impl->context = CGBitmapContextCreate(_impl->pixels.data(), width, height, 8, width * 4, rgb,
		static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little);
	CGColorSpaceRelease(rgb);
	if (_impl->context == nullptr)
		return false;

	// Grey-scale antialiasing, like GDI's ANTIALIASED_QUALITY (no subpixel colour fringes).
	CGContextSetShouldAntialias(_impl->context, true);
	CGContextSetShouldSmoothFonts(_impl->context, false);
	CGContextSetRGBFillColor(_impl->context, 0, 0, 0, 1);
	CGContextFillRect(_impl->context, CGRectMake(0, 0, width, height));
	return true;
}

void CoreTextFont::Draw(int x, int y, const std::string& szText)
{
	if (_impl->context == nullptr)
		return;

	CGColorRef white = CGColorCreateGenericRGB(1, 1, 1, 1);
	CTLineRef line   = _impl->CreateLine(szText, white);

	// The canvas's origin is at the bottom left; the caller's y is the top of the line.
	CGFloat baseline = std::round(_impl->height - (y + _impl->ascent));
	CGContextSetTextPosition(_impl->context, x, baseline);
	CTLineDraw(line, _impl->context);

	CFRelease(line);
	CGColorRelease(white);
}

const uint32_t* CoreTextFont::Pixels() const
{
	return _impl->pixels.empty() ? nullptr : _impl->pixels.data();
}

void CoreTextFont::EndCanvas()
{
	if (_impl == nullptr || _impl->context == nullptr)
		return;

	CGContextRelease(_impl->context);
	_impl->context = nullptr;
}

} // namespace Platform::macOS::Cocoa
