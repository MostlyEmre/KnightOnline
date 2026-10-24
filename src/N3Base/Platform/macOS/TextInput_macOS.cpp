#include "../TextInput.h"
#include "Cocoa/CocoaApp.h"
#include "CodePage.h"
#include "InputScript.h"
#include "MacState.h"

namespace Platform::TextInput
{

// The field behaves like the single-line EDIT control it stands in for: setting the text puts
// the caret at the start, Return and Tab don't go into the text, and an IME's composition shows
// inline at the caret until it's committed.

namespace
{

Handlers s_handlers;
bool s_bCreated = false;
bool s_bFocused = false;
std::string s_szText;   // committed text, ANSI
std::string s_szMarked; // composition in progress, ANSI
size_t s_nCaret = 0;

void NotifyTextChanged()
{
	const EventHandlers& handlers = macOS::Handlers();
	if (handlers.OnTextInputChanged)
		handlers.OnTextInputChanged();
}

void NotifyCaretMoved()
{
	if (s_handlers.OnCaretMayHaveMoved)
		s_handlers.OnCaretMayHaveMoved();
}

void InsertText(const std::string& szUtf8)
{
	std::string szAnsi = macOS::Utf8ToAnsi(szUtf8);
	std::erase_if(szAnsi, [](char ch) { return static_cast<unsigned char>(ch) < 0x20; });

	s_szMarked.clear();
	s_szText.insert(s_nCaret, szAnsi);
	s_nCaret += szAnsi.size();
	NotifyTextChanged();
	NotifyCaretMoved();
}

void Command(const std::string& szCommand)
{
	if (szCommand == "insertNewline:" || szCommand == "insertLineBreak:")
	{
		if (s_handlers.OnReturn)
			s_handlers.OnReturn();
		return;
	}

	bool bChanged = false;
	if (szCommand == "deleteBackward:")
	{
		if (s_nCaret > 0)
		{
			s_szText.erase(--s_nCaret, 1);
			bChanged = true;
		}
	}
	else if (szCommand == "deleteForward:")
	{
		if (s_nCaret < s_szText.size())
		{
			s_szText.erase(s_nCaret, 1);
			bChanged = true;
		}
	}
	else if (szCommand == "moveLeft:" && s_nCaret > 0)
	{
		--s_nCaret;
	}
	else if (szCommand == "moveRight:" && s_nCaret < s_szText.size())
	{
		++s_nCaret;
	}
	else if (szCommand == "moveToBeginningOfLine:" || szCommand == "moveToBeginningOfDocument:"
			 || szCommand == "scrollToBeginningOfDocument:" || szCommand == "moveUp:")
	{
		s_nCaret = 0;
	}
	else if (szCommand == "moveToEndOfLine:" || szCommand == "moveToEndOfDocument:"
			 || szCommand == "scrollToEndOfDocument:" || szCommand == "moveDown:")
	{
		s_nCaret = s_szText.size();
	}

	if (bChanged)
		NotifyTextChanged();
	NotifyCaretMoved();
}

} // namespace

bool Create(HWND /*hWndParent*/, const RECT& /*rc*/, const Handlers& handlers)
{
	if (s_bCreated)
		return false;

	s_handlers = handlers;
	s_bCreated = true;

	macOS::Cocoa::TextInputEvents events;
	events.OnInsertText    = InsertText;
	events.OnSetMarkedText = [](const std::string& szUtf8)
	{
		s_szMarked = macOS::Utf8ToAnsi(szUtf8);
		NotifyTextChanged();
	};
	events.OnUnmarkText = []
	{
		if (s_szMarked.empty())
			return;

		std::string szMarked = macOS::AnsiToUtf8(s_szMarked);
		InsertText(szMarked);
	};
	events.OnCommand = Command;

	// While a test script plays, typing comes from the script only.
	if (!macOS::InputScript::IsActive())
		macOS::Cocoa::SetTextInputEvents(events);
	return true;
}

bool IsCreated()
{
	return s_bCreated;
}

void Focus(const RECT& rcRegion, const std::string& szText)
{
	if (!s_bCreated)
		return;

	s_szText = szText;
	s_szMarked.clear();
	s_nCaret   = 0;
	s_bFocused = true;
	macOS::Cocoa::SetTextInputFocus(
		true, rcRegion.left, rcRegion.top, rcRegion.right, rcRegion.bottom);
}

void Unfocus()
{
	if (!s_bCreated)
		return;

	s_szText.clear();
	s_szMarked.clear();
	s_nCaret   = 0;
	s_bFocused = false;
	macOS::Cocoa::SetTextInputFocus(false, 0, 0, 0, 0);
}

std::string GetText()
{
	std::string szText = s_szText;
	szText.insert(s_nCaret, s_szMarked);
	if (szText.size() > 511)
		szText.resize(511);
	return szText;
}

int GetCaretPos()
{
	return static_cast<int>(s_nCaret);
}

void SetImeStatus(POINT /*ptPos*/, bool /*bOpen*/)
{
	// The input method places its own windows (at the rectangle given to SetTextInputFocus).
}

} // namespace Platform::TextInput

namespace Platform::macOS
{

// Scripted typing (InputScript.cpp) goes where the keyboard's text would.
void TypeScriptedText(const std::string& szUtf8)
{
	if (TextInput::s_bFocused)
		TextInput::InsertText(szUtf8);
}

void SendScriptedTextCommand(const std::string& szCommand)
{
	if (TextInput::s_bFocused)
		TextInput::Command(szCommand);
}

} // namespace Platform::macOS
