#!/usr/bin/env python3
"""Census of the types the client and servers read, write or copy as raw bytes (files, packets,
blobs), with their layout on macOS arm64, Windows x64 and Windows x86 (the original 1.298 target).

Finds every call that moves the bytes of an object without going through a field-by-field
serializer (File/FileReader/FileWriter Read and Write, fread/fwrite, ReadFile/WriteFile,
memcpy/memmove/memcmp, recv/send, and the templated ByteBuffer/Packet operators that copy
sizeof(T) bytes), resolves the type of the object whose bytes move, and computes that type's size,
alignment and field offsets for each target with libclang. Records are followed into their fields.

usage: layout_census.py <repo> --out <report.md> [--json <census.json>] [--jobs N]

Needs: libclang (pip install libclang), the macOS client compile database (build/mac-nopch,
configured with CMAKE_DISABLE_PRECOMPILE_HEADERS=ON), the MinGW one (build/mingw-nopch), the
macOS server build (build/mac-sqlite), and MinGW-w64 sysroots for x86_64 and i686.
"""

import argparse
import collections
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

import clang.cindex as ci

K = ci.CursorKind


def use_libclang(path):
    if path and not ci.Config.loaded:
        ci.Config.set_library_file(path)


def kind_id(c):
    """The cursor's kind as a number; newer libclangs have kinds the Python bindings don't know."""
    return c._kind_id  # pylint: disable=protected-access


def is_kind(c, *kinds):
    return kind_id(c) in {k.value for k in kinds}

# Calls that move raw bytes: callee name -> indexes of the arguments that point at the bytes.
RAW_IO = {
    "Read": (0,),
    "Write": (0,),
    "ReadFile": (1,),
    "WriteFile": (1,),
    "fread": (0,),
    "fwrite": (0,),
    "memcpy": (0, 1),
    "memmove": (0, 1),
    "memcmp": (0, 1),
    "CopyMemory": (0, 1),
    "recv": (1,),
    "send": (1,),
}

# ByteBuffer/Packet members that copy sizeof(T) bytes of a value, or bytes from a pointer.
BUFFER_CLASSES = {"ByteBuffer", "Packet"}

CASTS = {
    K.UNEXPOSED_EXPR,
    K.CSTYLE_CAST_EXPR,
    K.CXX_STATIC_CAST_EXPR,
    K.CXX_REINTERPRET_CAST_EXPR,
    K.CXX_CONST_CAST_EXPR,
    K.CXX_FUNCTIONAL_CAST_EXPR,
    K.PAREN_EXPR,
}

# Scalar types whose size differs between the targets (or between LLP64 and LP64).
HAZARDS = {"long", "unsigned long", "wchar_t", "long double"}


def compile_args(entry, extra):
    """The compile database entry's arguments, minus output, PCH and warnings-as-errors."""
    argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    out, skip = [], 0
    argv = argv[1:]
    for i, a in enumerate(argv):
        if skip:
            skip -= 1
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = 1
            continue
        if a == "-c" or a == entry["file"] or a.startswith("-W") or a == "-Werror":
            continue
        if a == "-Xclang" and i + 1 < len(argv) and argv[i + 1] == "-include-pch":
            skip = 3  # -Xclang -include-pch -Xclang <file.pch>
            continue
        out.append(a)
    return out + ["-w"] + extra


def strip(e):
    while is_kind(e, *CASTS):
        children = [c for c in e.get_children() if not is_kind(c, K.TYPE_REF, K.TEMPLATE_REF)]
        if not children:
            break
        e = children[-1]
    return e


def first_token(e):
    for t in e.get_tokens():
        return t.spelling
    return None


def object_type(arg):
    """The type of the object whose bytes the argument points at, or None."""
    e = strip(arg)
    if is_kind(e, K.UNARY_OPERATOR) and first_token(e) == "&":
        children = list(e.get_children())
        if children:
            return children[0].type.get_canonical()
    t = e.type.get_canonical()
    if t.kind == ci.TypeKind.POINTER:
        return t.get_pointee().get_canonical()
    if t.kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
        return t.get_array_element_type().get_canonical()
    return None


def describe(t):
    """(key, spelling, kind, decl usr, decl file) for a canonical type."""
    t = t.get_canonical()
    while t.kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
        t = t.get_array_element_type().get_canonical()
    spelling = t.spelling.replace("const ", "").replace("volatile ", "")
    if t.kind == ci.TypeKind.RECORD or t.kind == ci.TypeKind.ENUM:
        d = t.get_declaration()
        f = d.location.file.name if d.location.file else ""
        return spelling, spelling, "record" if t.kind == ci.TypeKind.RECORD else "enum", d.get_usr(), f
    if t.kind == ci.TypeKind.POINTER:
        return "pointer", spelling, "pointer", "", ""
    return spelling, spelling, "scalar", "", ""


def site(c, repo):
    loc = c.location
    return f"{os.path.relpath(loc.file.name, repo)}:{loc.line}"


FILE_CLASSES = {"File", "FileReader", "FileWriter"}
FILE_FUNCS = {"fread", "fwrite", "ReadFile", "WriteFile"}
COPY_FUNCS = {"memcpy", "memmove", "memcmp", "CopyMemory"}
BYTES = {"void", "char", "unsigned char", "signed char"}


def resolve_dependent(args_):
    """For template code: the type named in a sizeof() among the call's arguments."""
    for a in args_:
        for c in a.walk_preorder():
            if is_kind(c, K.TYPE_REF) and c.referenced is not None:
                return c.referenced.type.get_canonical()
    return None


def scan_tu(job):
    """Find raw byte moves in one translation unit."""
    repo, src, args = job
    index = ci.Index.create()
    try:
        tu = index.parse(src, args=args)
    except ci.TranslationUnitLoadError as ex:
        return {"file": src, "errors": [str(ex)], "finds": []}
    srcroot = os.path.join(repo, "src") + os.sep
    finds = []
    errors = [d.spelling for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error][:3]

    def add(c, callee, category, t, args_, other=None):
        if t is None:
            return
        key, spelling, kind, usr, decl = describe(t)
        if "type-parameter" in spelling or spelling == "<dependent type>":
            rt = resolve_dependent(args_)
            if rt is None:
                return
            key, spelling, kind, usr, decl = describe(rt)
        if spelling in BYTES:
            return
        finds.append(
            {
                "site": site(c, repo),
                "tu": os.path.relpath(src, repo),
                "callee": callee,
                "category": category,
                "key": key,
                "kind": kind,
                "usr": usr,
                "decl": os.path.relpath(decl, repo) if decl else "",
                "other": other,
            }
        )

    for c in tu.cursor.walk_preorder():
        if not is_kind(c, K.CALL_EXPR) or c.location.file is None:
            continue
        if not c.location.file.name.startswith(srcroot):
            continue
        callee = c.referenced
        if callee is None:
            continue
        name = callee.spelling
        args_ = list(c.get_arguments())
        parent = callee.semantic_parent.spelling if callee.semantic_parent else ""
        label = f"{parent}::{name}" if parent and parent != tu.spelling else name

        if parent in BUFFER_CLASSES and name in ("operator<<", "operator>>", "append", "put", "read"):
            # The value forms copy sizeof(T) bytes; the pointer forms copy `len` bytes from a buffer.
            n = len(args_)
            if name == "read" and n <= 1:  # read<T>() / read<T>(pos)
                add(c, f"{parent}::read<T>", "packet", c.type, args_)
            elif name == "read":  # read(dest, len) / read(pos, dest, len)
                add(c, f"{parent}::read", "packet", object_type(args_[n - 2]), args_)
            elif name in ("append", "put") and n >= 2 + (name == "put"):
                add(c, label, "packet", object_type(args_[n - 2]), args_)
            elif n:
                t = args_[-1].type.get_canonical()
                if t.kind == ci.TypeKind.LVALUEREFERENCE:
                    t = t.get_pointee().get_canonical()
                if not (t.kind == ci.TypeKind.RECORD and t.get_declaration().spelling in BUFFER_CLASSES):
                    add(c, f"{label}<T>", "packet", t, args_)
            continue

        if name in COPY_FUNCS and len(args_) >= 2:
            a, b = object_type(args_[0]), object_type(args_[1])
            sa = describe(a)[1] if a is not None else "void"
            sb = describe(b)[1] if b is not None else "void"
            if sa in BYTES and sb in BYTES:
                continue
            if sa == sb:
                add(c, name, "copy", a, args_)
            elif sa in BYTES:
                add(c, name, "serialize", b, args_)
            elif sb in BYTES:
                add(c, name, "serialize", a, args_)
            else:
                add(c, name, "reinterpret", a, args_, other=sb)
                add(c, name, "reinterpret", b, args_, other=sa)
            continue

        if name in ("Read", "Write") and parent in FILE_CLASSES or name in FILE_FUNCS:
            i = 1 if name in ("ReadFile", "WriteFile") else 0
            if i < len(args_):
                add(c, label, "file", object_type(args_[i]), args_)
        elif name in ("recv", "send") and len(args_) > 1:
            add(c, name, "socket", object_type(args_[1]), args_)
        elif name in ("Read", "Write") and args_:
            add(c, label, "other", object_type(args_[0]), args_)

    return {"file": src, "errors": errors, "finds": finds}


# --- Layouts ---------------------------------------------------------------------------------

TARGETS = ("macos-arm64", "windows-x64", "windows-x86")
TRIPLES = {"windows-x64": "x86_64-w64-mingw32", "windows-x86": "i686-w64-mingw32"}
RECORD_DECLS = (K.STRUCT_DECL, K.CLASS_DECL, K.UNION_DECL, K.ENUM_DECL, K.CLASS_TEMPLATE)


def norm(name):
    """Windows and the compat headers name some types differently (tagRECT/RECT, _D3DFORMAT/D3DFORMAT)."""
    name = name.replace("struct ", "").replace("enum ", "").replace("const ", "")
    head, _, last = name.rpartition("::")
    last = re.sub(r"^(tag|_)(?=[A-Z])", "", last)
    return f"{head}::{last}" if head else last


def layout_tu(job):
    """Layouts of the wanted types (and the records inside them) in one TU under one target."""
    src, target, args, wanted = job
    index = ci.Index.create()
    try:
        tu = index.parse(src, args=args)
    except ci.TranslationUnitLoadError as ex:
        return {"src": src, "target": target, "errors": [str(ex)], "layouts": {}}
    errors = [d.spelling for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error][:3]

    defs = {}
    for c in tu.cursor.walk_preorder():
        if is_kind(c, *RECORD_DECLS) and c.is_definition():
            defs.setdefault(norm(c.type.get_canonical().spelling), c)

    layouts, todo = {}, list(wanted)
    while todo:
        name = todo.pop()
        if name in layouts or name not in defs:
            continue
        d = defs[name]
        t = d.type
        lay = {
            "name": t.get_canonical().spelling,
            "size": t.get_size(),
            "align": t.get_align(),
            "fields": [],
            "bases": [],
            "access": d.access_specifier.name if d.semantic_parent and is_kind(d.semantic_parent, K.CLASS_DECL, K.STRUCT_DECL) else "",
        }
        if is_kind(d, K.ENUM_DECL):
            lay["underlying"] = d.enum_type.get_canonical().spelling
        for ch in d.get_children():
            if is_kind(ch, K.CXX_BASE_SPECIFIER):
                bt = ch.type.get_canonical()
                lay["bases"].append(norm(bt.spelling))
                todo.append(norm(bt.spelling))
            elif is_kind(ch, K.FIELD_DECL):
                ft = ch.type.get_canonical()
                et = ft
                while et.kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
                    et = et.get_array_element_type().get_canonical()
                bits = t.get_offset(ch.spelling) if ch.spelling else -1
                lay["fields"].append(
                    {
                        "name": ch.spelling,
                        "offset": bits // 8 if bits >= 0 else -1,
                        "bits": bits,
                        "width": ch.get_bitfield_width() if ch.is_bitfield() else None,
                        "type": ft.spelling,
                        "size": ft.get_size(),
                        "access": ch.access_specifier.name,
                    }
                )
                if et.kind in (ci.TypeKind.RECORD, ci.TypeKind.ENUM):
                    todo.append(norm(et.spelling))
            elif is_kind(ch, K.CXX_METHOD) and ch.is_virtual_method():
                lay["virtual"] = True
        loc = d.location.file.name if d.location.file else ""
        lay["decl"] = loc
        layouts[name] = lay
    return {"src": src, "target": target, "errors": errors, "layouts": layouts}


# Sizes of the scalar types on each target (fixed by the ABIs).
SCALAR_SIZES = {
    "bool": (1, 1, 1),
    "short": (2, 2, 2),
    "unsigned short": (2, 2, 2),
    "int": (4, 4, 4),
    "unsigned int": (4, 4, 4),
    "float": (4, 4, 4),
    "double": (8, 8, 8),
    "long long": (8, 8, 8),
    "unsigned long long": (8, 8, 8),
    "long": (8, 4, 4),
    "unsigned long": (8, 4, 4),
    "wchar_t": (4, 2, 2),
    "pointer": (8, 8, 4),
}


def jobs_for(repo, db_dir, extra, want):
    db = json.load(open(os.path.join(repo, db_dir, "compile_commands.json")))
    jobs = []
    for e in db:
        f = e["file"]
        rel = os.path.relpath(f, repo)
        if not rel.startswith("src/") or not want(rel) or not f.endswith(".cpp"):
            continue
        jobs.append((repo, f, compile_args(e, extra)))
    return jobs


def db_entries(repo, db_dir):
    db = json.load(open(os.path.join(repo, db_dir, "compile_commands.json")))
    return {e["file"]: e for e in db}


def drop_pair(args, flag):
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a == flag:
            skip = True
            continue
        out.append(a)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo")
    ap.add_argument("--out", required=True)
    ap.add_argument("--json")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--libclang", help="libclang to use (match it to the SDK's libc++)")
    ap.add_argument("--resource-dir", required=True)
    ap.add_argument("--mingw", required=True, help="MinGW-w64 prefix with toolchain-x86_64 and toolchain-i686")
    ap.add_argument("--emit", action="store_true", help="(re)generate the LayoutChecks.cpp files")
    opts = ap.parse_args()
    use_libclang(opts.libclang)
    repo = os.path.abspath(opts.repo)

    sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
    res = ["-resource-dir", opts.resource_dir]
    mac_extra = res + ["-isysroot", sdk]

    client = lambda r: "/Platform/" not in r
    server = lambda r: r.startswith("src/Server/") or r.startswith("src/db-library/")
    jobs = jobs_for(repo, "build/mac-nopch", mac_extra, client)
    jobs += jobs_for(repo, "build/mac-sqlite", mac_extra, server)
    print(f"scanning {len(jobs)} translation units", file=sys.stderr)

    pool = ProcessPoolExecutor(opts.jobs, initializer=use_libclang, initargs=(opts.libclang,))
    results = list(pool.map(scan_tu, jobs))
    for r in results:
        if r.get("errors"):
            print(f"  {os.path.relpath(r['file'], repo)}: {r['errors'][0]}", file=sys.stderr)

    unique = {}
    for f in (f for r in results for f in r["finds"]):
        unique.setdefault((f["site"], f["key"], f["callee"], f["category"]), f)
    finds = sorted(unique.values(), key=lambda f: (f["kind"], f["key"], f["site"]))

    # Layouts of every record and enum whose bytes leave the process (not plain copies).
    mac_db = {**db_entries(repo, "build/mac-nopch"), **db_entries(repo, "build/mac-sqlite")}
    mingw_db = db_entries(repo, "build/mingw-nopch")
    wanted = collections.defaultdict(set)
    for f in finds:
        if f["kind"] in ("record", "enum") and f["category"] != "copy":
            wanted[os.path.join(repo, f["tu"])].add(norm(f["key"]))

    ljobs = []
    for src, usrs in wanted.items():
        mac_args = compile_args(mac_db[src], mac_extra)
        ljobs.append((src, "macos-arm64", mac_args, sorted(usrs)))
        for target, triple in TRIPLES.items():
            sysroot = os.path.join(opts.mingw, "toolchain-" + ("x86_64" if "x64" in target else "i686"))
            win = ["--target=" + triple, "--sysroot=" + sysroot] + res
            if src in mingw_db:
                args = compile_args(mingw_db[src], win)
            else:  # servers: the macOS flags, retargeted
                args = drop_pair(drop_pair(compile_args(mac_db[src], win), "-arch"), "-isysroot")
            ljobs.append((src, target, args, sorted(usrs)))
    print(f"computing layouts in {len(ljobs)} parses", file=sys.stderr)

    layouts = collections.defaultdict(dict)
    for r in pool.map(layout_tu, ljobs):
        if r["errors"]:
            print(f"  {r['target']} {os.path.relpath(r['src'], repo)}: {r['errors'][0]}", file=sys.stderr)
        for name, lay in r["layouts"].items():
            layouts[name].setdefault(r["target"], lay)
    pool.shutdown()

    if opts.emit:
        emit_checks(repo, finds, layouts)

    census = {"finds": finds, "layouts": layouts}
    if opts.json:
        json.dump(census, open(opts.json, "w"), indent=1)
    write_report(opts.out, finds, layouts)


# Where the generated checks go: (directory, file, precompiled header, include for MathUtils types).
CHECK_FILES = {
    "N3Base": ("src/N3Base", "N3LayoutChecks.cpp", "StdAfxBase.h"),
    "WarFare": ("src/Client/WarFare", "LayoutChecks.cpp", "StdAfx.h"),
    "Ebenezer": ("src/Server/Ebenezer", "LayoutChecks.cpp", "pch.h"),
}

# Headers some declaring headers need before them (they rely on their .cpp's include order).
EXTRA_INCLUDES = {
    "WarFare": ["<N3Base/N3Texture.h>"],  # N3Pond.h uses CN3Mng<CN3Texture> through N3Base.h
}


def owner(path):
    if path.startswith("src/Client/WarFare/"):
        return "WarFare"
    if path.startswith("src/Server/") or path.startswith("src/shared/"):
        return "Ebenezer"
    return "N3Base"


def emit_checks(repo, finds, layouts):
    """Writes one LayoutChecks file per component with the size and offset asserts."""
    groups = collections.defaultdict(dict)  # component -> {name: (spelling, sites)}
    for f in finds:
        if f["kind"] not in ("record", "enum") or f["category"] == "copy":
            continue
        name = norm(f["key"])
        for comp in {owner(f["decl"]) if not f["decl"].startswith("src/MathUtils/") else owner(f["site"]), owner(f["site"]) if owner(f["site"]) == "Ebenezer" else None} - {None}:
            groups[comp].setdefault(name, (f["key"], set()))[1].add(f["site"].split(":")[0])

    for comp, types in groups.items():
        # Follow bases and record fields, so nested layouts are pinned too.
        todo = list(types)
        while todo:
            name = todo.pop()
            per = layouts.get(name, {})
            lay = per.get("windows-x86")
            if lay is None:
                continue
            for b in lay["bases"]:
                if b not in types:
                    types[b] = (b, set())
                    todo.append(b)
            for fld in lay["fields"]:
                et = norm(re.sub(r"\[\d+\]", "", fld["type"]).strip())
                if et in layouts and et not in types:
                    types[et] = (et, set())
                    todo.append(et)

        directory, filename, pch = CHECK_FILES[comp]
        includes, body, uses_bits = list(EXTRA_INCLUDES.get(comp, [])), [], False
        for name in sorted(types, key=lambda n: n.lstrip("_").lower()):
            spelling, sites = types[name]
            per = layouts.get(name, {})
            if not same_layout(per):
                body.append(f"// {spelling}: layouts differ between targets, see the census report.")
                continue
            lay = per["windows-x86"]
            decl = os.path.relpath(per["macos-arm64"]["decl"], repo) if per["macos-arm64"].get("decl") else ""
            inc = None
            if "/Platform/Compat/" in decl:
                pass  # Windows types: <windows.h>/<d3d9.h> there, the compat headers elsewhere; both via the PCH
            elif decl.startswith(directory + "/"):
                inc = f'"{os.path.relpath(decl, directory)}"'
            elif decl.startswith("src/MathUtils/"):
                inc = f"<MathUtils/{os.path.basename(decl)}>"
            if inc and inc not in includes:
                includes.append(inc)
            if spelling.startswith("_") and not spelling.startswith("__") or name != norm(spelling):
                spelling = name  # the typedef names (D3DMATERIAL9 rather than _D3DMATERIAL9)
            where = ", ".join(sorted(os.path.basename(s) for s in sites)) if sites else "nested in the types above"
            body.append(f"// {where}")
            body.append(f"static_assert(sizeof({spelling}) == {lay['size']});")
            for fld in lay["fields"]:
                if not fld["name"] or fld["bits"] < 0:
                    continue
                if fld["width"] is None:
                    body.append(f"static_assert(__builtin_offsetof({spelling}, {fld['name']}) == {fld['offset']});")
                    continue
                uses_bits = True
                body.append(f"static_assert(std::is_same_v<decltype({spelling}::{fld['name']}), {bitfield_type(fld)}>);")
            body.append("")

        out = [
            "// Generated by port/census/layout_census.py --emit. Don't edit by hand; rerun the census.",
            "//",
            "// The types whose bytes are read, written or copied raw (asset files, packets, blobs) must",
            "// have the same layout on every target. These are their sizes and field offsets on Windows",
            "// x86, the original 1.298 client's target; the census found them identical on Windows x64",
            "// and macOS arm64.",
            "",
            f'#include "{pch}"',
        ]
        out += [f"#include {i}" for i in includes[: len(EXTRA_INCLUDES.get(comp, []))]]
        out += [f"#include {i}" for i in sorted(includes[len(EXTRA_INCLUDES.get(comp, [])) :])]
        if uses_bits:
            out += ["", "#include <cstdint>", "#include <type_traits>"]
        out += [""]
        if uses_bits:
            out += [
                "// Bit-fields can't be checked at compile time: offsetof doesn't apply to them, and Apple",
                "// Clang can't bit_cast them in a constant expression yet. The census compared their bit",
                "// offsets on all three targets. MSVC and the Itanium ABI lay them out alike as long as every",
                "// bit-field in a struct has the same type and they fit in one unit of it, which is what the",
                "// is_same checks below hold in place.",
                "",
            ]
        out += [
            "// __builtin_offsetof rather than offsetof: MSVC's offsetof macro is a constant expression",
            "// only to MSVC itself (Clang folds it as an extension, which clang-tidy reports). On a",
            "// struct with fields in both a base and itself (not standard-layout) it's conditionally",
            "// supported; every compiler here supports it for single non-virtual inheritance, and GCC",
            "// and Clang warn about it.",
            "#if defined(__clang__) || defined(__GNUC__)",
            "#pragma GCC diagnostic push",
            '#pragma GCC diagnostic ignored "-Winvalid-offsetof"',
            "#endif",
            "",
        ]
        out += body
        out += ["#if defined(__clang__) || defined(__GNUC__)", "#pragma GCC diagnostic pop", "#endif"]
        path = os.path.join(repo, directory, filename)
        open(path, "w").write("\n".join(out).rstrip() + "\n")
        print(f"wrote {os.path.relpath(path, repo)} ({len(types)} types)", file=sys.stderr)


def bitfield_type(field):
    return {"unsigned int": "uint32_t", "int": "int32_t", "unsigned short": "uint16_t", "short": "int16_t",
            "unsigned char": "uint8_t", "signed char": "int8_t"}.get(field["type"], field["type"])


def same_layout(per_target):
    keys = [
        (l["size"], l["align"], tuple((f["name"], f["bits"], f["width"], f["size"]) for f in l["fields"]))
        for l in (per_target.get(t) for t in TARGETS)
        if l is not None
    ]
    return len(keys) == len(TARGETS) and len(set(keys)) == 1


def write_report(path, finds, layouts):
    by_type = collections.defaultdict(list)
    for f in finds:
        by_type[(f["kind"], f["key"])].append(f)
    usr_of = {f["key"]: norm(f["key"]) for f in finds if f["usr"]}

    lines = []
    w = lines.append
    w("# Raw-bytes layout census")
    w("")
    w("Generated by `port/census/layout_census.py`. Every place the client and servers move the bytes")
    w("of an object without a field-by-field serializer, the type whose bytes move, and that type's")
    w("layout on macOS arm64, Windows x64 and Windows x86 (the original 1.298 target).")
    w("")
    cats = collections.Counter(f["category"] for f in finds)
    w("Sites by kind: " + ", ".join(f"{k} {v}" for k, v in sorted(cats.items())) + ".")
    w("")
    w("- **file**: File/FileReader/FileWriter Read and Write, fread/fwrite, ReadFile/WriteFile.")
    w("- **packet**: ByteBuffer/Packet members that copy `sizeof(T)` bytes or a buffer.")
    w("- **serialize**: memcpy and friends between a typed object and a byte buffer.")
    w("- **copy**: memcpy between two objects of the same type, in memory. Not a layout question.")
    w("")
    w("Bit-fields are compared by bit offset. `src/N3Base/N3LayoutChecks.cpp`,")
    w("`src/Client/WarFare/LayoutChecks.cpp` and `src/Server/Ebenezer/LayoutChecks.cpp` are generated")
    w("from this census (`--emit`) and `static_assert` every size and offset below in every build.")
    w("")
    w("Moved as bytes, but not a fixed cross-target layout, by design:")
    w("")
    w("- **Table rows** (`.tbl`, `CN3TableBase<T>`): filled column by column at offsets the loader")
    w("  computes from `alignof(T)` and each column's size, using `sizeof(std::string)` for strings. It")
    w("  rejects a table whose computed row size isn't `sizeof(T)`. The rows hold `std::string`, which")
    w("  is 24 bytes in libc++ and 32 in MSVC's x64 library, so these structs differ per target and the")
    w("  loader adapts to each.")
    w("- **Packets and database blobs**: the client's `Parse_Get*` templates and the servers'")
    w("  `GetShort`/`GetDWORD`/`GetInt64`/`SetDWORD`… helpers copy fixed-width integers one field at a")
    w("  time (counted under Scalars).")
    w("- **Shared memory** between Ebenezer, Aujard and ItemManager: both sides are the same binary")
    w("  architecture, so there is no cross-target layout.")
    w("")

    w("## Records and enums")
    w("")
    w("| Type | Declared in | Sites | Size (mac / win64 / win32) | Same layout everywhere |")
    w("|---|---|---|---|---|")
    records = sorted(k for k in by_type if k[0] in ("record", "enum"))
    nested = set()
    for (kind, key) in records:
        fs = by_type[(kind, key)]
        moved = [f for f in fs if f["category"] != "copy"]
        cats_ = collections.Counter(f["category"] for f in fs)
        usr = usr_of.get(key, "")
        per = layouts.get(usr, {})
        sizes = " / ".join(str(per[t]["size"]) if t in per else "?" for t in TARGETS)
        same = ("yes" if same_layout(per) else "**no**") if moved else "(copies only)"
        w(f"| `{key}` | {fs[0]['decl']} | " + ", ".join(f"{k} {v}" for k, v in sorted(cats_.items())) + f" | {sizes} | {same} |")
    listed = {usr_of.get(k) for _, k in records}
    for name, per in sorted(layouts.items()):
        if name in listed:
            continue
        sizes = " / ".join(str(per[t]["size"]) if t in per else "?" for t in TARGETS)
        w(f"| `{name}` | (inside the above) | | {sizes} | {'yes' if same_layout(per) else '**no**'} |")
    w("")

    w("## Scalars")
    w("")
    w("| Type | Sites | Size (mac / win64 / win32) |")
    w("|---|---|---|")
    for (kind, key), fs in sorted(by_type.items()):
        if kind not in ("scalar", "pointer"):
            continue
        sz = SCALAR_SIZES.get(key)
        sizes = " / ".join(map(str, sz)) if sz else "?"
        flag = "" if sz and len(set(sz)) == 1 else " **differs**"
        w(f"| `{key}` | {len(fs)} | {sizes}{flag} |")
    w("")

    w("## Sites that need a look")
    w("")
    for f in finds:
        sz = SCALAR_SIZES.get(f["key"])
        if f["category"] == "reinterpret" or (f["kind"] in ("scalar", "pointer") and (not sz or len(set(sz)) > 1)):
            w(f"- `{f['site']}` {f['callee']} ({f['category']}): `{f['key']}`" + (f" vs `{f['other']}`" if f["other"] else ""))
    w("")

    w("## All sites")
    w("")
    for (kind, key), fs in sorted(by_type.items()):
        w(f"<details><summary><code>{key}</code> ({len(fs)})</summary>")
        w("")
        for f in fs:
            w(f"- `{f['site']}` {f['callee']} ({f['category']})")
        w("")
        w("</details>")
        w("")
    open(path, "w").write("\n".join(lines))


if __name__ == "__main__":
    main()
