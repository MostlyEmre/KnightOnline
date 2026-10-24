#include "StdAfx.h"
#include "UIArt.h"

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
	UIArt::AddString(pButton, UIArt::OffsetBy(rc, 1, 1), szLabel, 11, true, UIArt::COLOR_SHADOW, UISTYLE_STRING_ALIGNCENTER);
	CN3UIString* pLabel = UIArt::AddString(pButton, rc, szLabel, 11, true, UIArt::COLOR_BUTTON, UISTYLE_STRING_ALIGNCENTER);
	if (ppLabel != nullptr)
		*ppLabel = pLabel;
	return pButton;
}

void CUIArtButton::SetSelected(bool bSelected)
{
	UIArt::SetPiece(m_ImageRef[BS_NORMAL], bSelected ? UIArt::BAR_PRESSED : UIArt::BAR_DARK);
	UIArt::SetPiece(m_ImageRef[BS_ON], bSelected ? UIArt::BAR_PRESSED : UIArt::BAR_BLUE);
}
