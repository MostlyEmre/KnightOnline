#include "StdAfx.h"
#include "UIPowerUpStore.h"
#include "APISocket.h"
#include "GameProcedure.h"
#include "PacketDef.h"
#include "text_resources.h"

#include <N3Base/N3UIImage.h>
#include <N3Base/N3UIString.h>

#include <algorithm>
#include <cstdlib>

namespace
{

constexpr D3DCOLOR COLOR_LABEL     = 0xfffae6af;
constexpr D3DCOLOR COLOR_VALUE     = 0xffffffff;
constexpr D3DCOLOR COLOR_PRICE     = 0xff9fd8ff;
constexpr D3DCOLOR COLOR_EXPENSIVE = 0xffff6060;
constexpr D3DCOLOR COLOR_ASK       = 0xffffff00;
constexpr D3DCOLOR COLOR_DONE      = 0xff80ff80;
constexpr D3DCOLOR COLOR_ERROR     = 0xffff6060;

// Layout, from the window's top left. Items go in two columns of five.
constexpr int WIDTH                = 640;
constexpr int MARGIN               = 24;
constexpr int INFO_TOP             = 38;
constexpr int GRID_TOP             = 64;
constexpr int COLUMNS              = 2;
constexpr int ROWS                 = 5;
constexpr int CELL_WIDTH           = 292;
constexpr int CELL_HEIGHT          = 56;
constexpr int COLUMN_PITCH         = CELL_WIDTH + 8;
constexpr int ROW_PITCH            = CELL_HEIGHT + 6;
constexpr int PAGE_TOP             = GRID_TOP + ROWS * ROW_PITCH + 2;
constexpr int DETAIL_TOP           = PAGE_TOP + 26;
constexpr int STATUS_TOP           = DETAIL_TOP + 42;
constexpr int BUTTON_TOP           = STATUS_TOP + 26;
constexpr int HEIGHT               = BUTTON_TOP + UIArt::BAR_HEIGHT + 20;
constexpr int ARROW_WIDTH          = 32;
constexpr int ARROW_HEIGHT         = 17;
constexpr int ICON_SIZE            = 45;
constexpr float ICON_UV            = 45.0f / 64.0f; // item icons are 45x45 in a 64x64 texture

// 1234567 -> "1,234,567"
std::string FormatNumber(int iValue)
{
	std::string szDigits = std::to_string(iValue < 0 ? -static_cast<int64_t>(iValue) : iValue);
	std::string szResult;
	for (size_t i = 0; i < szDigits.size(); i++)
	{
		if (i > 0 && (szDigits.size() - i) % 3 == 0)
			szResult += ',';
		szResult += szDigits[i];
	}

	return iValue < 0 ? "-" + szResult : szResult;
}

} // namespace

CUIPowerUpStore::CUIPowerUpStore()  = default;

CUIPowerUpStore::~CUIPowerUpStore() = default;

void CUIPowerUpStore::Build()
{
	BuildFrame(WIDTH, HEIGHT, "Power-up Store");

	m_pKnightCash = UIArt::AddString(this, UIArt::MakeRect(MARGIN, INFO_TOP, WIDTH / 2, INFO_TOP + 20), "", 11, true, COLOR_LABEL);
	m_pFreeSlots  = UIArt::AddString(
        this, UIArt::MakeRect(WIDTH / 2, INFO_TOP, WIDTH - MARGIN, INFO_TOP + 20), "", 10, false, COLOR_VALUE, UISTYLE_STRING_ALIGNRIGHT);

	// Children added later are drawn over the ones added before them, and get the clicks first:
	// each cell's button goes on top, the rest let the clicks through.
	for (int i = 0; i < COLUMNS * ROWS; i++)
	{
		const int iLeft = MARGIN + (i % COLUMNS) * COLUMN_PITCH;
		const int iTop  = GRID_TOP + (i / COLUMNS) * ROW_PITCH;
		const RECT rc   = UIArt::MakeRect(iLeft, iTop, iLeft + CELL_WIDTH, iTop + CELL_HEIGHT);
		const int iSlot = (CELL_HEIGHT - UIArt::SLOT_SIZE) / 2;
		const int iIcon = (CELL_HEIGHT - ICON_SIZE) / 2;
		const int iText = iLeft + UIArt::SLOT_SIZE + 12;

		Cell cell;
		cell.barImages = UIArt::AddBar(this, rc, UIArt::BAR_DARK);
		cell.pSlot     = UIArt::AddImage(this,
				UIArt::MakeRect(iLeft + iSlot, iTop + iSlot, iLeft + iSlot + UIArt::SLOT_SIZE, iTop + iSlot + UIArt::SLOT_SIZE), UIArt::SLOT);

		cell.pIcon     = new CN3UIImage();
		cell.pIcon->Init(this);
		cell.pIcon->SetRegion(UIArt::MakeRect(iLeft + iIcon + 1, iTop + iIcon, iLeft + iIcon + 1 + ICON_SIZE, iTop + iIcon + ICON_SIZE));

		cell.pName = UIArt::AddString(this, UIArt::MakeRect(iText, iTop + 6, iLeft + CELL_WIDTH - 6, iTop + 28), "", 10, true, COLOR_LABEL);
		cell.pPrice = UIArt::AddString(
			this, UIArt::MakeRect(iText, iTop + 28, iLeft + CELL_WIDTH - 6, iTop + 50), "", 10, false, COLOR_PRICE);

		cell.pButton = new CN3UIButton();
		cell.pButton->Init(this);
		cell.pButton->SetRegion(rc);
		m_Cells.push_back(cell);
	}

	const int iArrowTop = PAGE_TOP + (20 - ARROW_HEIGHT) / 2;
	m_pBtnPrev          = CUIArtButton::Create(
        this, UIArt::MakeRect(WIDTH / 2 - 80, iArrowTop, WIDTH / 2 - 80 + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_LEFT);
	m_pPage    = UIArt::AddString(this, UIArt::MakeRect(WIDTH / 2 - 44, PAGE_TOP, WIDTH / 2 + 44, PAGE_TOP + 20), "", 10, true, COLOR_VALUE,
		   UISTYLE_STRING_ALIGNCENTER);
	m_pBtnNext = CUIArtButton::Create(
		this, UIArt::MakeRect(WIDTH / 2 + 80 - ARROW_WIDTH, iArrowTop, WIDTH / 2 + 80, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_RIGHT);

	m_pSelName   = UIArt::AddString(this, UIArt::MakeRect(MARGIN, DETAIL_TOP, WIDTH - MARGIN, DETAIL_TOP + 20), "", 11, true, COLOR_LABEL);
	m_pSelRemark = UIArt::AddString(
		this, UIArt::MakeRect(MARGIN, DETAIL_TOP + 20, WIDTH - MARGIN, DETAIL_TOP + 38), "", 10, false, COLOR_VALUE);
	m_pStatus = UIArt::AddString(
		this, UIArt::MakeRect(MARGIN, STATUS_TOP, WIDTH - MARGIN, STATUS_TOP + 20), "", 10, true, COLOR_ASK, UISTYLE_STRING_ALIGNCENTER);

	const int iButtonsLeft = WIDTH / 2 - UIArt::BAR_WIDTH - 3;
	m_pBtnBuy              = CUIArtButton::Create(
        this, UIArt::MakeRect(iButtonsLeft, BUTTON_TOP, iButtonsLeft + UIArt::BAR_WIDTH, BUTTON_TOP + UIArt::BAR_HEIGHT), "Buy");
	m_pBtnClose = CUIArtButton::Create(
		this, UIArt::MakeRect(WIDTH / 2 + 3, BUTTON_TOP, WIDTH / 2 + 3 + UIArt::BAR_WIDTH, BUTTON_TOP + UIArt::BAR_HEIGHT), "Close");
}

void CUIPowerUpStore::OpenStore(int iKnightCash, int iFreeSlots, const std::vector<Item>& items)
{
	if (m_Cells.empty())
		Build();

	// What the client's tables say about each item; one it doesn't know can't be shown.
	m_Entries.clear();
	for (const Item& item : items)
	{
		__TABLE_ITEM_BASIC* pItem  = CGameBase::s_pTbl_Items_Basic.Find(item.iItemID / 1000 * 1000);
		__TABLE_ITEM_EXT* pItemExt = nullptr;
		if (pItem != nullptr && pItem->byExtIndex < MAX_ITEM_EXTENSION)
			pItemExt = CGameBase::s_pTbl_Items_Exts[pItem->byExtIndex].Find(item.iItemID % 1000);
		if (pItem == nullptr || pItemExt == nullptr)
			continue;

		Entry entry;
		entry.item     = item;
		entry.szName   = static_cast<e_ItemAttrib>(pItemExt->byMagicOrRare) == ITEM_ATTRIB_UNIQUE ? pItemExt->szHeader : pItem->szName;
		entry.szRemark = pItem->szRemark;

		e_PartPosition ePart = PART_POS_UNKNOWN;
		e_PlugPosition ePlug = PLUG_POS_UNKNOWN;
		if (CGameBase::MakeResrcFileNameForUPC(pItem, pItemExt, nullptr, &entry.szIconFN, ePart, ePlug, RACE_UNKNOWN) == ITEM_TYPE_UNKNOWN)
			continue;

		m_Entries.push_back(std::move(entry));
	}

	m_iKnightCash = iKnightCash;
	m_iFreeSlots  = iFreeSlots;
	m_bBuying     = false;
	m_iPage       = std::min(m_iPage, PageCount() - 1);

	SetPosCenter();
	m_pBtnBuy->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnClose->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnPrev->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnNext->SetState(UI_STATE_BUTTON_NORMAL);
	Select(-1);
	SetStatus(m_Entries.empty() ? "There's nothing for sale." : "", COLOR_ERROR);

	if (!IsVisible())
		CUIArtWindow::Open();
}

void CUIPowerUpStore::Close()
{
	if (!IsVisible())
		return;

	CUIArtWindow::Close();

	if (CGameProcedure::s_pSocket != nullptr && CGameProcedure::s_pSocket->IsConnected())
	{
		uint8_t byBuff[4];
		int iOffset = 0;
		CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_SHOPPING_MALL);
		CAPISocket::MP_AddByte(byBuff, iOffset, STORE_CLOSE);
		CGameProcedure::s_pSocket->Send(byBuff, iOffset);
	}
}

int CUIPowerUpStore::PageCount() const
{
	const int iPerPage = COLUMNS * ROWS;
	return std::max(1, (static_cast<int>(m_Entries.size()) + iPerPage - 1) / iPerPage);
}

void CUIPowerUpStore::ShowPage(int iPage)
{
	m_iPage = std::clamp(iPage, 0, PageCount() - 1);
	m_pPage->SetString(fmt::format("{} / {}", m_iPage + 1, PageCount()));

	for (size_t i = 0; i < m_Cells.size(); i++)
	{
		Cell& cell  = m_Cells[i];
		cell.iEntry = m_iPage * COLUMNS * ROWS + static_cast<int>(i);
		bool bInUse = cell.iEntry < static_cast<int>(m_Entries.size());
		if (!bInUse)
			cell.iEntry = -1;

		ShowCell(cell, bInUse);
		if (!bInUse)
			continue;

		const Entry& entry = m_Entries[cell.iEntry];
		cell.pIcon->SetTex(entry.szIconFN);
		cell.pIcon->SetUVRect(0, 0, ICON_UV, ICON_UV);
		cell.pName->SetString(entry.item.iCount > 1 ? fmt::format("{} x{}", entry.szName, entry.item.iCount) : entry.szName);
		cell.pPrice->SetString(fmt::format("{} Knight Cash", FormatNumber(entry.item.iPrice)));
		cell.pPrice->SetColor(entry.item.iPrice > m_iKnightCash ? COLOR_EXPENSIVE : COLOR_PRICE);

		// The selected item's bar is the blue one.
		const UIArt::Piece& bar = cell.iEntry == m_iSelected ? UIArt::BAR_BLUE : UIArt::BAR_DARK;
		const UIArt::Piece left { bar.left, bar.top, bar.left + 8, bar.bottom };
		const UIArt::Piece middle { bar.left + 8, bar.top, bar.right - 8, bar.bottom };
		const UIArt::Piece right { bar.right - 8, bar.top, bar.right, bar.bottom };
		UIArt::SetPiece(cell.barImages[0], left);
		UIArt::SetPiece(cell.barImages[1], middle);
		UIArt::SetPiece(cell.barImages[2], right);
	}
}

void CUIPowerUpStore::ShowCell(const Cell& cell, bool bVisible)
{
	for (CN3UIImage* pImage : cell.barImages)
		pImage->SetVisible(bVisible);
	cell.pSlot->SetVisible(bVisible);
	cell.pIcon->SetVisible(bVisible);
	cell.pName->SetVisible(bVisible);
	cell.pPrice->SetVisible(bVisible);
	cell.pButton->SetVisible(bVisible);
}

void CUIPowerUpStore::Select(int iEntry)
{
	m_iSelected   = iEntry >= 0 && iEntry < static_cast<int>(m_Entries.size()) ? iEntry : -1;
	m_bConfirming = false;

	if (m_iSelected >= 0)
	{
		m_pSelName->SetString(m_Entries[m_iSelected].szName);
		m_pSelRemark->SetString(m_Entries[m_iSelected].szRemark);
	}
	else
	{
		m_pSelName->SetString("Choose an item to buy.");
		m_pSelRemark->SetString("");
	}

	SetStatus("", COLOR_ASK);
	SetBuyLabel();
	UpdateInfo();
	ShowPage(m_iPage);
}

void CUIPowerUpStore::UpdateInfo()
{
	m_pKnightCash->SetString(fmt::format("Knight Cash: {}", FormatNumber(m_iKnightCash)));
	m_pFreeSlots->SetString(fmt::format("Free inventory slots: {}", m_iFreeSlots));
}

void CUIPowerUpStore::SetStatus(const std::string& szText, D3DCOLOR color)
{
	m_pStatus->SetString(szText);
	m_pStatus->SetColor(color);
}

void CUIPowerUpStore::SetBuyLabel()
{
	if (m_bBuying)
		m_pBtnBuy->SetLabel("Buying...");
	else if (m_bConfirming)
		m_pBtnBuy->SetLabel("Confirm");
	else
		m_pBtnBuy->SetLabel("Buy");
}

void CUIPowerUpStore::Buy()
{
	if (m_bBuying)
		return;

	if (m_iSelected < 0)
	{
		SetStatus("Choose an item to buy.", COLOR_ERROR);
		return;
	}

	const Entry& entry = m_Entries[m_iSelected];
	if (entry.item.iPrice > m_iKnightCash)
	{
		SetStatus("You don't have enough Knight Cash.", COLOR_ERROR);
		return;
	}

	// The first press asks, the second buys.
	if (!m_bConfirming)
	{
		m_bConfirming = true;
		SetStatus(fmt::format("Buy {} for {} Knight Cash? Press Confirm.", entry.szName, FormatNumber(entry.item.iPrice)), COLOR_ASK);
		SetBuyLabel();
		return;
	}

	m_bConfirming = false;
	m_bBuying     = true;
	SetStatus("", COLOR_ASK);
	SetBuyLabel();

	uint8_t byBuff[16];
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_SHOPPING_MALL);
	CAPISocket::MP_AddByte(byBuff, iOffset, STORE_BUY);
	CAPISocket::MP_AddDword(byBuff, iOffset, static_cast<uint32_t>(entry.item.iItemID));
	CAPISocket::MP_AddShort(byBuff, iOffset, static_cast<int16_t>(entry.item.iCount));
	CGameProcedure::s_pSocket->Send(byBuff, iOffset);
}

void CUIPowerUpStore::OnBuyResult(uint8_t byResult, int iKnightCash, int iFreeSlots)
{
	m_bBuying     = false;
	m_iKnightCash = iKnightCash;
	m_iFreeSlots  = iFreeSlots;
	SetBuyLabel();
	UpdateInfo();
	ShowPage(m_iPage);

	const std::string szName = m_iSelected >= 0 ? m_Entries[m_iSelected].szName : std::string();
	switch (byResult)
	{
		case STORE_BUY_SUCCESS:
			SetStatus(fmt::format("You bought {}. It's in your inventory.", szName), COLOR_DONE);
			break;

		case STORE_BUY_NO_CASH:
			SetStatus("You don't have enough Knight Cash.", COLOR_ERROR);
			break;

		case STORE_BUY_NO_ROOM:
			SetStatus("There's no room for it in your inventory.", COLOR_ERROR);
			break;

		case STORE_BUY_TOO_HEAVY:
			SetStatus(fmt::format_text_resource(IDS_ITEM_WEIGHT_OVERFLOW), COLOR_ERROR);
			break;

		case STORE_BUY_UNAVAILABLE:
			SetStatus("The store can't be reached right now. Please try again later.", COLOR_ERROR);
			break;

		default:
			SetStatus("That can't be bought right now.", COLOR_ERROR);
			break;
	}
}

void CUIPowerUpStore::SetKnightCash(int iKnightCash)
{
	m_iKnightCash = iKnightCash;
	UpdateInfo();
	ShowPage(m_iPage);
}

bool CUIPowerUpStore::ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg)
{
	if (CUIArtWindow::ReceiveMessage(pSender, dwMsg))
		return true;

	if (dwMsg != UIMSG_BUTTON_CLICK)
		return false;

	if (pSender == m_pBtnClose)
	{
		Close();
		return true;
	}

	if (pSender == m_pBtnBuy)
	{
		Buy();
		return true;
	}

	if (pSender == m_pBtnPrev || pSender == m_pBtnNext)
	{
		ShowPage(m_iPage + (pSender == m_pBtnNext ? 1 : -1));
		return true;
	}

	for (const Cell& cell : m_Cells)
	{
		if (pSender == cell.pButton && cell.iEntry >= 0)
		{
			if (!m_bBuying)
				Select(cell.iEntry);
			return true;
		}
	}

	return false;
}

bool CUIPowerUpStore::OnMouseWheelEvent(short delta)
{
	// Scrolling turns the pages, down for the next: a page per notch of a wheel, or per swipe on a
	// trackpad. After a turn the rest of the gesture is let go, until the scrolling stops for a
	// moment.
	const float fNow = CN3Base::TimeGet();
	if (fNow - m_fWheelTime > 0.25f)
	{
		m_iWheelDelta  = 0;
		m_bWheelTurned = false;
	}

	m_fWheelTime = fNow;
	if (m_bWheelTurned)
		return true;

	m_iWheelDelta += delta;
	if (std::abs(m_iWheelDelta) >= 120)
	{
		ShowPage(m_iPage + (m_iWheelDelta < 0 ? 1 : -1));
		m_bWheelTurned = true;
	}

	return true;
}
