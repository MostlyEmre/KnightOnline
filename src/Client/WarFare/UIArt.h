#ifndef CLIENT_WARFARE_UIART_H
#define CLIENT_WARFARE_UIART_H

#pragma once

// Building UI in code out of the game's own art, for windows and buttons no uif has: the message
// box art (ui_message_us.dxt, which the in-game message box and the help window use).

#include <N3Base/N3UIButton.h>

#include <string>
#include <vector>

class CGameProcedure;
class CLocalInput;
class CN3UIEdit;
class CN3UIImage;
class CN3UIString;

namespace UIArt
{

/// \brief A piece of ui_message_us.dxt (512x512), in texels.
struct Piece
{
	int left, top, right, bottom;
};

// The frame, in nine pieces, as the help window puts it together. The corners are 64x64.
inline constexpr Piece FRAME_TOP_LEFT { 330, 2, 394, 66 };
inline constexpr Piece FRAME_TOP { 334, 2, 394, 66 };
inline constexpr Piece FRAME_TOP_RIGHT { 395, 2, 459, 66 };
inline constexpr Piece FRAME_LEFT { 330, 68, 394, 124 };
inline constexpr Piece FRAME_CENTER { 395, 67, 453, 124 };
inline constexpr Piece FRAME_RIGHT { 395, 67, 459, 126 };
inline constexpr Piece FRAME_BOTTOM_LEFT { 330, 67, 394, 131 };
inline constexpr Piece FRAME_BOTTOM { 337, 67, 394, 131 };
inline constexpr Piece FRAME_BOTTOM_RIGHT { 395, 67, 459, 131 };
inline constexpr int FRAME_CORNER = 64;

// The bars the message box buttons are made of (97x29): dark, blue pressed in, blue.
inline constexpr Piece BAR_DARK { 332, 134, 429, 163 };
inline constexpr Piece BAR_PRESSED { 332, 163, 429, 192 };
inline constexpr Piece BAR_BLUE { 332, 192, 429, 221 };
inline constexpr int BAR_WIDTH  = 97;
inline constexpr int BAR_HEIGHT = 29;

// An item slot (52x52): black, in a gold frame.
inline constexpr Piece SLOT { 15, 193, 67, 245 };
inline constexpr int SLOT_SIZE         = 52;

// The help window's close button (23x24) and page arrows (32x17): normal, down, mouse over.
inline constexpr Piece CLOSE[]         = { { 12, 379, 32, 399 }, { 56, 379, 76, 399 }, { 33, 377, 53, 397 } };
inline constexpr Piece ARROW_LEFT[]    = { { 94, 408, 126, 425 }, { 94, 444, 126, 461 }, { 94, 481, 126, 498 } };
inline constexpr Piece ARROW_RIGHT[]   = { { 127, 408, 159, 425 }, { 127, 444, 159, 461 }, { 127, 481, 159, 498 } };

// The message box buttons' label colours.
inline constexpr D3DCOLOR COLOR_BUTTON = 0xfffbe3b7;
inline constexpr D3DCOLOR COLOR_SHADOW = 0xff000000;

RECT MakeRect(int iLeft, int iTop, int iRight, int iBottom);
RECT OffsetBy(const RECT& rc, int iX, int iY);

void SetPiece(CN3UIImage* pImage, const Piece& piece);
CN3UIImage* AddImage(CN3UIBase* pParent, const RECT& rc, const Piece& piece);

/// \brief A bar stretched to any width, keeping its ends as they are.
std::vector<CN3UIImage*> AddBar(CN3UIBase* pParent, const RECT& rc, const Piece& piece);

/// \brief A single line of Arial, centred vertically in rc.
CN3UIString* AddString(CN3UIBase* pParent, const RECT& rc, const std::string& szText, uint32_t dwFontHeight, bool bBold, D3DCOLOR color,
	uint32_t dwAlign = UISTYLE_STRING_ALIGNLEFT);

} // namespace UIArt

/// \brief A button made in code (CN3UIButton only gets its pictures from a uif).
class CUIArtButton : public CN3UIButton
{
public:
	/// \brief From three pieces: normal, down and mouse over. Disabled looks normal.
	static CUIArtButton* Create(CN3UIBase* pParent, const RECT& rc, const UIArt::Piece (&pieces)[3]);

	/// \brief A message box style button: a bar with a label.
	static CUIArtButton* Create(CN3UIBase* pParent, const RECT& rc, const std::string& szLabel, CN3UIString** ppLabel = nullptr);

	/// \brief A selected bar (a tab) looks pressed in.
	void SetSelected(bool bSelected);

	/// \brief A new label, for a button made with one.
	void SetLabel(const std::string& szLabel);

private:
	CN3UIString* m_pLabel  = nullptr;
	CN3UIString* m_pShadow = nullptr;
};

/// \brief A window made in code: the frame, a title and a close button. It's modal: while it's open
/// it takes the input and hides it from the rest of the game.
class CUIArtWindow : public CN3UIBase
{
public:
	CUIArtWindow();

	virtual void Open();
	virtual void Close();

	/// \brief Called every frame before anything else reads the input. While the window is open
	/// it takes the input and hides it from the rest of the game, and keeps hiding it after it
	/// closes until the keys and buttons used to close it are released.
	void TakeInput(CLocalInput* pInput);

	bool ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg) override;

	/// \brief A key that closes the window, besides Esc (the one that opens it, say)
	void SetToggleKey(int iDIK);

protected:
	/// \brief Sizes the window and puts the frame, the title and the close button in. Children added
	/// later are drawn over the ones added before them, so this comes first.
	void BuildFrame(int iWidth, int iHeight, const std::string& szTitle);

	CN3UIButton* m_pBtnCloseX = nullptr;

private:
	CN3UIEdit* m_pEditFocused     = nullptr; // the text field that had the keyboard before opening
	CGameProcedure* m_pProcOpened = nullptr;
	bool m_bHoldInput             = false;
	int m_iToggleKey              = 0;
};

#endif // CLIENT_WARFARE_UIART_H
