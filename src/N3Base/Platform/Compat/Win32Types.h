#ifndef N3BASE_PLATFORM_COMPAT_WIN32TYPES_H
#define N3BASE_PLATFORM_COMPAT_WIN32TYPES_H

#pragma once

// Portable definitions of the Win32 types, constants and pure helper functions the client
// uses, for platforms without <windows.h>. Only what the client needs (see port/census).
// Widths follow Windows (LLP64): LONG and DWORD are 32-bit, unlike `long` on LP64.

#if defined(_WIN32)
#error "Win32Types.h is for non-Windows platforms; include <windows.h> on Windows"
#endif

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

// These definitions mirror the SDK headers: the names, values and widths have to match theirs.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,bugprone-macro-parentheses,cppcoreguidelines-pro-bounds-pointer-arithmetic,clang-analyzer-security.insecureAPI.strcpy)

// --- basic types --------------------------------------------------------------------

using BOOL      = int;
using BOOLEAN   = unsigned char;
using BYTE      = uint8_t;
using byte      = unsigned char;
using CHAR      = char;
using WCHAR     = wchar_t;
using SHORT     = int16_t;
using USHORT    = uint16_t;
using WORD      = uint16_t;
using INT       = int;
using UINT      = unsigned int;
using LONG      = int32_t;
using ULONG     = uint32_t;
using DWORD     = uint32_t;
using LONGLONG  = int64_t;
using ULONGLONG = uint64_t;
using FLOAT     = float;
using HRESULT   = int32_t;
using COLORREF  = DWORD;
using LPSTR     = char*;
using LPCSTR    = const char*;
using LPVOID    = void*;
using LPCVOID   = const void*;
using PVOID     = void*;
using LPBYTE    = BYTE*;
using LPWORD    = WORD*;
using LPDWORD   = DWORD*;
using UINT_PTR  = uintptr_t;
using INT_PTR   = intptr_t;
using LONG_PTR  = intptr_t;
using WPARAM    = uintptr_t;
using LPARAM    = intptr_t;
using LRESULT   = intptr_t;
using LANGID    = uint16_t;
using SIZE_T    = size_t;

static_assert(sizeof(LONG) == 4 && sizeof(DWORD) == 4, "Win32 LONG/DWORD are 32-bit");

// Opaque handles. On macOS the platform layer hands out pointers to its own objects.
#define N3_DECLARE_HANDLE(name) \
	struct name##__;            \
	using name = name##__*
N3_DECLARE_HANDLE(HWND);
N3_DECLARE_HANDLE(HINSTANCE);
N3_DECLARE_HANDLE(HDC);
N3_DECLARE_HANDLE(HCURSOR);
N3_DECLARE_HANDLE(HICON);
N3_DECLARE_HANDLE(HFONT);
N3_DECLARE_HANDLE(HBITMAP);
N3_DECLARE_HANDLE(HBRUSH);
N3_DECLARE_HANDLE(HPALETTE);
N3_DECLARE_HANDLE(HKEY);
N3_DECLARE_HANDLE(HACCEL);
N3_DECLARE_HANDLE(HIMC);
N3_DECLARE_HANDLE(HKL);
#undef N3_DECLARE_HANDLE
using HANDLE  = void*;
using HMODULE = HINSTANCE;
using HGDIOBJ = void*;
using HGLOBAL = void*;

// --- structures -----------------------------------------------------------------------

struct RECT
{
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
};
using LPRECT  = RECT*;
using LPCRECT = const RECT*;

struct POINT
{
	LONG x;
	LONG y;
};
using LPPOINT = POINT*;

struct SIZE
{
	LONG cx;
	LONG cy;
};

union LARGE_INTEGER
{
	struct
	{
		DWORD LowPart;
		LONG HighPart;
	};
	LONGLONG QuadPart;
};

struct SYSTEMTIME
{
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
};

#pragma pack(push, 2)
struct BITMAPFILEHEADER
{
	WORD bfType;
	DWORD bfSize;
	WORD bfReserved1;
	WORD bfReserved2;
	DWORD bfOffBits;
};
#pragma pack(pop)

struct BITMAPINFOHEADER
{
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
};
using LPBITMAPINFOHEADER = BITMAPINFOHEADER*;

struct RGBQUAD
{
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
};

struct BITMAPINFO
{
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD bmiColors[1];
};
using LPBITMAPINFO = BITMAPINFO*;
using LPRGBQUAD    = RGBQUAD*;

// On-disk layouts: these are read and written raw.
static_assert(sizeof(BITMAPFILEHEADER) == 14, "BITMAPFILEHEADER layout");
static_assert(sizeof(BITMAPINFOHEADER) == 40, "BITMAPINFOHEADER layout");
static_assert(sizeof(RGBQUAD) == 4, "RGBQUAD layout");
static_assert(sizeof(RECT) == 16, "RECT layout");

#define BI_RGB         0L
#define DIB_RGB_COLORS 0

// --- constants ------------------------------------------------------------------------

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_PATH      260
#define _MAX_PATH     260
#define _MAX_DRIVE    3
#define _MAX_DIR      256
#define _MAX_FNAME    256
#define _MAX_EXT      256

#define S_OK          (static_cast<HRESULT>(0))
#define S_FALSE       (static_cast<HRESULT>(1))
#define E_FAIL        (static_cast<HRESULT>(0x80004005))
#define E_OUTOFMEMORY (static_cast<HRESULT>(0x8007000E))
#define SUCCEEDED(hr) ((static_cast<HRESULT>(hr)) >= 0)
#define FAILED(hr)    ((static_cast<HRESULT>(hr)) < 0)

#define WINAPI
#define APIENTRY
#define CALLBACK

#define LOWORD(l) (static_cast<WORD>(static_cast<uintptr_t>(l) & 0xFFFF))
#define HIWORD(l) (static_cast<WORD>((static_cast<uintptr_t>(l) >> 16) & 0xFFFF))
#define LOBYTE(w) (static_cast<BYTE>(static_cast<uintptr_t>(w) & 0xFF))
#define HIBYTE(w) (static_cast<BYTE>((static_cast<uintptr_t>(w) >> 8) & 0xFF))
#define MAKEWORD(a, b)                                                                             \
	(static_cast<WORD>((static_cast<BYTE>(a)) | ((static_cast<WORD>(static_cast<BYTE>(b))) << 8)))
#define MAKELONG(a, b) \
	(static_cast<LONG>((static_cast<WORD>(a)) | ((static_cast<DWORD>(static_cast<WORD>(b))) << 16)))
#define RGB(r, g, b)                                                              \
	(static_cast<COLORREF>((static_cast<BYTE>(r))                                 \
						   | ((static_cast<WORD>(static_cast<BYTE>(g))) << 8)     \
						   | ((static_cast<DWORD>(static_cast<BYTE>(b))) << 16)))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE((static_cast<WORD>(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                        \
	(static_cast<DWORD>(static_cast<BYTE>(ch0))               \
		| (static_cast<DWORD>(static_cast<BYTE>(ch1)) << 8)   \
		| (static_cast<DWORD>(static_cast<BYTE>(ch2)) << 16)  \
		| (static_cast<DWORD>(static_cast<BYTE>(ch3)) << 24))

// MessageBox styles; the client's own message boxes take these too.
#define MB_OK                                   0x00000000L
#define MB_OKCANCEL                             0x00000001L
#define MB_YESNO                                0x00000004L
#define MB_ICONSTOP                             0x00000010L

#define WHEEL_DELTA                             120

// Virtual keys the client checks.
#define VK_LBUTTON                              0x01
#define VK_RBUTTON                              0x02
#define VK_MBUTTON                              0x04
#define VK_TAB                                  0x09
#define VK_RETURN                               0x0D
#define VK_SHIFT                                0x10
#define VK_CONTROL                              0x11
#define VK_MENU                                 0x12

// --- pure helpers (user32 / kernel32 / CRT names, no OS involvement) -------------------

#define ZeroMemory(Destination, Length)         std::memset((Destination), 0, (Length))
#define CopyMemory(Destination, Source, Length) std::memcpy((Destination), (Source), (Length))
#define FillMemory(Destination, Length, Fill)   std::memset((Destination), (Fill), (Length))

// Global memory as the client uses it: plain heap blocks (GMEM_FIXED semantics), where the
// "handle" is the pointer itself, so GlobalLock() is the identity.
#define GMEM_FIXED                              0x0000
#define GMEM_MOVEABLE                           0x0002
#define GMEM_ZEROINIT                           0x0040
#define GHND                                    (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR                                    (GMEM_FIXED | GMEM_ZEROINIT)

inline HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes)
{
	// NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
	return (uFlags & GMEM_ZEROINIT) != 0 ? std::calloc(1, dwBytes) : std::malloc(dwBytes);
}

inline HGLOBAL GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT /*uFlags*/)
{
	// NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
	return std::realloc(hMem, dwBytes);
}

inline HGLOBAL GlobalFree(HGLOBAL hMem)
{
	// NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
	std::free(hMem);
	return nullptr;
}

inline LPVOID GlobalLock(HGLOBAL hMem)
{
	return hMem;
}

inline BOOL GlobalUnlock(HGLOBAL /*hMem*/)
{
	return FALSE;
}

inline BOOL SetRect(LPRECT rc, int left, int top, int right, int bottom)
{
	if (rc == nullptr)
		return FALSE;
	rc->left   = left;
	rc->top    = top;
	rc->right  = right;
	rc->bottom = bottom;
	return TRUE;
}

inline BOOL SetRectEmpty(LPRECT rc)
{
	return SetRect(rc, 0, 0, 0, 0);
}

inline BOOL IsRectEmpty(const RECT* rc)
{
	return rc == nullptr || rc->right <= rc->left || rc->bottom <= rc->top;
}

inline BOOL PtInRect(const RECT* rc, POINT pt)
{
	return rc != nullptr && pt.x >= rc->left && pt.x < rc->right && pt.y >= rc->top
		   && pt.y < rc->bottom;
}

inline BOOL EqualRect(const RECT* a, const RECT* b)
{
	return a != nullptr && b != nullptr && a->left == b->left && a->top == b->top
		   && a->right == b->right && a->bottom == b->bottom;
}

inline int MulDiv(int number, int numerator, int denominator)
{
	if (denominator == 0)
		return -1;
	int64_t product = static_cast<int64_t>(number) * numerator;
	// Rounds half away from zero, like Windows.
	int64_t half    = (denominator < 0 ? -denominator : denominator) / 2;
	int64_t result  = ((product < 0) != (denominator < 0)) ? (product - half) / denominator
														   : (product + half) / denominator;
	return static_cast<int>(result);
}

inline int lstrlenA(const char* s)
{
	return s != nullptr ? static_cast<int>(std::strlen(s)) : 0;
}

inline char* lstrcpyA(char* dst, const char* src)
{
	return std::strcpy(dst, src);
}

inline char* lstrcpynA(char* dst, const char* src, int maxCount)
{
	if (maxCount <= 0)
		return dst;
	std::strncpy(dst, src, static_cast<size_t>(maxCount - 1));
	dst[maxCount - 1] = '\0';
	return dst;
}

inline char* lstrcatA(char* dst, const char* src)
{
	return std::strcat(dst, src);
}

inline int lstrcmpA(const char* a, const char* b)
{
	return std::strcmp(a, b);
}

inline int lstrcmpiA(const char* a, const char* b)
{
	return strcasecmp(a, b);
}

inline char* CharLowerA(char* s)
{
	for (char* p = s; p != nullptr && *p != '\0'; ++p)
		*p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
	return s;
}

#define lstrlen   lstrlenA
#define lstrcpy   lstrcpyA
#define lstrcpyn  lstrcpynA
#define lstrcat   lstrcatA
#define lstrcmp   lstrcmpA
#define lstrcmpi  lstrcmpiA
#define CharLower CharLowerA

inline int _stricmp(const char* a, const char* b)
{
	return strcasecmp(a, b);
}

inline int _strnicmp(const char* a, const char* b, size_t n)
{
	return strncasecmp(a, b, n);
}

inline char* _strlwr(char* s)
{
	return CharLowerA(s);
}

/// \brief CRT _splitpath: splits "C:\dir\name.ext" (either slash) into its parts.
/// Any output may be null; buffers are assumed to be the _MAX_* sizes.
void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext);

/// \brief CRT _makepath: the inverse of _splitpath.
void _makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext);

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,bugprone-macro-parentheses,cppcoreguidelines-pro-bounds-pointer-arithmetic,clang-analyzer-security.insecureAPI.strcpy)

#endif // N3BASE_PLATFORM_COMPAT_WIN32TYPES_H
