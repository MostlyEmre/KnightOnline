#include "StdAfx.h"
#include "UIGenie.h"
#include "Genie.h"
#include "GameProcMain.h"
#include "PlayerMySelf.h"
#include "PlayerOtherMgr.h"
#include "UIInventory.h"
#include "UISkillTreeDlg.h"

#include <N3Base/N3UIImage.h>
#include <N3Base/N3UIString.h>

#include <algorithm>

namespace
{

constexpr D3DCOLOR COLOR_LABEL    = 0xfffae6af;
constexpr D3DCOLOR COLOR_VALUE    = 0xffffffff;
constexpr D3DCOLOR COLOR_SELECTED = 0xffffffff;
constexpr D3DCOLOR COLOR_STATUS   = 0xff80ff80;

// Layout, from the window's top left.
constexpr int WIDTH               = 620;
constexpr int TAB_TOP             = 38;
constexpr int TAB_GAP             = 3;
constexpr int CONTENT_TOP         = 78;
constexpr int CONTENT_BOTTOM      = CONTENT_TOP + 330;
constexpr int ROW_PITCH           = 30;
constexpr int LABEL_LEFT          = 24;
constexpr int LABEL_RIGHT         = 300;
constexpr int ARROW_WIDTH         = 32;
constexpr int ARROW_HEIGHT        = 17;
constexpr int PREV_LEFT           = 306;
constexpr int BOX_LEFT            = PREV_LEFT + ARROW_WIDTH + 4;
constexpr int BOX_RIGHT           = 560;
constexpr int NEXT_LEFT           = BOX_RIGHT + 4;
constexpr int ROW_HEIGHT          = 26;
constexpr int SLOT_SIZE           = 40;
constexpr int SLOT_PITCH          = 46;
constexpr int STATUS_TOP          = CONTENT_BOTTOM + 4;
constexpr int BUTTON_TOP          = STATUS_TOP + 26;
constexpr int HEIGHT              = BUTTON_TOP + UIArt::BAR_HEIGHT + 18;
constexpr float ICON_UV           = 45.0f / 64.0f;
constexpr int PICKER_ROWS         = 9;

const char* const PAGE_NAMES[]    = { "Attack", "Recovery", "Loot", "Supplies", "Town" };

// 1234567 -> "1,234,567"
std::string FormatNumber(int64_t iValue)
{
	std::string szDigits = std::to_string(iValue < 0 ? -iValue : iValue);
	std::string szResult;
	for (size_t i = 0; i < szDigits.size(); i++)
	{
		if (i > 0 && (szDigits.size() - i) % 3 == 0)
			szResult += ',';
		szResult += szDigits[i];
	}

	return iValue < 0 ? "-" + szResult : szResult;
}

std::string ItemName(int iItemID)
{
	__TABLE_ITEM_BASIC* pItem = iItemID > 0 ? CGameBase::s_pTbl_Items_Basic.Find(iItemID / 1000 * 1000) : nullptr;
	return pItem != nullptr ? pItem->szName : fmt::format("Item {}", iItemID);
}

} // namespace

CUIGenie::CUIGenie()
{
	SetToggleKey(DIK_G);
}

CUIGenie::~CUIGenie() = default;

CGenie* CUIGenie::GetGenie()
{
	return CGameProcedure::s_pProcMain != nullptr ? CGameProcedure::s_pProcMain->m_pGenie : nullptr;
}

Genie::Settings& CUIGenie::Settings()
{
	return GetGenie()->Settings();
}

std::array<int, 8>& CUIGenie::SkillsOf(SkillKind eKind)
{
	switch (eKind)
	{
		case SkillKind::Heal:
			return Settings().healSkills;
		case SkillKind::Buff:
			return Settings().buffSkills;
		default:
			return Settings().attackSkills;
	}
}

void CUIGenie::Track(PAGE ePage, CN3UIBase* pWidget)
{
	m_PageWidgets[ePage].push_back(pWidget);
}

void CUIGenie::AddText(PAGE ePage, int iTop, const std::string& szText, CN3UIString** ppString)
{
	CN3UIString* pString = UIArt::AddString(
		this, UIArt::MakeRect(LABEL_LEFT, iTop, WIDTH - LABEL_LEFT, iTop + ROW_HEIGHT), szText, 11, true, COLOR_LABEL);
	Track(ePage, pString);
	if (ppString != nullptr)
		*ppString = pString;
}

void CUIGenie::AddRow(PAGE ePage, int& iTop, Setting setting)
{
	m_Settings.push_back(std::move(setting));

	Row row;
	row.pSetting        = &m_Settings.back();
	const int iArrowTop = iTop + (ROW_HEIGHT - ARROW_HEIGHT) / 2;
	const RECT rcBox    = UIArt::MakeRect(BOX_LEFT, iTop, BOX_RIGHT, iTop + ROW_HEIGHT);

	Track(ePage, UIArt::AddString(this, UIArt::MakeRect(LABEL_LEFT, iTop, LABEL_RIGHT, iTop + ROW_HEIGHT), row.pSetting->szLabel, 11, true,
					 COLOR_LABEL));
	row.pPrev = CUIArtButton::Create(
		this, UIArt::MakeRect(PREV_LEFT, iArrowTop, PREV_LEFT + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_LEFT);
	Track(ePage, row.pPrev);
	for (CN3UIImage* pImage : UIArt::AddBar(this, rcBox, UIArt::BAR_DARK))
		Track(ePage, pImage);

	// The value is a button without pictures over its box.
	row.pBox = new CN3UIButton();
	row.pBox->Init(this);
	row.pBox->SetRegion(rcBox);
	Track(ePage, row.pBox);
	row.pValue = UIArt::AddString(this, rcBox, "", 10, true, COLOR_VALUE, UISTYLE_STRING_ALIGNCENTER);
	Track(ePage, row.pValue);
	row.pNext = CUIArtButton::Create(
		this, UIArt::MakeRect(NEXT_LEFT, iArrowTop, NEXT_LEFT + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_RIGHT);
	Track(ePage, row.pNext);

	m_Rows.push_back(row);
	iTop += ROW_PITCH;
}

void CUIGenie::AddSkillSlots(PAGE ePage, int& iTop, const std::string& szLabel, SkillKind eKind)
{
	AddText(ePage, iTop, szLabel);
	iTop += ROW_HEIGHT;

	SkillSlots slots;
	slots.eKind = eKind;
	for (int i = 0; i < 8; i++)
	{
		const int iLeft = LABEL_LEFT + i * SLOT_PITCH;
		const RECT rc   = UIArt::MakeRect(iLeft, iTop, iLeft + SLOT_SIZE, iTop + SLOT_SIZE);
		Track(ePage, UIArt::AddImage(this, rc, UIArt::SLOT));

		slots.icons[i] = new CN3UIImage();
		slots.icons[i]->Init(this);
		slots.icons[i]->SetRegion(UIArt::MakeRect(iLeft + 3, iTop + 3, iLeft + SLOT_SIZE - 3, iTop + SLOT_SIZE - 3));
		Track(ePage, slots.icons[i]);

		slots.buttons[i] = new CN3UIButton();
		slots.buttons[i]->Init(this);
		slots.buttons[i]->SetRegion(rc);
		Track(ePage, slots.buttons[i]);
	}

	m_SkillSlots.push_back(slots);
	iTop += SLOT_SIZE + 8;
}

void CUIGenie::Build()
{
	BuildFrame(WIDTH, HEIGHT, "Genie");

	// Tabs.
	const int iTabsLeft = (WIDTH - (PAGE_COUNT * UIArt::BAR_WIDTH + (PAGE_COUNT - 1) * TAB_GAP)) / 2;
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		const int iLeft     = iTabsLeft + i * (UIArt::BAR_WIDTH + TAB_GAP);
		CN3UIString* pLabel = nullptr;
		m_Tabs.push_back(CUIArtButton::Create(
			this, UIArt::MakeRect(iLeft, TAB_TOP, iLeft + UIArt::BAR_WIDTH, TAB_TOP + UIArt::BAR_HEIGHT), PAGE_NAMES[i], &pLabel));
		m_TabLabels.push_back(pLabel);
	}

	// Choices the rows share.
	auto onOff    = [] { return std::vector<Choice> { { "Off", 0 }, { "On", 1 } }; };
	auto percents = []
	{
		std::vector<Choice> choices;
		for (int i = 5; i <= 95; i += 5)
			choices.push_back({ fmt::format("{}%", i), i });
		return choices;
	};
	auto numbers = [](std::vector<int> values, std::string szUnit)
	{
		return [values = std::move(values), szUnit = std::move(szUnit)]
		{
			std::vector<Choice> choices;
			for (int iValue : values)
				choices.push_back({ FormatNumber(iValue) + szUnit, iValue });
			return choices;
		};
	};
	auto toggle = [&onOff](const char* szLabel, bool Genie::Settings::* pMember) -> Setting
	{
		return { szLabel, onOff, [pMember] { return Settings().*pMember ? 1 : 0; },
			[pMember](int iValue) { Settings().*pMember = iValue != 0; } };
	};
	auto lootToggle = [&onOff](const char* szLabel, bool Genie::LootFilter::* pMember) -> Setting
	{
		return { szLabel, onOff, [pMember] { return Settings().loot.*pMember ? 1 : 0; },
			[pMember](int iValue) { Settings().loot.*pMember = iValue != 0; } };
	};
	auto townToggle = [&onOff](const char* szLabel, bool Genie::TownRules::* pMember) -> Setting
	{
		return { szLabel, onOff, [pMember] { return Settings().town.*pMember ? 1 : 0; },
			[pMember](int iValue) { Settings().town.*pMember = iValue != 0; } };
	};
	auto number = [](const char* szLabel, std::function<std::vector<Choice>()> fnChoices, int Genie::Settings::* pMember) -> Setting
	{
		return { szLabel, std::move(fnChoices), [pMember] { return Settings().*pMember; },
			[pMember](int iValue) { Settings().*pMember = iValue; } };
	};

	// The potions in the bag and the saved potion merchant's, by name; -1 for off.
	auto potionChoices = []
	{
		std::vector<Choice> choices { { "Off", -1 } };
		auto add = [&choices](int iItemID)
		{
			if (std::none_of(choices.begin(), choices.end(), [iItemID](const Choice& choice) { return choice.iValue == iItemID; }))
				choices.push_back({ ItemName(iItemID), iItemID });
		};
		for (__IconItemSkill* pItem : CGameProcedure::s_pProcMain->m_pUIInventory->m_pMyInvWnd)
		{
			if (pItem != nullptr && pItem->pItemBasic != nullptr && pItem->pItemBasic->byClass == ITEM_CLASS_POTION)
				add(pItem->GetItemID());
		}

		const Genie::Merchant& merchant = Settings().potionMerchant;
		if (merchant.IsSet())
		{
			const int iSize = CGameBase::s_pTbl_Items_Basic.GetSize();
			for (int i = 0; i < iSize; i++)
			{
				__TABLE_ITEM_BASIC* pItem = CGameBase::s_pTbl_Items_Basic.GetIndexedData(i);
				if (pItem != nullptr && pItem->bySellGroup == merchant.iTradeID / 1000 && pItem->byClass == ITEM_CLASS_POTION)
					add(static_cast<int>(pItem->dwID) + merchant.iTradeID % 1000);
			}
		}

		return choices;
	};
	auto potionRow = [&potionChoices](const char* szLabel, bool Genie::Settings::* pOn, int Genie::Settings::* pID) -> Setting
	{
		return { szLabel, potionChoices, [pOn, pID] { return Settings().*pOn ? Settings().*pID : -1; },
			[pOn, pID](int iValue)
			{
				Settings().*pOn = iValue >= 0;
				if (iValue > 0)
					Settings().*pID = iValue;
			} };
	};

	// Attack
	int iTop = CONTENT_TOP;
	AddRow(PAGE_ATTACK, iTop,
		{ "Movement", [] { return std::vector<Choice> { { "Go after monsters", 0 }, { "Stay put", 1 } }; },
			[] { return Settings().bStayPut ? 1 : 0; }, [](int iValue) { Settings().bStayPut = iValue != 0; } });
	AddRow(PAGE_ATTACK, iTop, toggle("Basic attack", &Genie::Settings::bBasicAttack));
	AddRow(PAGE_ATTACK, iTop,
		number("Hunting range (when moving)", numbers({ 3, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80 }, " m"), &Genie::Settings::iRange));
	AddSkillSlots(PAGE_ATTACK, iTop, "Attack skills (the first one ready is used)", SkillKind::Attack);
	AddText(PAGE_ATTACK, iTop, "Monsters to hunt (none: any monster)");
	iTop += ROW_HEIGHT;
	for (int i = 0; i < 8; i++)
	{
		const int iLeft   = LABEL_LEFT + (i % 2) * 290;
		const int iRowTop = iTop + (i / 2) * ROW_HEIGHT;
		m_MonsterNames[i] = UIArt::AddString(
			this, UIArt::MakeRect(iLeft + 28, iRowTop, iLeft + 270, iRowTop + ROW_HEIGHT), "", 10, true, COLOR_VALUE);
		Track(PAGE_ATTACK, m_MonsterNames[i]);
		m_MonsterRemove[i] = CUIArtButton::Create(this, UIArt::MakeRect(iLeft, iRowTop + 1, iLeft + 23, iRowTop + 25), UIArt::CLOSE);
		Track(PAGE_ATTACK, m_MonsterRemove[i]);
	}
	iTop            += 4 * ROW_HEIGHT + 4;
	m_pBtnAddTarget  = CUIArtButton::Create(
        this, UIArt::MakeRect(LABEL_LEFT, iTop, LABEL_LEFT + UIArt::BAR_WIDTH, iTop + UIArt::BAR_HEIGHT), "Add target");
	Track(PAGE_ATTACK, m_pBtnAddTarget);

	// Recovery
	iTop = CONTENT_TOP;
	AddRow(PAGE_RECOVERY, iTop, potionRow("HP potion", &Genie::Settings::bHpPotion, &Genie::Settings::iHpPotionID));
	AddRow(PAGE_RECOVERY, iTop, number("Drink it below", percents, &Genie::Settings::iHpPercent));
	AddRow(PAGE_RECOVERY, iTop, potionRow("MP potion", &Genie::Settings::bMpPotion, &Genie::Settings::iMpPotionID));
	AddRow(PAGE_RECOVERY, iTop, number("Drink it below", percents, &Genie::Settings::iMpPercent));
	AddRow(PAGE_RECOVERY, iTop, number("Heal below", percents, &Genie::Settings::iHealPercent));
	AddSkillSlots(PAGE_RECOVERY, iTop, "Heal skills", SkillKind::Heal);
	AddSkillSlots(PAGE_RECOVERY, iTop, "Buffs (cast again when they wear off)", SkillKind::Buff);

	// Loot
	iTop = CONTENT_TOP;
	AddRow(PAGE_LOOT, iTop, lootToggle("Potions, scrolls, arrows", &Genie::LootFilter::bConsumables));
	AddRow(PAGE_LOOT, iTop, lootToggle("Equipment", &Genie::LootFilter::bEquipment));
	AddRow(PAGE_LOOT, iTop, lootToggle("Other items", &Genie::LootFilter::bOther));
	AddRow(PAGE_LOOT, iTop, lootToggle("Normal equipment", &Genie::LootFilter::bNormal));
	AddRow(PAGE_LOOT, iTop, lootToggle("Magic equipment", &Genie::LootFilter::bMagic));
	AddRow(PAGE_LOOT, iTop, lootToggle("Rare equipment", &Genie::LootFilter::bRare));
	AddRow(PAGE_LOOT, iTop, lootToggle("Unique equipment", &Genie::LootFilter::bUnique));
	AddRow(PAGE_LOOT, iTop,
		{ "Only items that sell for at least", numbers({ 0, 100, 500, 1000, 5000, 10000, 50000, 100000 }, " coins"),
			[] { return Settings().loot.iMinSellPrice; }, [](int iValue) { Settings().loot.iMinSellPrice = iValue; } });
	AddText(PAGE_LOOT, iTop + 4, "Coins are always picked up.");

	// Supplies
	iTop = CONTENT_TOP;
	AddRow(PAGE_SUPPLIES, iTop, toggle("Repair gear", &Genie::Settings::bRepair));
	AddRow(PAGE_SUPPLIES, iTop, number("Repair below", percents, &Genie::Settings::iRepairPercent));
	AddRow(PAGE_SUPPLIES, iTop, toggle("Buy potions", &Genie::Settings::bBuyPotions));
	AddRow(PAGE_SUPPLIES, iTop,
		number("Keep HP potions", numbers({ 0, 10, 20, 30, 50, 100, 200, 500, 1000 }, ""), &Genie::Settings::iKeepHpPotions));
	AddRow(PAGE_SUPPLIES, iTop,
		number("Keep MP potions", numbers({ 0, 10, 20, 30, 50, 100, 200, 500, 1000 }, ""), &Genie::Settings::iKeepMpPotions));
	AddRow(PAGE_SUPPLIES, iTop, toggle("Buy arrows (bows)", &Genie::Settings::bBuyArrows));
	AddRow(PAGE_SUPPLIES, iTop,
		{ "Arrows",
			[]
			{
				std::vector<Choice> choices { { "The bow's own", 0 } };
				for (__IconItemSkill* pItem : CGameProcedure::s_pProcMain->m_pUIInventory->m_pMyInvWnd)
				{
					if (pItem == nullptr || pItem->pItemBasic == nullptr || pItem->pItemBasic->byClass != ITEM_CLASS_ARROW)
						continue;

					const int iItemID = pItem->GetItemID();
					if (std::none_of(choices.begin(), choices.end(), [iItemID](const Choice& choice) { return choice.iValue == iItemID; }))
						choices.push_back({ ItemName(iItemID), iItemID });
				}
				return choices;
			},
			[] { return Settings().iArrowID; }, [](int iValue) { Settings().iArrowID = iValue; } });
	AddRow(PAGE_SUPPLIES, iTop, number("Keep arrows", numbers({ 0, 100, 200, 500, 1000, 2000, 5000 }, ""), &Genie::Settings::iKeepArrows));
	AddRow(PAGE_SUPPLIES, iTop,
		{ "Never spend below", numbers({ 0, 10000, 100000, 1000000, 10000000, 100000000 }, " coins"),
			[] { return static_cast<int>(Settings().iCoinFloor); }, [](int iValue) { Settings().iCoinFloor = iValue; } });
	AddText(PAGE_SUPPLIES, iTop + 2, "", &m_pPotionMerchant);
	AddText(PAGE_SUPPLIES, iTop + 2 + 22, "", &m_pArrowMerchant);

	// Town
	iTop = CONTENT_TOP;
	AddText(PAGE_TOWN, iTop, "Go to town (/town) and stop when...");
	iTop += ROW_HEIGHT + 4;
	AddRow(PAGE_TOWN, iTop, townToggle("The bag is full", &Genie::TownRules::bBagFull));
	AddRow(PAGE_TOWN, iTop, townToggle("Out of HP potions", &Genie::TownRules::bNoHpPotions));
	AddRow(PAGE_TOWN, iTop, townToggle("Out of MP potions", &Genie::TownRules::bNoMpPotions));
	AddRow(PAGE_TOWN, iTop, townToggle("Out of arrows", &Genie::TownRules::bNoArrows));
	AddText(PAGE_TOWN, iTop + 4, "Potions and arrows Genie can still buy don't count as out.");

	BuildPicker();

	// Bottom: the status line, Start/Stop and Close.
	m_pStatus       = UIArt::AddString(this, UIArt::MakeRect(LABEL_LEFT, STATUS_TOP, WIDTH - LABEL_LEFT, STATUS_TOP + 22), "", 10, true,
			  COLOR_STATUS, UISTYLE_STRING_ALIGNCENTER);
	const int iLeft = WIDTH / 2 - UIArt::BAR_WIDTH - 3;
	m_pBtnStart     = CUIArtButton::Create(
        this, UIArt::MakeRect(iLeft, BUTTON_TOP, iLeft + UIArt::BAR_WIDTH, BUTTON_TOP + UIArt::BAR_HEIGHT), "Start");
	m_pBtnClose = CUIArtButton::Create(
		this, UIArt::MakeRect(WIDTH / 2 + 3, BUTTON_TOP, WIDTH / 2 + 3 + UIArt::BAR_WIDTH, BUTTON_TOP + UIArt::BAR_HEIGHT), "Close");
}

void CUIGenie::BuildPicker()
{
	auto track     = [this](CN3UIBase* pWidget) { m_PickerWidgets.push_back(pWidget); };

	m_pPickerTitle = UIArt::AddString(
		this, UIArt::MakeRect(LABEL_LEFT, CONTENT_TOP, WIDTH - LABEL_LEFT, CONTENT_TOP + ROW_HEIGHT), "", 11, true, COLOR_LABEL);
	track(m_pPickerTitle);
	for (int i = 0; i < PICKER_ROWS; i++)
	{
		const int iTop = CONTENT_TOP + ROW_HEIGHT + 4 + i * ROW_PITCH;
		const RECT rc  = UIArt::MakeRect(LABEL_LEFT, iTop, WIDTH - LABEL_LEFT, iTop + ROW_HEIGHT);
		for (CN3UIImage* pImage : UIArt::AddBar(this, rc, UIArt::BAR_DARK))
			track(pImage);
		m_PickerNames[i] = UIArt::AddString(this, UIArt::OffsetBy(rc, 10, 0), "", 10, true, COLOR_VALUE);
		track(m_PickerNames[i]);
		m_PickerEntries[i] = new CN3UIButton();
		m_PickerEntries[i]->Init(this);
		m_PickerEntries[i]->SetRegion(rc);
		track(m_PickerEntries[i]);
	}

	const int iNavTop   = CONTENT_BOTTOM - UIArt::BAR_HEIGHT - 4;
	const int iArrowTop = iNavTop + (UIArt::BAR_HEIGHT - ARROW_HEIGHT) / 2;
	m_pPickerPrev       = CUIArtButton::Create(
        this, UIArt::MakeRect(LABEL_LEFT, iArrowTop, LABEL_LEFT + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_LEFT);
	track(m_pPickerPrev);
	m_pPickerPage = UIArt::AddString(this, UIArt::MakeRect(LABEL_LEFT + 36, iNavTop, LABEL_LEFT + 116, iNavTop + UIArt::BAR_HEIGHT), "", 10,
		true, COLOR_VALUE, UISTYLE_STRING_ALIGNCENTER);
	track(m_pPickerPage);
	m_pPickerNext = CUIArtButton::Create(
		this, UIArt::MakeRect(LABEL_LEFT + 120, iArrowTop, LABEL_LEFT + 120 + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_RIGHT);
	track(m_pPickerNext);
	m_pPickerClear = CUIArtButton::Create(this,
		UIArt::MakeRect(
			WIDTH - LABEL_LEFT - 2 * UIArt::BAR_WIDTH - 6, iNavTop, WIDTH - LABEL_LEFT - UIArt::BAR_WIDTH - 6, iNavTop + UIArt::BAR_HEIGHT),
		"Clear slot");
	track(m_pPickerClear);
	m_pPickerBack = CUIArtButton::Create(
		this, UIArt::MakeRect(WIDTH - LABEL_LEFT - UIArt::BAR_WIDTH, iNavTop, WIDTH - LABEL_LEFT, iNavTop + UIArt::BAR_HEIGHT), "Back");
	track(m_pPickerBack);

	for (CN3UIBase* pWidget : m_PickerWidgets)
		pWidget->SetVisible(false);
}

void CUIGenie::Open()
{
	if (IsVisible())
		return;

	CGenie* pGenie = GetGenie();
	if (pGenie == nullptr)
		return;

	pGenie->LoadSettings();
	if (m_pBtnStart == nullptr)
		Build();

	SetPosCenter();
	m_pBtnStart->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnClose->SetState(UI_STATE_BUTTON_NORMAL);
	m_bPicking = false;
	ShowPage(m_iPage);
	CUIArtWindow::Open();
}

void CUIGenie::Close()
{
	if (!IsVisible())
		return;

	if (CGenie* pGenie = GetGenie())
		pGenie->SaveSettings();

	CUIArtWindow::Close();
}

void CUIGenie::Tick()
{
	CUIArtWindow::Tick();
	if (IsVisible())
		UpdateControls();
}

void CUIGenie::ShowPage(int iPage)
{
	m_iPage = std::clamp(iPage, 0, PAGE_COUNT - 1);
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		m_Tabs[i]->SetState(UI_STATE_BUTTON_NORMAL);
		m_Tabs[i]->SetSelected(i == m_iPage);
		m_TabLabels[i]->SetColor(i == m_iPage ? COLOR_SELECTED : UIArt::COLOR_BUTTON);
		for (CN3UIBase* pWidget : m_PageWidgets[i])
			pWidget->SetVisible(i == m_iPage && !m_bPicking);
	}

	for (CN3UIBase* pWidget : m_PickerWidgets)
		pWidget->SetVisible(m_bPicking);

	Refresh();
}

void CUIGenie::RefreshRow(const Row& row)
{
	const std::vector<Choice> choices = row.pSetting->fnChoices();
	const int iValue                  = row.pSetting->fnGet();
	auto it = std::find_if(choices.begin(), choices.end(), [iValue](const Choice& choice) { return choice.iValue == iValue; });
	if (it != choices.end())
		row.pValue->SetString(it->szName);
	else if (iValue == 0)
		row.pValue->SetString("Choose one");
	else
		row.pValue->SetString(iValue > 100000000 ? ItemName(iValue) : FormatNumber(iValue));
}

void CUIGenie::Refresh()
{
	for (const Row& row : m_Rows)
		RefreshRow(row);

	for (const SkillSlots& slots : m_SkillSlots)
	{
		const std::array<int, 8>& skills = SkillsOf(slots.eKind);
		const PAGE eOwner                = slots.eKind == SkillKind::Attack ? PAGE_ATTACK : PAGE_RECOVERY;
		for (int i = 0; i < 8; i++)
		{
			const int iSkillID = skills[i];
			if (iSkillID > 0)
			{
				slots.icons[i]->SetTex(fmt::format("UI\\skillicon_{:02}_{}.dxt", iSkillID % 100, iSkillID / 100));
				slots.icons[i]->SetUVRect(0, 0, ICON_UV, ICON_UV);
			}
			slots.icons[i]->SetVisible(iSkillID > 0 && m_iPage == eOwner && !m_bPicking);
		}
	}

	const std::vector<std::string>& monsters = Settings().monsters;
	for (int i = 0; i < 8; i++)
	{
		const bool bUsed = i < static_cast<int>(monsters.size());
		m_MonsterNames[i]->SetString(bUsed ? monsters[i] : (i == 0 ? "Any monster" : ""));
		m_MonsterRemove[i]->SetVisible(bUsed && m_iPage == PAGE_ATTACK && !m_bPicking);
	}

	// "Potions are bought from Pabian."
	auto merchantLine = [](const Genie::Merchant& merchant, const char* szThings, const char* szThing, const char* szSentence)
	{
		if (!merchant.IsSet())
			return fmt::format("No {} merchant yet: open a shop that sells {} once.", szThing, szThings);

		const std::string szFrom = merchant.szName.empty() ? fmt::format("the {} merchant you saved", szThing) : merchant.szName;
		return fmt::format("{} are bought from {}.", szSentence, szFrom);
	};
	std::string szPotions = merchantLine(Settings().potionMerchant, "potions", "potion", "Potions");
	std::string szArrows  = merchantLine(Settings().arrowMerchant, "arrows", "arrow", "Arrows");
	m_pPotionMerchant->SetString(szPotions);
	m_pArrowMerchant->SetString(szArrows);
}

void CUIGenie::Step(const Row& row, int iStep)
{
	const std::vector<Choice> choices = row.pSetting->fnChoices();
	if (choices.empty())
		return;

	const int iValue = row.pSetting->fnGet();
	auto it          = std::find_if(choices.begin(), choices.end(), [iValue](const Choice& choice) { return choice.iValue == iValue; });

	// From a value not among the choices, the first step lands on an end.
	const int iCount = static_cast<int>(choices.size());
	int iIndex       = iStep < 0 ? iCount - 1 : 0;
	if (it != choices.end())
		iIndex = (static_cast<int>(it - choices.begin()) + iStep + iCount) % iCount;

	row.pSetting->fnSet(choices[iIndex].iValue);
	Refresh();
}

void CUIGenie::UpdateControls()
{
	CGenie* pGenie = GetGenie();
	if (pGenie == nullptr || m_pBtnStart == nullptr)
		return;

	if (pGenie->IsRunning())
	{
		m_pBtnStart->SetLabel("Stop");
		m_pStatus->SetString("Genie is hunting. Moving by hand stops it.");
	}
	else if (pGenie->IsWaitingForStart())
	{
		m_pBtnStart->SetLabel("Starting...");
		m_pStatus->SetString("Asking the server...");
	}
	else if (!pGenie->IsAvailable())
	{
		m_pBtnStart->SetLabel("Start");
		m_pStatus->SetString("This server doesn't allow Genie.");
	}
	else
	{
		m_pBtnStart->SetLabel("Start");
		m_pStatus->SetString("Genie hunts around where you start it.");
	}
}

std::vector<int> CUIGenie::PickerSkills() const
{
	// The character's learned skills, as the skill tree holds them, that suit the slot.
	std::vector<int> skills;
	CUISkillTreeDlg* pTree = CGameProcedure::s_pProcMain->m_pUISkillTreeDlg;
	for (const auto& kind : pTree->m_pMySkillTree)
	{
		for (const auto& page : kind)
		{
			for (__IconItemSkill* pIcon : page)
			{
				const __TABLE_UPC_SKILL* pSkill = pIcon != nullptr ? pIcon->pSkill : nullptr;
				if (pSkill == nullptr || std::find(skills.begin(), skills.end(), static_cast<int>(pSkill->dwID)) != skills.end())
					continue;

				const int iTarget  = pSkill->iTarget;
				const bool bOnSelf = iTarget == SKILLMAGIC_TARGET_SELF || iTarget == SKILLMAGIC_TARGET_FRIEND_WITHME
									 || iTarget == SKILLMAGIC_TARGET_PARTY || iTarget == SKILLMAGIC_TARGET_PARTY_ALL;
				bool bSuits = false;
				switch (m_ePickKind)
				{
					case SkillKind::Attack:
						bSuits = iTarget == SKILLMAGIC_TARGET_ENEMY_ONLY || iTarget == SKILLMAGIC_TARGET_AREA_ENEMY
								 || iTarget == SKILLMAGIC_TARGET_AREA_ALL || iTarget == SKILLMAGIC_TARGET_AREA;
						break;
					case SkillKind::Heal:
						bSuits = bOnSelf && pSkill->dw1stTableType == 3;
						break;
					case SkillKind::Buff:
						bSuits = bOnSelf && pSkill->dw1stTableType == 4;
						break;
				}

				if (bSuits)
					skills.push_back(static_cast<int>(pSkill->dwID));
			}
		}
	}

	return skills;
}

void CUIGenie::OpenPicker(SkillKind eKind, int iSlot)
{
	m_bPicking   = true;
	m_ePickKind  = eKind;
	m_iPickSlot  = iSlot;
	m_PickSkills = PickerSkills();
	m_pPickerTitle->SetString(
		m_PickSkills.empty() ? "No learned skills fit this slot." : fmt::format("Choose a skill for slot {}", iSlot + 1));
	m_pPickerClear->SetState(UI_STATE_BUTTON_NORMAL);
	m_pPickerBack->SetState(UI_STATE_BUTTON_NORMAL);
	ShowPage(m_iPage);
	ShowPickerPage(0);
}

void CUIGenie::ClosePicker()
{
	m_bPicking = false;
	ShowPage(m_iPage);
}

void CUIGenie::ShowPickerPage(int iPage)
{
	const int iPages = std::max(1, (static_cast<int>(m_PickSkills.size()) + PICKER_ROWS - 1) / PICKER_ROWS);
	m_iPickPage      = std::clamp(iPage, 0, iPages - 1);
	m_pPickerPage->SetString(fmt::format("{} / {}", m_iPickPage + 1, iPages));
	for (int i = 0; i < PICKER_ROWS; i++)
	{
		const int iIndex          = m_iPickPage * PICKER_ROWS + i;
		const bool bUsed          = iIndex < static_cast<int>(m_PickSkills.size());
		__TABLE_UPC_SKILL* pSkill = bUsed ? CGameBase::s_pTbl_Skill.Find(m_PickSkills[iIndex]) : nullptr;
		m_PickerNames[i]->SetString(pSkill != nullptr ? pSkill->szName : "");
		m_PickerEntries[i]->SetVisible(bUsed);
	}
}

bool CUIGenie::ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg)
{
	if (CUIArtWindow::ReceiveMessage(pSender, dwMsg))
		return true;

	if (dwMsg != UIMSG_BUTTON_CLICK)
		return false;

	CGenie* pGenie = GetGenie();
	if (pGenie == nullptr)
		return false;

	if (pSender == m_pBtnClose)
	{
		Close();
		return true;
	}

	if (pSender == m_pBtnStart)
	{
		if (pGenie->IsRunning())
			pGenie->Stop(Genie::StopReason::ByPlayer);
		else
			pGenie->RequestStart();
		UpdateControls();
		return true;
	}

	if (m_bPicking)
	{
		if (pSender == m_pPickerBack)
		{
			ClosePicker();
			return true;
		}

		if (pSender == m_pPickerClear)
		{
			SkillsOf(m_ePickKind)[m_iPickSlot] = 0;
			ClosePicker();
			return true;
		}

		if (pSender == m_pPickerPrev || pSender == m_pPickerNext)
		{
			ShowPickerPage(m_iPickPage + (pSender == m_pPickerNext ? 1 : -1));
			return true;
		}

		for (int i = 0; i < PICKER_ROWS; i++)
		{
			const int iIndex = m_iPickPage * PICKER_ROWS + i;
			if (pSender == m_PickerEntries[i] && iIndex < static_cast<int>(m_PickSkills.size()))
			{
				SkillsOf(m_ePickKind)[m_iPickSlot] = m_PickSkills[iIndex];
				ClosePicker();
				return true;
			}
		}

		return false;
	}

	for (int i = 0; i < PAGE_COUNT; i++)
	{
		if (pSender == m_Tabs[i])
		{
			ShowPage(i);
			return true;
		}
	}

	for (const Row& row : m_Rows)
	{
		if (pSender == row.pPrev)
			Step(row, -1);
		else if (pSender == row.pNext || pSender == row.pBox)
			Step(row, 1);
		else
			continue;

		return true;
	}

	for (const SkillSlots& slots : m_SkillSlots)
	{
		for (int i = 0; i < 8; i++)
		{
			if (pSender == slots.buttons[i])
			{
				OpenPicker(slots.eKind, i);
				return true;
			}
		}
	}

	std::vector<std::string>& monsters = Settings().monsters;
	for (int i = 0; i < 8; i++)
	{
		if (pSender == m_MonsterRemove[i] && i < static_cast<int>(monsters.size()))
		{
			monsters.erase(monsters.begin() + i);
			Refresh();
			return true;
		}
	}

	if (pSender == m_pBtnAddTarget)
	{
		// The current target, if it's a monster (no nation, not an object).
		CPlayerNPC* pTarget = CGameProcedure::s_pOPMgr->NPCGetByID(CGameProcedure::s_pPlayer->m_iIDTarget, false);
		if (pTarget != nullptr && pTarget->m_InfoBase.eNation == NATION_NOTSELECTED && pTarget->m_pShapeExtraRef == nullptr
			&& monsters.size() < Genie::MONSTER_SLOTS && std::find(monsters.begin(), monsters.end(), pTarget->IDString()) == monsters.end())
			monsters.push_back(pTarget->IDString());
		Refresh();
		return true;
	}

	return false;
}
