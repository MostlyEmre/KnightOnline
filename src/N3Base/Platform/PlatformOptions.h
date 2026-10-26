#ifndef N3BASE_PLATFORM_PLATFORMOPTIONS_H
#define N3BASE_PLATFORM_PLATFORMOPTIONS_H

#pragma once

// What the game's Options window needs from the platform: whether options are a separate program
// (Option.exe on Windows), the size "fit the screen" means, the platform's own settings (shown
// as extra rows next to the game's), and a way to restart the game for the ones that need it.

#include <string>
#include <vector>

namespace Platform
{

/// \brief One of the platform's own settings. Shown in the game's Options window on its page; a
/// toggle when bToggle, otherwise one of the choices.
struct PlatformOption
{
	std::string szKey;  // stable identifier
	std::string szPage; // the Options page: "Display", "Controls" or "Performance"
	std::string szLabel;
	std::vector<std::string> choiceNames;
	std::vector<int> choiceValues;
	bool bToggle  = false; // 0 off, 1 on
	bool bRestart = false; // takes effect the next time the game starts
	int iValue    = 0;
};

/// \brief Whether the game's options are a separate program (Option.exe, run by the game's Option
/// buttons). If not, the game shows its own Options window.
bool HasOptionsProgram();

/// \brief The resolution, in game pixels, that fills the screen in fullscreen.
void GetFullscreenSize(int& iWidth, int& iHeight);

/// \brief The platform's own settings, with their current values (none on Windows).
std::vector<PlatformOption> GetPlatformOptions();

/// \brief Changes one of them: saves it, and applies it now unless it needs a restart.
void SetPlatformOption(const std::string& szKey, int iValue);

/// \brief Quits and starts the game again.
void RequestRestart();

} // namespace Platform

#endif // N3BASE_PLATFORM_PLATFORMOPTIONS_H
