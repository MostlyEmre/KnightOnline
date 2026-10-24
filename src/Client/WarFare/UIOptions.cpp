#include "StdAfx.h"
#include "UIOptions.h"
#include "UIArt.h"
#include "GameProcMain.h"
#include "LocalInput.h"
#include "PacketDef.h"
#include "UIChat.h"
#include "text_resources.h"

#include <FileIO/PathResolver.h>
#include <N3Base/N3FXBundle.h>
#include <N3Base/N3UIButton.h>
#include <N3Base/N3UIEdit.h>
#include <N3Base/N3UIImage.h>
#include <N3Base/N3UIString.h>
#include <N3Base/Platform/PlatformOptions.h>
#include <shared/Ini.h>

#include <algorithm>
#include <utility>

namespace
{

constexpr D3DCOLOR COLOR_TITLE              = 0xffffffff;
constexpr D3DCOLOR COLOR_LABEL              = 0xfffae6af;
constexpr D3DCOLOR COLOR_VALUE              = 0xffffffff;
constexpr D3DCOLOR COLOR_SELECTED           = 0xffffffff;
constexpr D3DCOLOR COLOR_NOTE               = 0xff80ff80;

// Layout, from the window's top left.
constexpr int WIDTH                         = 520;
constexpr int TAB_TOP                       = 38;
constexpr int TAB_GAP                       = 3;
constexpr int ROW_TOP                       = 82;
constexpr int ROW_PITCH                     = 34;
constexpr int LABEL_LEFT                    = 28;
constexpr int LABEL_RIGHT                   = 256;
constexpr int ARROW_WIDTH                   = 32;
constexpr int ARROW_HEIGHT                  = 17;
constexpr int PREV_LEFT                     = 262;
constexpr int BOX_LEFT                      = PREV_LEFT + ARROW_WIDTH + 4;
constexpr int BOX_RIGHT                     = 458;
constexpr int NEXT_LEFT                     = BOX_RIGHT + 4;
constexpr int BOTTOM_HEIGHT                 = 84; // below the rows: the note, the buttons and the margin

// The pages, in order. A platform's settings may add others, after these.
const char* const PAGES[]                   = { "Display", "Graphics", "Sound", "Controls", "Performance" };

// Option.exe's usual resolutions that WarFareMain leaves alone (it fixes the height for some
// widths).
constexpr std::pair<int, int> RESOLUTIONS[] = { { 1024, 768 }, { 1152, 864 }, { 1280, 1024 }, { 1366, 768 }, { 1600, 1200 },
	{ 1920, 1080 } };

std::string OptionIniPath()
{
	return CN3Base::PathGet() + "Option.ini";
}

void SaveOption(const char* szSection, const char* szKey, int iValue)
{
	// Start from what's there: the platform's settings live in the same file.
	CIni ini(PathResolver::Resolve(OptionIniPath()));
	ini.SetInt(szSection, szKey, iValue);
	ini.Save(PathResolver::ResolveForWriting(OptionIniPath()));
}

} // namespace

CUIOptions::CUIOptions()
{
	m_bVisible = false; // until it's opened
}

CUIOptions::~CUIOptions() = default;

void CUIOptions::LoadSettings()
{
	m_Settings.clear();
	CIni ini(PathResolver::Resolve(OptionIniPath()));

	auto add = [this](const char* szPage, const char* szLabel, std::vector<std::string> names, std::vector<int> values, int iValue,
				   bool bRestart, std::function<void(int)> save)
	{
		// A value set some other way (by editing Option.ini) stays one of the choices.
		auto it = std::find(values.begin(), values.end(), iValue);
		if (it == values.end())
		{
			names.push_back(std::to_string(iValue));
			values.push_back(iValue);
			it = values.end() - 1;
		}

		Setting setting;
		setting.szPage       = szPage;
		setting.szLabel      = szLabel;
		setting.iChoice      = static_cast<int>(it - values.begin());
		setting.choiceNames  = std::move(names);
		setting.choiceValues = std::move(values);
		setting.bRestart     = bRestart;
		setting.Save         = std::move(save);
		m_Settings.push_back(std::move(setting));
	};

	auto toggle = [&add](const char* szPage, const char* szLabel, bool bValue, bool bRestart, std::function<void(int)> save)
	{ add(szPage, szLabel, { "Off", "On" }, { 0, 1 }, bValue ? 1 : 0, bRestart, std::move(save)); };

	// Display. Resolutions are width * 10000 + height; "fit the screen" first, then Option.exe's
	// usual ones (where WarFareMain doesn't override the height).
	int iFitWidth = 0, iFitHeight = 0;
	Platform::GetFullscreenSize(iFitWidth, iFitHeight);
	std::vector<std::string> sizeNames;
	std::vector<int> sizeValues;
	auto addSize = [&](int iWidth, int iHeight, const std::string& szName)
	{
		int iValue = iWidth * 10000 + iHeight;
		if (iWidth <= 0 || iHeight <= 0 || std::find(sizeValues.begin(), sizeValues.end(), iValue) != sizeValues.end())
			return;
		sizeNames.push_back(szName);
		sizeValues.push_back(iValue);
	};
	addSize(iFitWidth, iFitHeight, fmt::format("Fit screen ({}x{})", iFitWidth, iFitHeight));
	for (const auto& [iSizeWidth, iSizeHeight] : RESOLUTIONS)
		addSize(iSizeWidth, iSizeHeight, fmt::format("{}x{}", iSizeWidth, iSizeHeight));
	int iWidth  = ini.GetInt("ViewPort", "Width", 1024);
	int iHeight = ini.GetInt("ViewPort", "Height", 768);
	addSize(iWidth, iHeight, fmt::format("{}x{}", iWidth, iHeight));
	add("Display", "Resolution", sizeNames, sizeValues, iWidth * 10000 + iHeight, true,
		[](int iValue)
		{
			SaveOption("ViewPort", "Width", iValue / 10000);
			SaveOption("ViewPort", "Height", iValue % 10000);
		});
	add("Display", "Display mode", { "Fullscreen", "Window" }, { 0, 1 },
		ini.GetBool("Screen", "WindowMode", CN3Base::s_Options.bWindowMode) ? 1 : 0, true,
		[](int iValue) { SaveOption("Screen", "WindowMode", iValue); });

	// Graphics
	add("Graphics", "View distance", { "Short", "Medium", "Long", "Maximum" }, { 256, 352, 448, 512 },
		std::clamp(ini.GetInt("ViewPort", "Distance", 512), 256, 512), false,
		[](int iValue)
		{
			SaveOption("ViewPort", "Distance", iValue);
			CN3Base::s_Options.iViewDist = iValue;
		});
	toggle("Graphics", "Shadows", ini.GetInt("Shadow", "Use", 1) != 0, false,
		[](int iValue)
		{
			SaveOption("Shadow", "Use", iValue);
			CN3Base::s_Options.iUseShadow = iValue;
		});
	const std::pair<const char*, const char*> textures[] = { { "Character textures", "LOD_Chr" }, { "Object textures", "LOD_Shape" },
		{ "Terrain textures", "LOD_Terrain" } };
	for (const auto& texture : textures)
	{
		const char* szKey = texture.second;
		add("Graphics", texture.first, { "High", "Low" }, { 0, 1 }, std::clamp(ini.GetInt("Texture", szKey, 0), 0, 1), true,
			[szKey](int iValue) { SaveOption("Texture", szKey, iValue); });
	}

	// Sound
	toggle("Sound", "Music", ini.GetBool("Sound", "Bgm", true), true, [](int iValue) { SaveOption("Sound", "Bgm", iValue); });
	toggle("Sound", "Sound effects", ini.GetBool("Sound", "Effect", true), true, [](int iValue) { SaveOption("Sound", "Effect", iValue); });
	add("Sound", "Effect sound range", { "Short", "Medium", "Long", "Maximum" }, { 20, 30, 40, 48 },
		std::clamp(ini.GetInt("Sound", "Distance", 48), 20, 48), false,
		[](int iValue)
		{
			SaveOption("Sound", "Distance", iValue);
			CN3Base::s_Options.iEffectSndDist = iValue;
			CN3FXBundle::SetEffectSndDistance(static_cast<float>(iValue));
		});

	// Controls and Performance
	toggle("Controls", "Hardware cursor", ini.GetBool("Cursor", "WindowCursor", true), true,
		[](int iValue) { SaveOption("Cursor", "WindowCursor", iValue); });
	toggle("Performance", "VSync", ini.GetBool("Screen", "VSyncEnabled", true), true,
		[](int iValue) { SaveOption("Screen", "VSyncEnabled", iValue); });

	// The platform's own settings, after the game's on their pages.
	for (const Platform::PlatformOption& option : Platform::GetPlatformOptions())
	{
		std::string szKey = option.szKey;
		add(option.szPage.c_str(), option.szLabel.c_str(), option.choiceNames, option.choiceValues, option.iValue, option.bRestart,
			[szKey](int iValue) { Platform::SetPlatformOption(szKey, iValue); });
	}

	// The first time, these are the values the game started with.
	for (const Setting& setting : m_Settings)
	{
		std::string szID = setting.szPage + "/" + setting.szLabel;
		if (setting.bRestart && !m_StartValues.contains(szID))
			m_StartValues[szID] = setting.choiceValues[setting.iChoice];
	}
}

void CUIOptions::Build()
{
	// The pages that have settings, in order.
	for (const char* szPage : PAGES)
	{
		if (std::any_of(m_Settings.begin(), m_Settings.end(), [szPage](const Setting& setting) { return setting.szPage == szPage; }))
			m_Pages.emplace_back(szPage);
	}
	for (const Setting& setting : m_Settings)
	{
		if (std::find(m_Pages.begin(), m_Pages.end(), setting.szPage) == m_Pages.end())
			m_Pages.push_back(setting.szPage);
	}

	size_t iRows = 0;
	for (const std::string& szPage : m_Pages)
	{
		iRows = std::max(iRows, static_cast<size_t>(std::count_if(m_Settings.begin(), m_Settings.end(),
									[&szPage](const Setting& setting) { return setting.szPage == szPage; })));
	}

	const int iHeight = ROW_TOP + static_cast<int>(iRows) * ROW_PITCH + BOTTOM_HEIGHT;
	SetRegion(UIArt::MakeRect(0, 0, WIDTH, iHeight));

	// Children added later are drawn over the ones added before them, so the frame comes first.
	// It's drawn three times: the art is see-through (73%), which makes text hard to read over a
	// busy screen.
	const int iRight  = WIDTH - UIArt::FRAME_CORNER;
	const int iBottom = iHeight - UIArt::FRAME_CORNER;
	for (int i = 0; i < 3; i++)
	{
		UIArt::AddImage(this, UIArt::MakeRect(0, 0, UIArt::FRAME_CORNER, UIArt::FRAME_CORNER), UIArt::FRAME_TOP_LEFT);
		UIArt::AddImage(this, UIArt::MakeRect(iRight, 0, WIDTH, UIArt::FRAME_CORNER), UIArt::FRAME_TOP_RIGHT);
		UIArt::AddImage(this, UIArt::MakeRect(0, iBottom, UIArt::FRAME_CORNER, iHeight), UIArt::FRAME_BOTTOM_LEFT);
		UIArt::AddImage(this, UIArt::MakeRect(iRight, iBottom, WIDTH, iHeight), UIArt::FRAME_BOTTOM_RIGHT);
		UIArt::AddImage(this, UIArt::MakeRect(UIArt::FRAME_CORNER, 0, iRight, UIArt::FRAME_CORNER), UIArt::FRAME_TOP);
		UIArt::AddImage(this, UIArt::MakeRect(UIArt::FRAME_CORNER, iBottom, iRight, iHeight), UIArt::FRAME_BOTTOM);
		UIArt::AddImage(this, UIArt::MakeRect(0, UIArt::FRAME_CORNER, UIArt::FRAME_CORNER, iBottom), UIArt::FRAME_LEFT);
		UIArt::AddImage(this, UIArt::MakeRect(iRight, UIArt::FRAME_CORNER, WIDTH, iBottom), UIArt::FRAME_RIGHT);
		UIArt::AddImage(this, UIArt::MakeRect(UIArt::FRAME_CORNER, UIArt::FRAME_CORNER, iRight, iBottom), UIArt::FRAME_CENTER);
	}

	UIArt::AddString(this, UIArt::MakeRect(16, 8, 300, 30), "Options", 12, true, COLOR_TITLE);
	m_pBtnCloseX        = CUIArtButton::Create(this, UIArt::MakeRect(WIDTH - 28, 4, WIDTH - 5, 28), UIArt::CLOSE);

	const int iTabs     = static_cast<int>(m_Pages.size());
	const int iTabsLeft = (WIDTH - (iTabs * UIArt::BAR_WIDTH + (iTabs - 1) * TAB_GAP)) / 2;
	for (int i = 0; i < iTabs; i++)
	{
		const int iLeft     = iTabsLeft + i * (UIArt::BAR_WIDTH + TAB_GAP);
		CN3UIString* pLabel = nullptr;
		m_Tabs.push_back(CUIArtButton::Create(
			this, UIArt::MakeRect(iLeft, TAB_TOP, iLeft + UIArt::BAR_WIDTH, TAB_TOP + UIArt::BAR_HEIGHT), m_Pages[i], &pLabel));
		m_TabLabels.push_back(pLabel);
	}

	m_Rows.resize(iRows);
	for (size_t i = 0; i < iRows; i++)
	{
		Row& row            = m_Rows[i];
		const int iTop      = ROW_TOP + static_cast<int>(i) * ROW_PITCH;
		const int iArrowTop = iTop + (UIArt::BAR_HEIGHT - ARROW_HEIGHT) / 2;
		const RECT rcBox    = UIArt::MakeRect(BOX_LEFT, iTop, BOX_RIGHT, iTop + UIArt::BAR_HEIGHT);
		row.pLabel          = UIArt::AddString(
            this, UIArt::MakeRect(LABEL_LEFT, iTop, LABEL_RIGHT, iTop + UIArt::BAR_HEIGHT), "", 11, true, COLOR_LABEL);
		row.pPrev = CUIArtButton::Create(
			this, UIArt::MakeRect(PREV_LEFT, iArrowTop, PREV_LEFT + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_LEFT);
		row.boxImages = UIArt::AddBar(this, rcBox, UIArt::BAR_DARK);

		// The value is a button without pictures over its box.
		row.pBox      = new CN3UIButton();
		row.pBox->Init(this);
		row.pBox->SetRegion(rcBox);
		row.pValue = UIArt::AddString(this, rcBox, "", 10, true, COLOR_VALUE, UISTYLE_STRING_ALIGNCENTER);
		row.pNext  = CUIArtButton::Create(
            this, UIArt::MakeRect(NEXT_LEFT, iArrowTop, NEXT_LEFT + ARROW_WIDTH, iArrowTop + ARROW_HEIGHT), UIArt::ARROW_RIGHT);
	}

	const int iNoteTop   = ROW_TOP + static_cast<int>(iRows) * ROW_PITCH + 2;
	m_pTextNote          = UIArt::AddString(this, UIArt::MakeRect(LABEL_LEFT, iNoteTop, WIDTH - LABEL_LEFT, iNoteTop + 18),
				 "* Takes effect when the game restarts.", 10, false, COLOR_NOTE);

	const int iButtonTop = iNoteTop + 18 + 12;
	m_pBtnRestart = CUIArtButton::Create(this, UIArt::MakeRect(0, iButtonTop, UIArt::BAR_WIDTH, iButtonTop + UIArt::BAR_HEIGHT), "Restart");
	m_pBtnClose   = CUIArtButton::Create(this, UIArt::MakeRect(0, iButtonTop, UIArt::BAR_WIDTH, iButtonTop + UIArt::BAR_HEIGHT), "Close");
}

void CUIOptions::Open()
{
	if (IsVisible())
		return;

	LoadSettings();
	if (m_Tabs.empty())
		Build();

	SetPosCenter();
	ShowPage(std::min(m_iPage, static_cast<int>(m_Pages.size()) - 1));
	m_pBtnClose->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnCloseX->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnRestart->SetState(UI_STATE_BUTTON_NORMAL);
	UpdateButtons();

	// Typing goes nowhere while the window is open.
	m_pEditFocused = CN3UIBase::GetFocusedEdit();
	m_pProcOpened  = CGameProcedure::s_pProcActive;
	if (m_pEditFocused != nullptr)
		m_pEditFocused->KillFocus();

	SetVisible(true);
}

void CUIOptions::Close()
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

void CUIOptions::TakeInput(CLocalInput* pInput)
{
	if (IsVisible())
	{
		MouseProc(static_cast<uint32_t>(pInput->MouseGetFlag()), pInput->MouseGetPos(), pInput->MouseGetPosOld());
		Tick();
		if (IsVisible() && pInput->IsKeyPress(DIK_ESCAPE))
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

void CUIOptions::ShowPage(int iPage)
{
	if (iPage < 0 || iPage >= static_cast<int>(m_Pages.size()))
		return;

	// A button only notices the mouse leaving it if nothing else took the click first, so the
	// tabs' highlights are cleared here.
	m_iPage = iPage;
	for (int i = 0; i < static_cast<int>(m_Tabs.size()); i++)
	{
		m_Tabs[i]->SetState(UI_STATE_BUTTON_NORMAL);
		m_Tabs[i]->SetSelected(i == iPage);
		m_TabLabels[i]->SetColor(i == iPage ? COLOR_SELECTED : UIArt::COLOR_BUTTON);
	}

	size_t iRow = 0;
	for (size_t i = 0; i < m_Settings.size() && iRow < m_Rows.size(); i++)
	{
		const Setting& setting = m_Settings[i];
		if (setting.szPage != m_Pages[iPage])
			continue;

		Row& row     = m_Rows[iRow++];
		row.iSetting = static_cast<int>(i);
		row.pLabel->SetString(setting.bRestart ? setting.szLabel + " *" : setting.szLabel);
		row.pValue->SetString(setting.choiceNames[setting.iChoice]);
		ShowRow(row, true);
	}

	for (; iRow < m_Rows.size(); iRow++)
	{
		m_Rows[iRow].iSetting = -1;
		ShowRow(m_Rows[iRow], false);
	}
}

void CUIOptions::ShowRow(const Row& row, bool bVisible)
{
	row.pLabel->SetVisible(bVisible);
	row.pValue->SetVisible(bVisible);
	row.pPrev->SetVisible(bVisible);
	row.pNext->SetVisible(bVisible);
	row.pBox->SetVisible(bVisible);
	for (CN3UIImage* pImage : row.boxImages)
		pImage->SetVisible(bVisible);
}

void CUIOptions::Step(Row& row, int iStep)
{
	if (row.iSetting < 0)
		return;

	Setting& setting = m_Settings[row.iSetting];
	const int iCount = static_cast<int>(setting.choiceValues.size());
	setting.iChoice  = (setting.iChoice + iStep + iCount) % iCount;
	row.pValue->SetString(setting.choiceNames[setting.iChoice]);
	setting.Save(setting.choiceValues[setting.iChoice]);
	UpdateButtons();
}

bool CUIOptions::IsRestartPending() const
{
	return std::any_of(m_Settings.begin(), m_Settings.end(),
		[this](const Setting& setting)
		{
			if (!setting.bRestart)
				return false;

			auto it = m_StartValues.find(setting.szPage + "/" + setting.szLabel);
			return it != m_StartValues.end() && it->second != setting.choiceValues[setting.iChoice];
		});
}

void CUIOptions::UpdateButtons()
{
	// Close in the middle, or Restart and Close side by side when there's something to restart for.
	const bool bRestart = IsRestartPending();
	const int iLeft     = GetRegion().left + WIDTH / 2;
	const int iTop      = m_pBtnClose->GetRegion().top;
	m_pBtnRestart->SetVisible(bRestart);
	m_pBtnRestart->SetPos(iLeft - UIArt::BAR_WIDTH - 3, iTop);
	m_pBtnClose->SetPos(bRestart ? iLeft + 3 : iLeft - UIArt::BAR_WIDTH / 2, iTop);
}

void CUIOptions::Restart()
{
	// In the game, restarting waits out the time after a battle, like quitting does.
	CGameProcMain* pMain = CGameProcedure::s_pProcMain;
	if (pMain != nullptr && CGameProcedure::s_pProcActive == pMain && pMain->m_fExitTimer != -1.0f)
	{
		if (pMain->m_pUIChatDlg != nullptr)
		{
			std::string szMsg = fmt::format_text_resource(IDS_CANNOT_EXIT_DURING_A_BATTLE);
			pMain->m_pUIChatDlg->AddChatMsg(N3_CHAT_NORMAL, szMsg, 0xFFFF0000);
		}

		pMain->m_eExitType = EXIT_TYPE_RESTART;
		Close();
		return;
	}

	Platform::RequestRestart();
}

bool CUIOptions::ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg)
{
	if (dwMsg != UIMSG_BUTTON_CLICK)
		return false;

	if (pSender == m_pBtnClose || pSender == m_pBtnCloseX)
	{
		Close();
		return true;
	}

	if (pSender == m_pBtnRestart)
	{
		Restart();
		return true;
	}

	for (size_t i = 0; i < m_Tabs.size(); i++)
	{
		if (pSender == m_Tabs[i])
		{
			ShowPage(static_cast<int>(i));
			return true;
		}
	}

	for (Row& row : m_Rows)
	{
		if (pSender == row.pPrev)
			Step(row, -1);
		else if (pSender == row.pNext || pSender == row.pBox)
			Step(row, 1);
		else
			continue;

		return true;
	}

	return false;
}
