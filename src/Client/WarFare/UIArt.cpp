#include "StdAfx.h"
#include "UIArt.h"
#include "GameProcedure.h"
#include "LocalInput.h"

#include <N3Base/N3UIEdit.h>
#include <N3Base/N3UIImage.h>
#include <N3Base/N3UIString.h>

namespace UIArt
{

namespace
{

constexpr const char* TEXTURE = "ui_us\\ui_message_us.dxt";
constexpr float TEXTURE_SIZE  = 512.0f;
constexpr int BAR_CAP         = 8; // the bars' ends, which aren't stretched

} // namespace

RECT MakeRect(int iLeft, int iTop, int iRight, int iBottom)
{
	RECT rc {};
	rc.left   = iLeft;
	rc.top    = iTop;
	rc.right  = iRight;
	rc.bottom = iBottom;
	return rc;
}

RECT OffsetBy(const RECT& rc, int iX, int iY)
{
	return MakeRect(rc.left + iX, rc.top + iY, rc.right + iX, rc.bottom + iY);
}

void SetPiece(CN3UIImage* pImage, const Piece& piece)
{
	pImage->SetUVRect(piece.left / TEXTURE_SIZE, piece.top / TEXTURE_SIZE, piece.right / TEXTURE_SIZE, piece.bottom / TEXTURE_SIZE);
}

CN3UIImage* AddImage(CN3UIBase* pParent, const RECT& rc, const Piece& piece)
{
	auto* pImage = new CN3UIImage();
	pImage->Init(pParent);
	pImage->SetTex(TEXTURE);
	SetPiece(pImage, piece);
	pImage->SetRegion(rc);
	return pImage;
}

std::vector<CN3UIImage*> AddBar(CN3UIBase* pParent, const RECT& rc, const Piece& piece)
{
	const Piece left { piece.left, piece.top, piece.left + BAR_CAP, piece.bottom };
	const Piece middle { piece.left + BAR_CAP, piece.top, piece.right - BAR_CAP, piece.bottom };
	const Piece right { piece.right - BAR_CAP, piece.top, piece.right, piece.bottom };
	return {
		AddImage(pParent, MakeRect(rc.left, rc.top, rc.left + BAR_CAP, rc.bottom), left),
		AddImage(pParent, MakeRect(rc.left + BAR_CAP, rc.top, rc.right - BAR_CAP, rc.bottom), middle),
		AddImage(pParent, MakeRect(rc.right - BAR_CAP, rc.top, rc.right, rc.bottom), right),
	};
}

CN3UIString* AddString(
	CN3UIBase* pParent, const RECT& rc, const std::string& szText, uint32_t dwFontHeight, bool bBold, D3DCOLOR color, uint32_t dwAlign)
{
	auto* pString = new CN3UIString();
	pString->Init(pParent);
	pString->SetFont("Arial", dwFontHeight, bBold ? TRUE : FALSE, FALSE);
	pString->SetRegion(rc);
	pString->SetStyle(UISTYLE_STRING_SINGLELINE | UISTYLE_STRING_ALIGNVCENTER | dwAlign);
	pString->SetColor(color);
	pString->SetString(szText);
	return pString;
}

} // namespace UIArt

CUIArtButton* CUIArtButton::Create(CN3UIBase* pParent, const RECT& rc, const UIArt::Piece (&pieces)[3])
{
	auto* pButton = new CUIArtButton();
	pButton->Init(pParent);
	for (int i = 0; i < NUM_BTN_STATE; i++)
	{
		CN3UIImage* pImage = UIArt::AddImage(pButton, rc, pieces[i < 3 ? i : 0]);
		pImage->SetReserved(i);
		pButton->m_ImageRef[i] = pImage;
	}
	pButton->SetRegion(rc);
	return pButton;
}

CUIArtButton* CUIArtButton::Create(CN3UIBase* pParent, const RECT& rc, const std::string& szLabel, CN3UIString** ppLabel)
{
	CUIArtButton* pButton = Create(pParent, rc, { UIArt::BAR_DARK, UIArt::BAR_PRESSED, UIArt::BAR_BLUE });

	// Later children are drawn over earlier ones: the shadow first.
	pButton->m_pShadow    = UIArt::AddString(
        pButton, UIArt::OffsetBy(rc, 1, 1), szLabel, 11, true, UIArt::COLOR_SHADOW, UISTYLE_STRING_ALIGNCENTER);
	pButton->m_pLabel = UIArt::AddString(pButton, rc, szLabel, 11, true, UIArt::COLOR_BUTTON, UISTYLE_STRING_ALIGNCENTER);
	if (ppLabel != nullptr)
		*ppLabel = pButton->m_pLabel;
	return pButton;
}

void CUIArtButton::SetSelected(bool bSelected)
{
	UIArt::SetPiece(m_ImageRef[BS_NORMAL], bSelected ? UIArt::BAR_PRESSED : UIArt::BAR_DARK);
	UIArt::SetPiece(m_ImageRef[BS_ON], bSelected ? UIArt::BAR_PRESSED : UIArt::BAR_BLUE);
}

void CUIArtButton::SetLabel(const std::string& szLabel)
{
	if (m_pLabel != nullptr)
		m_pLabel->SetString(szLabel);
	if (m_pShadow != nullptr)
		m_pShadow->SetString(szLabel);
}

CUIArtWindow::CUIArtWindow()
{
	m_bVisible = false; // until it's opened
}

void CUIArtWindow::BuildFrame(int iWidth, int iHeight, const std::string& szTitle)
{
	SetRegion(UIArt::MakeRect(0, 0, iWidth, iHeight));

	// The frame is drawn three times: the art is see-through (73%), which makes text hard to read
	// over a busy screen.
	const int iRight  = iWidth - UIArt::FRAME_CORNER;
	const int iBottom = iHeight - UIArt::FRAME_CORNER;
	for (int i = 0; i < 3; i++)
	{
		UIArt::AddImage(this, UIArt::MakeRect(0, 0, UIArt::FRAME_CORNER, UIArt::FRAME_CORNER), UIArt::FRAME_TOP_LEFT);
		UIArt::AddImage(this, UIArt::MakeRect(iRight, 0, iWidth, UIArt::FRAME_CORNER), UIArt::FRAME_TOP_RIGHT);
		UIArt::AddImage(this, UIArt::MakeRect(0, iBottom, UIArt::FRAME_CORNER, iHeight), UIArt::FRAME_BOTTOM_LEFT);
		UIArt::AddImage(this, UIArt::MakeRect(iRight, iBottom, iWidth, iHeight), UIArt::FRAME_BOTTOM_RIGHT);
		UIArt::AddImage(this, UIArt::MakeRect(UIArt::FRAME_CORNER, 0, iRight, UIArt::FRAME_CORNER), UIArt::FRAME_TOP);
		UIArt::AddImage(this, UIArt::MakeRect(UIArt::FRAME_CORNER, iBottom, iRight, iHeight), UIArt::FRAME_BOTTOM);
		UIArt::AddImage(this, UIArt::MakeRect(0, UIArt::FRAME_CORNER, UIArt::FRAME_CORNER, iBottom), UIArt::FRAME_LEFT);
		UIArt::AddImage(this, UIArt::MakeRect(iRight, UIArt::FRAME_CORNER, iWidth, iBottom), UIArt::FRAME_RIGHT);
		UIArt::AddImage(this, UIArt::MakeRect(UIArt::FRAME_CORNER, UIArt::FRAME_CORNER, iRight, iBottom), UIArt::FRAME_CENTER);
	}

	UIArt::AddString(this, UIArt::MakeRect(16, 8, iWidth - 32, 30), szTitle, 12, true, 0xffffffff);
	m_pBtnCloseX = CUIArtButton::Create(this, UIArt::MakeRect(iWidth - 28, 4, iWidth - 5, 28), UIArt::CLOSE);
}

void CUIArtWindow::Open()
{
	if (m_pBtnCloseX != nullptr)
		m_pBtnCloseX->SetState(UI_STATE_BUTTON_NORMAL);

	// Typing goes nowhere while the window is open.
	m_pEditFocused = CN3UIBase::GetFocusedEdit();
	m_pProcOpened  = CGameProcedure::s_pProcActive;
	if (m_pEditFocused != nullptr)
		m_pEditFocused->KillFocus();

	SetVisible(true);
}

void CUIArtWindow::Close()
{
	if (!IsVisible())
		return;

	SetVisible(false);
	m_bHoldInput = true;

	// Give the keyboard back, if the text field is still around.
	if (m_pEditFocused != nullptr && CGameProcedure::s_pProcActive == m_pProcOpened && m_pEditFocused->IsVisible())
		m_pEditFocused->SetFocus();
	m_pEditFocused = nullptr;
}

void CUIArtWindow::TakeInput(CLocalInput* pInput)
{
	if (IsVisible())
	{
		MouseProc(static_cast<uint32_t>(pInput->MouseGetFlag()), pInput->MouseGetPos(), pInput->MouseGetPosOld());
		Tick();
		if (IsVisible() && (pInput->IsKeyPress(DIK_ESCAPE) || (m_iToggleKey != 0 && pInput->IsKeyPress(m_iToggleKey))))
			Close();
	}
	else if (m_bHoldInput)
	{
		// Until everything is let go, including the frame it is, so the game sees no releases.
		constexpr int ANY_BUTTON = MOUSE_LBDOWN | MOUSE_MBDOWN | MOUSE_RBDOWN;
		if (pInput->IsNoKeyDown() && (pInput->MouseGetFlag() & ANY_BUTTON) == 0)
			m_bHoldInput = false;
	}
	else
	{
		return;
	}

	pInput->Mute(true);
}

void CUIArtWindow::SetToggleKey(int iDIK)
{
	m_iToggleKey = iDIK;
}

bool CUIArtWindow::ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg)
{
	if (dwMsg == UIMSG_BUTTON_CLICK && pSender == m_pBtnCloseX && m_pBtnCloseX != nullptr)
	{
		Close();
		return true;
	}

	return false;
}
