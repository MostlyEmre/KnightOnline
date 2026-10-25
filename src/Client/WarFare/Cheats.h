#ifndef CLIENT_WARFARE_CHEATS_H
#define CLIENT_WARFARE_CHEATS_H

#pragma once

#include <shared/CheatSettings.h>

#include <bitset>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CGameProcMain;
class CN3UIBase;
class Packet;

// The cheats menu: the server applies every cheat; the client asks for them (WIZ_CHEAT), shows the
// settings the server confirmed, and relaxes its own checks (MP, shop prices) to match. The window
// is CUICheats.
class CCheats
{
public:
	struct NpcHit
	{
		int16_t sID   = 0;
		int16_t sZone = 0;
		std::string szName;
	};

	explicit CCheats(CGameProcMain* pMain);
	~CCheats();

	CCheats(const CCheats&)            = delete;
	CCheats& operator=(const CCheats&) = delete;

	/// \brief Handles every WIZ_CHEAT sub-opcode from the server.
	void Receive(Packet& pkt);

	/// \brief Whether the server announced cheats as allowed. Until it does, nothing is ever sent.
	bool IsAvailable() const;

	/// \brief The settings the server last confirmed.
	const Cheats::Settings& Confirmed() const;

	/// \brief Whether a toggle is in effect, by the server's confirmed settings.
	bool IsOn(bool Cheats::Settings::* flag) const;

	/// \brief Whether a toggle is in effect for the player in the game now: false outside the game.
	static bool IsOnNow(bool Cheats::Settings::* flag);

	/// \brief Back to "not announced" (leaving the game, changing server).
	void OnLeaveGame();

	void SendSettings(const Cheats::Settings& settings);
	void SendSetLevel(int iLevel);
	void SendAddPoints(uint8_t byKind);
	void SendResetPoints(uint8_t byKind);
	void SendRefill();
	void SendSetCurrency(uint8_t byKind, uint32_t dwValue);
	void SendItemVariants(uint32_t dwBaseID);
	void SendGiveItem(uint32_t dwItemID, uint16_t wCount);
	void SendSetPlus(uint8_t bySlot, uint8_t byPlus);
	void SendRepairAll();
	void SendTeleportZone(int16_t sZone);
	void SendFindNpc(const std::string& szQuery);
	void SendTeleportNpc(int16_t sNpcID);
	void SendKillAround();

	/// \returns the server's last answer about which IDs of an item exist, or nullptr
	const std::bitset<1000>* Variants(uint32_t dwBaseID) const;

	/// \returns the last NPC search's results
	const std::vector<NpcHit>& NpcHits() const;

	/// \brief Increases each time an answer the window shows (variants, NPC search) arrives.
	int AnswerCount() const;

	/// \brief Draws the "Cheats" label while cheats are on.
	void RenderIndicator();

private:
	// Sends a request, unless the server hasn't announced cheats: it would disconnect us.
	void Send(uint8_t* pData, int iSize);
	void SendSimple(uint8_t bySubOpcode);

	void OnSettings(const Cheats::Settings& settings);
	void ApplySetPlus(uint8_t bySlot, uint32_t dwItemID, int16_t sDurability);
	void ApplyRepairAll();
	void BuildIndicator();

	CGameProcMain* m_pMain = nullptr;
	bool m_bAvailable      = false;
	bool m_bHaveSettings   = false;
	Cheats::Settings m_Confirmed;
	std::map<uint32_t, std::bitset<1000>> m_Variants;
	std::vector<NpcHit> m_NpcHits;
	int m_iAnswerCount      = 0;
	CN3UIBase* m_pIndicator = nullptr;
};

#endif // CLIENT_WARFARE_CHEATS_H
