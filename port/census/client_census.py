#!/usr/bin/env python3
"""Client platform-API census.

Parses every client translation unit (from a compile_commands.json produced by the
MinGW cross build) with libclang, so only code that is actually compiled into the
client is counted, and attributes every call / macro / type reference that resolves
into a platform header (D3D9, D3DX, Win32, IME, Winsock, ...) to the header that
declares it. Writes raw JSON plus markdown tables.

usage: client_census.py <compile_commands.json> <repo root> <out dir>
needs: pip install libclang; x86_64-w64-mingw32-g++ on PATH (for its include dirs)
"""

import collections
import json
import multiprocessing
import os
import re
import shlex
import subprocess
import sys

import clang.cindex as ci

# Use the system libclang so its builtin headers match the resource dir we pass.
APPLE_LIBCLANG = "/Library/Developer/CommandLineTools/usr/lib/libclang.dylib"
if os.path.exists(APPLE_LIBCLANG):
	ci.Config.set_library_file(APPLE_LIBCLANG)

CK = ci.CursorKind

# --- classification ---------------------------------------------------------------

HEADER_CATEGORY = [
	(r"^d3d9(types|caps)?\.h$", "D3D9"),
	(r"^d3dx9", "D3DX9"),
	(r"^dinput\.h$", "DirectInput"),
	(r"^imm\.h$", "IME (imm32)"),
	(r"^(winsock2?|ws2tcpip|ws2def|inaddr|in6addr|mswsock)\.h$", "Winsock"),
	(r"^(fileapi|handleapi|io|direct|ioapiset|winioctl)\.h$", "File I/O"),
	(r"^(mmsystem|timeapi|mmeapi|playsoundapi)\.h$", "Multimedia/timers (winmm)"),
	(r"^(processthreadsapi|synchapi|sysinfoapi|profileapi|libloaderapi|errhandlingapi|memoryapi|heapapi)\.h$", "Process/thread/time"),
	(r"^(winuser|windowsx)\.h$", "Windowing/input/messages (user32)"),
	(r"^wingdi\.h$", "GDI"),
	(r"^(shellapi|shlobj|shlobj_core|shlwapi|commdlg|commctrl|objbase|combaseapi)\.h$", "Shell/COM"),
	(r"^(winreg|winnls|stringapiset|winnt|minwindef|windef|winbase|basetsd|winerror|guiddef|unknwn|unknwnbase|wtypes|wtypesbase|winver|rpc.*)\.h$", "Win32 base"),
]

# winbase.h mixes file, path, time and misc APIs; split by name.
WINBASE_FILE_FUNCS = re.compile(r"^(Create|Read|Write|Delete|Copy|Move|Find|Get|Set)(File|Directory|Folder|CurrentDirectory|FileAttributes|FilePointer|FileSize|FileTime|FullPathName|TempPath|TempFileName|ModuleFileName)|^(CloseHandle|FlushFileBuffers|SetEndOfFile|lopen|lread|lwrite|lclose|_lopen|_lread|_lwrite|_lclose|OpenFile|GetPrivateProfile|WritePrivateProfile)")


# Standard C / CRT headers: portable, so only Microsoft-specific names from them count.
CRT_HEADER = re.compile(r"^(assert|string|strings|stdio|stdlib|math|time|ctype|wchar|wctype|stddef|stdint|errno|limits|float|locale|signal|setjmp|stdarg|malloc|memory|inttypes|process|corecrt.*|_mingw.*|crtdefs|vadefs|search|mbstring|conio|fcntl|share|sys_.*|.*_s)\.h$")
MS_CRT_NAME = re.compile(r"^_(?!assert$)[a-z]|_s$|^(itoa|ltoa|strupr|strlwr|stricmp|strnicmp|strcmpi|splitpath|makepath)$")


def header_category(header, name):
	base = os.path.basename(header).lower()
	if CRT_HEADER.match(base) or "/sec_api/" in header or "/sys/" in header:
		return "MS CRT extensions" if MS_CRT_NAME.search(name or "") else "portable C"
	if base == "winbase.h" and WINBASE_FILE_FUNCS.search(name or ""):
		return "File I/O"
	for pat, cat in HEADER_CATEGORY:
		if re.search(pat, base):
			return cat
	if base in ("winbase.h",):
		return "Win32 base"
	return None


# Calls whose argument text we keep, to enumerate the values passed.
VALUE_CALLS = {
	"SetRenderState", "SetTextureStageState", "SetSamplerState", "SetFVF", "SetTransform",
	"SetTexture", "SetMaterial", "SetLight", "LightEnable", "SetViewport", "Clear",
	"DrawPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitive", "DrawIndexedPrimitiveUP",
	"CreateTexture", "CreateVertexBuffer", "CreateIndexBuffer", "CreateOffscreenPlainSurface",
	"CreateRenderTarget", "CreateDepthStencilSurface", "SetRenderTarget", "SetDepthStencilSurface",
	"SetStreamSource", "SetIndices", "SetVertexShader", "SetPixelShader", "SetClipPlane",
	"Lock", "LockRect", "StretchRect", "UpdateSurface", "UpdateTexture", "GetRenderTargetData",
	"ColorFill", "SetGammaRamp", "Present", "Reset", "CreateDevice", "CheckDeviceFormat",
	"BeginScene", "EndScene", "GetDeviceCaps", "SetCursorProperties", "ShowCursor", "SetCursorPosition",
}

PLATFORM_HEADER_HINT = re.compile(r"(mingw32/include|dx9sdk|Windows Kits)", re.I)


def is_platform_header(path):
	return bool(path) and "/include/c++/" not in path and bool(PLATFORM_HEADER_HINT.search(path))


# --- per-TU parsing ---------------------------------------------------------------

def mingw_system_includes():
	out = subprocess.run(["x86_64-w64-mingw32-g++", "-E", "-x", "c++", "-", "-v"],
		input="", capture_output=True, text=True).stderr
	block = out.split("#include <...> search starts here:")[1].split("End of search list.")[0]
	dirs = [os.path.normpath(d.strip()) for d in block.strip().splitlines()]
	# Keep libstdc++ and the mingw headers; clang supplies its own compiler builtin headers.
	return [d for d in dirs if "/lib/gcc/" not in d or "/c++/" in d]


def clang_args(entry, sys_includes, resource_dir):
	argv = shlex.split(entry["command"])[1:]
	args, skip = [], False
	for i, a in enumerate(argv):
		if skip:
			skip = False
			continue
		if a in ("-o", "-c"):
			skip = True
			continue
		if a == "-include" and argv[i + 1].endswith("cmake_pch.hxx"):
			# Use the real precompiled header source; gcc's .gch is not readable by clang.
			txt = open(argv[i + 1]).read()
			m = re.search(r'#include "([^"]+)"', txt)
			if m:
				args += ["-include", m.group(1)]
			skip = True
			continue
		if a in ("-Werror", "-Winvalid-pch") or a.startswith("-W") or a == "-g":
			continue
		if a == entry["file"]:
			continue
		args.append(a)
	args = ["--target=x86_64-w64-mingw32", "-x", "c++", "-nostdinc", "-nostdinc++",
		"-resource-dir", resource_dir, "-isystem", os.path.join(resource_dir, "include")] + \
		sum((["-isystem", d] for d in sys_includes), []) + args + ["-Wno-everything"]
	return args


def tok_text(tu, extent):
	return " ".join(t.spelling for t in tu.get_tokens(extent=extent))


def call_args_text(tu, call):
	"""Argument texts split from the call's own tokens, so macro arguments (TRUE, FALSE,
	D3DFVF_...) keep their spelling instead of vanishing into their expansion."""
	toks = [t.spelling for t in tu.get_tokens(extent=call.extent)]
	try:
		start = toks.index("(")
	except ValueError:
		return [tok_text(tu, a.extent) for a in call.get_arguments()]
	args, cur, depth = [], [], 0
	for t in toks[start + 1:]:
		if t in "([{":
			depth += 1
		elif t in ")]}":
			if depth == 0:
				break
			depth -= 1
		if t == "," and depth == 0:
			args.append(" ".join(cur))
			cur = []
			continue
		cur.append(t)
	if cur:
		args.append(" ".join(cur))
	return args


def loc_key(cur):
	f = cur.location.file
	return (f.name if f else None, cur.location.line, cur.location.column)


def parse_tu(job):
	entry, src_root, sys_includes, resource_dir = job
	idx = ci.Index.create()
	opts = ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD | ci.TranslationUnit.PARSE_INCOMPLETE
	try:
		tu = idx.parse(entry["file"], args=clang_args(entry, sys_includes, resource_dir), options=opts)
	except ci.TranslationUnitLoadError as ex:
		return {"tu": entry["file"], "fatal": str(ex)}

	diags = [f"{d.location.file}:{d.location.line}: {d.spelling}" for d in tu.diagnostics
		if d.severity >= ci.Diagnostic.Error]

	calls, macros, types = [], [], []

	def in_src(cur):
		f = cur.location.file
		return f is not None and f.name.startswith(src_root)

	def visit(cur):
		for c in cur.walk_preorder():
			k = c.kind
			if k == CK.CALL_EXPR:
				ref = c.referenced
				if ref is None or ref.location.file is None:
					continue
				if ref.kind not in (CK.FUNCTION_DECL, CK.CXX_METHOD) or ref.spelling.startswith("operator"):
					continue
				decl_file = ref.location.file.name
				if not is_platform_header(decl_file):
					continue
				parent = ref.semantic_parent
				cls = parent.spelling if parent is not None and parent.kind in (CK.STRUCT_DECL, CK.CLASS_DECL) else ""
				rec = {"site": loc_key(c), "name": ref.spelling, "cls": cls, "header": decl_file}
				if ref.spelling in VALUE_CALLS or cls.startswith("ID3DX") or ref.spelling.startswith("D3DX"):
					rec["args"] = call_args_text(tu, c)
				calls.append(rec)
			elif k == CK.TYPE_REF:
				ref = c.referenced
				if ref is not None and ref.location.file is not None and is_platform_header(ref.location.file.name):
					types.append({"site": loc_key(c), "name": ref.spelling, "header": ref.location.file.name})

	for top in tu.cursor.get_children():
		if top.kind == CK.MACRO_INSTANTIATION:
			if in_src(top):
				ref = top.referenced
				hdr = ref.location.file.name if ref is not None and ref.location.file is not None else ""
				if is_platform_header(hdr):
					macros.append({"site": loc_key(top), "name": top.spelling, "header": hdr})
			continue
		if top.kind in (CK.MACRO_DEFINITION, CK.INCLUSION_DIRECTIVE):
			continue
		if in_src(top):
			visit(top)
		elif top.kind == CK.NAMESPACE:
			# e.g. namespace std reopened in a src header
			for ch in top.get_children():
				if in_src(ch):
					visit(ch)

	return {"tu": entry["file"], "errors": diags, "calls": calls, "macros": macros, "types": types}


# --- aggregation ------------------------------------------------------------------

def main():
	ccpath, repo, outdir = sys.argv[1:4]
	repo = os.path.abspath(repo)
	src_root = os.path.join(repo, "src") + os.sep
	os.makedirs(outdir, exist_ok=True)
	resource_dir = subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True).stdout.strip()
	sys_includes = mingw_system_includes()
	entries = [e for e in json.load(open(ccpath))
		if e["file"].startswith(src_root) and "/_deps/" not in e["file"] and e["file"].endswith((".cpp", ".c", ".cc"))]

	jobs = [(e, src_root, sys_includes, resource_dir) for e in entries]
	with multiprocessing.Pool() as pool:
		results = pool.map(parse_tu, jobs, chunksize=1)

	seen = set()
	calls, macros, types, errors = [], [], [], {}
	for r in results:
		if "fatal" in r:
			errors[r["tu"]] = [r["fatal"]]
			continue
		if r["errors"]:
			errors[r["tu"]] = r["errors"]
		for kind, bucket, lst in (("c", r["calls"], calls), ("m", r["macros"], macros), ("t", r["types"], types)):
			for rec in bucket:
				key = (kind, tuple(rec["site"]), rec["name"])
				if key in seen:
					continue
				seen.add(key)
				lst.append(rec)

	def rel(p):
		return os.path.relpath(p, repo) if p else p

	for lst in (calls, macros, types):
		for rec in lst:
			rec["site"] = [rel(rec["site"][0]), rec["site"][1], rec["site"][2]]
			rec["category"] = header_category(rec["header"], rec["name"]) or "Other platform"
			rec["header"] = os.path.basename(rec["header"])
	calls = [r for r in calls if r["category"] != "portable C"]
	macros = [r for r in macros if r["category"] != "portable C"]
	types = [r for r in types if r["category"] != "portable C"]

	json.dump({"tus": len(entries), "errors": errors, "calls": calls, "macros": macros, "types": types},
		open(os.path.join(outdir, "client-census.json"), "w"), indent=1)

	write_markdown(outdir, len(entries), errors, calls, macros, types)


def files_summary(recs, limit=6):
	c = collections.Counter(r["site"][0] for r in recs)
	parts = [f"{os.path.basename(f)} ({n})" for f, n in c.most_common(limit)]
	if len(c) > limit:
		parts.append(f"+{len(c) - limit} more")
	return ", ".join(parts)


def write_markdown(outdir, ntus, errors, calls, macros, types):
	L = []
	w = L.append
	w("# Client platform-API census\n")
	w(f"Generated by `port/census/client_census.py` from {ntus} client translation units "
	  "(MinGW cross-build compile commands, `_N3GAME` defined, `_N3TOOL` not), parsed with libclang. "
	  "Each call site / macro / type reference is counted once even when its header is included by many TUs.\n")
	w(f"Parse errors: {sum(len(v) for v in errors.values())} in {len(errors)} TUs.\n")

	# Summary by category
	w("## Summary by category\n")
	w("| Category | Call sites | Distinct functions | Macro uses | Type refs | Files |")
	w("|---|---:|---:|---:|---:|---:|")
	cats = sorted({r["category"] for r in calls + macros + types})
	for cat in cats:
		cc = [r for r in calls if r["category"] == cat]
		mm = [r for r in macros if r["category"] == cat]
		tt = [r for r in types if r["category"] == cat]
		files = {r["site"][0] for r in cc + mm + tt}
		w(f"| {cat} | {len(cc)} | {len({(r['cls'], r['name']) for r in cc})} | {len(mm)} | {len(tt)} | {len(files)} |")
	w("")

	# Per-category function tables
	for cat in cats:
		cc = [r for r in calls if r["category"] == cat]
		if not cc:
			continue
		w(f"## {cat}: calls\n")
		w("| Function | Sites | Files (sites) |")
		w("|---|---:|---|")
		by = collections.defaultdict(list)
		for r in cc:
			by[(r["cls"] + "::" if r["cls"] else "") + r["name"]].append(r)
		for name, recs in sorted(by.items(), key=lambda kv: (-len(kv[1]), kv[0])):
			w(f"| `{name}` | {len(recs)} | {files_summary(recs)} |")
		w("")

	# Values passed to state-setting calls
	def value_table(title, fname, key_idx, val_idx, cls_prefix="IDirect3DDevice9"):
		recs = [r for r in calls if r["name"] == fname and r.get("args")]
		if not recs:
			return
		w(f"## {title}\n")
		w("| State | Value | Sites | Files |")
		w("|---|---|---:|---|")
		by = collections.defaultdict(list)
		for r in recs:
			a = r["args"]
			k = a[key_idx] if key_idx is not None and key_idx < len(a) else ""
			v = a[val_idx] if val_idx < len(a) else ""
			by[(k, v)].append(r)
		for (k, v), rs in sorted(by.items()):
			vv = v.replace("|", "\\|")
			w(f"| `{k}` | `{vv}` | {len(rs)} | {files_summary(rs, 4)} |")
		w("")

	value_table("Render states (SetRenderState)", "SetRenderState", 0, 1)
	recs = [r for r in calls if r["name"] == "SetTextureStageState" and r.get("args")]
	if recs:
		w("## Texture stage states by stage and state\n")
		w("| Stage | State | Value | Sites | Files |")
		w("|---|---|---|---:|---|")
		by = collections.defaultdict(list)
		for r in recs:
			a = r["args"] + ["", "", ""]
			by[(a[0], a[1], a[2])].append(r)
		for (s, k, v), rs in sorted(by.items()):
			vv = v.replace("|", "\\|")
			w(f"| `{s}` | `{k}` | `{vv}` | {len(rs)} | {files_summary(rs, 4)} |")
		w("")
	recs = [r for r in calls if r["name"] == "SetSamplerState" and r.get("args")]
	if recs:
		w("## Sampler states (SetSamplerState)\n")
		w("| Sampler | State | Value | Sites | Files |")
		w("|---|---|---|---:|---|")
		by = collections.defaultdict(list)
		for r in recs:
			a = r["args"] + ["", "", ""]
			by[(a[0], a[1], a[2])].append(r)
		for (s, k, v), rs in sorted(by.items()):
			vv = v.replace("|", "\\|")
			w(f"| `{s}` | `{k}` | `{vv}` | {len(rs)} | {files_summary(rs, 4)} |")
		w("")

	for fname, idxs in (("SetFVF", (0,)), ("SetTransform", (0,)), ("DrawPrimitiveUP", (0,)), ("DrawIndexedPrimitiveUP", (0, 6)),
			("DrawPrimitive", (0,)), ("DrawIndexedPrimitive", (0,)), ("CreateTexture", (4, 5, 6)), ("CreateVertexBuffer", (1, 2, 3)),
			("CreateIndexBuffer", (1, 2, 3)), ("Clear", (2,)), ("Lock", (3,)), ("LockRect", (3,))):
		recs = [r for r in calls if r["name"] == fname and r.get("args")]
		if not recs:
			continue
		w(f"## {fname} arguments {list(idxs)}\n")
		w("| Arguments | Sites | Files |")
		w("|---|---:|---|")
		by = collections.defaultdict(list)
		for r in recs:
			a = r["args"]
			by[" , ".join(a[i] if i < len(a) else "" for i in idxs)].append(r)
		for k, rs in sorted(by.items(), key=lambda kv: -len(kv[1])):
			kk = k.replace("|", "\\|")
			w(f"| `{kk}` | {len(rs)} | {files_summary(rs, 4)} |")
		w("")

	# Macros: D3D constants, virtual keys, window messages, ...
	w("## Platform macros used\n")
	w("| Macro | Category | Uses | Files |")
	w("|---|---|---:|---|")
	by = collections.defaultdict(list)
	for r in macros:
		by[(r["name"], r["category"])].append(r)
	for (name, cat), rs in sorted(by.items(), key=lambda kv: (kv[0][1], -len(kv[1]), kv[0][0])):
		w(f"| `{name}` | {cat} | {len(rs)} | {files_summary(rs, 4)} |")
	w("")

	w("## Platform types referenced\n")
	w("| Type | Category | Refs | Files |")
	w("|---|---|---:|---|")
	by = collections.defaultdict(list)
	for r in types:
		by[(r["name"], r["category"])].append(r)
	for (name, cat), rs in sorted(by.items(), key=lambda kv: (kv[0][1], -len(kv[1]), kv[0][0])):
		w(f"| `{name}` | {cat} | {len(rs)} | {files_summary(rs, 4)} |")
	w("")

	if errors:
		w("## Parse errors\n")
		for tu, errs in sorted(errors.items()):
			w(f"- {tu}: " + "; ".join(errs[:3]))
		w("")

	open(os.path.join(outdir, "client-census.md"), "w").write("\n".join(L))


if __name__ == "__main__":
	main()
