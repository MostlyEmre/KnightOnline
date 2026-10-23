#include "Collation.h"

#include <array>

#include <sqlite3.h>

namespace db::sqlite
{

namespace
{

// Upper-case mapping for Windows-1252.
constexpr std::array<uint8_t, 256> BuildFoldTable()
{
	std::array<uint8_t, 256> table {};
	for (int i = 0; i < 256; i++)
		table[i] = static_cast<uint8_t>(i);

	for (int c = 'a'; c <= 'z'; c++)
		table[c] = static_cast<uint8_t>(c - 0x20);

	table[0x9A] = 0x8A; // š -> Š
	table[0x9C] = 0x8C; // œ -> Œ
	table[0x9E] = 0x8E; // ž -> Ž
	table[0xFF] = 0x9F; // ÿ -> Ÿ

	for (int c = 0xE0; c <= 0xFE; c++)
	{
		if (c != 0xF7) // ÷ has no case
			table[c] = static_cast<uint8_t>(c - 0x20);
	}

	return table;
}

constexpr std::array<uint8_t, 256> FoldTable = BuildFoldTable();

size_t TrimmedLength(const uint8_t* text, size_t len)
{
	while (len > 0 && text[len - 1] == ' ')
		--len;
	return len;
}

int CollationCallback(void* /*arg*/, int lhsLen, const void* lhs, int rhsLen, const void* rhs)
{
	return KoCompare(static_cast<const uint8_t*>(lhs), static_cast<size_t>(lhsLen),
		static_cast<const uint8_t*>(rhs), static_cast<size_t>(rhsLen));
}

} // namespace

int KoCompare(const uint8_t* lhs, size_t lhsLen, const uint8_t* rhs, size_t rhsLen)
{
	lhsLen     = TrimmedLength(lhs, lhsLen);
	rhsLen     = TrimmedLength(rhs, rhsLen);

	size_t len = lhsLen < rhsLen ? lhsLen : rhsLen;
	for (size_t i = 0; i < len; i++)
	{
		uint8_t a = FoldTable[lhs[i]];
		uint8_t b = FoldTable[rhs[i]];
		if (a != b)
			return a < b ? -1 : 1;
	}

	if (lhsLen == rhsLen)
		return 0;

	return lhsLen < rhsLen ? -1 : 1;
}

int RegisterCollations(sqlite3* db)
{
	return sqlite3_create_collation_v2(
		db, KoCollationName, SQLITE_UTF8, nullptr, &CollationCallback, nullptr);
}

} // namespace db::sqlite
