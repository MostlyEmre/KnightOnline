#ifndef CLIENT_WARFARE_UIOPTIONS_H
#define CLIENT_WARFARE_UIOPTIONS_H

#pragma once

#include "UIArt.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class CN3UIButton;
class CN3UIImage;
class CN3UIString;

// The game's Options window, for platforms without Option.exe (Platform::HasOptionsProgram()):
// Option.exe's settings that the game reads, and the platform's own, on pages. There's no uif for
// it, so it's put together from the message box art. Every change is saved to Option.ini straight
// away; the ones marked * take effect when the game restarts, which the window offers to do.
class CUIOptions : public CUIArtWindow
{
public:
	CUIOptions();
	~CUIOptions() override;

	void Open() override;

	bool ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg) override;

private:
	struct Setting
	{
		std::string szPage;
		std::string szLabel;
		std::vector<std::string> choiceNames;
		std::vector<int> choiceValues;
		int iChoice   = 0;
		bool bRestart = false;
		std::function<void(int iValue)> Save; // saves the chosen value, and applies it if it can
	};

	struct Row
	{
		CN3UIString* pLabel = nullptr;
		CN3UIString* pValue = nullptr;
		CN3UIButton* pPrev  = nullptr;
		CN3UIButton* pNext  = nullptr;
		CN3UIButton* pBox   = nullptr; // the value; clicking it picks the next choice
		std::vector<CN3UIImage*> boxImages;
		int iSetting = -1;
	};

	void LoadSettings();
	void Build();
	void ShowPage(int iPage);
	void Step(Row& row, int iStep);
	void ShowRow(const Row& row, bool bVisible);
	void UpdateButtons();
	bool IsRestartPending() const;
	void Restart();

	std::vector<Setting> m_Settings;
	std::map<std::string, int> m_StartValues; // the restart settings' values the game started with
	std::vector<std::string> m_Pages;
	std::vector<CUIArtButton*> m_Tabs;
	std::vector<CN3UIString*> m_TabLabels;
	std::vector<Row> m_Rows;
	int m_iPage                = 0;

	CN3UIButton* m_pBtnClose   = nullptr;
	CN3UIButton* m_pBtnRestart = nullptr;
	CN3UIString* m_pTextNote   = nullptr;
};

#endif // CLIENT_WARFARE_UIOPTIONS_H
