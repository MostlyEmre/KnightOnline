#!/usr/bin/env python3
"""Enumerates the fixed-function state that client assets feed into D3D9.

N3 materials (__Material: D3DMATERIAL9 + dwColorOp, dwColorArg1, dwColorArg2,
nRenderFlags, dwSrcBlend, dwDestBlend; 92 bytes) are read raw from .n3shape,
.n3cplug, .n3cpart, ... files and passed straight to SetTextureStageState /
SetRenderState. This scans every asset for byte windows that are valid materials
(17 finite colour floats in a sane range, a valid D3DTOP, valid D3DTA args, valid
D3DBLEND values, known render flags) and counts the distinct state combinations.

usage: asset_material_scan.py <client asset dir> <out.md>
"""

import collections
import os
import sys

import numpy as np

D3DTOP = {1: "DISABLE", 2: "SELECTARG1", 3: "SELECTARG2", 4: "MODULATE", 5: "MODULATE2X", 6: "MODULATE4X",
	7: "ADD", 8: "ADDSIGNED", 9: "ADDSIGNED2X", 10: "SUBTRACT", 11: "ADDSMOOTH", 12: "BLENDDIFFUSEALPHA",
	13: "BLENDTEXTUREALPHA", 14: "BLENDFACTORALPHA", 15: "BLENDTEXTUREALPHAPM", 16: "BLENDCURRENTALPHA",
	17: "PREMODULATE", 18: "MODULATEALPHA_ADDCOLOR", 19: "MODULATECOLOR_ADDALPHA", 20: "MODULATEINVALPHA_ADDCOLOR",
	21: "MODULATEINVCOLOR_ADDALPHA", 22: "BUMPENVMAP", 23: "BUMPENVMAPLUMINANCE", 24: "DOTPRODUCT3",
	25: "MULTIPLYADD", 26: "LERP"}
D3DTA = {0: "DIFFUSE", 1: "CURRENT", 2: "TEXTURE", 3: "TFACTOR", 4: "SPECULAR", 5: "TEMP", 6: "CONSTANT"}
D3DBLEND = {1: "ZERO", 2: "ONE", 3: "SRCCOLOR", 4: "INVSRCCOLOR", 5: "SRCALPHA", 6: "INVSRCALPHA", 7: "DESTALPHA",
	8: "INVDESTALPHA", 9: "DESTCOLOR", 10: "INVDESTCOLOR", 11: "SRCALPHASAT", 12: "BOTHSRCALPHA",
	13: "BOTHINVSRCALPHA", 14: "BLENDFACTOR", 15: "INVBLENDFACTOR", 16: "SRCCOLOR2", 17: "INVSRCCOLOR2"}
RF = {0x1: "ALPHABLENDING", 0x2: "NOTUSEFOG", 0x4: "DOUBLESIDED", 0x8: "BOARD_Y", 0x10: "POINTSAMPLING",
	0x20: "WINDY", 0x40: "NOTUSELIGHT", 0x80: "DIFFUSEALPHA", 0x100: "NOTZWRITE", 0x200: "UV_CLAMP", 0x400: "NOTZBUFFER"}

SKIP_EXT = {".n3pmesh", ".dxt", ".wav", ".mp3", ".tga", ".bmp", ".jpg", ".dll", ".exe", ".md", ".csv", ".ini", ".txt"}


def ta(v):
	s = D3DTA.get(v & 0xF, f"?{v}")
	if v & 0x10:
		s += "|COMPLEMENT"
	if v & 0x20:
		s += "|ALPHAREPLICATE"
	return s


def flags(v):
	return "|".join(n for b, n in RF.items() if v & b) or "0"


MTL = np.dtype([("colors", "<f4", 16), ("power", "<f4"), ("op", "<u4"), ("a1", "<u4"), ("a2", "<u4"),
	("rf", "<u4"), ("sb", "<u4"), ("db", "<u4")])


def valid(m):
	c = m["colors"]
	return (np.all(np.isfinite(c)) and 0 <= m["op"] <= 26 and m["a1"] < 0x40 and (m["a1"] & 0xF) <= 6
		and m["a2"] < 0x40 and (m["a2"] & 0xF) <= 6 and m["rf"] < 0x800 and 0 <= m["sb"] <= 17 and 0 <= m["db"] <= 17)


def material_at(data, off):
	if off < 0 or off + MTL.itemsize > len(data):
		return None
	m = np.frombuffer(data, dtype=MTL, count=1, offset=off)[0]
	return m if valid(m) else None


def lpstr_at(data, off):
	"""int32 length + bytes, as written by the N3 tools."""
	if off + 4 > len(data):
		return None, off
	n = int.from_bytes(data[off:off + 4], "little")
	if n <= 0 or n > 260 or off + 4 + n > len(data):
		return None, off
	return data[off + 4:off + 4 + n], off + 4 + n


def scan(data, ext):
	"""Yields (offset, material) using the loaders' exact read order:
	CN3CPart::Load: name, dwReserved, __Material
	CN3CPlugBase::Load: name, plugType, jointIndex, pos(12), rot matrix(64), scale(12), __Material
	CN3SPart::Load (inside .n3shape and zone .opd shape managers): pivot(12), mesh file name, __Material
	"""
	out = []
	if ext == ".n3cpart":
		name, p = lpstr_at(data, 0)
		if name is not None or data[0:4] == b"\0\0\0\0":
			p = p if name is not None else 4
			m = material_at(data, p + 4)
			if m is not None:
				out.append((p + 4, m))
	elif ext == ".n3cplug":
		name, p = lpstr_at(data, 0)
		p = p if name is not None else 4
		m = material_at(data, p + 4 + 4 + 12 + 64 + 12)
		if m is not None:
			out.append((p + 96, m))
	else:
		low = data.lower()
		pos = 0
		while True:
			i = low.find(b".n3pmesh", pos)
			if i < 0:
				break
			pos = i + 1
			end = i + len(b".n3pmesh")
			# walk back to the length prefix: the string is printable and its int32 length
			# sits immediately before it
			for start in range(end - 1, max(end - 261, 3), -1):
				if not (32 <= data[start - 1] < 127):
					n = int.from_bytes(data[start - 4:start], "little")
					if n == end - start:
						m = material_at(data, end)
						if m is not None:
							out.append((end, m))
					break
	return out


def main():
	root, out = sys.argv[1:3]
	combos = collections.Counter()
	ops = collections.Counter()
	args = collections.Counter()
	blends = collections.Counter()
	rflags = collections.Counter()
	by_ext = collections.Counter()
	examples = {}
	files_scanned = 0
	for dirpath, _, names in os.walk(root):
		for name in names:
			ext = os.path.splitext(name)[1].lower()
			if ext in SKIP_EXT:
				continue
			path = os.path.join(dirpath, name)
			data = open(path, "rb").read()
			files_scanned += 1
			for off, mm in scan(data, ext):
				op, a1, a2, rf, sb, db = (int(mm[k]) for k in ("op", "a1", "a2", "rf", "sb", "db"))
				key = (D3DTOP.get(op, str(op)), ta(a1), ta(a2), D3DBLEND.get(sb, str(sb)), D3DBLEND.get(db, str(db)), flags(rf))
				combos[key] += 1
				ops[key[0]] += 1
				args[ta(a1)] += 1
				args[ta(a2)] += 1
				blends[(key[3], key[4])] += 1
				for b, n in RF.items():
					if rf & b:
						rflags[n] += 1
				by_ext[ext] += 1
				examples.setdefault(key, f"{os.path.relpath(path, root)}@{off}")

	L = ["# Asset-driven fixed-function state (materials)\n",
		f"Scanned {files_scanned} asset files under `{os.path.basename(os.path.abspath(root))}` "
		"(textures/audio skipped) with `port/census/asset_material_scan.py`. "
		f"Found {sum(combos.values())} material records.\n",
		"## Records by file type\n", "| Extension | Materials |", "|---|---:|"]
	L += [f"| `{e}` | {n} |" for e, n in by_ext.most_common()]
	L += ["", "## Colour ops (stage 0)\n", "| D3DTOP | Materials |", "|---|---:|"]
	L += [f"| {k} | {n} |" for k, n in ops.most_common()]
	L += ["", "## Colour args\n", "| D3DTA | Uses |", "|---|---:|"]
	L += [f"| {k} | {n} |" for k, n in args.most_common()]
	L += ["", "## Blend pairs (src, dest)\n", "| Src | Dest | Materials |", "|---|---|---:|"]
	L += [f"| {s} | {d} | {n} |" for (s, d), n in blends.most_common()]
	L += ["", "## Render flags\n", "| Flag | Materials |", "|---|---:|"]
	L += [f"| {k} | {n} |" for k, n in rflags.most_common()]
	L += ["", "## Distinct combinations\n", "| Op | Arg1 | Arg2 | Src | Dest | Flags | Materials | Example |", "|---|---|---|---|---|---|---:|---|"]
	L += [f"| {k[0]} | {k[1]} | {k[2]} | {k[3]} | {k[4]} | {k[5]} | {n} | `{examples[k]}` |" for k, n in combos.most_common()]
	open(out, "w").write("\n".join(L) + "\n")
	print(f"{files_scanned} files, {sum(combos.values())} materials, {len(combos)} combinations")


if __name__ == "__main__":
	main()
