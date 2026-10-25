#include "pch.h"
#include "EbenezerApp.h"
#include "User.h"

#include <shared/CheatSettings.h>
#include <shared/globals.h>
#include <shared/packets.h>

// OpenKO: the cheats menu (WIZ_CHEAT). The settings live in EbenezerApp; this file handles the
// client's requests.

namespace Ebenezer
{

static_assert(Cheats::MAX_GAIN == MAX_CURRENCY);

void CUser::SendCheatAvailable()
{
	char sendBuffer[8] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_AVAILABLE, sendIndex);
	SetByte(sendBuffer, m_pMain->m_bCheatsAllowed ? 1 : 0, sendIndex);
	Send(sendBuffer, sendIndex);

	if (!m_pMain->m_bCheatsAllowed)
		return;

	char settingsBuffer[32] {};
	sendIndex = 0;
	SetByte(settingsBuffer, WIZ_CHEAT, sendIndex);
	SetByte(settingsBuffer, CHEAT_SETTINGS, sendIndex);
	EbenezerApp::WriteCheatSettings(settingsBuffer, m_pMain->CheatSettings(), sendIndex);
	Send(settingsBuffer, sendIndex);
}

void CUser::SendCheatResult(uint8_t opcode, uint8_t result)
{
	char sendBuffer[8] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_RESULT, sendIndex);
	SetByte(sendBuffer, opcode, sendIndex);
	SetByte(sendBuffer, result, sendIndex);
	Send(sendBuffer, sendIndex);
}

void CUser::Cheat(char* pBuf, int len)
{
	if (GetState() != CONNECTION_STATE_GAMESTART || len < 1)
		return;

	int index            = 0;
	const uint8_t opcode = GetByte(pBuf, index);

	if (!m_pMain->m_bCheatsAllowed)
	{
		SendCheatResult(opcode, CHEAT_NOT_ALLOWED);
		return;
	}

	switch (opcode)
	{
		case CHEAT_SETTINGS:
		{
			// Accepted settings are echoed to every player in the game.
			auto settings = EbenezerApp::ReadCheatSettings(pBuf, len, index);
			if (!settings.has_value() || !m_pMain->SetCheatSettings(*settings))
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
		}
		break;

		default:
			SendCheatResult(opcode, CHEAT_BAD_VALUE);
			break;
	}
}

} // namespace Ebenezer
