#ifndef N3BASE_PLATFORM_MACOS_COCOA_OPTIONSWINDOW_H
#define N3BASE_PLATFORM_MACOS_COCOA_OPTIONSWINDOW_H

#pragma once

// The Settings window: the macOS counterpart of the Windows client's Option.exe, plus the macOS
// app's own settings. Neutral C++ interface over AppKit (see CocoaApp.h).

#include "../Options_macOS.h"

#include <functional>
#include <string>

namespace Platform::macOS::Cocoa
{

struct OptionsWindowHooks
{
	/// \brief Called after every change, with all the settings.
	std::function<void(const GameOptions& options)> OnChange;

	/// \brief "Restart Game" (while playing).
	std::function<void()> OnRestart;
};

/// \brief Shows the Settings window, or brings it to the front. iFitWidth/iFitHeight is the "fit
/// the screen" resolution.
///
/// With bAfterQuit (the game closed itself to show its options, as it does for Option.exe on
/// Windows) the window runs modally and offers Start Game and Quit; the return value says whether
/// the player chose Start Game. Otherwise it returns at once and the game keeps running.
bool ShowOptionsWindow(const GameOptions& options, int iFitWidth, int iFitHeight, bool bAfterQuit,
	const OptionsWindowHooks& hooks);

/// \brief Testing aid: renders each tab of the window to <prefix>-<n>.png.
bool SnapshotOptionsWindow(
	const GameOptions& options, int iFitWidth, int iFitHeight, const std::string& szPrefix);

} // namespace Platform::macOS::Cocoa

#endif // N3BASE_PLATFORM_MACOS_COCOA_OPTIONSWINDOW_H
