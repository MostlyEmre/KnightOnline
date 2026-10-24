#ifndef N3BASE_PLATFORM_MACOS_OPTIONS_MACOS_H
#define N3BASE_PLATFORM_MACOS_OPTIONS_MACOS_H

#pragma once

// The settings in Option.ini: the game's own, which it reads at startup (WarFareMain), and the
// macOS app's in a [macOS] section, which the platform layer reads. On Windows the separate
// Option.exe edits the game's; on macOS the Settings window does (Cocoa/OptionsWindow.h).
// No Win32 types here: the Cocoa code includes this header too.

namespace Platform::macOS
{

struct GameOptions
{
	// The game's (Option.ini as Option.exe writes it).
	bool bFullscreen    = true; // [Screen] WindowMode is the opposite
	int iWidth          = 1024; // [ViewPort] Width, Height
	int iHeight         = 768;
	int iViewDistance   = 512;  // [ViewPort] Distance, 256 to 512
	bool bShadows       = true; // [Shadow] Use
	int iTextureChr     = 0;    // [Texture] LOD_Chr, LOD_Shape, LOD_Terrain: 0 full, 1 half size
	int iTextureShape   = 0;
	int iTextureTerrain = 0;
	bool bSystemCursor  = true; // [Cursor] WindowCursor (false: the game draws its own)
	bool bMusic         = true; // [Sound] Bgm, Effect, Distance (20 to 48)
	bool bSoundEffects  = true;
	int iSoundDistance  = 48;
	bool bVSync         = true; // [Screen] VSyncEnabled

	// The macOS app's ([macOS]).
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

/// \brief Reads Option.ini from the working directory (the game's folder, or in the app the
/// user's folder over the game data), with the game's defaults for what's missing.
GameOptions LoadOptions();

/// \brief Writes them back, keeping anything else in the file.
void SaveOptions(const GameOptions& options);

/// \brief The options as they were when the game started (what's in effect).
const GameOptions& StartupOptions();

} // namespace Platform::macOS

#endif // N3BASE_PLATFORM_MACOS_OPTIONS_MACOS_H
