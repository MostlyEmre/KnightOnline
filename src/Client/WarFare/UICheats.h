#ifndef CLIENT_WARFARE_UICHEATS_H
#define CLIENT_WARFARE_UICHEATS_H

#pragma once

#include "UIArt.h"

#include "CheatRules.h"

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class CCheats;
class CN3UIImage;
class CN3UIString;

// The cheats menu's window, which J opens: the settings and the one-shot cheats on five pages.
// Every cheat is done by the server (CCheats sends the requests). Built from the message box art,
// like Genie's window.
class CUICheats : public CUIArtWindow
{
public:
	CUICheats();
	~CUICheats() override;

	void Open() override;
	void Close() override;
	void Tick() override;

	bool ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg) override;
	bool OnMouseWheelEvent(short delta) override;

private:
	enum PAGE : int8_t
	{
		PAGE_EVERY = -1,
		PAGE_MULTIPLIERS,
		PAGE_CHARACTER,
		PAGE_ITEMS,
		PAGE_GEAR,
		PAGE_WORLD,
		PAGE_COUNT
	};

	struct Choice
	{
		std::string szName;
		int iValue = 0;
	};

	// A setting shown as a row: label, then ◀ value ▶.
	struct Setting
	{
		std::string szLabel;
		std::function<std::vector<Choice>()> fnChoices;
		std::function<int()> fnGet;
		std::function<void(int)> fnSet;
	};

	struct Row
	{
		const Setting* pSetting = nullptr;
		CN3UIString* pValue     = nullptr;
		CN3UIButton* pPrev      = nullptr;
		CN3UIButton* pNext      = nullptr;
		CN3UIButton* pBox       = nullptr; // the value; clicking it picks the next choice
	};

	// One line of a list: an icon (item lists only), a name, and a button over the line.
	struct ListLine
	{
		CN3UIImage* pIcon    = nullptr;
		CN3UIString* pName   = nullptr;
		CN3UIButton* pButton = nullptr;
	};

	void Build();
	void BuildMultipliers();
	void BuildCharacter();
	void BuildItems();
	void BuildGear();
	void BuildWorld();

	void Track(PAGE ePage, CN3UIBase* pWidget);
	void AddRow(PAGE ePage, int& iTop, Setting setting);
	CUIArtButton* AddButton(PAGE ePage, int iLeft, int iTop, int iWidth, const std::string& szLabel);
	CUIArtEdit* AddEdit(PAGE ePage, int iTop, int iRight);
	CN3UIString* AddText(PAGE ePage, int iTop, const std::string& szText);
	void AddList(PAGE ePage, int& iTop, int iLines, bool bIcons, std::vector<ListLine>& lines);

	void ShowPage(int iPage);
	void Refresh();
	void RefreshRow(const Row& row);
	void Step(const Row& row, int iStep);
	void UpdateStatus();

	// The settings with one change, sent to the server; the rows show what it confirms.
	void ChangeSettings(const std::function<void(Cheats::Settings&)>& fnChange);

	void SearchItems();
	void PickItemLine(int iLine);
	void ShowItemList();
	void BuildVariants();
	void ShowNpcList();
	void RefreshSlotItem();

	static CCheats* GetCheats();

	std::deque<Setting> m_Settings; // rows point into it
	std::vector<Row> m_Rows;
	std::array<std::vector<CN3UIBase*>, PAGE_COUNT> m_PageWidgets;
	std::vector<CUIArtButton*> m_Tabs;
	std::vector<CN3UIString*> m_TabLabels;
	int m_iPage                    = PAGE_MULTIPLIERS;

	// Values the window holds until a button sends them.
	int m_iLevel                   = 1;
	int m_iCoins                   = 0;
	int m_iLoyalty                 = 0;
	int m_iKnightCash              = 0;
	int m_iCount                   = 1;
	int m_iSlot                    = 6; // the right hand
	int m_iPlus                    = 0;
	int m_iZone                    = 21;

	// Character
	CUIArtButton* m_pBtnSetLevel   = nullptr;
	CUIArtButton* m_pBtnLevelDown  = nullptr;
	CUIArtButton* m_pBtnLevelUp    = nullptr;
	CUIArtButton* m_pBtnStatPoints = nullptr;
	CUIArtButton* m_pBtnSkillPts   = nullptr;
	CUIArtButton* m_pBtnResetStats = nullptr;
	CUIArtButton* m_pBtnResetSkill = nullptr;
	CUIArtButton* m_pBtnRefill     = nullptr;
	CUIArtButton* m_pBtnSetCoins   = nullptr;
	CUIArtButton* m_pBtnSetNP      = nullptr;
	CUIArtButton* m_pBtnSetKC      = nullptr;

	// Items: search results, or one item's +levels after a pick.
	CUIArtEdit* m_pItemSearch      = nullptr;
	CUIArtButton* m_pBtnItemSearch = nullptr;
	std::vector<ListLine> m_ItemLines;
	CN3UIButton* m_pItemPrev     = nullptr;
	CN3UIButton* m_pItemNext     = nullptr;
	CN3UIString* m_pItemPage     = nullptr;
	CUIArtButton* m_pBtnItemBack = nullptr;
	CN3UIString* m_pPicked       = nullptr;
	CUIArtButton* m_pBtnGive     = nullptr;
	std::vector<Cheats::ItemName> m_ItemNames;
	std::vector<uint32_t> m_Results;
	std::vector<uint32_t> m_VariantIDs;
	bool m_bShowingVariants       = false;
	int m_iItemListPage           = 0;
	uint32_t m_dwBaseID           = 0;
	uint32_t m_dwPickedID         = 0;

	// Gear
	CN3UIString* m_pSlotItem      = nullptr;
	CUIArtButton* m_pBtnSetPlus   = nullptr;
	CUIArtButton* m_pBtnRepair    = nullptr;

	// World
	CUIArtButton* m_pBtnTeleport  = nullptr;
	CUIArtEdit* m_pNpcSearch      = nullptr;
	CUIArtButton* m_pBtnNpcSearch = nullptr;
	std::vector<ListLine> m_NpcLines;
	CUIArtButton* m_pBtnGoToNpc    = nullptr;
	CUIArtButton* m_pBtnKillAround = nullptr;
	int m_iNpcPicked               = -1;

	CUIArtButton* m_pBtnClose      = nullptr;
	CN3UIString* m_pStatus         = nullptr;

	// What the window last showed, to notice the server's answers.
	Cheats::Settings m_Shown;
	int m_iAnswersSeen = 0;
};

#endif // CLIENT_WARFARE_UICHEATS_H
