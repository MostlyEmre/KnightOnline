#ifndef N3BASE_PLATFORM_SCREENCAPTURE_H
#define N3BASE_PLATFORM_SCREENCAPTURE_H

#pragma once

#include "Types.h"

namespace Platform
{

/// \brief Captures what the window shows into a packed DIB (BITMAPINFOHEADER, colour table, then
/// the pixels) in memory from GlobalAlloc; the caller frees it with GlobalFree. Returns nullptr on
/// failure.
HANDLE CaptureWindowToDIB(HWND hWnd);

} // namespace Platform

#endif // N3BASE_PLATFORM_SCREENCAPTURE_H
