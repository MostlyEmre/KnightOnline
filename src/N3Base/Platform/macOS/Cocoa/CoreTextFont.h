#ifndef N3BASE_PLATFORM_MACOS_COCOA_CORETEXTFONT_H
#define N3BASE_PLATFORM_MACOS_COCOA_CORETEXTFONT_H

#pragma once

// CoreText text rendering for FontRasterizer_macOS.cpp. Neutral types only (see CocoaApp.h); text
// is UTF-8.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Platform::macOS::Cocoa
{

class CoreTextFont
{
public:
	/// \brief A font of the given face whose em is iPixelHeight pixels (GDI's negative CreateFont
	/// height). Returns nullptr on failure.
	static std::unique_ptr<CoreTextFont> Create(
		const std::string& szFace, int iPixelHeight, bool bBold, bool bItalic);

	~CoreTextFont();

	CoreTextFont(const CoreTextFont&)            = delete;
	CoreTextFont& operator=(const CoreTextFont&) = delete;

	/// \brief The text's advance width and the font's line height, in pixels.
	void Measure(const std::string& szText, int& iWidth, int& iHeight) const;

	/// \brief A black canvas, rows top-down, 0x00RRGGBB pixels.
	bool BeginCanvas(uint32_t width, uint32_t height);

	/// \brief White text with its top-left corner (the line's top, not the baseline) at (x, y).
	void Draw(int x, int y, const std::string& szText);

	const uint32_t* Pixels() const;

	void EndCanvas();

private:
	struct Impl;

	explicit CoreTextFont(std::unique_ptr<Impl> impl);

	std::unique_ptr<Impl> _impl;
};

} // namespace Platform::macOS::Cocoa

#endif // N3BASE_PLATFORM_MACOS_COCOA_CORETEXTFONT_H
