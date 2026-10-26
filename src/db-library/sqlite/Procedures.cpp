// C++ reimplementations of the T-SQL stored procedures the servers call
// (OpenKO-db ManualSetup/8_CreateStoredProc_*.sql). Each one follows its T-SQL statement
// by statement, keeping the behaviour the servers can observe:
// - variables and OUTPUT-only parameters start NULL, and comparisons use three-valued logic;
// - string comparisons are case-insensitive and ignore trailing spaces (database collation);
// - every count-producing statement hands the client a row count (NOCOUNT is OFF);
// - @@ERROR reflects only the statement immediately before it;
// - a failing statement raises its error to the client and execution continues.

#include "Collation.h"
#include "ProcContext.h"

#include <ctime>
#include <optional>
#include <string>

namespace db::sqlite
{

namespace
{

using Tri  = std::optional<bool>;
using Text = std::optional<std::string>;
using Int  = std::optional<int64_t>;

Text AsText(const Value& v, size_t maxLength = std::string::npos)
{
	if (v.IsNull())
		return std::nullopt;
	std::string s = v.Kind == Value::Type::Integer ? std::to_string(v.Int) : v.Bytes;
	if (s.size() > maxLength)
		s.resize(maxLength);
	return s;
}

Int AsInt(const Value& v)
{
	if (v.IsNull())
		return std::nullopt;
	if (v.Kind == Value::Type::Integer)
		return v.Int;
	if (v.Kind == Value::Type::Real)
		return static_cast<int64_t>(v.Real);
	return std::stoll(v.Bytes);
}

Value TextValue(const Text& t)
{
	return t.has_value() ? Value::Text(*t) : Value::Null();
}

Value BlobValue(const Text& t)
{
	return t.has_value() ? Value::Blob(*t) : Value::Null();
}

Value IntValue(const Int& i)
{
	return i.has_value() ? Value::Integer(*i) : Value::Null();
}

Tri Eq(const Text& a, const Text& b)
{
	if (!a || !b)
		return std::nullopt;
	return KoEquals(*a, *b);
}

Tri Ne(const Text& a, const Text& b)
{
	Tri eq = Eq(a, b);
	return eq.has_value() ? Tri(!*eq) : std::nullopt;
}

Tri Cmp(const Int& a, bool (*op)(int64_t, int64_t), int64_t b)
{
	if (!a)
		return std::nullopt;
	return op(*a, b);
}

Tri And(Tri a, Tri b)
{
	if (a == false || b == false)
		return false;
	if (!a || !b)
		return std::nullopt;
	return true;
}

Tri Or(Tri a, Tri b)
{
	if (a == true || b == true)
		return true;
	if (!a || !b)
		return std::nullopt;
	return false;
}

bool Lt(int64_t a, int64_t b)
{
	return a < b;
}
bool Le(int64_t a, int64_t b)
{
	return a <= b;
}
bool Gt(int64_t a, int64_t b)
{
	return a > b;
}
bool Ge(int64_t a, int64_t b)
{
	return a >= b;
}
bool EqI(int64_t a, int64_t b)
{
	return a == b;
}
bool NeI(int64_t a, int64_t b)
{
	return a != b;
}

Value Col(const std::optional<std::vector<Value>>& row, size_t i)
{
	return row.has_value() && i < row->size() ? (*row)[i] : Value::Null();
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y                  -= m <= 2;
	const int64_t era   = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe  = static_cast<unsigned>(y - era * 400);
	const unsigned doy  = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	const unsigned doe  = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// DATEDIFF(dd, GETDATE(), value): midnights crossed between today (local) and value.
Int DateDiffDaysFromNow(const Value& value)
{
	if (value.Kind != Value::Type::Text)
		return std::nullopt;
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
	if (!ParseDateTime(value.Bytes, y, mo, d, h, mi, s, ms))
		return std::nullopt;

	std::tm local = LocalTime(std::time(nullptr));
	int64_t today = DaysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
		static_cast<unsigned>(local.tm_mday));
	return DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) - today;
}

} // namespace

// ACCOUNT_LOGIN @AccountID varchar(21), @Password varchar(13), @nRet smallint OUTPUT
void Proc_ACCOUNT_LOGIN(ProcContext& ctx)
{
	Text accountId   = ctx.ArgText(0, 21);
	Text password    = ctx.ArgText(1, 13);

	int64_t rowCount = 0;
	auto row         = ctx.AssignSelect(
        "SELECT strPasswd FROM TB_USER WHERE strAccountID = ? AND strAuthority != 255",
        { TextValue(accountId) }, &rowCount);
	Text pwd = AsText(Col(row, 0), 13);

	if (IsTrue(Or(rowCount == 0, Ne(pwd, password))))
	{
		ctx.SetOutput(2, Value::Integer(0));
		return;
	}

	row = ctx.AssignSelect("SELECT bNation, bCharNum FROM ACCOUNT_CHAR WHERE strAccountID = ?",
		{ TextValue(accountId) }, &rowCount);
	Int nation  = AsInt(Col(row, 0));
	Int charNum = AsInt(Col(row, 1));

	if (IsTrue(Or(rowCount == 0, Cmp(charNum, EqI, 0))))
		ctx.SetOutput(2, Value::Integer(1));
	else
		ctx.SetOutput(2, nation.has_value() ? Value::Integer(*nation + 1) : Value::Null());
}

// ACCOUNT_LOGOUT @AccountID varchar(21), @LogoutCode int, @nRet smallint OUTPUT, @nRet2 smallint OUTPUT
void Proc_ACCOUNT_LOGOUT(ProcContext& ctx)
{
	Text accountId = ctx.ArgText(0, 21);
	ctx.Dml("DELETE FROM CURRENTUSER WHERE strAccountID = ?", { TextValue(accountId) });
	ctx.SetOutput(2, Value::Integer(1));
	ctx.SetOutput(3, Value::Integer(1));
}

// LOAD_ACCOUNT_CHARID @Accountid varchar(21)  -- RETURN @@ROWCOUNT
void Proc_LOAD_ACCOUNT_CHARID(ProcContext& ctx)
{
	Text accountId = ctx.ArgText(0, 21);
	RowSet rows    = ctx.Select("SELECT strCharID1, strCharID2, strCharID3, NULL, NULL FROM "
								   "ACCOUNT_CHAR WHERE strAccountID = ?",
		   { TextValue(accountId) }, { "strCharID1", "strCharID2", "strCharID3", "", "" });
	int64_t count  = static_cast<int64_t>(rows.Rows.size());
	ctx.Rows(std::move(rows));
	ctx.SetReturn(count);
}

// LOAD_CHAR_INFO @CharId varchar(21), @nRet smallint OUTPUT
void Proc_LOAD_CHAR_INFO(ProcContext& ctx)
{
	Text charId = ctx.ArgText(0, 21);

	auto row    = ctx.AssignSelect(
        "SELECT COUNT(strUserId) FROM USERDATA WHERE strUserId = ?", { TextValue(charId) });
	Int count = AsInt(Col(row, 0));
	ctx.SetOutput(1, IntValue(count));
	if (IsTrue(Cmp(count, EqI, 0)))
		return;

	ctx.Rows(ctx.Select("SELECT Race, Class, HairColor, [Level], Face, Zone, strItem FROM USERDATA "
						"WHERE strUserID = ?",
		{ TextValue(charId) },
		{ "Race", "Class", "HairColor", "Level", "Face", "Zone", "strItem" }));
	ctx.SetOutput(1, Value::Integer(1));
}

// LOAD_KNIGHTS_MEMBERS @knightsindex smallint
void Proc_LOAD_KNIGHTS_MEMBERS(ProcContext& ctx)
{
	Int knightsIndex = ctx.ArgInt(0);
	ctx.Rows(ctx.Select("SELECT strUserId, Fame, [Level], Class FROM USERDATA WHERE Knights = ?",
		{ IntValue(knightsIndex) }, { "strUserId", "Fame", "Level", "Class" }));
}

// LOAD_USER_DATA @AccountID varchar(21), @id varchar(21), @nRet smallint OUTPUT
void Proc_LOAD_USER_DATA(ProcContext& ctx)
{
	Text accountId = ctx.ArgText(0, 21);
	Text id        = ctx.ArgText(1, 21);

	ctx.SetOutput(2, Value::Integer(0));

	auto row = ctx.AssignSelect(
		"SELECT IFNULL(strCharID1, ''), IFNULL(strCharID2, ''), IFNULL(strCharID3, '') "
		"FROM ACCOUNT_CHAR WHERE strAccountID = ?",
		{ TextValue(accountId) });
	Text charId1 = AsText(Col(row, 0), 21);
	Text charId2 = AsText(Col(row, 1), 21);
	Text charId3 = AsText(Col(row, 2), 21);

	// @id NOT IN (@charid1, @charid2, @charid3): with no ACCOUNT_CHAR row this is UNKNOWN and
	// the procedure carries on.
	if (IsTrue(And(And(Ne(id, charId1), Ne(id, charId2)), Ne(id, charId3))))
		return;

	RowSet rows = ctx.Select(
		"SELECT Nation, Race, Class, HairColor, Rank, Title, [Level], [Exp], Loyalty, Face, City, "
		"Knights, Fame, Hp, Mp, Sp, Strong, Sta, Dex, Intel, Cha, Authority, Points, Gold, [Zone], "
		"Bind, PX, PZ, PY, dwTime, strSkill, strItem, strSerial, sQuestCount, strQuest, "
		"MannerPoint, "
		"LoyaltyMonthly FROM USERDATA WHERE strUserId = ?",
		{ TextValue(id) },
		{ "Nation", "Race", "Class", "HairColor", "Rank", "Title", "Level", "Exp", "Loyalty",
			"Face", "City", "Knights", "Fame", "Hp", "Mp", "Sp", "Strong", "Sta", "Dex", "Intel",
			"Cha", "Authority", "Points", "Gold", "Zone", "Bind", "PX", "PZ", "PY", "dwTime",
			"strSkill", "strItem", "strSerial", "sQuestCount", "strQuest", "MannerPoint",
			"LoyaltyMonthly" });
	int64_t count = static_cast<int64_t>(rows.Rows.size());
	ctx.Rows(std::move(rows));
	ctx.SetOutput(2, Value::Integer(count));
}

// LOAD_PREMIUM_SERVICE_USER @AccountID varchar(21), @type int OUTPUT, @days int OUTPUT
void Proc_LOAD_PREMIUM_SERVICE_USER(ProcContext& ctx)
{
	Text accountId = ctx.ArgText(0, 21);

	auto row       = ctx.AssignSelect(
        "SELECT PremiumExpire FROM TB_USER WHERE strAccountID = ?", { TextValue(accountId) });
	Int days = DateDiffDaysFromNow(Col(row, 0));
	Int type = 1;

	// IF @days <= 0 OR @@ERROR <> 0 OR @days IS NULL
	if (IsTrue(Or(Cmp(days, Le, 0), !days.has_value())))
	{
		type = 0;
		days = 0;
	}

	ctx.SetOutput(1, IntValue(type));
	ctx.SetOutput(2, IntValue(days));
}

// NATION_SELECT @nRet smallint OUTPUT, @AccountID varchar(21), @Nation tinyint
void Proc_NATION_SELECT(ProcContext& ctx)
{
	Text accountId = ctx.ArgText(1, 21);
	Int nation     = ctx.ArgInt(2);

	ctx.BeginTran();

	auto row = ctx.AssignSelect(
		"SELECT COUNT(*) FROM ACCOUNT_CHAR WHERE strAccountID = ?", { TextValue(accountId) });
	if (IsTrue(Cmp(AsInt(Col(row, 0)), Gt, 0)))
		ctx.Dml("UPDATE ACCOUNT_CHAR SET bNation = ? WHERE strAccountID = ?",
			{ IntValue(nation), TextValue(accountId) });
	else
		ctx.Dml("INSERT INTO ACCOUNT_CHAR (strAccountID, bNation) VALUES (?, ?)",
			{ TextValue(accountId), IntValue(nation) });

	row = ctx.AssignSelect(
		"SELECT COUNT(*) FROM WAREHOUSE WHERE strAccountID = ?", { TextValue(accountId) });
	int lastError = 0;
	if (IsTrue(Cmp(AsInt(Col(row, 0)), EqI, 0)))
		lastError = ctx.Dml(
			"INSERT INTO WAREHOUSE (strAccountID) VALUES (?)", { TextValue(accountId) });

	if (lastError != 0)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(-2));
		return;
	}

	ctx.CommitTran();
	ctx.SetOutput(0, Value::Integer(1));
}

// CREATE_NEW_CHAR @nRet smallint OUTPUT, @AccountID varchar(21), @index tinyint, @CharID varchar(21),
//   @Race tinyint, @Class smallint, @Hair tinyint, @Face tinyint, @Str .. @Cha tinyint
void Proc_CREATE_NEW_CHAR(ProcContext& ctx)
{
	Text accountId = ctx.ArgText(1, 21);
	Int index      = ctx.ArgInt(2);
	Text charId    = ctx.ArgText(3, 21);
	Int race       = ctx.ArgInt(4);
	Int nRet; // OUTPUT-only: starts NULL

	auto row = ctx.AssignSelect("SELECT bNation, bCharNum FROM ACCOUNT_CHAR WHERE strAccountID = ?",
		{ TextValue(accountId) });
	Int nation   = AsInt(Col(row, 0));
	Int bCharNum = AsInt(Col(row, 1));

	if (IsTrue(Cmp(bCharNum, Ge, 3)))
		nRet = 1;

	// IF .. SET @nRet = 2 ELSE IF .. SET @nRet = 2 ELSE IF .. SET @nRet = 2
	if (IsTrue(And(Cmp(nation, EqI, 1), Cmp(race, Gt, 10)))
		|| IsTrue(And(Cmp(nation, EqI, 2), Cmp(race, Lt, 10)))
		|| IsTrue(And(Cmp(nation, NeI, 1), Cmp(nation, NeI, 2))))
		nRet = 2;

	if (IsTrue(Cmp(nRet, Gt, 0)))
	{
		ctx.SetOutput(0, IntValue(nRet));
		return;
	}

	row = ctx.AssignSelect(
		"SELECT COUNT(*) FROM USERDATA WHERE strUserId = ?", { TextValue(charId) });
	if (IsTrue(Cmp(AsInt(Col(row, 0)), Gt, 0)))
	{
		ctx.SetOutput(0, Value::Integer(3));
		return;
	}

	const int64_t zone = 21;
	row                = ctx.AssignSelect(
        "SELECT InitX, InitZ FROM ZONE_INFO WHERE ZoneNo = ?", { Value::Integer(zone) });
	Value posX = Col(row, 0);
	Value posZ = Col(row, 1);

	ctx.BeginTran();

	if (IsTrue(Cmp(index, EqI, 0)))
		ctx.Dml("UPDATE ACCOUNT_CHAR SET strCharID1 = ?, bCharNum = bCharNum + 1 WHERE "
				"strAccountID = ? AND strCharID1 IS NULL",
			{ TextValue(charId), TextValue(accountId) });
	else if (IsTrue(Cmp(index, EqI, 1)))
		ctx.Dml("UPDATE ACCOUNT_CHAR SET strCharID2 = ?, bCharNum = bCharNum + 1 WHERE "
				"strAccountID = ? AND strCharID2 IS NULL",
			{ TextValue(charId), TextValue(accountId) });
	else if (IsTrue(Cmp(index, EqI, 2)))
		ctx.Dml("UPDATE ACCOUNT_CHAR SET strCharID3 = ?, bCharNum = bCharNum + 1 WHERE "
				"strAccountID = ? AND strCharID3 IS NULL",
			{ TextValue(charId), TextValue(accountId) });

	// IF ((SELECT bCharNum FROM ACCOUNT_CHAR WHERE strAccountID = @AccountID) != @bCharNum + 1)
	Int newCount = AsInt(ctx.Scalar(
		"SELECT bCharNum FROM ACCOUNT_CHAR WHERE strAccountID = ?", { TextValue(accountId) }));
	Int expected = bCharNum.has_value() ? Int(*bCharNum + 1) : std::nullopt;
	if (newCount.has_value() && expected.has_value() && *newCount != *expected)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(4));
		return;
	}

	int error = ctx.Dml("INSERT INTO USERDATA (strUserId, Nation, Race, Class, HairColor, Face, "
						"Strong, Sta, Dex, Intel, "
						"Cha, Zone, PX, PZ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
		{ TextValue(charId), IntValue(nation), IntValue(race), IntValue(ctx.ArgInt(5)),
			IntValue(ctx.ArgInt(6)), IntValue(ctx.ArgInt(7)), IntValue(ctx.ArgInt(8)),
			IntValue(ctx.ArgInt(9)), IntValue(ctx.ArgInt(10)), IntValue(ctx.ArgInt(11)),
			IntValue(ctx.ArgInt(12)), Value::Integer(zone), posX, posZ });
	if (error != 0)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(4));
		return;
	}

	ctx.CommitTran();
	ctx.SetOutput(0, Value::Integer(0));
}

// CREATE_KNIGHTS @nRet smallint OUTPUT, @index smallint, @nation tinyint, @community tinyint,
//   @strName varchar(21), @strChief varchar(21)
void Proc_CREATE_KNIGHTS(ProcContext& ctx)
{
	Int index     = ctx.ArgInt(1);
	Int nation    = ctx.ArgInt(2);
	Int community = ctx.ArgInt(3);
	Text name     = ctx.ArgText(4, 21);
	Text chief    = ctx.ArgText(5, 21);

	auto row      = ctx.AssignSelect("SELECT COUNT(*) FROM KNIGHTS WHERE IDNum = ? OR IDName = ?",
			 { IntValue(index), TextValue(name) });
	if (IsTrue(Or(Cmp(AsInt(Col(row, 0)), Gt, 0), Cmp(index, EqI, 0))))
	{
		ctx.SetOutput(0, Value::Integer(3));
		return;
	}

	ctx.BeginTran();
	ctx.Dml("INSERT INTO KNIGHTS (IDNum, Nation, Flag, IDName, Chief) VALUES (?, ?, ?, ?, ?)",
		{ IntValue(index), IntValue(nation), IntValue(community), TextValue(name),
			TextValue(chief) });
	int error = ctx.Dml("INSERT INTO KNIGHTS_USER (sIDNum, strUserID) VALUES (?, ?)",
		{ IntValue(index), TextValue(chief) });
	if (error != 0)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(6));
		return;
	}
	// The second "IF @@ERROR <> 0" tests the IF above, so it never fires.

	ctx.CommitTran();
	ctx.SetOutput(0, Value::Integer(0));
}

// DELETE_KNIGHTS @nRet smallint OUTPUT, @knightsindex smallint
void Proc_DELETE_KNIGHTS(ProcContext& ctx)
{
	Int knightsIndex = ctx.ArgInt(1);

	auto row         = ctx.AssignSelect(
        "SELECT COUNT(*) FROM KNIGHTS WHERE IDNum = ?", { IntValue(knightsIndex) });
	if (IsTrue(Cmp(AsInt(Col(row, 0)), EqI, 0)))
	{
		ctx.SetOutput(0, Value::Integer(7));
		return;
	}

	ctx.BeginTran();
	ctx.Dml("DELETE FROM KNIGHTS WHERE IDNum = ?", { IntValue(knightsIndex) });
	int error = ctx.Dml("DELETE FROM KNIGHTS_USER WHERE sIDNum = ?", { IntValue(knightsIndex) });
	if (error != 0)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(7));
		return;
	}

	ctx.CommitTran();
	ctx.SetOutput(0, Value::Integer(0));
}

// UPDATE_KNIGHTS @nRet smallint OUTPUT, @Type tinyint, @UserId varchar(21), @KnightsIndex smallint,
//   @Domanation tinyint
void Proc_UPDATE_KNIGHTS(ProcContext& ctx)
{
	Int type         = ctx.ArgInt(1);
	Text userId      = ctx.ArgText(2, 21);
	Int knightsIndex = ctx.ArgInt(3);
	Value knights    = IntValue(knightsIndex);
	Value user       = TextValue(userId);

	auto row = ctx.AssignSelect("SELECT COUNT(*) FROM KNIGHTS WHERE IDNum = ?", { knights });
	if (IsTrue(Cmp(AsInt(Col(row, 0)), EqI, 0)))
	{
		ctx.SetOutput(0, Value::Integer(7));
		return;
	}

	row = ctx.AssignSelect(
		"SELECT Members, ViceChief_1, ViceChief_2, ViceChief_3 FROM KNIGHTS WHERE IDNum = ?",
		{ knights });
	Int members     = AsInt(Col(row, 0));
	Text viceChief1 = AsText(Col(row, 1), 21);
	Text viceChief2 = AsText(Col(row, 2), 21);
	Text viceChief3 = AsText(Col(row, 3), 21);

	if (IsTrue(Cmp(type, EqI, 18)))
	{
		row = ctx.AssignSelect("SELECT COUNT(*) FROM USERDATA WHERE Knights = ?", { knights });
		Int userMembers = AsInt(Col(row, 0));
		if (IsTrue(Or(Cmp(members, Ge, 36), Cmp(userMembers, Ge, 36))))
		{
			ctx.SetOutput(0, Value::Integer(8));
			return;
		}
	}
	else if (IsTrue(Cmp(members, Gt, 36)))
	{
		ctx.SetOutput(0, Value::Integer(8));
		return;
	}

	row = ctx.AssignSelect("SELECT COUNT(*) FROM USERDATA WHERE strUserId = ?", { user });
	if (IsTrue(Cmp(AsInt(Col(row, 0)), EqI, 0)))
	{
		ctx.SetOutput(0, Value::Integer(2));
		return;
	}

	ctx.BeginTran();

	// @@ERROR at the check below is the error of the last statement executed in the branch;
	// an IF whose condition was false resets it to 0.
	int lastError        = 0;
	auto clearViceChiefs = [&]()
	{
		const char* columns[] = { "ViceChief_1", "ViceChief_2", "ViceChief_3" };
		const Text* values[]  = { &viceChief1, &viceChief2, &viceChief3 };
		for (int i = 0; i < 3; i++)
		{
			if (IsTrue(Eq(*values[i], userId)))
				lastError = ctx.Dml(
					std::string("UPDATE KNIGHTS SET ") + columns[i] + " = NULL WHERE IDNum = ?",
					{ knights });
			else
				lastError = 0;
		}
	};

	if (IsTrue(Cmp(type, EqI, 18))) // JOIN
	{
		ctx.Dml("UPDATE KNIGHTS SET Members = Members + 1 WHERE IDNum = ?", { knights });
		lastError = ctx.Dml(
			"INSERT INTO KNIGHTS_USER (sIDNum, strUserID) VALUES (?, ?)", { knights, user });
	}
	else if (IsTrue(Or(Or(Cmp(type, EqI, 19), Cmp(type, EqI, 20)),
				 Cmp(type, EqI, 23)))) // WITHDRAW, REMOVE, REJECT
	{
		if (IsTrue(Cmp(members, Le, 1)))
			ctx.Dml("UPDATE KNIGHTS SET Members = 1 WHERE IDNum = ?", { knights });
		else
			ctx.Dml("UPDATE KNIGHTS SET Members = Members - 1 WHERE IDNum = ?", { knights });
		ctx.Dml("DELETE FROM KNIGHTS_USER WHERE strUserID = ?", { user });
		clearViceChiefs();
	}
	else if (IsTrue(Cmp(type, EqI, 25))) // change chief
	{
		ctx.Dml("UPDATE KNIGHTS SET Chief = ? WHERE IDNum = ?", { user, knights });
		clearViceChiefs();
	}
	else if (IsTrue(Cmp(type, EqI, 26))) // change vice chief
	{
		if (viceChief1.has_value() && viceChief2.has_value() && viceChief3.has_value())
		{
			ctx.SetOutput(0, Value::Integer(8));
			ctx.CommitTran();
			return;
		}

		if (!viceChief1.has_value())
			lastError = ctx.Dml(
				"UPDATE KNIGHTS SET ViceChief_1 = ? WHERE IDNum = ?", { user, knights });
		else if (!viceChief2.has_value())
			lastError = ctx.Dml(
				"UPDATE KNIGHTS SET ViceChief_2 = ? WHERE IDNum = ?", { user, knights });
		else
			lastError = ctx.Dml(
				"UPDATE KNIGHTS SET ViceChief_3 = ? WHERE IDNum = ?", { user, knights });
	}

	if (lastError != 0)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(2));
		return;
	}

	if (IsTrue(Cmp(type, EqI, 20))) // REMOVE
		ctx.Dml("UPDATE USERDATA SET Knights = -1, Fame = 0 WHERE strUserId = ?", { user });

	ctx.CommitTran();
	ctx.SetOutput(0, Value::Integer(0));
}

// UPDATE_USER_DATA @id varchar(21), @Nation .. @LoyaltyMonthly (38 parameters)
void Proc_UPDATE_USER_DATA(ProcContext& ctx)
{
	Text id     = ctx.ArgText(0, 21);
	Int knights = ctx.ArgInt(12);
	Int fame    = ctx.ArgInt(13);
	Int zone    = ctx.ArgInt(25);

	if (IsTrue(Cmp(zone, Gt, 2))) // battle zone user
	{
		auto row = ctx.AssignSelect(
			"SELECT Knights FROM USERDATA WHERE strUserId = ?", { TextValue(id) });
		Int knightsIndex = AsInt(Col(row, 0));
		if (IsTrue(Cmp(knightsIndex, EqI, -1))) // expelled
		{
			knights = 0;
			fame    = 0;
		}
	}

	auto blob = [&](size_t index, size_t maxLength)
	{ return BlobValue(ctx.ArgText(index, maxLength)); };

	ctx.Dml("UPDATE USERDATA SET Nation = ?, Race = ?, Class = ?, HairColor = ?, Rank = ?, Title = "
			"?, [Level] = ?, "
			"[Exp] = ?, Loyalty = ?, Face = ?, City = ?, Knights = ?, Fame = ?, Hp = ?, Mp = ?, Sp "
			"= ?, Strong = ?, "
			"Sta = ?, Dex = ?, Intel = ?, Cha = ?, Authority = ?, Points = ?, Gold = ?, [Zone] = "
			"?, Bind = ?, PX = ?, "
			"PZ = ?, PY = ?, dwTime = ?, strSkill = ?, strItem = ?, strSerial = ?, sQuestCount = "
			"?, strQuest = ?, "
			"MannerPoint = ?, LoyaltyMonthly = ?, UpdateTime = ? WHERE strUserId = ?",
		{ IntValue(ctx.ArgInt(1)), IntValue(ctx.ArgInt(2)), IntValue(ctx.ArgInt(3)),
			IntValue(ctx.ArgInt(4)), IntValue(ctx.ArgInt(5)), IntValue(ctx.ArgInt(6)),
			IntValue(ctx.ArgInt(7)), IntValue(ctx.ArgInt(8)), IntValue(ctx.ArgInt(9)),
			IntValue(ctx.ArgInt(10)), IntValue(ctx.ArgInt(11)), IntValue(knights), IntValue(fame),
			IntValue(ctx.ArgInt(14)), IntValue(ctx.ArgInt(15)), IntValue(ctx.ArgInt(16)),
			IntValue(ctx.ArgInt(17)), IntValue(ctx.ArgInt(18)), IntValue(ctx.ArgInt(19)),
			IntValue(ctx.ArgInt(20)), IntValue(ctx.ArgInt(21)), IntValue(ctx.ArgInt(22)),
			IntValue(ctx.ArgInt(23)), IntValue(ctx.ArgInt(24)), IntValue(zone),
			IntValue(ctx.ArgInt(26)), IntValue(ctx.ArgInt(27)), IntValue(ctx.ArgInt(28)),
			IntValue(ctx.ArgInt(29)), IntValue(ctx.ArgInt(30)), blob(32, 20), blob(33, 400),
			blob(34, 400), IntValue(ctx.ArgInt(31)), blob(35, 400), IntValue(ctx.ArgInt(36)),
			IntValue(ctx.ArgInt(37)), Value::Text(GetDate(true)), TextValue(id) });
}

// UPDATE_WAREHOUSE @accountid varchar(21), @Money int, @dwTime int, @strItem varchar(1600),
//   @strSerial varchar(1600)
void Proc_UPDATE_WAREHOUSE(ProcContext& ctx)
{
	ctx.Dml("UPDATE WAREHOUSE SET nMoney = ?, dwTime = ?, WarehouseData = ?, strSerial = ? WHERE "
			"strAccountID = ?",
		{ IntValue(ctx.ArgInt(1)), IntValue(ctx.ArgInt(2)), BlobValue(ctx.ArgText(3, 1600)),
			BlobValue(ctx.ArgText(4, 1600)), TextValue(ctx.ArgText(0, 21)) });
}

// CLAIM_USER_RANK_STIPEND @nRet tinyint OUTPUT, @type tinyint, @rank tinyint, @nation tinyint,
//   @charId varchar(21)
void Proc_CLAIM_USER_RANK_STIPEND(ProcContext& ctx)
{
	Int type        = ctx.ArgInt(1);
	Int rank        = ctx.ArgInt(2);
	Int nation      = ctx.ArgInt(3);
	Text charId     = ctx.ArgText(4, 21);

	Tri typeNotIn   = And(Cmp(type, NeI, 0), Cmp(type, NeI, 1));
	Tri nationNotIn = And(Cmp(nation, NeI, 1), Cmp(nation, NeI, 2));
	if (IsTrue(Or(Or(Or(typeNotIn, Cmp(rank, Gt, 100)), Cmp(rank, EqI, 0)), nationNotIn)))
	{
		ctx.SetOutput(0, Value::Integer(255));
		return;
	}

	ctx.BeginTran();

	const char* table   = IsTrue(Cmp(type, EqI, 0)) ? "USER_KNIGHTS_RANK" : "USER_PERSONAL_RANK";
	const char* keyName = IsTrue(Cmp(type, EqI, 0)) ? "shIndex" : "nRank";
	std::string select  = std::string(
                             "SELECT CASE WHEN ?1 = 1 THEN isClaimedKarus ELSE isClaimedElmo END, "
							  "CASE WHEN ?1 = 1 THEN strKarusUserID ELSE strElmoUserID END FROM ")
						 + table + " WHERE " + keyName + " = ?2";
	auto row        = ctx.AssignSelect(select, { IntValue(nation), IntValue(rank) });
	Int isClaimed   = AsInt(Col(row, 0));
	Text rankHolder = AsText(Col(row, 1), 21);

	if (IsTrue(Ne(rankHolder, charId)))
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(2));
		return;
	}

	if (IsTrue(Cmp(isClaimed, EqI, 1)))
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(1));
		return;
	}

	const char* column = IsTrue(Cmp(nation, EqI, 1)) ? "isClaimedKarus" : "isClaimedElmo";
	int error          = ctx.Dml(
        std::string("UPDATE ") + table + " SET " + column + " = 1 WHERE " + keyName + " = ?",
        { IntValue(rank) });
	if (error != 0)
	{
		ctx.RollbackTran();
		ctx.SetOutput(0, Value::Integer(255));
		return;
	}

	ctx.CommitTran();
	ctx.SetOutput(0, Value::Integer(0));
}

} // namespace db::sqlite
