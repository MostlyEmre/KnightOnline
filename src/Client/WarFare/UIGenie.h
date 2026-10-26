#ifndef CLIENT_WARFARE_UIGENIE_H
#define CLIENT_WARFARE_UIGENIE_H

#pragma once

#include "UIArt.h"

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class CGenie;
class CN3UIImage;
class CN3UIString;
namespace Genie
{
struct Settings;
}

// Genie's (auto-hunt's) window, which G opens: its settings on pages, and Start/Stop. There's no
// Genie art in 1.298, so it's put together from the message box art, like the Options window.
class CUIGenie : public CUIArtWindow
{
public:
	CUIGenie();
	~CUIGenie() override;

	void Open() override;
	void Close() override;
	void Tick() override;

	bool ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg) override;

private:
	enum PAGE : uint8_t
	{
		PAGE_ATTACK,
		PAGE_RECOVERY,
		PAGE_LOOT,
		PAGE_SUPPLIES,
		PAGE_TOWN,
		PAGE_COUNT
	};

	enum class SkillKind : uint8_t
	{
		Attack,
		Heal,
		Buff
	};

	struct Choice
	{
		std::string szName;
		int iValue = 0;
	};

	// A setting shown as a row: label, then ◀ value ▶. Changes go straight into Genie's settings.
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

	// Eight skill slots: the skill's icon over a slot, under a button that opens the picker.
	struct SkillSlots
	{
		SkillKind eKind = SkillKind::Attack;
		std::array<CN3UIImage*, 8> icons {};
		std::array<CN3UIButton*, 8> buttons {};
	};

	void Build();
	void AddRow(PAGE ePage, int& iTop, Setting setting);
	void AddSkillSlots(PAGE ePage, int& iTop, const std::string& szLabel, SkillKind eKind);
	void AddText(PAGE ePage, int iTop, const std::string& szText, CN3UIString** ppString = nullptr);
	void Track(PAGE ePage, CN3UIBase* pWidget);
	void BuildPicker();

	void ShowPage(int iPage);
	void Refresh();
	void RefreshRow(const Row& row);
	void Step(const Row& row, int iStep);
	void UpdateControls();

	void OpenPicker(SkillKind eKind, int iSlot);
	void ClosePicker();
	void ShowPickerPage(int iPage);
	std::vector<int> PickerSkills() const;

	std::array<int, 8>& SkillsOf(SkillKind eKind);
	static CGenie* GetGenie();
	static Genie::Settings& Settings();

	std::deque<Setting> m_Settings; // rows point into it
	std::vector<Row> m_Rows;
	std::vector<SkillSlots> m_SkillSlots;
	std::array<std::vector<CN3UIBase*>, PAGE_COUNT> m_PageWidgets;
	std::vector<CUIArtButton*> m_Tabs;
	std::vector<CN3UIString*> m_TabLabels;
	int m_iPage                                 = PAGE_ATTACK;

	// The monster list (Attack): names, each with a remove button, and "Add target".
	std::array<CN3UIString*, 8> m_MonsterNames  = {};
	std::array<CN3UIButton*, 8> m_MonsterRemove = {};
	CUIArtButton* m_pBtnAddTarget               = nullptr;

	// The merchants Genie buys from (Supplies).
	CN3UIString* m_pPotionMerchant              = nullptr;
	CN3UIString* m_pArrowMerchant               = nullptr;

	// The skill picker, over the page while it's open.
	std::vector<CN3UIBase*> m_PickerWidgets;
	CN3UIString* m_pPickerTitle                 = nullptr;
	std::array<CN3UIString*, 9> m_PickerNames   = {};
	std::array<CN3UIButton*, 9> m_PickerEntries = {};
	CN3UIString* m_pPickerPage                  = nullptr;
	CN3UIButton* m_pPickerPrev                  = nullptr;
	CN3UIButton* m_pPickerNext                  = nullptr;
	CUIArtButton* m_pPickerClear                = nullptr;
	CUIArtButton* m_pPickerBack                 = nullptr;
	bool m_bPicking                             = false;
	SkillKind m_ePickKind                       = SkillKind::Attack;
	int m_iPickSlot                             = 0;
	int m_iPickPage                             = 0;
	std::vector<int> m_PickSkills;

	CUIArtButton* m_pBtnStart = nullptr;
	CUIArtButton* m_pBtnClose = nullptr;
	CN3UIString* m_pStatus    = nullptr;
};

#endif // CLIENT_WARFARE_UIGENIE_H
