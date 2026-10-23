// Database parity probe (proof 2).
//
// Built twice, against the ODBC backend (SQL Server) and the SQLite backend, and run against
// databases seeded from the same OpenKO-db revision. It drives every stored procedure the
// servers call through the same generated wrappers (db-models) and db-library pool the servers
// use, plus the servers' raw queries, with the same inputs on both, and prints one JSON object
// per case: every result in the ODBC result sequence (row counts, result sets, errors), output
// parameters, return values. It then dumps every table. port/db/parity/compare.py diffs the two.
//
// usage: db-parity-probe <out.jsonl>
// SQLite backend: set OPENKO_SQLITE_DB to the database file.

#include <db-library/ConnectionManager.h>
#include <db-library/PoolConnection.h>
#include <db-library/StoredProc.h>

#include <ModelUtil/ModelUtil.h>
#include <StoredProc/StoredProc.h>

#include <nanodbc/nanodbc.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using modelUtil::DbType;

// Column names from OpenKO-db jsonSchema, so values are read the way the servers read them.
const std::set<std::string> BinaryColumns = { "ItemNum", "ItemSerial", "Mark", "Stash",
	"WarehouseData", "strChallengeList", "strItem", "strNewUserItem", "strNewUserSkill",
	"strNewWHItem", "strNewWHValue", "strNotice", "strOldUserItem", "strOldUserSkill",
	"strOldWHItem", "strOldWHValue", "strQuest", "strSerial", "strSkill", "strSkillData" };
const std::set<std::string> DateColumns = { "BuyTime", "CreateTime", "EditorTime", "PremiumExpire",
	"UpdateDate", "UpdateTime", "tWriteTime", "timeLender", "timeRegister", "timeRental" };
const std::set<std::string> FloatColumns = { "Ac", "Axe", "Bow", "Club", "Evasionrate", "Hitrate",
	"Hp", "Mp", "Pole", "ShortSword", "Sp", "Spear", "Staff", "Sword" };
// Written with GETDATE() by procedures and defaults: compared only for NULL-ness.
const std::set<std::string> VolatileColumns = { "UpdateTime", "CreateTime" };

std::string JsonString(const std::string& bytes)
{
	std::string out = "\"";
	for (unsigned char c : bytes)
	{
		if (c == '"' || c == '\\')
		{
			out += '\\';
			out += static_cast<char>(c);
		}
		else if (c < 0x20 || c >= 0x7F)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x", c);
			out += buf;
		}
		else
		{
			out += static_cast<char>(c);
		}
	}
	return out + "\"";
}

std::string ReadCell(const nanodbc::result& r, short col, const std::string& name)
{
	if (r.is_null(col))
		return "null";
	if (VolatileColumns.contains(name))
		return "\"<set>\"";
	if (BinaryColumns.contains(name))
	{
		std::vector<uint8_t> bytes = r.get<std::vector<uint8_t>>(col);
		std::string hex            = "\"0x";
		char buf[4];
		for (uint8_t b : bytes)
		{
			std::snprintf(buf, sizeof(buf), "%02x", b);
			hex += buf;
		}
		return hex + "\"";
	}
	if (DateColumns.contains(name))
	{
		nanodbc::timestamp ts = r.get<nanodbc::timestamp>(col);
		char buf[48];
		std::snprintf(buf, sizeof(buf), "\"%04d-%02d-%02d %02d:%02d:%02d.%03d\"", ts.year, ts.month,
			ts.day, ts.hour, ts.min, ts.sec, ts.fract / 1000000);
		return buf;
	}
	if (FloatColumns.contains(name))
	{
		char buf[48];
		std::snprintf(buf, sizeof(buf), "%.17g", r.get<double>(col));
		return buf;
	}
	return JsonString(r.get<std::string>(col));
}

std::string ReadRow(const nanodbc::result& r, const std::vector<std::string>& names)
{
	std::string row = "[";
	for (short i = 0; i < static_cast<short>(names.size()); i++)
	{
		if (i > 0)
			row += ",";
		row += ReadCell(r, i, names[static_cast<size_t>(i)]);
	}
	return row + "]";
}

std::string ErrorJson(const nanodbc::database_error& ex)
{
	return "{\"state\":" + JsonString(ex.state()) + ",\"native\":" + std::to_string(ex.native())
		   + "}";
}

// Walks the whole ODBC result sequence the way StoredProcedure::flush() does, recording it.
std::string DrainSequence(nanodbc::result& r)
{
	std::string steps = "[";
	bool firstStep    = true;
	try
	{
		do
		{
			if (!firstStep)
				steps += ",";
			firstStep  = false;

			short cols = r.columns();
			std::string step = "{\"affected\":" + std::to_string(r.affected_rows());
			if (cols > 0)
			{
				std::vector<std::string> names;
				std::string columns = "[";
				for (short i = 0; i < cols; i++)
				{
					names.push_back(r.column_name(i));
					columns += (i > 0 ? "," : "") + JsonString(names.back());
				}
				step += ",\"columns\":" + columns + "],\"rows\":[";
				bool firstRow = true;
				while (r.next())
				{
					step += (firstRow ? "" : ",") + ReadRow(r, names);
					firstRow = false;
				}
				step += "]";
			}
			steps += step + "}";
		}
		while (r.next_result());
	}
	catch (const nanodbc::database_error& ex)
	{
		steps += std::string(firstStep ? "" : ",") + "{\"error\":" + ErrorJson(ex) + "}";
	}
	return steps + "]";
}

class Probe
{
public:
	explicit Probe(std::ostream& out) : _out(out)
	{
	}

	/// \brief Runs a procedure case. `call` executes the generated wrapper and returns its result;
	/// `outputs` renders the output variables once the sequence has been drained.
	template <typename Proc, typename Call, typename Outputs>
	void ProcCase(const std::string& name, Call call, Outputs outputs)
	{
		std::string line = "{\"case\":" + JsonString(name) + ",\"kind\":\"proc\"";
		try
		{
			db::StoredProc<Proc> proc;
			auto weak   = call(proc);
			auto result = weak.lock();
			line += ",\"steps\":" + (result ? DrainSequence(*result) : std::string("null"));
			line += ",\"outputs\":" + outputs();
		}
		catch (const nanodbc::database_error& ex)
		{
			line += ",\"error\":" + ErrorJson(ex) + ",\"outputs\":" + outputs();
		}
		_out << line << "}\n";
	}

	/// \brief Runs a raw statement with bound parameters, as the servers' hand-written queries do.
	void SqlCase(const std::string& name, DbType dbType, const std::string& sql,
		const std::function<void(nanodbc::statement&)>& bind = nullptr)
	{
		std::string line = "{\"case\":" + JsonString(name) + ",\"kind\":\"sql\"";
		try
		{
			auto conn               = db::ConnectionManager::CreatePoolConnection(dbType);
			nanodbc::statement stmt = conn->CreateStatement(sql);
			if (bind)
				bind(stmt);
			nanodbc::result result = stmt.execute();
			line += ",\"steps\":" + DrainSequence(result);
		}
		catch (const nanodbc::database_error& ex)
		{
			line += ",\"error\":" + ErrorJson(ex);
		}
		_out << line << "}\n";
	}

	/// \brief Dumps a table with rows sorted client-side, so differing server-side order of ties
	/// (collation) doesn't show up as a difference; row order is compared separately by the
	/// startup row-count and loader checks.
	void DumpTable(const std::string& table)
	{
		std::string line = "{\"case\":" + JsonString("dump:" + table) + ",\"kind\":\"dump\"";
		try
		{
			auto conn               = db::ConnectionManager::CreatePoolConnection(DbType::GAME);
			nanodbc::statement stmt = conn->CreateStatement("SELECT * FROM [" + table + "]");
			nanodbc::result r       = stmt.execute();
			std::vector<std::string> names;
			std::string columns = "[";
			for (short i = 0; i < r.columns(); i++)
			{
				names.push_back(r.column_name(i));
				columns += (i > 0 ? "," : "") + JsonString(names.back());
			}
			std::vector<std::string> rows;
			while (r.next())
				rows.push_back(ReadRow(r, names));
			std::sort(rows.begin(), rows.end());
			line += ",\"columns\":" + columns + "],\"count\":" + std::to_string(rows.size())
					+ ",\"rows\":[";
			for (size_t i = 0; i < rows.size(); i++)
				line += (i > 0 ? "," : "") + rows[i];
			line += "]";
		}
		catch (const nanodbc::database_error& ex)
		{
			line += ",\"error\":" + ErrorJson(ex);
		}
		_out << line << "}\n";
	}

private:
	std::ostream& _out;
};

std::string Int(int64_t v)
{
	return std::to_string(v);
}

std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
{
	// Deterministic "inventory": item-like records with embedded zero bytes.
	std::vector<uint8_t> bytes(size);
	for (size_t i = 0; i < size; i++)
		bytes[i] = (i % 8 < 4) ? static_cast<uint8_t>((i * 37 + seed) & 0xFF) : 0;
	return bytes;
}

void RunCases(Probe& p)
{
	using namespace storedProc;

	// --- ACCOUNT_LOGIN: TB_USER has 'BitShift', ACCOUNT_CHAR has 'bitshift' ----------------
	struct Login
	{
		const char* name;
		const char* account;
		const char* password;
	};
	for (const Login& c : { Login { "login exact", "BitShift", "test" },
			 Login { "login lower id", "bitshift", "test" },
			 Login { "login upper id", "BITSHIFT", "test" },
			 Login { "login upper password", "bitshift", "TEST" },
			 Login { "login trailing spaces", "bitshift  ", "test  " },
			 Login { "login wrong password", "bitshift", "nope" },
			 Login { "login unknown account", "nobody_here", "test" },
			 Login { "login empty", "", "" },
			 Login { "login three chars", "twostars", "test" },
			 Login { "login one char", "testing2", "testing2" } })
	{
		int16_t ret = -99;
		p.ProcCase<AccountLogin>(
			c.name, [&](auto& proc) { return proc.execute(c.account, c.password, &ret); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- LOAD_ACCOUNT_CHARID (? = CALL) ------------------------------------------------------
	for (const char* account : { "bitshift", "BitShift", "TWOSTARS", "nobody_here" })
	{
		int ret = -99;
		p.ProcCase<LoadAccountCharid>(
			std::string("charids ") + account,
			[&](auto& proc) { return proc.execute(&ret, account); },
			[&] { return "{\"return\":" + Int(ret) + "}"; });
	}

	// --- LOAD_CHAR_INFO -------------------------------------------------------------------
	for (const char* charId : { "BitShift", "bitshift", "LORD_GANON", "nobody_here", "" })
	{
		int16_t ret = -99;
		p.ProcCase<LoadCharInfo>(
			std::string("charinfo ") + charId,
			[&](auto& proc) { return proc.execute(charId, &ret); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- LOAD_USER_DATA ------------------------------------------------------------------
	struct UserData
	{
		const char* name;
		const char* account;
		const char* charId;
	};
	for (const UserData& c : { UserData { "userdata own", "bitshift", "BitShift" },
			 UserData { "userdata mixed case", "BITSHIFT", "lord_ganon" },
			 UserData { "userdata not own", "bitshift", "test3" },
			 UserData { "userdata no account row", "nobody_here", "test3" },
			 UserData { "userdata full inventory", "twostars", "test3" },
			 UserData { "userdata unknown char", "twostars", "nobody_here" } })
	{
		int16_t ret = -99;
		p.ProcCase<LoadUserData>(
			c.name, [&](auto& proc) { return proc.execute(c.account, c.charId, &ret); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- LOAD_PREMIUM_SERVICE_USER ---------------------------------------------------------
	for (const char* account : { "kenner", "KENNER", "BitShift", "nobody_here" })
	{
		int32_t type = -99, days = -99;
		p.ProcCase<LoadPremiumServiceUser>(
			std::string("premium ") + account,
			[&](auto& proc) { return proc.execute(account, &type, &days); },
			[&] { return "{\"type\":" + Int(type) + ",\"days\":" + Int(days) + "}"; });
	}
	p.SqlCase("premium set future", DbType::GAME,
		"UPDATE [TB_USER] SET [PremiumExpire] = '2099-12-31 23:59:59' WHERE [strAccountID] = 'kenner'");
	{
		int32_t type = -99, days = -99;
		p.ProcCase<LoadPremiumServiceUser>(
			"premium future", [&](auto& proc) { return proc.execute("Kenner", &type, &days); },
			[&] { return "{\"type\":" + Int(type) + ",\"days\":" + Int(days) + "}"; });
	}

	// --- NATION_SELECT / CREATE_NEW_CHAR on fresh accounts ------------------------------------
	struct Nation
	{
		const char* name;
		const char* account;
		uint8_t nation;
	};
	for (const Nation& c : { Nation { "nation new karus", "probe_karus", 1 },
			 Nation { "nation new elmo", "probe_elmo", 2 },
			 Nation { "nation existing mixed case", "PROBE_KARUS", 1 },
			 Nation { "nation empty account", "", 1 } })
	{
		int16_t ret = -99;
		p.ProcCase<NationSelect>(
			c.name, [&](auto& proc) { return proc.execute(&ret, c.account, c.nation); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	struct NewChar
	{
		const char* name;
		const char* account;
		uint8_t index;
		const char* charId;
		uint8_t race;
	};
	for (const NewChar& c : { NewChar { "newchar slot0", "probe_karus", 0, "ProbeKarus1", 1 },
			 NewChar { "newchar slot0 again", "probe_karus", 0, "ProbeKarus2", 1 },
			 NewChar { "newchar slot2 empty middle", "probe_karus", 2, "ProbeKarus3", 2 },
			 NewChar { "newchar duplicate name other case", "probe_karus", 1, "probekarus1", 1 },
			 NewChar { "newchar duplicate seed name", "probe_karus", 1, "BITSHIFT", 1 },
			 NewChar { "newchar wrong race for nation", "probe_elmo", 0, "ProbeElmo1", 1 },
			 NewChar { "newchar elmo", "probe_elmo", 0, "ProbeElmo1", 12 },
			 NewChar { "newchar full account", "twostars", 0, "ProbeFull", 12 },
			 NewChar { "newchar unknown account", "nobody_here", 0, "ProbeOrphan", 1 },
			 NewChar { "newchar bad index", "probe_elmo", 7, "ProbeElmo2", 12 } })
	{
		int16_t ret = -99;
		p.ProcCase<CreateNewChar>(
			c.name,
			[&](auto& proc) {
				return proc.execute(&ret, c.account, c.index, c.charId, c.race, 101, 1, 2, 60, 60,
					60, 50, 50);
			},
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- UPDATE_USER_DATA / UPDATE_WAREHOUSE ---------------------------------------------------
	struct Update
	{
		const char* name;
		const char* charId;
		uint8_t zone;
		int16_t knights;
		size_t itemBytes;
	};
	for (const Update& c : { Update { "update full inventory", "ProbeKarus1", 21, 0, 400 },
			 Update { "update mixed case", "PROBEKARUS1", 21, 0, 120 },
			 Update { "update battle zone", "ProbeKarus1", 48, 5, 400 },
			 Update { "update unknown char", "nobody_here", 21, 0, 400 } })
	{
		std::vector<uint8_t> skill = Bytes(10, 1), item = Bytes(c.itemBytes, 2),
							 serial = Bytes(400, 3), quest = Bytes(40, 4);
		p.ProcCase<UpdateUserData>(
			c.name,
			[&](auto& proc) {
				return proc.execute(c.charId, 1, 1, 101, 2, 0, 0, 33, 123456, 77, 1, 0, c.knights, 0,
					900, 800, 100, 60, 60, 60, 50, 50, 1, 5, 99999, c.zone, -1, 81500, 43200, 0,
					1700000000, 3, skill, item, serial, quest, 10, 5);
			},
			[] { return std::string("{}"); });
	}
	p.SqlCase("expel ProbeKarus1", DbType::GAME,
		"UPDATE [USERDATA] SET [Knights] = -1, [Fame] = 3 WHERE [strUserId] = 'ProbeKarus1'");
	{
		std::vector<uint8_t> skill = Bytes(10, 5), item = Bytes(400, 6), serial = Bytes(400, 7),
							 quest = Bytes(0, 0);
		p.ProcCase<UpdateUserData>(
			"update expelled in battle zone",
			[&](auto& proc) {
				return proc.execute("ProbeKarus1", 1, 1, 101, 2, 0, 0, 34, 223456, 77, 1, 0, 9, 4,
					900, 800, 100, 60, 60, 60, 50, 50, 1, 5, 99999, 48, -1, 81500, 43200, 0,
					1700000001, 0, skill, item, serial, quest, 10, 5);
			},
			[] { return std::string("{}"); });
	}
	{
		std::vector<uint8_t> skill = Bytes(11, 1), item = Bytes(400, 2), serial = Bytes(400, 3),
							 quest = Bytes(40, 4);
		p.ProcCase<UpdateUserData>(
			"update skill too long",
			[&](auto& proc) {
				return proc.execute("ProbeKarus1", 1, 1, 101, 2, 0, 0, 33, 123456, 77, 1, 0, 0, 0,
					900, 800, 100, 60, 60, 60, 50, 50, 1, 5, 99999, 21, -1, 81500, 43200, 0,
					1700000002, 3, skill, item, serial, quest, 10, 5);
			},
			[] { return std::string("{}"); });
	}

	for (const char* account : { "probe_karus", "PROBE_KARUS", "nobody_here" })
	{
		std::vector<uint8_t> item = Bytes(1600, 8), serial = Bytes(1600, 9);
		p.ProcCase<UpdateWarehouse>(
			std::string("warehouse ") + account,
			[&](auto& proc) { return proc.execute(account, 12345, 1700000000, item, serial); },
			[] { return std::string("{}"); });
	}
	for (const char* charId : { "ProbeKarus1", "probekarus1", "BitShift", "nobody_here" })
	{
		int16_t ret = -99;
		p.ProcCase<LoadUserData>(
			std::string("userdata readback ") + charId,
			[&](auto& proc) { return proc.execute("probe_karus", charId, &ret); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- clans ------------------------------------------------------------------------------
	struct NewClan
	{
		const char* name;
		int16_t index;
		const char* clan;
		const char* chief;
	};
	for (const NewClan& c : { NewClan { "clan create", 30001, "ProbeClan", "ProbeKarus1" },
			 NewClan { "clan duplicate name other case", 30002, "PROBECLAN", "ProbeKarus3" },
			 NewClan { "clan duplicate index", 30001, "OtherClan", "ProbeKarus3" },
			 NewClan { "clan index zero", 0, "ZeroClan", "ProbeKarus3" } })
	{
		int16_t ret = -99;
		p.ProcCase<CreateKnights>(
			c.name, [&](auto& proc) { return proc.execute(&ret, c.index, 1, 1, c.clan, c.chief); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	struct ClanOp
	{
		const char* name;
		uint8_t type;
		const char* user;
		int16_t clan;
	};
	for (const ClanOp& c : { ClanOp { "clan join", 18, "ProbeKarus3", 30001 },
			 ClanOp { "clan join again", 18, "ProbeKarus3", 30001 },
			 ClanOp { "clan vice chief mixed case", 26, "probekarus3", 30001 },
			 ClanOp { "clan withdraw", 19, "PROBEKARUS3", 30001 },
			 ClanOp { "clan remove unknown user", 20, "nobody_here", 30001 },
			 ClanOp { "clan unknown clan", 18, "ProbeKarus3", 31999 },
			 ClanOp { "clan change chief", 25, "ProbeKarus3", 30001 },
			 ClanOp { "clan remove", 20, "ProbeKarus3", 30001 } })
	{
		int16_t ret = -99;
		p.ProcCase<UpdateKnights>(
			c.name, [&](auto& proc) { return proc.execute(&ret, c.type, c.user, c.clan, 0); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}
	for (int16_t clan : { int16_t(1), int16_t(30001), int16_t(31999) })
	{
		p.ProcCase<LoadKnightsMembers>(
			"clan members " + std::to_string(clan),
			[&](auto& proc) { return proc.execute(clan); }, [] { return std::string("{}"); });
	}
	for (int16_t clan : { int16_t(30001), int16_t(30001), int16_t(31999) })
	{
		int16_t ret = -99;
		p.ProcCase<DeleteKnights>(
			"clan delete " + std::to_string(clan),
			[&](auto& proc) { return proc.execute(&ret, clan); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- CLAIM_USER_RANK_STIPEND --------------------------------------------------------------
	struct Claim
	{
		const char* name;
		uint8_t type, rank, nation;
		const char* charId;
	};
	for (const Claim& c : { Claim { "claim knights rank karus", 0, 1, 1, "KA_RANK_1" },
			 Claim { "claim again", 0, 1, 1, "ka_rank_1" },
			 Claim { "claim wrong holder", 0, 2, 1, "KA_RANK_1" },
			 Claim { "claim personal elmo mixed case", 1, 1, 2, "el_rank_1" },
			 Claim { "claim bad type", 2, 1, 1, "KA_RANK_1" },
			 Claim { "claim rank zero", 0, 0, 1, "KA_RANK_1" },
			 Claim { "claim bad nation", 0, 1, 3, "KA_RANK_1" },
			 Claim { "claim missing rank row", 0, 100, 1, "KA_RANK_1" } })
	{
		uint8_t ret = 99;
		p.ProcCase<ClaimUserRankStipend>(
			c.name,
			[&](auto& proc) { return proc.execute(&ret, c.type, c.rank, c.nation, c.charId); },
			[&] { return "{\"nRet\":" + Int(ret) + "}"; });
	}

	// --- the servers' raw queries (Aujard / VersionManager / telnet) ---------------------------
	p.SqlCase("Q4 insert currentuser", DbType::GAME,
		"INSERT INTO [CURRENTUSER] ([strAccountID], [strCharID], [nServerNo], [strServerIP], "
		"[strClientIP]) VALUES ('probe_karus','ProbeKarus1',1,'127.0.0.1','127.0.0.1')");
	p.SqlCase("Q4 insert currentuser duplicate other case", DbType::GAME,
		"INSERT INTO [CURRENTUSER] ([strAccountID], [strCharID], [nServerNo], [strServerIP], "
		"[strClientIP]) VALUES ('PROBE_KARUS','ProbeKarus1',1,'127.0.0.1','127.0.0.1')");
	p.SqlCase("Q5 update currentuser mixed case", DbType::GAME,
		"UPDATE [CURRENTUSER] SET [nServerNo]=2, [strServerIP]='127.0.0.2' WHERE [strAccountID] = "
		"'Probe_Karus'");
	p.SqlCase("Q14 currentuser by pk", DbType::GAME,
		"SELECT [nServerNo], [strServerIP], [strAccountID] FROM [CURRENTUSER] WHERE [strAccountID] = ?",
		[](nanodbc::statement& s) { s.bind(0, "PROBE_karus"); });
	p.SqlCase("Q6 warehouse check", DbType::GAME,
		"SELECT [dwTime], [nMoney] FROM [WAREHOUSE] WHERE [strAccountID] = 'PROBE_KARUS'");
	p.SqlCase("Q7 userdata check", DbType::GAME,
		"SELECT [dwTime], [Exp] FROM [USERDATA] WHERE [strUserID] = 'probekarus1'");
	p.SqlCase("Q10 tb_user by pk", DbType::GAME,
		"SELECT [strAccountID], [strPasswd], [strAuthority] FROM [TB_USER] WHERE [strAccountID] = ?",
		[](nanodbc::statement& s) { s.bind(0, "BITSHIFT"); });
	p.SqlCase("Q9 update battle", DbType::GAME,
		"UPDATE BATTLE SET byNation = ?, strUserName = ? WHERE sIndex = 1",
		[](nanodbc::statement& s) {
			static const int nation = 2;
			s.bind(0, &nation);
			s.bind(1, "ProbeKarus1");
		});
	p.SqlCase("Q1 update concurrent", DbType::ACCOUNT,
		"UPDATE CONCURRENT SET [zone1_count] = ? WHERE [serverid] = ?", [](nanodbc::statement& s) {
			static const int count = 7, server = 1;
			s.bind(0, &count);
			s.bind(1, &server);
		});
	p.SqlCase("Q8 knights ranking", DbType::GAME,
		"SELECT [IDNum], [Nation], [Ranking], [IDName], [Members], [Points] FROM [KNIGHTS] WHERE "
		"[Nation] = 1 AND [Points] <> 0 ORDER BY [Points] DESC");

	for (const char* account : { "probe_karus", "Probe_Karus", "nobody_here" })
	{
		int16_t ret1 = -99, ret2 = -99;
		p.ProcCase<AccountLogout>(
			std::string("logout ") + account,
			[&](auto& proc) { return proc.execute(account, 1, &ret1, &ret2); },
			[&] { return "{\"nRet\":" + Int(ret1) + ",\"nRet2\":" + Int(ret2) + "}"; });
	}
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: db-parity-probe <out.jsonl>\n";
		return 2;
	}

	std::ofstream out(argv[1], std::ios::binary);
	db::ConnectionManager::Create();
	db::ConnectionManager::SetDatasourceConfig(DbType::GAME, "KN_online", "knight", "knight");
	db::ConnectionManager::SetDatasourceConfig(DbType::ACCOUNT, "KN_online", "knight", "knight");

	Probe probe(out);
	RunCases(probe);

	// Every table, after the cases above have run.
	std::vector<std::string> tables;
	{
		auto conn = db::ConnectionManager::CreatePoolConnection(DbType::GAME);
#if defined(OPENKO_DB_BACKEND_SQLITE)
		const char* listTables = "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT "
								 "LIKE '\\_%' ESCAPE '\\' ORDER BY name";
#else
		const char* listTables = "SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_TYPE "
								 "= 'BASE TABLE' ORDER BY TABLE_NAME";
#endif
		nanodbc::statement stmt = conn->CreateStatement(listTables);
		nanodbc::result r       = stmt.execute();
		while (r.next())
			tables.push_back(r.get<std::string>(0));
	}
	std::sort(tables.begin(), tables.end());
	for (const std::string& table : tables)
		probe.DumpTable(table);

	db::ConnectionManager::Destroy();
	std::cout << "db-parity-probe: wrote " << argv[1] << " (" << tables.size() << " tables)\n";
	return 0;
}
