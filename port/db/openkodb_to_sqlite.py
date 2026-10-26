#!/usr/bin/env python3
"""Converts OpenKO-db (jsonSchema + ManualSetup/6_InsertData_*.sql) into a SQL script that
`openko-sqlite-load` turns into the seed SQLite database for the SQLite backend.

The schema comes from jsonSchema, not the 5_CreateTable_*.sql scripts (those are stale; e.g.
they lack USER_*_RANK.isClaimed*), which is also what kodb-util builds SQL Server from.

Storage conventions (the C++ backend in src/db-library/sqlite relies on them):
- char/varchar/text columns: TEXT COLLATE KO_CI holding CP1252 bytes, as SQL Server stores
  varchar under SQL_Latin1_General_CP1_CI_AS; char(n) values are space-padded to n.
- binary data (forceBinary varchar/char, binary, varbinary, image): BLOB; binary(n) is
  zero-padded to n.
- integers: INTEGER with range CHECKs named range_<col>; lengths: CHECKs named len_<col>
  (the backend maps these to SQL Server's overflow / truncation errors).
- datetime/smalldatetime: TEXT "YYYY-MM-DD HH:MM:SS.fff" (datetime rounded to 1/300 s,
  smalldatetime to the minute).
- a clustered primary key becomes a WITHOUT ROWID table, so unordered scans return rows in
  key order like SQL Server's clustered index scans; heaps keep insertion (file) order.

usage: openkodb_to_sqlite.py <OpenKO-db dir> <out.sql>
"""

import datetime
import glob
import json
import os
import re
import subprocess
import sys

INT_RANGES = {
	"bit": (0, 1),
	"tinyint": (0, 255),
	"smallint": (-32768, 32767),
	"int": (-2147483648, 2147483647),
	"bigint": (-9223372036854775808, 9223372036854775807),
}
TEXT_TYPES = {"char", "varchar", "text", "nchar", "nvarchar", "ntext"}
BINARY_TYPES = {"binary", "varbinary", "image"}
DATE_TYPES = {"datetime", "smalldatetime"}
FLOAT_TYPES = {"float", "real"}


def column_kind(col):
	t = col["type"].lower()
	if t in INT_RANGES:
		return "int"
	if t in FLOAT_TYPES:
		return "real"
	if t in DATE_TYPES:
		return "date"
	if t in BINARY_TYPES or (t in TEXT_TYPES and col.get("forceBinary")):
		return "blob"
	if t in TEXT_TYPES:
		return "text"
	raise ValueError(f"unhandled column type {t} ({col['name']})")


def quote_ident(name):
	return "[" + name.replace("]", "]]") + "]"


def hex_literal(data):
	return "X'" + data.hex().upper() + "'"


def text_literal(data):
	# CP1252 bytes stored as TEXT: CAST(blob AS TEXT) keeps the bytes as they are.
	if data.isascii() and b"\x00" not in data:
		return "'" + data.decode("ascii").replace("'", "''") + "'"
	return f"CAST({hex_literal(data)} AS TEXT)"


def round_datetime(value, small):
	if small:
		# smalldatetime: to the minute; 29.998 s and up rounds up
		seconds = value.second + value.microsecond / 1e6
		value = value.replace(second=0, microsecond=0)
		if seconds >= 29.998:
			value += datetime.timedelta(minutes=1)
		return value.strftime("%Y-%m-%d %H:%M:00.000")
	# datetime: 1/300 s ticks
	ms = value.microsecond / 1000.0
	ticks = int(ms * 0.3 + 0.5)
	value = value.replace(microsecond=0)
	if ticks >= 300:
		ticks = 0
		value += datetime.timedelta(seconds=1)
	return value.strftime("%Y-%m-%d %H:%M:%S") + ".%03d" % int((ticks * 10 + 1) // 3)


def parse_datetime(text):
	text = text.replace("T", " ")
	for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"):
		try:
			return datetime.datetime.strptime(text, fmt)
		except ValueError:
			continue
	raise ValueError(f"bad datetime literal {text!r}")


def default_clause(col, kind):
	d = col.get("defaultValue")
	if d is None:
		return ""
	d = d.strip()
	while d.startswith("(") and d.endswith(")"):
		d = d[1:-1].strip()
	small = col["type"].lower() == "smalldatetime"
	if d.lower().startswith("getdate()"):
		extra = d[len("getdate()"):].replace(" ", "").replace("(", "").replace(")", "")
		modifiers = ["'now'", "'localtime'"]
		if extra:
			days = int(extra)
			modifiers.append(f"'{days:+d} days'")
		if small:
			modifiers.append("'+30 seconds'")
			return f" DEFAULT (strftime('%Y-%m-%d %H:%M:00.000', {', '.join(modifiers)}))"
		return f" DEFAULT (strftime('%Y-%m-%d %H:%M:%f', {', '.join(modifiers)}))"
	if kind == "blob" and d.lower().startswith("0x"):
		return f" DEFAULT {hex_literal(bytes.fromhex(d[2:]))}"
	if d.startswith("'") and d.endswith("'"):
		value = d[1:-1].replace("''", "'").encode("cp1252")
		if kind == "blob":
			return f" DEFAULT {hex_literal(value)}"
		return f" DEFAULT {text_literal(value)}"
	if re.fullmatch(r"-?\d+(\.\d+)?", d):
		return f" DEFAULT {d}"
	raise ValueError(f"unhandled default {col['defaultValue']!r} on {col['name']}")


def create_table(table):
	cols = table["columns"]
	pk = next((ix for ix in table.get("indexes", []) if ix.get("isPrimaryKey")), None)
	lines = []
	for col in cols:
		kind = column_kind(col)
		# Declared types keep the SQL Server type names where SQLite's affinity rules agree
		# (tinyint/int -> INTEGER, float -> REAL, char/varchar/text -> TEXT); the backend uses
		# the declared type to tell long-data (text) columns from bound character columns.
		t = col["type"].lower()
		length = col.get("length")
		if kind == "text":
			sized = length is not None and int(length) > 0 and t not in ("text", "ntext")
			decl = (f"{t}({int(length)})" if sized else t) + " COLLATE KO_CI"
		else:
			decl = {"int": t, "real": "float", "date": "TEXT", "blob": "BLOB"}[kind]
		line = f"  {quote_ident(col['name'])} {decl}"
		if not col.get("allowNull", False):
			line += " NOT NULL"
		line += default_clause(col, kind)
		if kind == "int":
			lo, hi = INT_RANGES[t]
			line += f" CONSTRAINT range_{col['name']} CHECK ({quote_ident(col['name'])} BETWEEN {lo} AND {hi})"
		if kind in ("text", "blob") and length is not None and int(length) > 0:
			line += f" CONSTRAINT len_{col['name']} CHECK (length(CAST({quote_ident(col['name'])} AS BLOB)) <= {int(length)})"
		lines.append(line)
	if pk:
		lines.append("  PRIMARY KEY (" + ", ".join(quote_ident(c) for c in pk["columns"]) + ")")
	without_rowid = pk is not None and pk.get("type", "").upper() == "CLUSTERED"
	sql = f"CREATE TABLE {quote_ident(table['name'])} (\n" + ",\n".join(lines) + "\n)" + (" WITHOUT ROWID" if without_rowid else "") + ";\n"
	for ix in table.get("indexes", []):
		if ix.get("isPrimaryKey"):
			continue
		unique = "UNIQUE " if ix.get("isUnique") else ""
		sql += f"CREATE {unique}INDEX {quote_ident(ix['name'])} ON {quote_ident(table['name'])} (" + ", ".join(quote_ident(c) for c in ix["columns"]) + ");\n"
	return sql


# --- T-SQL value tokenizer for the seed files ---------------------------------------------

class Tok:
	def __init__(self, kind, value):
		self.kind = kind  # "null", "num", "str", "bin", "date"
		self.value = value


def parse_values(text, pos):
	"""Parses one parenthesised value tuple starting at text[pos] == '('. Returns (values, pos)."""
	assert text[pos] == "(", text[pos:pos + 40]
	pos += 1
	values = []
	while True:
		while text[pos] in " \t\r\n":
			pos += 1
		tok, pos = parse_value(text, pos)
		values.append(tok)
		while text[pos] in " \t\r\n":
			pos += 1
		if text[pos] == ",":
			pos += 1
			continue
		if text[pos] == ")":
			return values, pos + 1
		raise ValueError(f"unexpected {text[pos:pos + 40]!r}")


def parse_string(text, pos):
	# at the opening quote
	assert text[pos] == "'"
	pos += 1
	out = []
	while True:
		q = text.index("'", pos)
		out.append(text[pos:q])
		if q + 1 < len(text) and text[q + 1] == "'":
			out.append("'")
			pos = q + 2
			continue
		return "".join(out), q + 1


def parse_value(text, pos):
	upper = text[pos:pos + 8].upper()
	if upper.startswith("NULL"):
		return Tok("null", None), pos + 4
	if text[pos] == "N" and text[pos + 1] == "'":
		s, pos = parse_string(text, pos + 1)
		return Tok("str", s), pos
	if text[pos] == "'":
		s, pos = parse_string(text, pos)
		return Tok("str", s), pos
	if upper.startswith("0X"):
		m = re.compile(r"0[xX]([0-9a-fA-F]*)").match(text, pos)
		return Tok("bin", bytes.fromhex(m.group(1))), m.end()
	if upper.startswith("CAST("):
		inner, pos = parse_value(text, pos + 5)
		m = re.compile(r"\s+AS\s+([A-Za-z]+)(\s*\([^)]*\))?\s*\)", re.I).match(text, pos)
		target = m.group(1).lower()
		if target in DATE_TYPES:
			return Tok("date", (parse_datetime(inner.value), target == "smalldatetime")), m.end()
		return inner, m.end()
	if upper.startswith("CONVERT("):
		m = re.compile(r"CONVERT\(\s*([A-Za-z]+)(\s*\([^)]*\))?\s*,\s*", re.I).match(text, pos)
		inner, pos = parse_value(text, m.end())
		end = re.compile(r"\s*\)").match(text, pos)
		return inner, end.end()
	m = re.compile(r"-?\d+(\.\d+)?([eE][-+]?\d+)?").match(text, pos)
	if m:
		return Tok("num", m.group(0)), m.end()
	raise ValueError(f"cannot parse value at {text[pos:pos + 40]!r}")


def encode_text(s):
	return s.encode("cp1252")


def render(tok, col, kind):
	t = col["type"].lower()
	length = int(col.get("length") or 0)
	if tok.kind == "null":
		# kodb-util (which loads the SQL Server reference) writes NULL text-type values as ''.
		if t in ("text", "ntext"):
			return "''"
		return "NULL"
	if kind == "int":
		if tok.kind == "num":
			return str(int(float(tok.value))) if "." in tok.value or "e" in tok.value.lower() else tok.value
		if tok.kind == "str":
			return str(int(tok.value))
	if kind == "real":
		return tok.value if tok.kind == "num" else repr(float(tok.value))
	if kind == "date":
		if tok.kind == "date":
			value, _ = tok.value
		else:
			value = parse_datetime(tok.value)
		return "'" + round_datetime(value, t == "smalldatetime") + "'"
	if kind == "blob":
		data = tok.value if tok.kind == "bin" else encode_text(tok.value) if tok.kind == "str" else tok.value.encode("ascii")
		if t == "binary" and length > 0:
			data = data[:length].ljust(length, b"\x00")
		if t == "char" and length > 0:
			data = data[:length].ljust(length, b" ")
		return hex_literal(data)
	if kind == "text":
		data = encode_text(tok.value) if tok.kind == "str" else tok.value if tok.kind == "bin" else tok.value.encode("ascii")
		if t in ("char", "nchar") and length > 0:
			data = data.ljust(length, b" ")
		return text_literal(data)
	raise ValueError(f"cannot render {tok.kind} for {col['name']} ({t})")


def convert_seed(table, path):
	text = open(path, encoding="utf-8-sig").read()
	header = re.compile(r"INSERT INTO \[([^\]]+)\]\s*\(([^)]*)\)\s*VALUES", re.I)
	cols_by_name = {c["name"].lower(): c for c in table["columns"]}
	out = []
	pos = 0
	rows = 0
	while True:
		m = header.search(text, pos)
		if not m:
			break
		names = [n.strip().strip("[]") for n in m.group(2).split(",")]
		cols = [cols_by_name[n.lower()] for n in names]
		kinds = [column_kind(c) for c in cols]
		insert = f"INSERT INTO {quote_ident(table['name'])} (" + ", ".join(quote_ident(c["name"]) for c in cols) + ") VALUES\n"
		pos = m.end()
		batch = []
		while True:
			while pos < len(text) and text[pos] in " \t\r\n":
				pos += 1
			if pos >= len(text) or text[pos] != "(":
				break
			values, pos = parse_values(text, pos)
			if len(values) != len(cols):
				raise ValueError(f"{path}: row {rows + 1} has {len(values)} values for {len(cols)} columns")
			batch.append("(" + ", ".join(render(v, c, k) for v, c, k in zip(values, cols, kinds)) + ")")
			rows += 1
			while pos < len(text) and text[pos] in " \t\r\n":
				pos += 1
			if pos < len(text) and text[pos] == ",":
				pos += 1
			if len(batch) == 500:
				out.append(insert + ",\n".join(batch) + ";\n")
				batch = []
		if batch:
			out.append(insert + ",\n".join(batch) + ";\n")
		while pos < len(text) and text[pos] in " \t\r\n;":
			pos += 1
	return "".join(out), rows


def source_revision(root):
	try:
		return subprocess.run(["git", "-C", root, "describe", "--tags", "--always"], capture_output=True, text=True, check=True).stdout.strip()
	except Exception:
		return "unknown"


def main():
	root, out_path = sys.argv[1:3]
	tables = [json.load(open(f)) for f in sorted(glob.glob(os.path.join(root, "jsonSchema", "*.json")))]
	parts = ["-- Generated by port/db/openkodb_to_sqlite.py; load with openko-sqlite-load.\n"]
	counts = {}
	for table in tables:
		parts.append(create_table(table))
	for table in tables:
		seed = os.path.join(root, "ManualSetup", f"6_InsertData_{table['name']}.sql")
		if os.path.exists(seed):
			sql, rows = convert_seed(table, seed)
			parts.append(sql)
			counts[table["name"]] = rows
		else:
			counts[table["name"]] = 0
	revision = source_revision(root)
	parts.append("CREATE TABLE [_openko_seed] ([key] TEXT PRIMARY KEY, [value] TEXT) WITHOUT ROWID;\n")
	parts.append(f"INSERT INTO [_openko_seed] VALUES ('source', 'OpenKO-db {revision}'), ('converter', 'port/db/openkodb_to_sqlite.py 1');\n")
	with open(out_path, "w", encoding="ascii", newline="\n") as f:
		f.write("".join(parts))
	print(f"{len(tables)} tables, {sum(counts.values())} rows from OpenKO-db {revision} -> {out_path}")
	for name, n in sorted(counts.items()):
		print(f"  {name}: {n}")


if __name__ == "__main__":
	main()
