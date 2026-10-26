#ifndef N3BASE_PLATFORM_MACOS_OPTIONS_MACOS_H
#define N3BASE_PLATFORM_MACOS_OPTIONS_MACOS_H

#pragma once

// The macOS app's own settings, in the [macOS] section of Option.ini (the game ignores it). The
// game's Options window shows them through PlatformOptions.h. No Win32 types here: the Cocoa code
// includes this header too.

namespace Platform::macOS
{

struct MacOptions
{
	int iWindowScale    = 100;   // percent of the game's resolution, in window mode
	bool bSmoothScaling = false; // otherwise nearest-neighbour (sharp pixels)
	bool bSwipeTurns    = true;  // two fingers sideways on a trackpad turn the camera
	int iSwipeSpeed     = 100;   // percent
	bool bSwipeReversed = false;
	bool bControlClick  = false; // Control-click is a right-click
	int iMaxFps         = 0;     // 0: no limit beyond VSync
	int iBackgroundFps  = 10;    // while another app is active; 0: no limit
	bool bKeepAwake     = true;  // the display doesn't sleep while the game is active
};

/// \brief Reads them from Option.ini in the working directory (the game's folder, or in the app
/// the user's folder over the game data).
MacOptions LoadMacOptions();

/// \brief Writes them back, keeping everything else in the file.
void SaveMacOptions(const MacOptions& options);

/// \brief The settings in effect: read on first use, changed by Platform::SetPlatformOption.
MacOptions& CurrentMacOptions();

} // namespace Platform::macOS

#endif // N3BASE_PLATFORM_MACOS_OPTIONS_MACOS_H
