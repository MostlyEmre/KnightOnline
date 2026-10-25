#include "StdAfx.h"
#include "UICheats.h"
#include "Cheats.h"
#include "GameProcMain.h"
#include "IconItemSkill.h"
#include "PlayerMySelf.h"
#include "UIInventory.h"

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
constexpr int MASTER_TOP          = 78;
constexpr int CONTENT_TOP         = MASTER_TOP + 36;
constexpr int CONTENT_BOTTOM      = CONTENT_TOP + 390;
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
constexpr int BUTTON_PITCH        = UIArt::BAR_HEIGHT + 5;
constexpr int BUTTON_GAP          = 8;
constexpr int LINE_PITCH          = 22;
constexpr int LINE_HEIGHT         = 20;
constexpr int ITEM_LINES          = 8;
constexpr int NPC_LINES           = 9;
constexpr int STATUS_TOP          = CONTENT_BOTTOM + 4;
constexpr int BUTTON_TOP          = STATUS_TOP + 26;
constexpr int HEIGHT              = BUTTON_TOP + UIArt::BAR_HEIGHT + 18;
constexpr float ICON_UV           = 45.0f / 64.0f;
constexpr size_t SEARCH_LENGTH    = 30;

const char* const PAGE_NAMES[]    = { "Multipliers", "Character", "Items", "Gear", "World" };

// The equipped slots +levels can be set on, with the server's slot numbers.
const std::vector<std::pair<std::string, int>> SLOTS { { "Right hand", 6 }, { "Left hand", 8 }, { "Head", 1 }, { "Chest", 4 },
	{ "Legs", 10 }, { "Gloves", 12 }, { "Feet", 13 } };

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

std::string ZoneName(int iZone)
{
	for (const Cheats::Zone& zone : Cheats::ZONES)
	{
		if (zone.sId == iZone)
			return zone.szName;
	}

	return fmt::format("zone {}", iZone);
}

// An item's tables, found as the anvil finds them.
bool FindItem(uint32_t dwID, __TABLE_ITEM_BASIC** ppBasic, __TABLE_ITEM_EXT** ppExt)
{
	*ppBasic = CGameBase::s_pTbl_Items_Basic.Find(dwID / 1000 * 1000);
	*ppExt   = nullptr;
	if (*ppBasic != nullptr && (*ppBasic)->byExtIndex < MAX_ITEM_EXTENSION)
		*ppExt = CGameBase::s_pTbl_Items_Exts[(*ppBasic)->byExtIndex].Find(dwID % 1000);

	return *ppBasic != nullptr && *ppExt != nullptr;
}

std::string IconOf(__TABLE_ITEM_BASIC* pBasic, __TABLE_ITEM_EXT* pExt)
{
	std::string szIconFN;
	e_PartPosition ePart = PART_POS_UNKNOWN;
	e_PlugPosition ePlug = PLUG_POS_UNKNOWN;
	if (CGameBase::MakeResrcFileNameForUPC(pBasic, pExt, nullptr, &szIconFN, ePart, ePlug, RACE_UNKNOWN) == ITEM_TYPE_UNKNOWN)
		return {};

	return szIconFN;
}

std::string VariantName(uint32_t dwID)
{
	__TABLE_ITEM_BASIC* pBasic = nullptr;
	__TABLE_ITEM_EXT* pExt     = nullptr;
	if (!FindItem(dwID, &pBasic, &pExt))
		return fmt::format("Item {}", dwID);

	return Cheats::VariantLabel(pBasic->szName, static_cast<int>(dwID % 1000), pExt->szHeader, pExt->byMagicOrRare);
}

} // namespace

CUICheats::CUICheats()
{
	SetToggleKey(DIK_J);
}

CUICheats::~CUICheats() = default;

CCheats* CUICheats::GetCheats()
{
	return CGameProcedure::s_pProcMain != nullptr ? CGameProcedure::s_pProcMain->m_pCheats : nullptr;
}

void CUICheats::ChangeSettings(const std::function<void(Cheats::Settings&)>& fnChange)
{
	CCheats* pCheats = GetCheats();
	if (pCheats == nullptr)
		return;

	Cheats::Settings settings = pCheats->Confirmed();
	fnChange(settings);
	pCheats->SendSettings(settings);
}

void CUICheats::Track(PAGE ePage, CN3UIBase* pWidget)
{
	if (ePage != PAGE_EVERY)
		m_PageWidgets[ePage].push_back(pWidget);
}

CN3UIString* CUICheats::AddText(PAGE ePage, int iTop, const std::string& szText)
{
	CN3UIString* pString = UIArt::AddString(
		this, UIArt::MakeRect(LABEL_LEFT, iTop, WIDTH - LABEL_LEFT, iTop + ROW_HEIGHT), szText, 11, true, COLOR_LABEL);
	Track(ePage, pString);
	return pString;
}

void CUICheats::AddRow(PAGE ePage, int& iTop, Setting setting)
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

CUIArtButton* CUICheats::AddButton(PAGE ePage, int iLeft, int iTop, int iWidth, const std::string& szLabel)
{
	CUIArtButton* pButton = CUIArtButton::Create(this, UIArt::MakeRect(iLeft, iTop, iLeft + iWidth, iTop + UIArt::BAR_HEIGHT), szLabel);
	Track(ePage, pButton);
	return pButton;
}

CUIArtEdit* CUICheats::AddEdit(PAGE ePage, int iTop, int iRight)
{
	const RECT rc = UIArt::MakeRect(LABEL_LEFT, iTop, iRight, iTop + ROW_HEIGHT);
	for (CN3UIImage* pImage : UIArt::AddBar(this, rc, UIArt::BAR_DARK))
		Track(ePage, pImage);

	CUIArtEdit* pEdit = CUIArtEdit::Create(this, rc, SEARCH_LENGTH);
	Track(ePage, pEdit);
	return pEdit;
}

void CUICheats::AddList(PAGE ePage, int& iTop, int iLines, bool bIcons, std::vector<ListLine>& lines)
{
	for (int i = 0; i < iLines; i++)
	{
		const RECT rc = UIArt::MakeRect(LABEL_LEFT, iTop, WIDTH - LABEL_LEFT, iTop + LINE_HEIGHT);
		ListLine line;
		for (CN3UIImage* pImage : UIArt::AddBar(this, rc, UIArt::BAR_DARK))
			Track(ePage, pImage);

		int iTextLeft = LABEL_LEFT + 8;
		if (bIcons)
		{
			line.pIcon = new CN3UIImage();
			line.pIcon->Init(this);
			line.pIcon->SetRegion(UIArt::MakeRect(LABEL_LEFT + 4, iTop + 1, LABEL_LEFT + 22, iTop + 19));
			Track(ePage, line.pIcon);
			iTextLeft = LABEL_LEFT + 28;
		}

		line.pName = UIArt::AddString(this, UIArt::MakeRect(iTextLeft, iTop, WIDTH - LABEL_LEFT - 8, iTop + LINE_HEIGHT), "", 10, true,
			COLOR_LABEL, UISTYLE_STRING_ALIGNLEFT | UISTYLE_STRING_ALIGNVCENTER);
		Track(ePage, line.pName);

		line.pButton = new CN3UIButton();
		line.pButton->Init(this);
		line.pButton->SetRegion(rc);
		Track(ePage, line.pButton);

		lines.push_back(line);
		iTop += LINE_PITCH;
	}
}

void CUICheats::Build()
{
	BuildFrame(WIDTH, HEIGHT, "Cheats");

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

	// The master switch, on every page.
	int iTop = MASTER_TOP;
	AddRow(PAGE_EVERY, iTop,
		{ "Cheats", [] { return std::vector<Choice> { { "Off", 0 }, { "On", 1 } }; }, [] { return GetCheats()->Confirmed().bOn ? 1 : 0; },
			[this](int iValue) { ChangeSettings([iValue](Cheats::Settings& s) { s.bOn = iValue != 0; }); } });

	BuildMultipliers();
	BuildCharacter();
	BuildItems();
	BuildGear();
	BuildWorld();

	m_pStatus   = UIArt::AddString(this, UIArt::MakeRect(LABEL_LEFT, STATUS_TOP, WIDTH - LABEL_LEFT, STATUS_TOP + ROW_HEIGHT), "", 11, true,
		  COLOR_STATUS, UISTYLE_STRING_ALIGNCENTER);
	m_pBtnClose = CUIArtButton::Create(this,
		UIArt::MakeRect((WIDTH - UIArt::BAR_WIDTH) / 2, BUTTON_TOP, (WIDTH + UIArt::BAR_WIDTH) / 2, BUTTON_TOP + UIArt::BAR_HEIGHT),
		"Close");
}

void CUICheats::BuildMultipliers()
{
	auto steps = [](bool bTaken)
	{
		std::vector<Choice> choices;
		if (bTaken)
		{
			for (uint16_t pct : Cheats::DAMAGE_TAKEN_STEPS)
				choices.push_back({ Cheats::MultiplierLabel(pct), pct });
		}
		else
		{
			for (uint16_t pct : Cheats::MULTIPLIER_STEPS)
				choices.push_back({ Cheats::MultiplierLabel(pct), pct });
		}
		return choices;
	};

	auto multiplier = [&](const char* szLabel, uint16_t Cheats::Settings::* pct, bool bTaken)
	{
		return Setting { szLabel, [steps, bTaken] { return steps(bTaken); },
			[pct] { return static_cast<int>(GetCheats()->Confirmed().*pct); },
			[this, pct](int iValue) { ChangeSettings([pct, iValue](Cheats::Settings& s) { s.*pct = static_cast<uint16_t>(iValue); }); } };
	};

	int iTop = CONTENT_TOP;
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("EXP gained", &Cheats::Settings::sExpPct, false));
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("Damage you deal", &Cheats::Settings::sDamageDealtPct, false));
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("Damage you take", &Cheats::Settings::sDamageTakenPct, true));
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("Coins from monsters", &Cheats::Settings::sCoinPct, false));
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("Item drop chance", &Cheats::Settings::sDropPct, false));
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("National Points gained", &Cheats::Settings::sLoyaltyPct, false));
	AddRow(PAGE_MULTIPLIERS, iTop, multiplier("Monster respawn speed", &Cheats::Settings::sRespawnPct, false));
}

void CUICheats::BuildCharacter()
{
	auto amounts = [](auto steps)
	{
		std::vector<Choice> choices;
		choices.reserve(steps.size());
		for (uint32_t value : steps)
			choices.push_back({ FormatNumber(value), static_cast<int>(value) });
		return choices;
	};

	int iTop = CONTENT_TOP;
	AddRow(PAGE_CHARACTER, iTop,
		{ "Level",
			[]
			{
				std::vector<Choice> choices;
				for (int i = Cheats::MIN_LEVEL; i <= Cheats::MAX_LEVEL; i++)
					choices.push_back({ std::to_string(i), i });
				return choices;
			},
			[this] { return m_iLevel; }, [this](int iValue) { m_iLevel = iValue; } });

	constexpr int iWide  = 170;
	m_pBtnLevelDown      = AddButton(PAGE_CHARACTER, LABEL_LEFT, iTop, UIArt::BAR_WIDTH, "Level -10");
	m_pBtnLevelUp        = AddButton(PAGE_CHARACTER, LABEL_LEFT + UIArt::BAR_WIDTH + BUTTON_GAP, iTop, UIArt::BAR_WIDTH, "Level +10");
	m_pBtnSetLevel       = AddButton(PAGE_CHARACTER, LABEL_LEFT + 2 * (UIArt::BAR_WIDTH + BUTTON_GAP), iTop, iWide, "Set level");
	iTop                += BUTTON_PITCH;

	m_pBtnStatPoints     = AddButton(PAGE_CHARACTER, LABEL_LEFT, iTop, iWide, "Add 10 stat points");
	m_pBtnSkillPts       = AddButton(PAGE_CHARACTER, LABEL_LEFT + iWide + BUTTON_GAP, iTop, iWide, "Add 10 skill points");
	iTop                += BUTTON_PITCH;

	m_pBtnResetStats     = AddButton(PAGE_CHARACTER, LABEL_LEFT, iTop, iWide, "Reset stats");
	m_pBtnResetSkill     = AddButton(PAGE_CHARACTER, LABEL_LEFT + iWide + BUTTON_GAP, iTop, iWide, "Reset skills");
	m_pBtnRefill         = AddButton(PAGE_CHARACTER, LABEL_LEFT + 2 * (iWide + BUTTON_GAP), iTop, iWide, "Refill HP and MP");
	iTop                += BUTTON_PITCH + 4;

	AddRow(PAGE_CHARACTER, iTop,
		{ "Infinite MP", [] { return std::vector<Choice> { { "Off", 0 }, { "On", 1 } }; },
			[] { return GetCheats()->Confirmed().bInfiniteMp ? 1 : 0; },
			[this](int iValue) { ChangeSettings([iValue](Cheats::Settings& s) { s.bInfiniteMp = iValue != 0; }); } });

	AddRow(PAGE_CHARACTER, iTop,
		{ "Coins", [amounts] { return amounts(Cheats::COIN_STEPS); }, [this] { return m_iCoins; },
			[this](int iValue) { m_iCoins = iValue; } });
	AddRow(PAGE_CHARACTER, iTop,
		{ "National Points", [amounts] { return amounts(Cheats::POINT_STEPS); }, [this] { return m_iLoyalty; },
			[this](int iValue) { m_iLoyalty = iValue; } });
	AddRow(PAGE_CHARACTER, iTop,
		{ "Knight Cash", [amounts] { return amounts(Cheats::POINT_STEPS); }, [this] { return m_iKnightCash; },
			[this](int iValue) { m_iKnightCash = iValue; } });

	m_pBtnSetCoins = AddButton(PAGE_CHARACTER, LABEL_LEFT, iTop, iWide, "Set coins");
	m_pBtnSetNP    = AddButton(PAGE_CHARACTER, LABEL_LEFT + iWide + BUTTON_GAP, iTop, iWide, "Set National Points");
	m_pBtnSetKC    = AddButton(PAGE_CHARACTER, LABEL_LEFT + 2 * (iWide + BUTTON_GAP), iTop, iWide, "Set Knight Cash");
}

void CUICheats::BuildItems()
{
	int iTop          = CONTENT_TOP;
	m_pItemSearch     = AddEdit(PAGE_ITEMS, iTop, 420);
	m_pBtnItemSearch  = AddButton(PAGE_ITEMS, 430, iTop - 1, 166, "Search");
	iTop             += BUTTON_PITCH;

	AddList(PAGE_ITEMS, iTop, ITEM_LINES, true, m_ItemLines);

	const int iArrowTop = iTop + (UIArt::BAR_HEIGHT - ARROW_HEIGHT) / 2;
	m_pItemPrev         = CUIArtButton::Create(
        this, UIArt::MakeRect(LABEL_LEFT, iArrowTop, LABEL_LEFT + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_LEFT);
	Track(PAGE_ITEMS, m_pItemPrev);
	m_pItemPage = UIArt::AddString(this, UIArt::MakeRect(LABEL_LEFT + 36, iTop, LABEL_LEFT + 116, iTop + UIArt::BAR_HEIGHT), "", 10, true,
		COLOR_VALUE, UISTYLE_STRING_ALIGNCENTER);
	Track(PAGE_ITEMS, m_pItemPage);
	m_pItemNext = CUIArtButton::Create(
		this, UIArt::MakeRect(LABEL_LEFT + 120, iArrowTop, LABEL_LEFT + 120 + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_RIGHT);
	Track(PAGE_ITEMS, m_pItemNext);
	m_pBtnItemBack  = AddButton(PAGE_ITEMS, WIDTH - LABEL_LEFT - 166, iTop, 166, "Back to results");
	iTop           += BUTTON_PITCH;

	m_pPicked       = AddText(PAGE_ITEMS, iTop, "");
	iTop           += ROW_HEIGHT;

	AddRow(PAGE_ITEMS, iTop,
		{ "How many",
			[this]
			{
				__TABLE_ITEM_BASIC* pBasic = nullptr;
				__TABLE_ITEM_EXT* pExt     = nullptr;
				const bool bCountable      = FindItem(m_dwPickedID, &pBasic, &pExt) && (pBasic->byContable == 1 || pBasic->byContable == 3);

				std::vector<Choice> choices;
				if (bCountable)
				{
					for (uint16_t count : Cheats::COUNTABLE_STEPS)
						choices.push_back({ FormatNumber(count), count });
				}
				else
				{
					for (uint16_t count : Cheats::SINGLE_STEPS)
						choices.push_back({ std::to_string(count), count });
				}
				return choices;
			},
			[this] { return m_iCount; }, [this](int iValue) { m_iCount = iValue; } });

	m_pBtnGive = AddButton(PAGE_ITEMS, LABEL_LEFT, iTop, 170, "Give");
}

void CUICheats::BuildGear()
{
	int iTop = CONTENT_TOP;
	AddRow(PAGE_GEAR, iTop,
		{ "Equipped item",
			[]
			{
				std::vector<Choice> choices;
				choices.reserve(SLOTS.size());
				for (const auto& [szName, iSlot] : SLOTS)
					choices.push_back({ szName, iSlot });
				return choices;
			},
			[this] { return m_iSlot; }, [this](int iValue) { m_iSlot = iValue; } });

	m_pSlotItem  = AddText(PAGE_GEAR, iTop, "");
	iTop        += ROW_HEIGHT + 4;

	AddRow(PAGE_GEAR, iTop,
		{ "+level",
			[]
			{
				std::vector<Choice> choices;
				for (int i = 0; i <= Cheats::MAX_PLUS; i++)
					choices.push_back({ fmt::format("+{}", i), i });
				return choices;
			},
			[this] { return m_iPlus; }, [this](int iValue) { m_iPlus = iValue; } });

	m_pBtnSetPlus  = AddButton(PAGE_GEAR, LABEL_LEFT, iTop, 170, "Set +level");
	iTop          += BUTTON_PITCH + 8;

	auto toggle    = [this](const char* szLabel, bool Cheats::Settings::* flag)
	{
		return Setting { szLabel, [] { return std::vector<Choice> { { "Off", 0 }, { "On", 1 } }; },
			[flag] { return GetCheats()->Confirmed().*flag ? 1 : 0; },
			[this, flag](int iValue) { ChangeSettings([flag, iValue](Cheats::Settings& s) { s.*flag = iValue != 0; }); } };
	};

	AddRow(PAGE_GEAR, iTop, toggle("No durability loss", &Cheats::Settings::bNoDurabilityLoss));
	AddRow(PAGE_GEAR, iTop, toggle("Arrows and potions aren't used up", &Cheats::Settings::bKeepConsumables));
	AddRow(PAGE_GEAR, iTop, toggle("No weight limit", &Cheats::Settings::bNoWeightLimit));
	AddRow(PAGE_GEAR, iTop, toggle("Shops are free", &Cheats::Settings::bFreeShops));

	m_pBtnRepair = AddButton(PAGE_GEAR, LABEL_LEFT, iTop + 4, 170, "Repair everything");
}

void CUICheats::BuildWorld()
{
	int iTop = CONTENT_TOP;
	AddRow(PAGE_WORLD, iTop,
		{ "Zone",
			[]
			{
				std::vector<Choice> choices;
				choices.reserve(Cheats::ZONES.size());
				for (const Cheats::Zone& zone : Cheats::ZONES)
					choices.push_back({ zone.szName, zone.sId });
				return choices;
			},
			[this] { return m_iZone; }, [this](int iValue) { m_iZone = iValue; } });

	m_pBtnTeleport   = AddButton(PAGE_WORLD, LABEL_LEFT, iTop, 170, "Teleport");
	iTop            += BUTTON_PITCH + 4;

	m_pNpcSearch     = AddEdit(PAGE_WORLD, iTop, 420);
	m_pBtnNpcSearch  = AddButton(PAGE_WORLD, 430, iTop - 1, 166, "Find NPC");
	iTop            += BUTTON_PITCH;

	AddList(PAGE_WORLD, iTop, NPC_LINES, false, m_NpcLines);

	m_pBtnGoToNpc     = AddButton(PAGE_WORLD, LABEL_LEFT, iTop, 170, "Go to the NPC");
	m_pBtnKillAround  = AddButton(PAGE_WORLD, LABEL_LEFT + 170 + BUTTON_GAP, iTop, 220, "Kill monsters around me");
	iTop             += BUTTON_PITCH + 4;

	AddRow(PAGE_WORLD, iTop,
		{ "Weather",
			[]
			{
				return std::vector<Choice> { { "Normal", 0 }, { "Clear", 1 }, { "Light rain", 2 }, { "Heavy rain", 3 }, { "Light snow", 4 },
					{ "Heavy snow", 5 } };
			},
			[] { return static_cast<int>(GetCheats()->Confirmed().eWeather); },
			[this](int iValue) { ChangeSettings([iValue](Cheats::Settings& s) { s.eWeather = static_cast<Cheats::Weather>(iValue); }); } });

	AddRow(PAGE_WORLD, iTop,
		{ "Time of day",
			[]
			{
				std::vector<Choice> choices { { "Normal", Cheats::HOUR_NORMAL } };
				for (int i = 0; i < 24; i++)
					choices.push_back({ fmt::format("{:02}:00", i), i });
				return choices;
			},
			[] { return static_cast<int>(GetCheats()->Confirmed().cHour); },
			[this](int iValue) { ChangeSettings([iValue](Cheats::Settings& s) { s.cHour = static_cast<int8_t>(iValue); }); } });
}

void CUICheats::Open()
{
	if (IsVisible() || GetCheats() == nullptr)
		return;

	if (m_pBtnClose == nullptr)
		Build();

	if (CGameBase::s_pPlayer != nullptr)
		m_iLevel = std::clamp(CGameBase::s_pPlayer->m_InfoBase.iLevel, Cheats::MIN_LEVEL, Cheats::MAX_LEVEL);

	SetPosCenter();
	m_pBtnClose->SetState(UI_STATE_BUTTON_NORMAL);
	m_Shown        = GetCheats()->Confirmed();
	m_iAnswersSeen = GetCheats()->AnswerCount();
	ShowPage(m_iPage);
	CUIArtWindow::Open();
}

void CUICheats::Close()
{
	CUIArtWindow::Close();
}

void CUICheats::Tick()
{
	CUIArtWindow::Tick();
	if (!IsVisible())
		return;

	CCheats* pCheats = GetCheats();
	if (pCheats == nullptr)
		return;

	// The server's answers: confirmed settings, an item's +levels, NPCs found.
	if (pCheats->Confirmed() != m_Shown)
	{
		m_Shown = pCheats->Confirmed();
		Refresh();
	}

	if (pCheats->AnswerCount() != m_iAnswersSeen)
	{
		m_iAnswersSeen = pCheats->AnswerCount();
		if (m_dwBaseID != 0 && pCheats->Variants(m_dwBaseID) != nullptr && m_VariantIDs.empty())
			BuildVariants();
		ShowNpcList();
	}

	if (m_iPage == PAGE_GEAR)
		RefreshSlotItem();

	UpdateStatus();
}

void CUICheats::ShowPage(int iPage)
{
	m_iPage = std::clamp(iPage, 0, PAGE_COUNT - 1);
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		m_Tabs[i]->SetState(UI_STATE_BUTTON_NORMAL);
		m_Tabs[i]->SetSelected(i == m_iPage);
		m_TabLabels[i]->SetColor(i == m_iPage ? COLOR_SELECTED : UIArt::COLOR_BUTTON);
		for (CN3UIBase* pWidget : m_PageWidgets[i])
			pWidget->SetVisible(i == m_iPage);
	}

	// A text field on a hidden page lets go of the keyboard.
	for (CUIArtEdit* pEdit : { m_pItemSearch, m_pNpcSearch })
	{
		if (pEdit != nullptr && !pEdit->IsVisible() && pEdit->HaveFocus())
			pEdit->KillFocus();
	}

	Refresh();
}

void CUICheats::RefreshRow(const Row& row)
{
	const std::vector<Choice> choices = row.pSetting->fnChoices();
	const int iValue                  = row.pSetting->fnGet();
	auto it = std::find_if(choices.begin(), choices.end(), [iValue](const Choice& choice) { return choice.iValue == iValue; });
	row.pValue->SetString(it != choices.end() ? it->szName : FormatNumber(iValue));
}

void CUICheats::Refresh()
{
	for (const Row& row : m_Rows)
		RefreshRow(row);

	ShowItemList();
	ShowNpcList();
	RefreshSlotItem();
	UpdateStatus();
}

void CUICheats::Step(const Row& row, int iStep)
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
	RefreshRow(row);
}

void CUICheats::UpdateStatus()
{
	CCheats* pCheats = GetCheats();
	if (pCheats == nullptr || m_pStatus == nullptr)
		return;

	if (!pCheats->IsAvailable())
		m_pStatus->SetString("This server doesn't allow cheats.");
	else if (!pCheats->Confirmed().bOn)
		m_pStatus->SetString("Cheats are off: turn them on at the top to use anything here.");
	else
		m_pStatus->SetString("Cheats are on. The server keeps these settings.");
}

void CUICheats::SearchItems()
{
	if (m_ItemNames.empty())
	{
		for (const auto& [dwID, item] : CGameBase::s_pTbl_Items_Basic.GetMap())
			m_ItemNames.push_back({ dwID, item.szName });
	}

	m_Results          = Cheats::SearchItems(m_ItemNames, m_pItemSearch->GetString());
	m_bShowingVariants = false;
	m_iItemListPage    = 0;
	m_dwBaseID         = 0;
	m_dwPickedID       = 0;
	m_VariantIDs.clear();
	ShowItemList();

	if (m_Results.empty())
		m_pPicked->SetString("No item has that in its name.");
}

void CUICheats::PickItemLine(int iLine)
{
	const std::vector<uint32_t>& ids = m_bShowingVariants ? m_VariantIDs : m_Results;
	const size_t iIndex              = static_cast<size_t>(m_iItemListPage) * ITEM_LINES + static_cast<size_t>(iLine);
	if (iIndex >= ids.size())
		return;

	if (m_bShowingVariants)
	{
		m_dwPickedID = ids[iIndex];
		m_iCount     = 1;
		ShowItemList();
		Refresh();
		return;
	}

	// An item from the search: the server says which of its IDs exist.
	m_dwBaseID         = ids[iIndex];
	m_dwPickedID       = 0;
	m_bShowingVariants = true;
	m_iItemListPage    = 0;
	m_VariantIDs.clear();
	if (GetCheats()->Variants(m_dwBaseID) != nullptr)
		BuildVariants();
	else
		GetCheats()->SendItemVariants(m_dwBaseID);
	ShowItemList();
}

void CUICheats::BuildVariants()
{
	m_VariantIDs.clear();
	const std::bitset<1000>* pBits = GetCheats()->Variants(m_dwBaseID);
	if (pBits == nullptr)
		return;

	// The IDs the server has and the client can show.
	for (int iExt = 0; iExt < 1000; iExt++)
	{
		__TABLE_ITEM_BASIC* pBasic = nullptr;
		__TABLE_ITEM_EXT* pExt     = nullptr;
		const uint32_t dwID        = m_dwBaseID + static_cast<uint32_t>(iExt);
		if ((*pBits)[iExt] && FindItem(dwID, &pBasic, &pExt))
			m_VariantIDs.push_back(dwID);
	}

	// A single choice is picked for the player.
	if (m_VariantIDs.size() == 1)
		m_dwPickedID = m_VariantIDs.front();

	ShowItemList();
	Refresh();
}

void CUICheats::ShowItemList()
{
	if (m_ItemLines.empty())
		return;

	const std::vector<uint32_t>& ids = m_bShowingVariants ? m_VariantIDs : m_Results;
	const int iPages                 = std::max(1, (static_cast<int>(ids.size()) + ITEM_LINES - 1) / ITEM_LINES);
	m_iItemListPage                  = std::clamp(m_iItemListPage, 0, iPages - 1);
	const bool bShow                 = m_iPage == PAGE_ITEMS;

	for (int i = 0; i < ITEM_LINES; i++)
	{
		const ListLine& line = m_ItemLines[i];
		const size_t iIndex  = static_cast<size_t>(m_iItemListPage) * ITEM_LINES + static_cast<size_t>(i);
		const bool bUsed     = iIndex < ids.size();
		line.pButton->SetVisible(bShow && bUsed);
		line.pIcon->SetVisible(false);
		line.pName->SetString("");
		if (!bUsed)
			continue;

		const uint32_t dwID        = ids[iIndex];
		__TABLE_ITEM_BASIC* pBasic = CGameBase::s_pTbl_Items_Basic.Find(dwID / 1000 * 1000);
		__TABLE_ITEM_EXT* pExt     = nullptr;
		if (pBasic != nullptr && pBasic->byExtIndex < MAX_ITEM_EXTENSION)
		{
			auto& exts = CGameBase::s_pTbl_Items_Exts[pBasic->byExtIndex];
			pExt       = exts.Find(dwID % 1000);
			if (pExt == nullptr && !m_bShowingVariants && !exts.GetMap().empty())
				pExt = exts.Find(exts.GetMap().begin()->first);
		}

		line.pName->SetString(m_bShowingVariants ? VariantName(dwID) : (pBasic != nullptr ? pBasic->szName : ""));
		line.pName->SetColor(dwID == m_dwPickedID ? COLOR_SELECTED : COLOR_LABEL);

		const std::string szIcon = pBasic != nullptr && pExt != nullptr ? IconOf(pBasic, pExt) : "";
		if (!szIcon.empty())
		{
			line.pIcon->SetTex(szIcon);
			line.pIcon->SetUVRect(0, 0, ICON_UV, ICON_UV);
			line.pIcon->SetVisible(bShow);
		}
	}

	m_pItemPage->SetString(fmt::format("{} / {}", m_iItemListPage + 1, iPages));
	m_pBtnItemBack->SetVisible(bShow && m_bShowingVariants);

	if (m_dwPickedID != 0)
		m_pPicked->SetString(fmt::format("Selected: {}", VariantName(m_dwPickedID)));
	else if (m_bShowingVariants)
		m_pPicked->SetString(GetCheats()->Variants(m_dwBaseID) == nullptr
								 ? "Asking the server which +levels exist..."
								 : (m_VariantIDs.empty() ? "The server has none of this item." : "Pick one."));
	else
		m_pPicked->SetString(m_Results.empty() ? "Type part of an item's name, then Search." : "Pick an item.");
}

void CUICheats::ShowNpcList()
{
	CCheats* pCheats = GetCheats();
	if (pCheats == nullptr || m_NpcLines.empty())
		return;

	const std::vector<CCheats::NpcHit>& hits = pCheats->NpcHits();
	if (m_iNpcPicked >= static_cast<int>(hits.size()))
		m_iNpcPicked = -1;

	const bool bShow = m_iPage == PAGE_WORLD;
	for (int i = 0; i < NPC_LINES; i++)
	{
		const ListLine& line = m_NpcLines[i];
		const bool bUsed     = i < static_cast<int>(hits.size());
		line.pButton->SetVisible(bShow && bUsed);
		line.pName->SetString(bUsed ? fmt::format("{}  ({})", hits[i].szName, ZoneName(hits[i].sZone)) : "");
		line.pName->SetColor(i == m_iNpcPicked ? COLOR_SELECTED : COLOR_LABEL);
	}
}

void CUICheats::RefreshSlotItem()
{
	if (m_pSlotItem == nullptr)
		return;

	const CUIInventory* pInventory = CGameProcedure::s_pProcMain->m_pUIInventory;
	const __IconItemSkill* pItem   = pInventory != nullptr && m_iSlot >= 0 && m_iSlot < ITEM_SLOT_COUNT ? pInventory->m_pMySlot[m_iSlot]
																										: nullptr;
	if (pItem == nullptr || pItem->pItemBasic == nullptr || pItem->pItemExt == nullptr)
	{
		m_pSlotItem->SetString("Nothing is equipped there.");
		return;
	}

	m_pSlotItem->SetString(fmt::format("Wearing: {}", VariantName(pItem->pItemBasic->dwID + pItem->pItemExt->dwID)));
}

bool CUICheats::OnMouseWheelEvent(short delta)
{
	if (!IsVisible() || m_iPage != PAGE_ITEMS)
		return false;

	m_iItemListPage += delta > 0 ? -1 : 1;
	ShowItemList();
	return true;
}

bool CUICheats::ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg)
{
	if (CUIArtWindow::ReceiveMessage(pSender, dwMsg))
		return true;

	CCheats* pCheats = GetCheats();
	if (pCheats == nullptr)
		return false;

	// Return in a search field searches.
	if (dwMsg == UIMSG_EDIT_RETURN)
	{
		if (pSender == m_pItemSearch)
			SearchItems();
		else if (pSender == m_pNpcSearch)
			pCheats->SendFindNpc(m_pNpcSearch->GetString());
		return true;
	}

	if (dwMsg != UIMSG_BUTTON_CLICK)
		return false;

	if (pSender == m_pBtnClose)
	{
		Close();
		return true;
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
		if (pSender == row.pPrev || pSender == row.pNext || pSender == row.pBox)
		{
			Step(row, pSender == row.pPrev ? -1 : 1);
			if (row.pSetting->szLabel == "+level" || row.pSetting->szLabel == "Equipped item")
				RefreshSlotItem();
			if (row.pSetting->szLabel == "How many")
				ShowItemList();
			return true;
		}
	}

	// Character
	if (pSender == m_pBtnLevelDown || pSender == m_pBtnLevelUp)
	{
		m_iLevel = std::clamp(m_iLevel + (pSender == m_pBtnLevelUp ? 10 : -10), Cheats::MIN_LEVEL, Cheats::MAX_LEVEL);
		Refresh();
	}
	else if (pSender == m_pBtnSetLevel)
		pCheats->SendSetLevel(m_iLevel);
	else if (pSender == m_pBtnStatPoints)
		pCheats->SendAddPoints(0);
	else if (pSender == m_pBtnSkillPts)
		pCheats->SendAddPoints(1);
	else if (pSender == m_pBtnResetStats)
		pCheats->SendResetPoints(0);
	else if (pSender == m_pBtnResetSkill)
		pCheats->SendResetPoints(1);
	else if (pSender == m_pBtnRefill)
		pCheats->SendRefill();
	else if (pSender == m_pBtnSetCoins)
		pCheats->SendSetCurrency(0, static_cast<uint32_t>(m_iCoins));
	else if (pSender == m_pBtnSetNP)
		pCheats->SendSetCurrency(1, static_cast<uint32_t>(m_iLoyalty));
	else if (pSender == m_pBtnSetKC)
		pCheats->SendSetCurrency(2, static_cast<uint32_t>(m_iKnightCash));
	// Items
	else if (pSender == m_pBtnItemSearch)
		SearchItems();
	else if (pSender == m_pItemPrev || pSender == m_pItemNext)
	{
		m_iItemListPage += pSender == m_pItemNext ? 1 : -1;
		ShowItemList();
	}
	else if (pSender == m_pBtnItemBack)
	{
		m_bShowingVariants = false;
		m_dwBaseID         = 0;
		m_dwPickedID       = 0;
		m_iItemListPage    = 0;
		m_VariantIDs.clear();
		ShowItemList();
	}
	else if (pSender == m_pBtnGive)
	{
		if (m_dwPickedID != 0)
			pCheats->SendGiveItem(m_dwPickedID, static_cast<uint16_t>(m_iCount));
		else
			m_pPicked->SetString("Pick an item first.");
	}
	// Gear
	else if (pSender == m_pBtnSetPlus)
		pCheats->SendSetPlus(static_cast<uint8_t>(m_iSlot), static_cast<uint8_t>(m_iPlus));
	else if (pSender == m_pBtnRepair)
		pCheats->SendRepairAll();
	// World
	else if (pSender == m_pBtnTeleport)
		pCheats->SendTeleportZone(static_cast<int16_t>(m_iZone));
	else if (pSender == m_pBtnNpcSearch)
		pCheats->SendFindNpc(m_pNpcSearch->GetString());
	else if (pSender == m_pBtnGoToNpc)
	{
		const std::vector<CCheats::NpcHit>& hits = pCheats->NpcHits();
		if (m_iNpcPicked >= 0 && m_iNpcPicked < static_cast<int>(hits.size()))
			pCheats->SendTeleportNpc(hits[m_iNpcPicked].sID);
	}
	else if (pSender == m_pBtnKillAround)
		pCheats->SendKillAround();
	else
	{
		for (int i = 0; i < ITEM_LINES; i++)
		{
			if (pSender == m_ItemLines[i].pButton)
			{
				PickItemLine(i);
				return true;
			}
		}

		for (int i = 0; i < NPC_LINES; i++)
		{
			if (pSender == m_NpcLines[i].pButton)
			{
				m_iNpcPicked = i;
				ShowNpcList();
				return true;
			}
		}

		return false;
	}

	return true;
}
