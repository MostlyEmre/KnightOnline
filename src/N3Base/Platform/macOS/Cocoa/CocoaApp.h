#ifndef N3BASE_PLATFORM_MACOS_COCOA_COCOAAPP_H
#define N3BASE_PLATFORM_MACOS_COCOA_COCOAAPP_H

#pragma once

// The AppKit side of the macOS platform layer: the application, the game window and its input.
//
// Only neutral types cross this boundary. The Win32 compat types the rest of the client uses clash
// with Objective-C's and CoreFoundation's (BOOL, S_OK, HRESULT, ...), so the Objective-C++ files
// that implement this never see them, and the Platform adapters (../*_macOS.cpp) never see Cocoa.

#include <cstdint>
#include <functional>
#include <string>

namespace Platform::macOS::Cocoa
{

struct WindowEvents
{
	std::function<void(bool bActive)> OnActivate;
	std::function<void()> OnCloseRequested;
	std::function<void(int iDelta)> OnMouseWheel; // 120 per notch, like WHEEL_DELTA
};

/// \brief What the keyboard does while the text input field has focus (NSTextInputClient).
/// Text is UTF-8; positions count UTF-8 bytes of the marked text.
struct TextInputEvents
{
	std::function<void(const std::string& szText)> OnInsertText;
	std::function<void(const std::string& szMarkedText)> OnSetMarkedText;
	std::function<void()> OnUnmarkText;
	/// \brief An editing command, by its NSResponder selector name ("deleteBackward:", ...).
	std::function<void(const std::string& szCommand)> OnCommand;
};

/// \brief Creates the application (first call) and the game window, whose content is a
/// CAMetalLayer of the game's resolution. Fullscreen covers the main display, scaling the
/// game's image to fit. Without bTakeFocus (scripted test runs) the window is shown without
/// activating the application, so it doesn't take the keyboard from whatever the user is doing.
/// Returns false on failure.
bool CreateGameWindow(const std::string& szTitle, int iWidth, int iHeight, bool bFullscreen,
	bool bTakeFocus, const WindowEvents& events);

/// \brief Dispatches the pending window-system events without waiting.
void PumpEvents();

/// \brief The game window's CAMetalLayer, as an opaque pointer (nullptr before CreateGameWindow).
void* GameLayer();

/// \brief The game's resolution (the client area, in game pixels).
int ClientWidth();
int ClientHeight();

/// \brief "Screen" coordinates are global display points with the origin at the top left of the
/// main display; client coordinates are game pixels.
void CursorScreenPosition(int& x, int& y);
void ScreenToClient(int& x, int& y);
void ClientToScreen(int& x, int& y);
void WarpCursor(int iScreenX, int iScreenY);

/// \brief The game window's frame in screen coordinates.
void WindowScreenRect(int& left, int& top, int& right, int& bottom);

/// \brief Whether the game window has keyboard focus.
bool IsActive();

/// \brief Brings the game window to the front with keyboard focus.
void ActivateGameWindow();

/// \brief Mouse buttons held anywhere: bit 0 left, bit 1 right, bit 2 middle.
uint32_t PressedMouseButtons();

/// \brief Option/Alt and Control state.
bool IsOptionDown();
bool IsControlDown();

/// \brief Whether a key is down, by DirectInput scan code (DIK_*), while the window has focus.
bool IsKeyDown(int iScanCode);

/// \brief The 256-entry key state by DirectInput scan code, 0x80 for keys down.
void KeyboardState(uint8_t (&keys)[256]);

/// \brief The double-click interval, in milliseconds.
uint32_t DoubleClickIntervalMs();

/// \brief Loads a Windows .cur file as a cursor (opaque, kept for the process). nullptr on failure.
void* LoadCursorFile(const std::string& szPath);

/// \brief Shows the cursor over the game window (nullptr hides it).
void SetCursor(void* pCursor);

/// \brief Hides or shows the cursor (balanced like ShowCursor's counter by the caller).
void SetCursorVisible(bool bVisible);

/// \brief Starts (with the edit box's client rectangle, for placing IME candidates) or stops
/// sending keyboard text to the handlers.
void SetTextInputFocus(bool bFocused, int iLeft, int iTop, int iRight, int iBottom);
void SetTextInputEvents(const TextInputEvents& events);

/// \brief A modal alert with an OK button.
void ShowAlert(const std::string& szCaption, const std::string& szText);

/// \brief Opens a URL or a file with its default application.
void OpenURL(const std::string& szURL);

/// \brief The user's preferred language as a BCP 47 tag ("en-US", "zh-Hant-TW", ...).
std::string PreferredLanguage();

/// \brief The directory of the application's bundled resources (the executable's directory when
/// not running from a bundle).
std::string ResourceDirectory();

} // namespace Platform::macOS::Cocoa

#endif // N3BASE_PLATFORM_MACOS_COCOA_COCOAAPP_H
