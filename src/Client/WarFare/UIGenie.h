#ifndef CLIENT_WARFARE_UIGENIE_H
#define CLIENT_WARFARE_UIGENIE_H

#pragma once

#include "UIArt.h"

class CGenie;
class CN3UIString;

// Genie's (auto-hunt's) window, which G opens: its settings, and Start/Stop. There's no Genie art
// in 1.298, so it's put together from the message box art, like the Options window.
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
	void Build();
	void UpdateControls();
	static CGenie* GetGenie();

	CUIArtButton* m_pBtnStart = nullptr;
	CUIArtButton* m_pBtnClose = nullptr;
	CN3UIString* m_pStatus    = nullptr;
};

#endif // CLIENT_WARFARE_UIGENIE_H
