#include "../ScreenCapture.h"

#include <cassert>

// The screen capture CJpegFile (JpegFile) used to do, for the client's screenshots.

namespace Platform
{

namespace
{

constexpr WORD PALVERSION = 0x300;

constexpr auto WIDTHBYTES(auto bits)
{
	return (((bits) + 31) / 32 * 4);
}

// Both DIB header types start with their size.
bool IsWin30Dib(const void* lpDIB)
{
	return static_cast<const BITMAPINFOHEADER*>(lpDIB)->biSize == sizeof(BITMAPINFOHEADER);
}

int PalEntriesOnDevice(HDC hDC)
{
	int nColors = 0; // number of colors

	/*  Find out the number of palette entries on this
	 *  device.
	 */

	//	nColors = GetDeviceCaps(hDC, SIZEPALETTE);
	nColors     = GetDeviceCaps(hDC, BITSPIXEL);
	if (nColors > 8)
		return 0;

	nColors = 2 << nColors;

	/*  For non-palette devices, we'll use the # of system
	 *  colors for our palette size.
	 */
	if (nColors == 0)
		nColors = GetDeviceCaps(hDC, NUMCOLORS);
	assert(nColors);
	return nColors;
}

HPALETTE GetSystemPalette()
{
	HPALETTE hPal         = nullptr; // handle to a palette
	HDC hDC               = nullptr; // handle to a DC
	HANDLE hLogPal        = nullptr; // handle to a logical palette
	LPLOGPALETTE lpLogPal = nullptr; // pointer to a logical palette
	int nColors           = 0;       // number of colors

	/* Find out how many palette entries we want. */
	hDC                   = GetDC(nullptr);
	if (hDC == nullptr)
		return nullptr;

	nColors = PalEntriesOnDevice(hDC); // Number of palette entries
	if (nColors == 0)
	{
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	/* Allocate room for the palette and lock it. */
	hLogPal = GlobalAlloc(GHND, sizeof(LOGPALETTE) + nColors * sizeof(PALETTEENTRY));

	/* if we didn't get a logical palette, return nullptr */
	if (hLogPal == nullptr)
	{
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	/* get a pointer to the logical palette */
	lpLogPal = static_cast<LPLOGPALETTE>(GlobalLock(hLogPal));
	if (lpLogPal == nullptr)
	{
		GlobalFree(hLogPal);
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	/* set some important fields */
	lpLogPal->palVersion    = PALVERSION;
	lpLogPal->palNumEntries = static_cast<WORD>(nColors);

	/* Copy the current system palette into our logical palette */

	GetSystemPaletteEntries(hDC, 0, nColors, lpLogPal->palPalEntry);

	/*  Go ahead and create the palette.  Once it's created,
	 *  we no longer need the LOGPALETTE, so free it.
	 */

	hPal = CreatePalette(lpLogPal);

	/* clean up */
	GlobalUnlock(hLogPal);
	GlobalFree(hLogPal);
	ReleaseDC(nullptr, hDC);

	return hPal;
}

WORD DIBNumColors(const void* lpDIB)
{
	WORD wBitCount = 0; // DIB bit count

	/*  If this is a Windows-style DIB, the number of colors in the
	 *  color table can be less than the number of bits per pixel
	 *  allows for (i.e. lpbi->biClrUsed can be set to some value).
	 *  If this is the case, return the appropriate value.
	 */

	if (IsWin30Dib(lpDIB))
	{
		DWORD dwClrUsed = static_cast<const BITMAPINFOHEADER*>(lpDIB)->biClrUsed;
		if (dwClrUsed)
			return static_cast<WORD>(dwClrUsed);
	}

	/*  Calculate the number of colors in the color table based on
	 *  the number of bits per pixel for the DIB.
	 */
	if (IsWin30Dib(lpDIB))
		wBitCount = static_cast<const BITMAPINFOHEADER*>(lpDIB)->biBitCount;
	else
		wBitCount = static_cast<const BITMAPCOREHEADER*>(lpDIB)->bcBitCount;

	/* return number of colors based on bits per pixel */
	switch (wBitCount)
	{
		case 1:
			return 2;

		case 4:
			return 16;

		case 8:
			return 256;

		default:
			return 0;
	}
}

WORD PaletteSize(const void* lpDIB)
{
	/* calculate the size required by the palette */
	if (IsWin30Dib(lpDIB))
		return static_cast<WORD>(DIBNumColors(lpDIB) * sizeof(RGBQUAD));

	return static_cast<WORD>(DIBNumColors(lpDIB) * sizeof(RGBTRIPLE));
}

HANDLE BitmapToDIB(HBITMAP hBitmap, HPALETTE hPal)
{
	BITMAP bm {};                       // bitmap structure
	BITMAPINFOHEADER bi {};             // bitmap header
	BITMAPINFOHEADER* lpbi = nullptr;   // pointer to BITMAPINFOHEADER
	DWORD dwLen            = 0;         // size of memory block
	HANDLE hDIB = nullptr, h = nullptr; // handle to DIB, temp handle
	HDC hDC     = nullptr;              // handle to DC
	WORD biBits = 0;                    // bits per pixel

	/* check if bitmap handle is valid */
	if (hBitmap == nullptr)
		return nullptr;

	/* fill in BITMAP structure, return nullptr if it didn't work */
	if (!GetObject(hBitmap, sizeof(bm), &bm))
		return nullptr;

	/* if no palette is specified, use default palette */
	if (hPal == nullptr)
		hPal = static_cast<HPALETTE>(GetStockObject(DEFAULT_PALETTE));

	/* calculate bits per pixel */
	biBits = bm.bmPlanes * bm.bmBitsPixel;

	/* make sure bits per pixel is valid */
	if (biBits <= 1)
		biBits = 1;
	else if (biBits <= 4)
		biBits = 4;
	else if (biBits <= 8)
		biBits = 8;
	else /* if greater than 8-bit, force to 24-bit */
		biBits = 24;

	/* initialize BITMAPINFOHEADER */
	bi.biSize          = sizeof(BITMAPINFOHEADER);
	bi.biWidth         = bm.bmWidth;
	bi.biHeight        = bm.bmHeight;
	bi.biPlanes        = 1;
	bi.biBitCount      = biBits;
	bi.biCompression   = BI_RGB;
	bi.biSizeImage     = 0;
	bi.biXPelsPerMeter = 0;
	bi.biYPelsPerMeter = 0;
	bi.biClrUsed       = 0;
	bi.biClrImportant  = 0;

	/* calculate size of memory block required to store BITMAPINFO */
	dwLen              = bi.biSize + PaletteSize(&bi);

	/* get a DC */
	hDC                = GetDC(nullptr);

	/* select and realize our palette */
	hPal               = SelectPalette(hDC, hPal, FALSE);
	RealizePalette(hDC);

	/* alloc memory block to store our bitmap */
	hDIB = GlobalAlloc(GHND, dwLen);

	/* if we couldn't get memory block */
	if (hDIB == nullptr)
	{
		/* clean up and return nullptr */
		SelectPalette(hDC, hPal, TRUE);
		RealizePalette(hDC);
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	/* lock memory and get pointer to it */
	lpbi = static_cast<BITMAPINFOHEADER*>(GlobalLock(hDIB));
	if (lpbi == nullptr)
	{
		GlobalFree(hDIB);

		/* clean up and return nullptr */
		SelectPalette(hDC, hPal, TRUE);
		RealizePalette(hDC);
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	/* use our bitmap info. to fill BITMAPINFOHEADER */
	*lpbi = bi;

	/*  call GetDIBits with a nullptr lpBits param, so it will calculate the
	 *  biSizeImage field for us
	 */
	GetDIBits(hDC, hBitmap, 0, static_cast<WORD>(bi.biHeight), nullptr,
		reinterpret_cast<LPBITMAPINFO>(lpbi), DIB_RGB_COLORS);

	/* get the info. returned by GetDIBits and unlock memory block */
	bi = *lpbi;
	GlobalUnlock(hDIB);

	/* if the driver did not fill in the biSizeImage field, make one up */
	if (bi.biSizeImage == 0)
		bi.biSizeImage = WIDTHBYTES(static_cast<DWORD>(bm.bmWidth) * biBits) * bm.bmHeight;

	/* realloc the buffer big enough to hold all the bits */
	dwLen = bi.biSize + PaletteSize(&bi) + bi.biSizeImage;

	h     = GlobalReAlloc(hDIB, dwLen, 0);
	if (h == nullptr)
	{
		/* clean up and return nullptr */
		GlobalFree(hDIB);
		hDIB = nullptr;
		SelectPalette(hDC, hPal, TRUE);
		RealizePalette(hDC);
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	hDIB = h;

	/* lock memory block and get pointer to it */
	lpbi = static_cast<BITMAPINFOHEADER*>(GlobalLock(hDIB));
	if (lpbi == nullptr)
	{
		/* clean up and return nullptr */
		GlobalFree(hDIB);
		hDIB = nullptr;
		SelectPalette(hDC, hPal, TRUE);
		RealizePalette(hDC);
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}

	/*  call GetDIBits with a NON-nullptr lpBits param, and actualy get the
	 *  bits this time
	 */
	// The bits follow the header and colour table.
	// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	BYTE* lpBits = reinterpret_cast<BYTE*>(lpbi) + static_cast<WORD>(lpbi->biSize)
				   + PaletteSize(lpbi);
	// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	if (GetDIBits(hDC, hBitmap, 0, static_cast<WORD>(bi.biHeight), lpBits,
			reinterpret_cast<LPBITMAPINFO>(lpbi), DIB_RGB_COLORS)
		== 0)
	{
		/* clean up and return nullptr */
		GlobalUnlock(hDIB);
		GlobalFree(hDIB);
		hDIB = nullptr;
		SelectPalette(hDC, hPal, TRUE);
		RealizePalette(hDC);
		ReleaseDC(nullptr, hDC);
		return nullptr;
	}
	bi = *lpbi;

	/* clean up */
	GlobalUnlock(hDIB);
	SelectPalette(hDC, hPal, TRUE);
	RealizePalette(hDC);
	ReleaseDC(nullptr, hDC);

	/* return handle to the DIB */
	return hDIB;
}

HBITMAP CopyScreenToBitmap(LPRECT lpRect)
{
	HDC hScrDC = nullptr, hMemDC = nullptr;          // screen DC and memory DC
	HBITMAP hBitmap = nullptr, hOldBitmap = nullptr; // handles to deice-dependent bitmaps
	int nX = 0, nY = 0, nX2 = 0, nY2 = 0;            // coordinates of rectangle to grab
	int nWidth = 0, nHeight = 0;                     // DIB width and height
	int xScrn = 0, yScrn = 0;                        // screen resolution

	/* check for an empty rectangle */

	if (IsRectEmpty(lpRect))
		return nullptr;

	/*  create a DC for the screen and create
	 *  a memory DC compatible to screen DC
	 */
	hScrDC = CreateDC("DISPLAY", nullptr, nullptr, nullptr);
	hMemDC = CreateCompatibleDC(hScrDC);

	/* get points of rectangle to grab */
	nX     = lpRect->left;
	nY     = lpRect->top;
	nX2    = lpRect->right;
	nY2    = lpRect->bottom;

	/* get screen resolution */
	xScrn  = GetDeviceCaps(hScrDC, HORZRES);
	yScrn  = GetDeviceCaps(hScrDC, VERTRES);

	/* make sure bitmap rectangle is visible */
	if (nX < 0)
		nX = 0;
	if (nY < 0)
		nY = 0;
	if (nX2 > xScrn)
		nX2 = xScrn;
	if (nY2 > yScrn)
		nY2 = yScrn;
	nWidth     = nX2 - nX;
	nHeight    = nY2 - nY;

	/* create a bitmap compatible with the screen DC */
	hBitmap    = CreateCompatibleBitmap(hScrDC, nWidth, nHeight);

	/* select new bitmap into memory DC */
	hOldBitmap = static_cast<HBITMAP>(SelectObject(hMemDC, hBitmap));

	/* bitblt screen DC to memory DC */
	BitBlt(hMemDC, 0, 0, nWidth, nHeight, hScrDC, nX, nY, SRCCOPY);

	/*  select old bitmap back into memory DC and get handle to
	 *  bitmap of the screen
	 */
	hBitmap = static_cast<HBITMAP>(SelectObject(hMemDC, hOldBitmap));

	/* clean up */
	DeleteDC(hScrDC);
	DeleteDC(hMemDC);

	/* return handle to the bitmap */
	return hBitmap;
}

HANDLE CopyScreenToDIB(LPRECT lpRect)
{
	HBITMAP hBitmap   = nullptr; // handle to device-dependent bitmap
	HPALETTE hPalette = nullptr; // handle to palette
	HANDLE hDIB       = nullptr; // handle to DIB

	/*  get the device-dependent bitmap in lpRect by calling
	 *  CopyScreenToBitmap and passing it the rectangle to grab
	 */

	hBitmap           = CopyScreenToBitmap(lpRect);

	/* check for a valid bitmap handle */
	if (!hBitmap)
		return nullptr;

	/* get the current palette */
	hPalette = GetSystemPalette();

	/* convert the bitmap to a DIB */
	hDIB     = BitmapToDIB(hBitmap, hPalette);

	/* clean up */
	DeleteObject(hPalette);
	DeleteObject(hBitmap);

	/* return handle to the packed-DIB */
	return hDIB;
}

} // namespace

HANDLE CaptureWindowToDIB(HWND hWnd)
{
	RECT wndRect {};
	GetWindowRect(hWnd, &wndRect);

	return CopyScreenToDIB(&wndRect);
}

} // namespace Platform
