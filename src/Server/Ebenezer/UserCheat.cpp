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

void CUser::ExpGain(int base)
{
	ExpChange(
		static_cast<int>(Cheats::ScaleGain(base, m_pMain->CheatPct(&Cheats::Settings::sExpPct))));

	// ExpChange levels up once per call.
	while (m_pUserData->m_iExp >= m_iMaxExp && m_pUserData->m_bLevel < MAX_LEVEL)
		ExpChange(0);
}

void CUser::LoyaltyGain(int base)
{
	CurrencyChange(m_pUserData->m_iLoyalty,
		static_cast<int>(
			Cheats::ScaleGain(base, m_pMain->CheatPct(&Cheats::Settings::sLoyaltyPct))));
	SendLoyaltyChange();
}

void CUser::SendLoyaltyChange()
{
	char sendBuffer[16] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_LOYALTY_CHANGE, sendIndex);
	SetByte(sendBuffer, LOYALTY_CHANGE_NATIONAL, sendIndex);
	SetInt(sendBuffer, m_pUserData->m_iLoyalty, sendIndex);
	SetInt(sendBuffer, m_pUserData->m_iLoyaltyMonthly, sendIndex);
	Send(sendBuffer, sendIndex);
}

int CUser::CoinPickupAmount(int count) const
{
	return static_cast<int>(
		Cheats::ScaleGain(count, m_pMain->CheatPct(&Cheats::Settings::sCoinPct)));
}

void CUser::SendStatsUpdate()
{
	char sendBuffer[64] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_ITEM_MOVE, sendIndex);
	SetByte(sendBuffer, 0x01, sendIndex);
	SetShort(sendBuffer, m_sTotalHit, sendIndex);
	SetShort(sendBuffer, m_sTotalAc, sendIndex);
	SetShort(sendBuffer, GetMaxWeightForClient(), sendIndex);
	SetShort(sendBuffer, m_iMaxHp, sendIndex);
	SetShort(sendBuffer, m_iMaxMp, sendIndex);
	SetShort(sendBuffer, m_sItemStr + m_sStrAmount, sendIndex);
	SetShort(sendBuffer, m_sItemSta + m_sStaAmount, sendIndex);
	SetShort(sendBuffer, m_sItemDex + m_sDexAmount, sendIndex);
	SetShort(sendBuffer, m_sItemIntel + m_sIntelAmount, sendIndex);
	SetShort(sendBuffer, m_sItemCham + m_sChaAmount, sendIndex);
	SetShort(sendBuffer, m_bFireR, sendIndex);
	SetShort(sendBuffer, m_bColdR, sendIndex);
	SetShort(sendBuffer, m_bLightningR, sendIndex);
	SetShort(sendBuffer, m_bMagicR, sendIndex);
	SetShort(sendBuffer, m_bDiseaseR, sendIndex);
	SetShort(sendBuffer, m_bPoisonR, sendIndex);
	Send(sendBuffer, sendIndex);
}

bool CUser::CanPayMana(int cost) const
{
	return m_pMain->CheatFlag(&Cheats::Settings::bInfiniteMp) || cost <= m_pUserData->m_sMp;
}

void CUser::PayMana(int cost)
{
	if (!m_pMain->CheatFlag(&Cheats::Settings::bInfiniteMp))
		MSpChange(-cost);
}

bool CUser::UseConsumable(int itemId, int count)
{
	if (m_pMain->CheatFlag(&Cheats::Settings::bKeepConsumables))
		return CheckExistItem(itemId, static_cast<int16_t>(count));

	return ItemCountChange(itemId, 1, count) >= 2;
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
