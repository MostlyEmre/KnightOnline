# Phase 2: the client compiles on macOS

Branch `macos-port`. Phase 2's scope was N3Base and WarFare (plus JpegFile, which WarFare links) building for arm64 with CMake, Ninja and Apple Clang, warnings as errors, with the platform code behind clean boundaries in N3Base, and the Windows build kept working.

## Result

| | |
|---|---|
| macOS client build | `KnightOnLine`, a Mach-O arm64 executable, builds with Apple Clang 21 (Xcode 27.1), `-Wall -Wextra -Werror`: 0 warnings, 0 errors. `lipo -archs` → `arm64`. It links only system libraries plus libjpeg and OpenAL Soft, both built from source (they are dylibs today; Phase 5 bundles or links them statically). Configure with `-DOPENKO_BUILD_MACOS_CLIENT=ON`. |
| What runs | Nothing yet, by design. The macOS side of the platform layer is mostly stubs that log "not implemented" once; Phases 3 and 4 fill them in. |
| Windows build | MinGW cross-build of the client (local) and upstream's CI: CMake (MSVC/clang/gcc on Windows, Linux, macOS), MSBuild, clang-format, clang-tidy. Results below. |
| Servers and tests | Unaffected: all five servers build, upstream tests and the SQLite backend tests pass on arm64 (4/4 suites). |
| Lint | Upstream's clang-format 19: all changed files clean. Upstream's clang-tidy 19 on changed lines of the Windows build: 0 findings, checked locally against a MinGW compile database before pushing. The macOS-only platform code, which upstream's Windows lint job never sees, has its own job in `port-lint.yml`: 0 findings. |

Upstream files touched: 57 under `src/` plus the top-level `CMakeLists.txt`, +818/−1466 lines. Most of the removed lines are Win32 code that moved into the platform layer rather than being rewritten. New code: 4,254 lines under `src/N3Base/Platform/`.

## The boundary

The rule I followed: **`Platform/Compat` holds only data types, constants and pure functions; anything that talks to the OS goes through a `Platform::` interface with a Windows and a macOS implementation.** Game code calls the interface; neither game code nor N3Base engine code has `#ifdef __APPLE__`.

```
src/N3Base/Platform/
  *.h                 portable interfaces (namespace Platform)
  Win32/              the original Win32 / DirectInput / GDI / CryptoAPI / Winsock code, moved here
  macOS/              the macOS implementations (Phase 2: real where trivial, stubs otherwise)
  Compat/             non-Windows only: Win32 types, the D3D9 API surface, DIK_* scan codes
  FileSystem.cpp      portable (std::filesystem)
```

| Interface | What it covers | Windows implementation (moved from) | macOS in Phase 2 |
|---|---|---|---|
| `Types.h`, `RenderApi.h`, `KeyCodes.h` | Win32 data types; Direct3D 9 + the two D3DX calls; DirectInput scan codes | `<windows.h>`, `<d3dx9.h>`, `<dinput.h>`, as before | `Compat/` headers |
| `Time.h` | `timeGetTime`, `GetTickCount64`, QPC, `Sleep`, `GetLocalTime` | direct calls (N3Base, N3SkyMng, LocalInput, LogWriter, GameProcedure…) | **real** (mach_absolute_time, CLOCK_MONOTONIC_RAW) |
| `Shell.h` | message boxes, opening files/URLs, executable path, user language, debugger output | MessageBox*, ShellExecute, GetModuleFileName… (N3Eng, UI dialogs, DebugUtils…) | **real** exe path + debug output; message box/open/language stubbed (Phase 3: NSAlert, NSWorkspace, CFLocale) |
| `Input.h` | key state (VK_*), cursor position/shape/visibility, double-click time; `KeyboardDevice` (polled DIK_* state) | GetAsyncKeyState, cursor APIs; DirectInput 8 (LocalInput) | stubs (Phase 3: NSEvent) |
| `Window.h` | client/window rects, focus, active window, quit, pumping events while the device is lost, the display-mode change around the game | user32 + ChangeDisplaySettings (N3Eng, GameProcedure) | stubs (Phase 3) |
| `Application.h` | main window + event loop, delivering activate / close / wheel / text-changed / socket events to handlers | WinMain's window class, message loop and WndProc (WarFareMain) | stubs (Phase 3: AppKit) |
| `TextInput.h` | the hidden native field that does the typing for the focused `CN3UIEdit` (text, caret, IME) | the subclassed EDIT control + IMM (N3UIEdit) | stubs (Phase 3: NSTextInputClient) |
| `FontRasterizer.h` | measure text and draw it white-on-black into a 32-bit canvas for `CDFont` | GDI (DFont) | stub (Phase 3: CoreText) |
| `Crypto.h` | the RC4 decryption of the client's encrypted data files | CryptoAPI (WinCrypt) | **real** (portable SHA-1 + RC4, checked against a reference implementation) |
| `Settings.h` | per-user binary settings | registry (GameProcedure) | stub (Phase 3) |
| `Socket.h` | the client's TCP socket | Winsock + WSAAsyncSelect (APISocket) | stub (Phase 3: asio) |
| `ScreenCapture.h` | screenshot into a DIB | GDI screen capture (JpegFile) | stub (Phase 4: render-target readback) |
| `FileSystem.h` | files in a directory by extension | `_findfirst` (GameProcMain) | portable |

`Compat/D3D9.h` declares exactly the API surface the client uses (the 56 methods from the Phase 0 census) as abstract classes with D3D9's names, signatures and constant values, so the ~1,400 render call sites compile unchanged. Phase 4's Metal renderer implements them. The structs that file formats store raw (`D3DMATERIAL9`, `D3DLIGHT9`, `D3DCOLORVALUE`, …) and the enums stored in file headers (`D3DFORMAT`) keep D3D9's sizes, enforced with `static_assert`s.

## Windows behaviour

The Windows implementations are the original code, moved; the call sites call the same Win32 functions in the same order. The deliberate differences, all minor:

1. **Screenshot capture** (moved from JpegFile): C++ casts instead of C-style ones, and three leaks on early-return paths fixed (the screen DC when the display isn't palettized, which is every screenshot on a modern display; the palette block; the DIB when `GetDIBits` fails).
2. **Character resource preload** (`CGameProcMain`): lists `Chr\*.N3Anim` etc. with `Platform::FindFiles` instead of `SetCurrentDirectory` + `_findfirst`. The process's current directory is no longer changed and restored. It never mattered, because the loaders resolve paths against `CN3Base::PathGet()`. This also removes a read of an uninitialised `_finddata_t` when a folder is missing (`if (hFind)` was true for the -1 error handle).
3. **Server.ini** (1298 login): read with the repo's `CIni` instead of `GetPrivateProfileString`. Same sections and keys, still case-insensitive; `CIni` doesn't strip surrounding quotes, which Server.ini doesn't use.
4. **`CLocalInput::Init`** returns FALSE when the keyboard device can't be created (it returned TRUE with a null device that crashed in `Tick`). Nothing reads the return value.
5. **`CDFont`**: the shared GDI DC now comes and goes with the first and last font rather than with the first and last `CDFont` object. `GetTextExtent` without a font returns FALSE instead of measuring with whichever font the shared DC last had.
6. **Current directory** (`WarFareMain`, `CN3UIButton`): `std::filesystem::current_path` instead of `GetCurrentDirectory`/`SetCurrentDirectory`. Both use the ANSI code page for narrow strings.
7. **Link dependencies of the tools** (MSBuild): unchanged. DirectInput lives in its own object file, and the IME calls keep upstream's `#ifndef _N3TOOL`, so the MFC tools still don't need `dinput8`/`imm32`.

Fixes for warnings that `-Wextra` surfaced (Apple Clang; upstream builds the client with MSVC `/W3` or GCC `-Wall` only): commented-out unused parameter names, and defaulted copy constructor/assignment where the other was user-declared (`__AnimData`, `__Matrix44`, `__Quaternion`). Behaviour is unchanged.

The build files: the client builds on macOS only with `-DOPENKO_BUILD_MACOS_CLIENT=ON`, so upstream's macOS CI job keeps building just the servers; the tools stay Windows-only. N3Base's Windows-only libraries (`d3d9`, `d3dx9`, `dinput8`, `dsound`, `dxguid`) and the DirectX SDK fetch are under `WIN32`. Warning flags now match `AppleClang` as well as `Clang` for the client targets. The new files are in the MSBuild projects too (`N3Base_client`, `N3Base_tool`); the Win32 platform files build without the precompiled header, because `Socket_Win32.cpp` needs `winsock2.h` before `windows.h` and MSBuild compiles N3Base with `WIN32_LEAN_AND_MEAN`. All of them were compiled that way locally (lean, no PCH, MinGW headers).

## Checks run

- macOS: `cmake -DOPENKO_BUILD_MACOS_CLIENT=ON … && ninja WarFare`, 0 warnings. Binary: Mach-O 64-bit arm64; `otool -L` shows libSystem, libc++, libjpeg, libopenal.
- Windows: MinGW-w64 cross-build of the full client (`KnightOnLine.exe`), and every Win32 platform file compiled with `WIN32_LEAN_AND_MEAN` and without the PCH, as MSBuild does.
- Portable pieces: the RC4 decryptor against Python's SHA-1 and an RC4 reference (itself checked against the published "Key"/"Plaintext" vector), including the SHA-1 padding boundaries at 55, 56 and 64 bytes. `_splitpath`/`_makepath` against the MSVC CRT's rules (drive, either slash, last-dot extension, dot-files).
- Lint: clang-format 19 on every changed file; clang-tidy 19 on changed lines of all 50 changed Windows translation units (MinGW compile database, no PCH); clang-tidy 19 on the macOS platform code (macOS compile database, no PCH).
- CI: see the table below.

## CI

Upstream's workflows run on the fork through draft PR #1 (`macos-port` → fork `master`); the port's own workflows run on push.

| Workflow | Commit | Result |
|---|---|---|
| Build all (CMake): Windows MSVC/clang/gcc, Linux, macOS | `76111975` | ✅ [40061129819](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061129819) |
| Build all (MSBuild) | `76111975` | ✅ [40061129805](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061129805) |
| clang-format-check | `76111975` | ✅ [40061129804](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061129804) |
| clang-tidy-check | `76111975` | ✅ [40061129800](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061129800) |
| Build tools (MSBuild) | `7f7a5c8f` | ✅ [40062423120](https://github.com/MostlyEmre/KnightOnline/actions/runs/40062423120) |
| Build client tools (MSBuild) | `7f7a5c8f` | ✅ [40062423119](https://github.com/MostlyEmre/KnightOnline/actions/runs/40062423119) |
| Port: macOS client (arm64) | `76111975` | ✅ [40061126569](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061126569) |
| Port: macOS servers (arm64, SQLite) | `76111975` | ✅ [40061126434](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061126434) |
| Port: clang-tidy (SQLite backend, macOS platform code) | `76111975` | ✅ [40061126411](https://github.com/MostlyEmre/KnightOnline/actions/runs/40061126411) |

The two MSBuild tool workflows only run when their own files change, so `7f7a5c8f` touched them temporarily to make them run on the PR; `7d9f6d3d` reverts that. `76111975` is `fae70347` plus one fix that MSVC needed and MinGW didn't: `Socket_Win32.cpp` keeps WinSock 1.1 calls (`inet_addr`, `gethostbyname`), which MSVC rejects as deprecated (C4996, an error under `/WX`) unless `_WINSOCK_DEPRECATED_NO_WARNINGS` is defined.

## Noted for the next phases

- **Paths.** `CN3Base::PathSet` lowercases the whole base path and appends `\`, and the client builds paths with `\` throughout. The Phase 3 file layer has to map these to real paths: `/` separators, and the on-disk case, so that a case-sensitive volume also works.
- **Text encoding.** Client strings are in the ANSI code page, single- and double-byte (DFont and N3UIEdit split DBCS lead bytes). The CoreText rasterizer and the text input field have to convert at the boundary.
- **RC4 key derivation.** The portable decryptor assumes `CryptDeriveKey(CALG_RC4, SHA-1, 128-bit)` keys RC4 with the first 16 hash bytes and no salt (the enhanced provider's behaviour). It's confirmed only once real encrypted client data decrypts and parses, in Phase 3.
- **Struct layout asserts.** The compat headers already assert the sizes of the bitmap headers and the D3D structs that are stored raw. The full census of on-disk and on-wire structs is Phase 3's.
- **Packaging.** libjpeg and OpenAL Soft are built as dylibs on macOS. Phase 5 links them statically or bundles them.
