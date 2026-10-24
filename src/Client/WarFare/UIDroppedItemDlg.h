// UIDroppedItemDlg.h: interface for the UIDroppedItemDlg class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_UIDROPPEDITEMDLG_H__1E9B7E7F_FB6F_4D07_8386_6E84E22D73BB__INCLUDED_)
#define AFX_UIDROPPEDITEMDLG_H__1E9B7E7F_FB6F_4D07_8386_6E84E22D73BB__INCLUDED_

#pragma once

#include "GameDef.h"
#include "N3UIWndBase.h"

#include <functional>

class CUIArtButton;

class CUIDroppedItemDlg : public CN3UIWndBase
{
public:
	__IconItemSkill* m_pMyDroppedItem[MAX_ITEM_BUNDLE_DROP_PIECE];
	bool m_bSendedIconArray[MAX_ITEM_BUNDLE_DROP_PIECE];

	int m_iItemBundleID;
	CUIImageTooltipDlg* m_pUITooltipDlg;

	int m_iBackupiOrder;

protected:
	CUIArtButton* m_pBtnLootAll; // not in the uif: added under the items
	bool m_bLootingAll;          // picking the items up one after another
	bool m_bLootPending;         // a pick-up request is waiting for its result

	// Genie's looting: the window stays hidden and picks up only what fnWants accepts.
	bool m_bQuietLoot;
	std::function<bool(const __IconItemSkill&)> m_fnQuietWants;

	int GetInventoryEmptyInviOrder(__IconItemSkill* spItem = nullptr);
	int GetItemiOrder(__IconItemSkill* spItem);
	bool RequestItem(int iOrder);
	void LootNext();

public:
	CUIDroppedItemDlg();
	~CUIDroppedItemDlg() override;
	void Release() override;

	void SetVisibleWithNoSound(bool bVisible, bool bWork = false, bool bReFocus = false) override;
	uint32_t MouseProc(uint32_t dwFlags, const POINT& ptCur, const POINT& ptOld) override;
	bool ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg) override;
	void Render() override;
	void EnterDroppedState(int xpos, int ypos);
	void LeaveDroppedState();
	void GetItemByIDToInventory(uint8_t bResult, int iItemId, int iGold, int iPos, int iItemCount, const std::string& characterName);

	/// \brief After a pick-up result (and GetItemByIDToInventory): Loot All asks for the next item.
	void ContinueLootAll(uint8_t bResult);

	/// \brief Genie's looting: the next opened bundle (iBundleID) fills the window without showing it;
	/// StartQuietLoot() then picks up what fnWants accepts, and the window leaves the dropped state.
	void BeginQuietLoot(int iBundleID, std::function<bool(const __IconItemSkill&)> fnWants);
	void StartQuietLoot();
	void CancelQuietLoot();
	bool IsQuietLooting() const;
	void AddToItemTable(int iItemId, int iItemCount, int iOrder);
	void AddToItemTableToInventory(int iItemId, int iItemCount, int iOrder);

	bool ReceiveIconDrop(__IconItemSkill* spItem, POINT ptCur) override;

	void InitIconWnd(e_UIWND eWnd) override;
	void InitIconUpdate() override;

	__IconItemSkill* GetHighlightIconItem(CN3UIIcon* pUIIcon) override;
};

#endif // !defined(AFX_UIDROPPEDITEMDLG_H__1E9B7E7F_FB6F_4D07_8386_6E84E22D73BB__INCLUDED_)
