#ifndef N3BASE_PLATFORM_FONTRASTERIZER_H
#define N3BASE_PLATFORM_FONTRASTERIZER_H

#pragma once

#include "Types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Draws text with the system's fonts into a CPU bitmap, which CDFont turns into a texture.
// Text is in the client's ANSI code page (single- and double-byte characters).
namespace Platform
{

class FontRasterizer
{
public:
	/// \brief The screen's vertical resolution in dots per inch (GetDeviceCaps LOGPIXELSY), for
	/// converting point sizes to pixel heights.
	static int GetLogPixelsY();

	/// \brief Creates a font. iHeight follows CreateFont: negative is the character height in
	/// pixels. Returns nullptr if the font can't be created.
	static std::unique_ptr<FontRasterizer> Create(
		const std::string& szFaceName, int iHeight, bool bBold, bool bItalic);

	virtual ~FontRasterizer()                                    = default;

	/// \brief The size of the text in pixels, as GetTextExtentPoint32 measures it.
	virtual SIZE MeasureText(std::string_view text)              = 0;

	/// \brief Starts drawing into a cleared (black) 32-bit canvas of the given size, rows top-down.
	/// Returns false on failure.
	virtual bool BeginCanvas(uint32_t width, uint32_t height)    = 0;

	/// \brief Draws white text with its top-left corner at (x, y) on an opaque black background.
	virtual void DrawString(int x, int y, std::string_view text) = 0;

	/// \brief The canvas pixels (0x00RRGGBB, width * height of them).
	virtual const uint32_t* CanvasPixels() const                 = 0;

	/// \brief Ends drawing and releases the canvas.
	virtual void EndCanvas()                                     = 0;

protected:
	FontRasterizer() = default;
};

} // namespace Platform

#endif // N3BASE_PLATFORM_FONTRASTERIZER_H
