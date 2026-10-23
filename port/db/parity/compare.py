#!/usr/bin/env python3
"""Compares two db-parity-probe outputs (SQL Server reference vs SQLite) case by case.

usage: compare.py <reference.jsonl> <test.jsonl> [--report out.md]
Exit status 1 if any case differs.
"""

import json
import sys


def load(path):
	cases = {}
	order = []
	with open(path, encoding="utf-8") as f:
		for line in f:
			line = line.strip()
			if not line:
				continue
			obj = json.loads(line)
			name = obj["case"]
			if name in cases:
				name = f"{name} #{sum(1 for k in cases if k.startswith(obj['case']))+1}"
			cases[name] = obj
			order.append(name)
	return cases, order


def first_difference(a, b, path="$"):
	if type(a) != type(b):
		return path, a, b
	if isinstance(a, dict):
		for k in sorted(set(a) | set(b)):
			if k not in a or k not in b:
				return f"{path}.{k}", a.get(k, "<missing>"), b.get(k, "<missing>")
			d = first_difference(a[k], b[k], f"{path}.{k}")
			if d:
				return d
		return None
	if isinstance(a, list):
		for i, (x, y) in enumerate(zip(a, b)):
			d = first_difference(x, y, f"{path}[{i}]")
			if d:
				return d
		if len(a) != len(b):
			return f"{path}.length", len(a), len(b)
		return None
	return None if a == b else (path, a, b)


def short(v):
	s = json.dumps(v)
	return s if len(s) < 160 else s[:157] + "..."


def main():
	ref, order = load(sys.argv[1])
	test, _ = load(sys.argv[2])
	report = sys.argv[sys.argv.index("--report") + 1] if "--report" in sys.argv else None
	lines = []
	same = 0
	diffs = 0
	for name in order:
		a = ref[name]
		b = test.get(name)
		if b is None:
			lines.append(f"- **{name}**: missing from test")
			diffs += 1
			continue
		d = first_difference(a, b)
		if d is None:
			same += 1
		else:
			diffs += 1
			path, x, y = d
			lines.append(f"- **{name}** at `{path}`: reference {short(x)} vs test {short(y)}")
	for name in test:
		if name not in ref:
			lines.append(f"- **{name}**: missing from reference")
			diffs += 1
	summary = f"{same} cases identical, {diffs} differ (of {len(order)} reference cases)"
	text = f"# Database parity: SQL Server vs SQLite\n\n{summary}\n\n" + "\n".join(lines) + "\n"
	print(text)
	if report:
		open(report, "w").write(text)
	return 1 if diffs else 0


if __name__ == "__main__":
	sys.exit(main())
