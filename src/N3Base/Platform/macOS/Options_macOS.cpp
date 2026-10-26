#include "Options_macOS.h"

#include <FileIO/PathResolver.h>
#include <shared/Ini.h>

#include <algorithm>

namespace Platform::macOS
{

MacOptions LoadMacOptions()
{
	CIni ini(PathResolver::Resolve("Option.ini"));
	MacOptions o;
	o.iWindowScale   = std::clamp(ini.GetInt("macOS", "WindowScale", 100), 50, 400);
	o.bSmoothScaling = ini.GetBool("macOS", "SmoothScaling", false);
	o.bSwipeTurns    = ini.GetBool("macOS", "SwipeTurnsCamera", true);
	o.iSwipeSpeed    = std::clamp(ini.GetInt("macOS", "SwipeSpeed", 100), 25, 400);
	o.bSwipeReversed = ini.GetBool("macOS", "SwipeReversed", false);
	o.bControlClick  = ini.GetBool("macOS", "ControlClickIsRightClick", false);
	o.iMaxFps        = std::max(ini.GetInt("macOS", "MaxFps", 0), 0);
	o.iBackgroundFps = std::max(ini.GetInt("macOS", "BackgroundFps", 10), 0);
	o.bKeepAwake     = ini.GetBool("macOS", "KeepDisplayAwake", true);
	return o;
}

void SaveMacOptions(const MacOptions& o)
{
	// Start from what's there (the game's own settings live in the same file).
	CIni ini(PathResolver::Resolve("Option.ini"));
	ini.SetInt("macOS", "WindowScale", o.iWindowScale);
	ini.SetInt("macOS", "SmoothScaling", o.bSmoothScaling ? 1 : 0);
	ini.SetInt("macOS", "SwipeTurnsCamera", o.bSwipeTurns ? 1 : 0);
	ini.SetInt("macOS", "SwipeSpeed", o.iSwipeSpeed);
	ini.SetInt("macOS", "SwipeReversed", o.bSwipeReversed ? 1 : 0);
	ini.SetInt("macOS", "ControlClickIsRightClick", o.bControlClick ? 1 : 0);
	ini.SetInt("macOS", "MaxFps", o.iMaxFps);
	ini.SetInt("macOS", "BackgroundFps", o.iBackgroundFps);
	ini.SetInt("macOS", "KeepDisplayAwake", o.bKeepAwake ? 1 : 0);
	ini.Save(PathResolver::ResolveForWriting("Option.ini"));
}

MacOptions& CurrentMacOptions()
{
	static MacOptions options = LoadMacOptions();
	return options;
}

} // namespace Platform::macOS
