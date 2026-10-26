#include "StdAfx.h"
#include "Cheats.h"
#include "APISocket.h"
#include "CheatRules.h"
#include "GameProcMain.h"
#include "IconItemSkill.h"
#include "N3UIIcon.h"
#include "PacketDef.h"
#include "PlayerMySelf.h"
#include "UIArt.h"
#include "UIHotKeyDlg.h"
#include "UIInventory.h"
#include "UIManager.h"
#include "UISkillTreeDlg.h"

#include <array>

namespace
{

constexpr int INDICATOR_WIDTH = 110;

// Below Genie's indicator (at 64), so both can show.
constexpr int INDICATOR_TOP   = 96;

constexpr uint32_t COLOR_GOOD = 0xff80ff80;
constexpr uint32_t COLOR_BAD  = 0xffff3b3b;

} // namespace

CCheats::CCheats(CGameProcMain* pMain) : m_pMain(pMain)
{
}

CCheats::~CCheats()
{
	delete m_pIndicator;
}

bool CCheats::IsAvailable() const
{
	return m_bAvailable;
}

const Cheats::Settings& CCheats::Confirmed() const
{
	return m_Confirmed;
}

bool CCheats::IsOn(bool Cheats::Settings::* flag) const
{
	return m_bAvailable && Cheats::EffectiveFlag(m_Confirmed, flag);
}

bool CCheats::IsOnNow(bool Cheats::Settings::* flag)
{
	const CGameProcMain* pMain = CGameProcedure::s_pProcMain;
	return pMain != nullptr && pMain->m_pCheats != nullptr && pMain->m_pCheats->IsOn(flag);
}

void CCheats::OnLeaveGame()
{
	m_bAvailable    = false;
	m_bHaveSettings = false;
	m_Confirmed     = {};
	m_Variants.clear();
	m_NpcHits.clear();
}

const std::bitset<1000>* CCheats::Variants(uint32_t dwBaseID) const
{
	auto itr = m_Variants.find(dwBaseID);
	return itr != m_Variants.end() ? &itr->second : nullptr;
}

const std::vector<CCheats::NpcHit>& CCheats::NpcHits() const
{
	return m_NpcHits;
}

int CCheats::AnswerCount() const
{
	return m_iAnswerCount;
}

void CCheats::Receive(Packet& pkt)
{
	const uint8_t bySubOpcode = pkt.read<uint8_t>();
	switch (bySubOpcode)
	{
		case CHEAT_AVAILABLE:
			m_bAvailable = pkt.read<uint8_t>() != 0;
			if (!m_bAvailable)
			{
				m_bHaveSettings = false;
				m_Confirmed     = {};
			}
			break;

		case CHEAT_SETTINGS:
		{
			std::array<uint8_t, Cheats::SETTINGS_SIZE> bytes {};
			for (uint8_t& byte : bytes)
				byte = pkt.read<uint8_t>();

			if (const auto settings = Cheats::Read(bytes); settings.has_value())
				OnSettings(*settings);
		}
		break;

		case CHEAT_RESULT:
		{
			const uint8_t byRequest = pkt.read<uint8_t>();
			const uint8_t byResult  = pkt.read<uint8_t>();
			m_pMain->MsgOutput(Cheats::ResultMessage(byRequest, byResult), byResult == CHEAT_OK ? COLOR_GOOD : COLOR_BAD);
		}
		break;

		case CHEAT_ITEM_VARIANTS:
		{
			const uint32_t dwBaseID = pkt.read<uint32_t>();
			std::bitset<1000> bits;
			for (int iByte = 0; iByte < 125; iByte++)
			{
				const uint8_t byBits = pkt.read<uint8_t>();
				for (int iBit = 0; iBit < 8; iBit++)
					bits[iByte * 8 + iBit] = ((byBits >> iBit) & 1) != 0;
			}

			m_Variants[dwBaseID] = bits;
			++m_iAnswerCount;
		}
		break;

		case CHEAT_SET_PLUS:
		{
			const uint8_t bySlot      = pkt.read<uint8_t>();
			const uint32_t dwItemID   = pkt.read<uint32_t>();
			const int16_t sDurability = pkt.read<int16_t>();
			ApplySetPlus(bySlot, dwItemID, sDurability);
		}
		break;

		case CHEAT_REPAIR_ALL:
			ApplyRepairAll();
			break;

		case CHEAT_FIND_NPC:
		{
			m_NpcHits.clear();
			const uint8_t byCount = pkt.read<uint8_t>();
			for (int i = 0; i < byCount; i++)
			{
				NpcHit hit;
				hit.sID   = pkt.read<int16_t>();
				hit.sZone = pkt.read<int16_t>();
				pkt.readString(hit.szName, pkt.read<uint8_t>());
				m_NpcHits.push_back(std::move(hit));
			}

			++m_iAnswerCount;
		}
		break;

		default:
			break;
	}
}

void CCheats::OnSettings(const Cheats::Settings& settings)
{
	const bool bWasOn = m_bHaveSettings && m_Confirmed.bOn;
	const bool bFirst = !m_bHaveSettings;
	m_Confirmed       = settings;
	m_bHaveSettings   = true;

	// Infinite MP changes which skills can be cast.
	if (m_pMain->m_pUIHotKeyDlg != nullptr)
		m_pMain->m_pUIHotKeyDlg->UpdateDisableCheck();
	if (m_pMain->m_pUISkillTreeDlg != nullptr)
		m_pMain->m_pUISkillTreeDlg->UpdateDisableCheck();

	if (settings.bOn != bWasOn && !(bFirst && !settings.bOn))
		m_pMain->MsgOutput(settings.bOn ? "Cheats are on." : "Cheats are off.", COLOR_GOOD);
}

void CCheats::ApplySetPlus(uint8_t bySlot, uint32_t dwItemID, int16_t sDurability)
{
	if (bySlot >= ITEM_SLOT_COUNT || m_pMain->m_pUIInventory == nullptr)
		return;

	__IconItemSkill* pItem = m_pMain->m_pUIInventory->m_pMySlot[bySlot];
	if (pItem == nullptr)
		return;

	// Found as the anvil finds an upgraded item (CUIItemUpgrade).
	__TABLE_ITEM_BASIC* pItemBasic = CGameBase::s_pTbl_Items_Basic.Find(dwItemID / 1000 * 1000);
	__TABLE_ITEM_EXT* pItemExt     = nullptr;
	if (pItemBasic != nullptr && pItemBasic->byExtIndex >= 0 && pItemBasic->byExtIndex < MAX_ITEM_EXTENSION)
		pItemExt = CGameBase::s_pTbl_Items_Exts[pItemBasic->byExtIndex].Find(dwItemID % 1000);

	if (pItemBasic == nullptr || pItemExt == nullptr)
		return;

	std::string szIconFN;
	e_PartPosition ePart = PART_POS_UNKNOWN;
	e_PlugPosition ePlug = PLUG_POS_UNKNOWN;
	if (CGameBase::MakeResrcFileNameForUPC(pItemBasic, pItemExt, nullptr, &szIconFN, ePart, ePlug, CGameBase::s_pPlayer->m_InfoBase.eRace)
		== ITEM_TYPE_UNKNOWN)
		return;

	pItem->pItemBasic  = pItemBasic;
	pItem->pItemExt    = pItemExt;
	pItem->iDurability = sDurability;
	if (pItem->pUIIcon != nullptr)
	{
		pItem->pUIIcon->SetTex(szIconFN);
		if (sDurability > 0)
			pItem->pUIIcon->SetStyle(pItem->pUIIcon->GetStyle() & ~UISTYLE_DURABILITY_EXHAUST);
	}

	// The model and the durability on the character.
	m_pMain->m_pUIInventory->SlotItemChanged(static_cast<e_ItemSlot>(bySlot));
}

void CCheats::ApplyRepairAll()
{
	CUIInventory* pInventory = m_pMain->m_pUIInventory;
	if (pInventory == nullptr)
		return;

	// As Genie's repair does (and CItemRepairMgr): full durability, no broken look.
	auto repair = [](__IconItemSkill* pItem)
	{
		if (pItem == nullptr || pItem->pItemBasic == nullptr || pItem->pItemExt == nullptr)
			return false;

		pItem->iDurability = pItem->pItemBasic->siMaxDurability + pItem->pItemExt->siMaxDurability;
		if (pItem->pUIIcon != nullptr)
			pItem->pUIIcon->SetStyle(pItem->pUIIcon->GetStyle() & ~UISTYLE_DURABILITY_EXHAUST);
		return true;
	};

	for (int iSlot = 0; iSlot < ITEM_SLOT_COUNT; iSlot++)
	{
		if (repair(pInventory->m_pMySlot[iSlot]))
			CGameProcedure::s_pPlayer->DurabilitySet(static_cast<e_ItemSlot>(iSlot), pInventory->m_pMySlot[iSlot]->iDurability);
	}

	for (__IconItemSkill* pItem : pInventory->m_pMyInvWnd)
		repair(pItem);
}

void CCheats::Send(uint8_t* pData, int iSize)
{
	// Never to a server that hasn't said it knows WIZ_CHEAT: it would disconnect us.
	if (!m_bAvailable || CGameProcedure::s_pSocket == nullptr || !CGameProcedure::s_pSocket->IsConnected())
		return;

	CGameProcedure::s_pSocket->Send(pData, iSize);
}

void CCheats::SendSimple(uint8_t bySubOpcode)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, bySubOpcode);
	Send(byBuff, iOffset);
}

void CCheats::SendSettings(const Cheats::Settings& settings)
{
	std::array<uint8_t, Cheats::SETTINGS_SIZE> bytes {};
	Cheats::Write(settings, bytes);

	uint8_t byBuff[32] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_SETTINGS);
	for (uint8_t byte : bytes)
		CAPISocket::MP_AddByte(byBuff, iOffset, byte);
	Send(byBuff, iOffset);
}

void CCheats::SendSetLevel(int iLevel)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_SET_LEVEL);
	CAPISocket::MP_AddByte(byBuff, iOffset, static_cast<uint8_t>(iLevel));
	Send(byBuff, iOffset);
}

void CCheats::SendAddPoints(uint8_t byKind)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_ADD_POINTS);
	CAPISocket::MP_AddByte(byBuff, iOffset, byKind);
	Send(byBuff, iOffset);
}

void CCheats::SendResetPoints(uint8_t byKind)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_RESET_POINTS);
	CAPISocket::MP_AddByte(byBuff, iOffset, byKind);
	Send(byBuff, iOffset);
}

void CCheats::SendRefill()
{
	SendSimple(CHEAT_REFILL);
}

void CCheats::SendSetCurrency(uint8_t byKind, uint32_t dwValue)
{
	uint8_t byBuff[8] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_SET_CURRENCY);
	CAPISocket::MP_AddByte(byBuff, iOffset, byKind);
	CAPISocket::MP_AddDword(byBuff, iOffset, dwValue);
	Send(byBuff, iOffset);
}

void CCheats::SendItemVariants(uint32_t dwBaseID)
{
	uint8_t byBuff[8] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_ITEM_VARIANTS);
	CAPISocket::MP_AddDword(byBuff, iOffset, dwBaseID);
	Send(byBuff, iOffset);
}

void CCheats::SendGiveItem(uint32_t dwItemID, uint16_t wCount)
{
	uint8_t byBuff[8] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_GIVE_ITEM);
	CAPISocket::MP_AddDword(byBuff, iOffset, dwItemID);
	CAPISocket::MP_AddWord(byBuff, iOffset, wCount);
	Send(byBuff, iOffset);
}

void CCheats::SendSetPlus(uint8_t bySlot, uint8_t byPlus)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_SET_PLUS);
	CAPISocket::MP_AddByte(byBuff, iOffset, bySlot);
	CAPISocket::MP_AddByte(byBuff, iOffset, byPlus);
	Send(byBuff, iOffset);
}

void CCheats::SendRepairAll()
{
	SendSimple(CHEAT_REPAIR_ALL);
}

void CCheats::SendTeleportZone(int16_t sZone)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_TELEPORT_ZONE);
	CAPISocket::MP_AddShort(byBuff, iOffset, sZone);
	Send(byBuff, iOffset);
}

void CCheats::SendFindNpc(const std::string& szQuery)
{
	// NPC names are at most 30 characters (MAX_NPC_NAME_SIZE on the server).
	const std::string szSent = szQuery.substr(0, 30);

	uint8_t byBuff[40] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_FIND_NPC);
	CAPISocket::MP_AddByte(byBuff, iOffset, static_cast<uint8_t>(szSent.size()));
	CAPISocket::MP_AddString(byBuff, iOffset, szSent);
	Send(byBuff, iOffset);
}

void CCheats::SendTeleportNpc(int16_t sNpcID)
{
	uint8_t byBuff[4] {};
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_CHEAT);
	CAPISocket::MP_AddByte(byBuff, iOffset, CHEAT_TELEPORT_NPC);
	CAPISocket::MP_AddShort(byBuff, iOffset, sNpcID);
	Send(byBuff, iOffset);
}

void CCheats::SendKillAround()
{
	SendSimple(CHEAT_KILL_AROUND);
}

void CCheats::BuildIndicator()
{
	m_pIndicator = new CN3UIBase();
	m_pIndicator->Init(nullptr);
	const RECT rc = UIArt::MakeRect(0, 0, INDICATOR_WIDTH, UIArt::BAR_HEIGHT);
	m_pIndicator->SetRegion(rc);
	UIArt::AddBar(m_pIndicator, rc, UIArt::BAR_BLUE);
	UIArt::AddString(m_pIndicator, UIArt::OffsetBy(rc, 1, 1), "Cheats", 11, true, UIArt::COLOR_SHADOW, UISTYLE_STRING_ALIGNCENTER);
	UIArt::AddString(m_pIndicator, rc, "Cheats", 11, true, UIArt::COLOR_BUTTON, UISTYLE_STRING_ALIGNCENTER);
}

void CCheats::RenderIndicator()
{
	if (!m_bAvailable || !m_Confirmed.bOn)
		return;

	if (m_pIndicator == nullptr)
		BuildIndicator();

	// Top middle of the screen, under Genie's.
	const int iLeft = static_cast<int>((CN3Base::s_CameraData.vp.Width - INDICATOR_WIDTH) / 2);
	const RECT& rc  = m_pIndicator->GetRegion();
	if (rc.left != iLeft || rc.top != INDICATOR_TOP)
		m_pIndicator->MoveOffset(iLeft - rc.left, INDICATOR_TOP - rc.top);

	CUIManager::RenderStateSet();
	m_pIndicator->Render();
	CUIManager::RenderStateRestore();
}
