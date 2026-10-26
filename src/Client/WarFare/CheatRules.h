#ifndef CLIENT_WARFARE_CHEATRULES_H
#define CLIENT_WARFARE_CHEATRULES_H

#pragma once

#include <shared/CheatSettings.h>
#include <shared/packets.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// \brief The cheats window's rules that don't need the game: item search, the names of an
/// item's +levels, multiplier labels and result messages. Standard library only, so Cheats.Tests
/// can test them.
namespace Cheats
{

struct ItemName
{
	uint32_t dwID = 0;
	std::string szName;
};

namespace detail
{

inline std::string Lowercase(std::string_view text)
{
	std::string lower(text);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower;
}

} // namespace detail

/// \returns the IDs of the items whose names contain the query (ignoring case), sorted by name and
/// then ID, at most maxResults. Nameless items never match; a blank query matches nothing.
inline std::vector<uint32_t> SearchItems(std::span<const ItemName> items, std::string_view query, size_t maxResults = 200)
{
	const std::string needle = detail::Lowercase(query);
	if (needle.find_first_not_of(' ') == std::string::npos)
		return {};

	std::vector<const ItemName*> hits;
	for (const ItemName& item : items)
	{
		if (!item.szName.empty() && detail::Lowercase(item.szName).find(needle) != std::string::npos)
			hits.push_back(&item);
	}

	std::sort(hits.begin(), hits.end(),
		[](const ItemName* a, const ItemName* b) { return a->szName != b->szName ? a->szName < b->szName : a->dwID < b->dwID; });

	std::vector<uint32_t> ids;
	for (size_t i = 0; i < hits.size() && i < maxResults; i++)
		ids.push_back(hits[i]->dwID);

	return ids;
}

/// \returns the name of one of an item's IDs, as the item tooltip names it: a unique by its own
/// name; otherwise the line's name (when it isn't the plain upgrade line), the item's name and the
/// +level.
inline std::string VariantLabel(std::string_view baseName, int ext, std::string_view header, int magicOrRare)
{
	// e_ItemAttrib: 4 is unique
	if (magicOrRare == 4 && !header.empty())
		return std::string(header);

	std::string label;
	if (!header.empty() && header != "Upgrade")
		label = std::string(header) + " ";

	label          += baseName;

	const int plus  = PlusOf(ext);
	if (plus > 0)
		label += " (+" + std::to_string(plus) + ")";

	return label;
}

/// \returns how a multiplier reads in the window: "x1 (normal)", "x0.5", "x25"...
inline std::string MultiplierLabel(uint16_t pct)
{
	if (pct == 0)
		return "x0 (no damage)";

	if (pct == 100)
		return "x1 (normal)";

	std::string label = "x" + std::to_string(pct / 100);
	if (pct % 100 != 0)
	{
		if (pct < 100)
			label = "x0";

		label += "." + std::to_string(pct % 100 / 10);
	}

	return label;
}

/// \returns the message window's text for a request's result.
inline std::string ResultMessage(uint8_t opcode, uint8_t result)
{
	switch (result)
	{
		case CHEAT_OK:
			switch (opcode)
			{
				case CHEAT_SET_LEVEL:
					return "Level set.";
				case CHEAT_ADD_POINTS:
					return "Points added.";
				case CHEAT_RESET_POINTS:
					return "Points reset.";
				case CHEAT_REFILL:
					return "HP and MP refilled.";
				case CHEAT_SET_CURRENCY:
					return "Amount set.";
				case CHEAT_GIVE_ITEM:
					return "Item given.";
				case CHEAT_SET_PLUS:
					return "+level set.";
				case CHEAT_REPAIR_ALL:
					return "Everything repaired.";
				case CHEAT_TELEPORT_ZONE:
				case CHEAT_TELEPORT_NPC:
					return "Teleporting.";
				case CHEAT_KILL_AROUND:
					return "Monsters around you are dying.";
				default:
					return "Done.";
			}

		case CHEAT_NOT_ALLOWED:
			return "This server doesn't allow cheats.";
		case CHEAT_OFF:
			return "Turn cheats on first.";
		case CHEAT_BAD_VALUE:
			return "That value isn't allowed.";
		case CHEAT_DEAD:
			return "You can't do that while dead.";
		case CHEAT_BAG_FULL:
			return "Your bag is full.";
		case CHEAT_NO_SUCH_ITEM:
			return "That item doesn't exist on this server.";
		case CHEAT_NO_PLUS:
			return "That item has no such +level.";
		case CHEAT_EMPTY_SLOT:
			return "Nothing is equipped there.";
		case CHEAT_BUSY:
			return "Wait until you've finished moving or trading.";
		case CHEAT_NOT_FOUND:
			return "Not found.";
		case CHEAT_NO_AI:
			return "The monster server isn't connected.";
		default:
			return "Something went wrong.";
	}
}

} // namespace Cheats

#endif // CLIENT_WARFARE_CHEATRULES_H
