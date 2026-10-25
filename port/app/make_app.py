#!/usr/bin/env python3
"""Assembles KnightOnline.app: the macOS client, the server supervisor, the five servers, the game
data and the seed database, ad-hoc signed.

usage: make_app.py --build <cmake build dir> --assets <repo>/assets --seed <KN_online.sqlite>
                   --out <output dir> [--config Release]

The build dir is a macOS build with the client and servers (OPENKO_BUILD_MACOS_CLIENT=ON,
OPENKO_BUILD_SERVERS=ON, OPENKO_DB_BACKEND=SQLITE). The seed database comes from
port/db/openkodb_to_sqlite.py. Game data is copied with APFS clones where possible, so the 750 MB
of client data costs no extra disk space on the same volume.

Layout:
  Contents/MacOS/KnightOnline        the client
  Contents/Helpers/                  ko-servers and AIServer, Ebenezer, Aujard, VersionManager, ItemManager
  Contents/Frameworks/               libjpeg, OpenAL Soft
  Contents/Resources/Client/         the client's game data (read-only; the game writes to
                                     ~/Library/Application Support/KnightOnline)
  Contents/Resources/Server/         MAP, QUESTS, KN_online.sqlite (the seed), port-game-data.sql
                                     (port/db/port_game_data.py; ko-servers adds it to the database)
  Contents/Resources/Cursors/        the client's cursors
"""

import argparse
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile

SERVERS = ["AIServer", "Ebenezer", "Aujard", "VersionManager", "ItemManager"]
DYLIBS = {"libjpeg.9.dylib": "jpeg-build/libjpeg", "libopenal.1.dylib": "openalsoft-build"}
SKIP_CLIENT_DATA = {".dll", ".exe", ".md"}  # Windows binaries and the submodule's README


def run(*args):
    subprocess.run(args, check=True)


def output(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def clone_tree(src, dst, skip_suffixes=()):
    """Copies a directory tree with APFS clones (cp -c), falling back to a plain copy."""
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if os.path.splitext(name)[1].lower() in skip_suffixes:
            continue
        s, d = os.path.join(src, name), os.path.join(dst, name)
        if subprocess.run(["cp", "-c", "-R", s, d]).returncode != 0:
            if os.path.isdir(s):
                shutil.copytree(s, d)
            else:
                shutil.copy2(s, d)


def rpaths(binary):
    lines = output("otool", "-l", binary).splitlines()
    found = []
    for i, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            found.append(lines[i + 2].split()[1])
    return found


def min_os(binary):
    """The minimum macOS version the binary was built for (LC_BUILD_VERSION minos)."""
    for line in output("otool", "-l", binary).splitlines():
        if line.strip().startswith("minos "):
            return line.split()[1]
    raise RuntimeError(f"no LC_BUILD_VERSION in {binary}")


def make_icon(ico, out_icns):
    """The client's Windows icon as AppIcon.icns."""
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "icon.png")
        run("sips", "-s", "format", "png", ico, "--out", png)
        iconset = os.path.join(tmp, "AppIcon.iconset")
        os.makedirs(iconset)
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                pixels = size * scale
                name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
                run("sips", "-z", str(pixels), str(pixels), png, "--out", os.path.join(iconset, name))
        run("iconutil", "-c", "icns", iconset, "-o", out_icns)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--assets", required=True)
    ap.add_argument("--seed", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--config", default="Release")
    opts = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    bindir = os.path.join(opts.build, "bin", opts.config)
    app = os.path.join(opts.out, "KnightOnline.app")
    contents = os.path.join(app, "Contents")
    if os.path.exists(app):
        shutil.rmtree(app)
    for sub in ("MacOS", "Helpers", "Frameworks", "Resources/Server"):
        os.makedirs(os.path.join(contents, sub))

    # The client, with its libraries looked up in Frameworks instead of the build tree.
    client = os.path.join(contents, "MacOS", "KnightOnline")
    shutil.copy2(os.path.join(bindir, "KnightOnLine"), client)
    for rpath in rpaths(client):
        run("install_name_tool", "-delete_rpath", rpath, client)
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", client)
    for name, subdir in DYLIBS.items():
        src = os.path.realpath(os.path.join(opts.build, "_deps", subdir, name))
        shutil.copy2(src, os.path.join(contents, "Frameworks", name))

    # The supervisor and the servers.
    for name in ["ko-servers"] + SERVERS:
        shutil.copy2(os.path.join(bindir, name), os.path.join(contents, "Helpers", name))

    # Game data.
    resources = os.path.join(contents, "Resources")
    client_data = os.path.join(resources, "Client")
    clone_tree(os.path.join(opts.assets, "Client"), client_data, SKIP_CLIENT_DATA)
    shutil.copy2(os.path.join(opts.assets, "Client", "Server.ini.default"), os.path.join(client_data, "Server.ini"))
    for sub in ("MAP", "QUESTS"):
        clone_tree(os.path.join(opts.assets, "Server", sub), os.path.join(resources, "Server", sub))
    shutil.copy2(opts.seed, os.path.join(resources, "Server", "KN_online.sqlite"))
    run(sys.executable, os.path.join(repo, "port", "db", "port_game_data.py"), os.path.join(opts.assets, "Client", "Data"),
        os.path.join(resources, "Server", "port-game-data.sql"))
    shutil.copytree(os.path.join(bindir, "Cursors"), os.path.join(resources, "Cursors"))
    make_icon(os.path.join(repo, "src", "Client", "WarFare", "WarFare_1298.ico"), os.path.join(resources, "AppIcon.icns"))

    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(
            {
                "CFBundleDevelopmentRegion": "en",
                "CFBundleDisplayName": "Knight Online",
                "CFBundleExecutable": "KnightOnline",
                "CFBundleIconFile": "AppIcon",
                "CFBundleIdentifier": "io.github.mostlyemre.knightonline",
                "CFBundleInfoDictionaryVersion": "6.0",
                "CFBundleName": "Knight Online",
                "CFBundlePackageType": "APPL",
                "CFBundleShortVersionString": "1.298",
                "CFBundleVersion": "1298",
                "LSApplicationCategoryType": "public.app-category.role-playing-games",
                "LSSupportsGameMode": True,
                "LSMinimumSystemVersion": min_os(client),
                "NSHighResolutionCapable": True,
                "NSHumanReadableCopyright": "Open-KO (github.com/Open-KO/KnightOnline), built from github.com/MostlyEmre/KnightOnline",
            },
            f,
        )
    with open(os.path.join(contents, "PkgInfo"), "w") as f:
        f.write("APPL????")

    # Ad-hoc signature, inside out.
    for name in DYLIBS:
        run("codesign", "--force", "--sign", "-", os.path.join(contents, "Frameworks", name))
    for name in ["ko-servers"] + SERVERS:
        run("codesign", "--force", "--sign", "-", os.path.join(contents, "Helpers", name))
    run("codesign", "--force", "--sign", "-", app)
    run("codesign", "--verify", "--strict", "--deep", app)

    size = output("du", "-sh", app).split()[0]
    print(f"{app} ({size})")


if __name__ == "__main__":
    sys.exit(main())
