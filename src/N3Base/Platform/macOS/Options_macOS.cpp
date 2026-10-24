#include "Options_macOS.h"

#include <FileIO/PathResolver.h>
#include <shared/Ini.h>

#include <algorithm>

namespace Platform::macOS
{

namespace
{

// The game's own defaults (WarFareMain): Debug builds prefer a window.
#if defined(_DEBUG)
constexpr bool DefaultWindowMode = true;
#else
constexpr bool DefaultWindowMode = false;
#endif

} // namespace

GameOptions LoadOptions()
{
	CIni ini(PathResolver::Resolve("Option.ini"));
	GameOptions o;
	o.bFullscreen     = !ini.GetBool("Screen", "WindowMode", DefaultWindowMode);
	o.iWidth          = ini.GetInt("ViewPort", "Width", 1024);
	o.iHeight         = ini.GetInt("ViewPort", "Height", 768);
	o.iViewDistance   = std::clamp(ini.GetInt("ViewPort", "Distance", 512), 256, 512);
	o.bShadows        = ini.GetInt("Shadow", "Use", 1) != 0;
	o.iTextureChr     = std::clamp(ini.GetInt("Texture", "LOD_Chr", 0), 0, 1);
	o.iTextureShape   = std::clamp(ini.GetInt("Texture", "LOD_Shape", 0), 0, 1);
	o.iTextureTerrain = std::clamp(ini.GetInt("Texture", "LOD_Terrain", 0), 0, 1);
	o.bSystemCursor   = ini.GetBool("Cursor", "WindowCursor", true);
	o.bMusic          = ini.GetBool("Sound", "Bgm", true);
	o.bSoundEffects   = ini.GetBool("Sound", "Effect", true);
	o.iSoundDistance  = std::clamp(ini.GetInt("Sound", "Distance", 48), 20, 48);
	o.bVSync          = ini.GetBool("Screen", "VSyncEnabled", true);

	o.iWindowScale    = std::clamp(ini.GetInt("macOS", "WindowScale", 100), 50, 400);
	o.bSmoothScaling  = ini.GetBool("macOS", "SmoothScaling", false);
	o.bSwipeTurns     = ini.GetBool("macOS", "SwipeTurnsCamera", true);
	o.iSwipeSpeed     = std::clamp(ini.GetInt("macOS", "SwipeSpeed", 100), 25, 400);
	o.bSwipeReversed  = ini.GetBool("macOS", "SwipeReversed", false);
	o.bControlClick   = ini.GetBool("macOS", "ControlClickIsRightClick", false);
	o.iMaxFps         = std::max(ini.GetInt("macOS", "MaxFps", 0), 0);
	o.iBackgroundFps  = std::max(ini.GetInt("macOS", "BackgroundFps", 10), 0);
	o.bKeepAwake      = ini.GetBool("macOS", "KeepDisplayAwake", true);
	return o;
}

void SaveOptions(const GameOptions& o)
{
	// Start from what's there (the game's file may hold keys this doesn't know), then write it
	// where the game will find it first.
	CIni ini(PathResolver::Resolve("Option.ini"));
	ini.SetInt("Screen", "WindowMode", o.bFullscreen ? 0 : 1);
	ini.SetInt("ViewPort", "Width", o.iWidth);
	ini.SetInt("ViewPort", "Height", o.iHeight);
	ini.SetInt("ViewPort", "Distance", o.iViewDistance);
	ini.SetInt("Shadow", "Use", o.bShadows ? 1 : 0);
	ini.SetInt("Texture", "LOD_Chr", o.iTextureChr);
	ini.SetInt("Texture", "LOD_Shape", o.iTextureShape);
	ini.SetInt("Texture", "LOD_Terrain", o.iTextureTerrain);
	ini.SetInt("Cursor", "WindowCursor", o.bSystemCursor ? 1 : 0);
	ini.SetInt("Sound", "Bgm", o.bMusic ? 1 : 0);
	ini.SetInt("Sound", "Effect", o.bSoundEffects ? 1 : 0);
	ini.SetInt("Sound", "Distance", o.iSoundDistance);
	ini.SetInt("Screen", "VSyncEnabled", o.bVSync ? 1 : 0);

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

const GameOptions& StartupOptions()
{
	static const GameOptions options = LoadOptions();
	return options;
}

} // namespace Platform::macOS
