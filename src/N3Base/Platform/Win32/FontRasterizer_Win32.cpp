#include "../FontRasterizer.h"

namespace Platform
{

namespace
{

// One memory DC shared by all fonts, alive while any font is.
HDC s_hDC            = nullptr;
HFONT s_hFontOld     = nullptr; // the DC's default font
int s_iInstanceCount = 0;

class GdiFontRasterizer : public FontRasterizer
{
public:
	explicit GdiFontRasterizer(HFONT hFont) : _hFont(hFont)
	{
	}

	~GdiFontRasterizer() override
	{
		ReleaseCanvas();

		if (s_hFontOld)
			SelectObject(s_hDC, s_hFontOld);
		DeleteObject(_hFont);

		if (--s_iInstanceCount <= 0)
		{
			DeleteDC(s_hDC);
			s_hDC      = nullptr;
			s_hFontOld = nullptr;
		}
	}

	SIZE MeasureText(std::string_view text) override
	{
		SIZE size {};
		SelectObject(s_hDC, _hFont);
		GetTextExtentPoint32A(s_hDC, text.data(), static_cast<int>(text.size()), &size);
		return size;
	}

	bool BeginCanvas(uint32_t width, uint32_t height) override
	{
		ReleaseCanvas();

		// Prepare to create a bitmap
		BITMAPINFO bmi {};
		bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth       = (int) width;
		bmi.bmiHeader.biHeight      = -(int) height;
		bmi.bmiHeader.biPlanes      = 1;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biBitCount    = 32;

		// Create a DC and a bitmap for the font
		_hbmBitmap                  = CreateDIBSection(
            s_hDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&_pBitmapBits), nullptr, 0);
		if (nullptr == _hbmBitmap)
			return false;

		_hObjPrev = ::SelectObject(s_hDC, _hbmBitmap);

		// Set text properties
		SetTextColor(s_hDC, RGB(255, 255, 255));
		SetBkColor(s_hDC, RGB(0, 0, 0));
		SetTextAlign(s_hDC, TA_TOP);
		return true;
	}

	void DrawString(int x, int y, std::string_view text) override
	{
		SelectObject(s_hDC, _hFont);
		ExtTextOutA(
			s_hDC, x, y, ETO_OPAQUE, nullptr, text.data(), static_cast<UINT>(text.size()), nullptr);
	}

	const uint32_t* CanvasPixels() const override
	{
		return _pBitmapBits;
	}

	void EndCanvas() override
	{
		ReleaseCanvas();
	}

private:
	void ReleaseCanvas()
	{
		if (_hbmBitmap == nullptr)
			return;

		::SelectObject(s_hDC, _hObjPrev); // 반드시 전의걸 선택해야..
		DeleteObject(_hbmBitmap);         // 제대로 지워진다..

		_hbmBitmap   = nullptr;
		_hObjPrev    = nullptr;
		_pBitmapBits = nullptr;
	}

	HFONT _hFont           = nullptr;
	HBITMAP _hbmBitmap     = nullptr;
	HGDIOBJ _hObjPrev      = nullptr;
	uint32_t* _pBitmapBits = nullptr;
};

} // namespace

int FontRasterizer::GetLogPixelsY()
{
	if (s_hDC != nullptr)
		return GetDeviceCaps(s_hDC, LOGPIXELSY);

	HDC hDC         = CreateCompatibleDC(nullptr);
	int iLogPixelsY = GetDeviceCaps(hDC, LOGPIXELSY);
	DeleteDC(hDC);
	return iLogPixelsY;
}

std::unique_ptr<FontRasterizer> FontRasterizer::Create(
	const std::string& szFaceName, int iHeight, bool bBold, bool bItalic)
{
	if (0 == s_iInstanceCount)
	{
		s_hDC      = CreateCompatibleDC(nullptr);
		s_hFontOld = static_cast<HFONT>(GetCurrentObject(s_hDC, OBJ_FONT));
		SetMapMode(s_hDC, MM_TEXT);
	}

	// Create a font.  By specifying ANTIALIASED_QUALITY, we might get an
	// antialiased font, but this is not guaranteed.
	HFONT hFont = CreateFontA(iHeight, 0, 0, 0, bBold ? FW_BOLD : FW_NORMAL, bItalic ? TRUE : FALSE,
		FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		VARIABLE_PITCH, szFaceName.c_str());
	if (nullptr == hFont)
	{
		if (0 == s_iInstanceCount)
		{
			DeleteDC(s_hDC);
			s_hDC      = nullptr;
			s_hFontOld = nullptr;
		}

		return nullptr;
	}

	++s_iInstanceCount;
	return std::make_unique<GdiFontRasterizer>(hFont);
}

} // namespace Platform
