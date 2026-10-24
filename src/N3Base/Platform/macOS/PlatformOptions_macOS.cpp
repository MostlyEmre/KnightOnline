#include "../PlatformOptions.h"
#include "Cocoa/CocoaApp.h"
#include "MacState.h"
#include "Options_macOS.h"

namespace Platform
{

namespace macOS
{

void ApplyInputOptions()
{
	const MacOptions& options = CurrentMacOptions();
	Cocoa::InputOptions input;
	input.bSwipeTurns   = options.bSwipeTurns;
	input.dSwipeScale   = options.iSwipeSpeed / 100.0 * (options.bSwipeReversed ? -1.0 : 1.0);
	input.bControlClick = options.bControlClick;
	Cocoa::SetInputOptions(input);
}

} // namespace macOS

namespace
{

PlatformOption Toggle(const char* szKey, const char* szPage, const char* szLabel, bool bValue)
{
	PlatformOption option;
	option.szKey        = szKey;
	option.szPage       = szPage;
	option.szLabel      = szLabel;
	option.bToggle      = true;
	option.choiceNames  = { "Off", "On" };
	option.choiceValues = { 0, 1 };
	option.iValue       = bValue ? 1 : 0;
	return option;
}

PlatformOption Choice(const char* szKey, const char* szPage, const char* szLabel,
	std::vector<std::string> names, std::vector<int> values, int iValue, bool bRestart = false)
{
	PlatformOption option;
	option.szKey        = szKey;
	option.szPage       = szPage;
	option.szLabel      = szLabel;
	option.choiceNames  = std::move(names);
	option.choiceValues = std::move(values);
	option.iValue       = iValue;
	option.bRestart     = bRestart;
	return option;
}

} // namespace

bool HasOptionsProgram()
{
	return false; // the game shows its own Options window
}

void GetFullscreenSize(int& iWidth, int& iHeight)
{
	macOS::Cocoa::FullscreenSize(iWidth, iHeight);
}

std::vector<PlatformOption> GetPlatformOptions()
{
	const macOS::MacOptions& o = macOS::CurrentMacOptions();
	return {
		Choice("WindowScale", "Display", "Window size", { "100%", "150%", "200%" },
			{ 100, 150, 200 }, o.iWindowScale, true),
		Choice("SmoothScaling", "Display", "Scaling", { "Sharp pixels", "Smooth" }, { 0, 1 },
			o.bSmoothScaling ? 1 : 0, true),
		Toggle("SwipeTurnsCamera", "Controls", "Two-finger swipe turns", o.bSwipeTurns),
		Choice("SwipeSpeed", "Controls", "Swipe turn speed",
			{ "50%", "75%", "100%", "150%", "200%", "300%" }, { 50, 75, 100, 150, 200, 300 },
			o.iSwipeSpeed),
		Toggle("SwipeReversed", "Controls", "Reverse swipe direction", o.bSwipeReversed),
		Toggle(
			"ControlClickIsRightClick", "Controls", "Control-click right-clicks", o.bControlClick),
		Choice("MaxFps", "Performance", "Frame rate limit",
			{ "None", "120 fps", "60 fps", "30 fps" }, { 0, 120, 60, 30 }, o.iMaxFps),
		Choice("BackgroundFps", "Performance", "Behind other apps",
			{ "Full speed", "30 fps", "10 fps" }, { 0, 30, 10 }, o.iBackgroundFps),
		Toggle("KeepDisplayAwake", "Performance", "Keep display awake", o.bKeepAwake),
	};
}

void SetPlatformOption(const std::string& szKey, int iValue)
{
	macOS::MacOptions& o = macOS::CurrentMacOptions();
	if (szKey == "WindowScale")
		o.iWindowScale = iValue;
	else if (szKey == "SmoothScaling")
		o.bSmoothScaling = iValue != 0;
	else if (szKey == "SwipeTurnsCamera")
		o.bSwipeTurns = iValue != 0;
	else if (szKey == "SwipeSpeed")
		o.iSwipeSpeed = iValue;
	else if (szKey == "SwipeReversed")
		o.bSwipeReversed = iValue != 0;
	else if (szKey == "ControlClickIsRightClick")
		o.bControlClick = iValue != 0;
	else if (szKey == "MaxFps")
		o.iMaxFps = iValue;
	else if (szKey == "BackgroundFps")
		o.iBackgroundFps = iValue;
	else if (szKey == "KeepDisplayAwake")
		o.bKeepAwake = iValue != 0;
	else
		return;

	macOS::SaveMacOptions(o);
	macOS::ApplyInputOptions(); // the frame rate limits and keep-awake are read every frame
}

} // namespace Platform
