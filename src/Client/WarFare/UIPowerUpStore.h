#ifndef CLIENT_WARFARE_UIPOWERUPSTORE_H
#define CLIENT_WARFARE_UIPOWERUPSTORE_H

#pragma once

#include "UIArt.h"

#include <string>
#include <vector>

class CN3UIImage;
class CN3UIString;

// The Power-up Store, which the P button opens: the items the server sells for Knight Cash, on
// pages. The official store was a web page (co_ShoppingMall_us.uif is a frame around a browser),
// so this one is put together from the message box art, like the Options window.
class CUIPowerUpStore : public CUIArtWindow
{
public:
	/// \brief An item for sale, as the server sends it (STORE_OPEN)
	struct Item
	{
		int iItemID = 0;
		int iCount  = 0;
		int iPrice  = 0;
	};

	CUIPowerUpStore();
	~CUIPowerUpStore() override;

	/// \brief Opens the store with what the server sent (STORE_OPEN).
	void OpenStore(int iKnightCash, int iFreeSlots, const std::vector<Item>& items);

	/// \brief Closes the store, and tells the server (STORE_CLOSE).
	void Close() override;

	/// \brief The server's answer to a purchase (STORE_BUY).
	void OnBuyResult(uint8_t byResult, int iKnightCash, int iFreeSlots);

	/// \brief Knight Cash earned while the store is open (STORE_KNIGHT_CASH).
	void SetKnightCash(int iKnightCash);

	bool ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg) override;
	bool OnMouseWheelEvent(short delta) override;

private:
	// An item for sale, with what the client's tables say about it.
	struct Entry
	{
		Item item;
		std::string szName;
		std::string szRemark;
		std::string szIconFN;
	};

	// A place on the page for an item. The button on top takes the clicks; it has no pictures.
	struct Cell
	{
		std::vector<CN3UIImage*> barImages;
		CN3UIImage* pSlot    = nullptr;
		CN3UIImage* pIcon    = nullptr;
		CN3UIString* pName   = nullptr;
		CN3UIString* pPrice  = nullptr;
		CN3UIButton* pButton = nullptr;
		int iEntry           = -1;
	};

	void Build();
	void ShowPage(int iPage);
	void ShowCell(const Cell& cell, bool bVisible);
	void Select(int iEntry);
	void UpdateInfo();
	void SetStatus(const std::string& szText, D3DCOLOR color);
	void SetBuyLabel();
	void Buy();
	int PageCount() const;

	std::vector<Entry> m_Entries;
	std::vector<Cell> m_Cells;
	int m_iPage                = 0;
	int m_iSelected            = -1;
	int m_iKnightCash          = 0;
	int m_iFreeSlots           = 0;
	bool m_bConfirming         = false; // Buy was pressed once: pressing it again buys
	bool m_bBuying             = false; // waiting on the server

	// Scrolling: what's come in since the last page turn, and whether this gesture turned one.
	int m_iWheelDelta          = 0;
	float m_fWheelTime         = 0.0f;
	bool m_bWheelTurned        = false;

	CN3UIString* m_pKnightCash = nullptr;
	CN3UIString* m_pFreeSlots  = nullptr;
	CN3UIString* m_pPage       = nullptr;
	CN3UIButton* m_pBtnPrev    = nullptr;
	CN3UIButton* m_pBtnNext    = nullptr;
	CN3UIString* m_pSelName    = nullptr;
	CN3UIString* m_pSelRemark  = nullptr;
	CN3UIString* m_pStatus     = nullptr;
	CUIArtButton* m_pBtnBuy    = nullptr;
	CUIArtButton* m_pBtnClose  = nullptr;
};

#endif // CLIENT_WARFARE_UIPOWERUPSTORE_H
