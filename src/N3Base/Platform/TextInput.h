#ifndef N3BASE_PLATFORM_TEXTINPUT_H
#define N3BASE_PLATFORM_TEXTINPUT_H

#pragma once

#include "Types.h"

#include <functional>
#include <string>

// A hidden native text field that does the actual typing for the focused UI edit box
// (CN3UIEdit): it owns the text, the caret and IME composition, and the UI mirrors it. On Windows
// this is the EDIT control the client has always used.
namespace Platform::TextInput
{

struct Handlers
{
	/// \brief Return was pressed.
	std::function<void()> OnReturn;

	/// \brief A key was handled, so the caret may have moved.
	std::function<void()> OnCaretMayHaveMoved;
};

/// \brief Creates the field as a child of the main window. Returns false if it already exists.
bool Create(HWND hWndParent, const RECT& rc, const Handlers& handlers);

/// \brief Whether Create() succeeded.
bool IsCreated();

/// \brief Gives the field keyboard focus, positions it over the edit box and sets its text.
void Focus(const RECT& rcRegion, const std::string& szText);

/// \brief Clears the field and returns keyboard focus to the main window.
void Unfocus();

/// \brief The field's text (up to 511 bytes, in the client's ANSI code page).
std::string GetText();

/// \brief The caret position, in bytes from the start of the text.
int GetCaretPos();

/// \brief Shows (at ptPos) or hides the IME status window, where the platform has one.
void SetImeStatus(POINT ptPos, bool bOpen);

} // namespace Platform::TextInput

#endif // N3BASE_PLATFORM_TEXTINPUT_H
