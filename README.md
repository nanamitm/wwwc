# WWWC

Source code of WWWC, a web page update checker for Windows, based on WWWC
Ver 1.1.3 by Ohno Tomoaki. This version builds for x64 (64-bit) as well as x86
(32-bit) and adds Unicode (UTF-8) support, dark mode, HTTPS certificate
verification and encrypted password storage. See [CHANGELOG.md](CHANGELOG.md) for all changes.

Original: Copyright (C) 1996-2018 by Ohno Tomoaki — https://www.nakka.com/

The user interface and the user manual (`docs/wwwc.html`) are in Japanese.

## Layout

```
WWWC.sln        Visual Studio solution
version.h       Version number (used by both projects)
WWWC/           Main program (WWWC.exe)
  res/          Icons, bitmaps and other resources
http_dll/       HTTP/FILE protocol DLL (wwwc.dll)
  wwwcdll.h     Public header for plugins (protocol DLLs / tool DLLs)
docs/           Original readme, manual and samples (shipped to users)
tests/          Automated tests (run-tests.ps1)
tools/          Packaging script (make-package.ps1)
third_party/    win32-darkmodelib (git submodule)
.github/        GitHub Actions workflow (build, test, release)
```

## Building

Requirements: Visual Studio 2026 (platform toolset v145) with the "Desktop
development with C++" workload, and the Windows 10/11 SDK.

Clone with submodules (`git clone --recursive`, or run
`git submodule update --init` after cloning).

Open `WWWC.sln` in Visual Studio and choose the configuration (`Debug` /
`Release`) and platform (`x86` / `x64`). From the command line:

```bat
MSBuild WWWC.sln -p:Configuration=Release -p:Platform=x64
```

Output folders:

| Platform | Folder |
|---|---|
| x86 | `Debug\` / `Release\` |
| x64 | `x64\Debug\` / `x64\Release\` |

Put `WWWC.exe` and `wwwc.dll` in the same folder and run `WWWC.exe`. The data
folder can be changed with the command line option `/d:"folder"`.

- Windows 10 version 1903 or later and Windows 11 are supported.
- Text is handled as UTF-8: the manifest sets the process code page to UTF-8
  (`activeCodePage`), so the existing ANSI (`char`) code and the `A` APIs work
  with all Unicode characters. String literals are compiled as UTF-8
  (`/execution-charset:utf-8`). WWWC exits with a message if the code page is
  not UTF-8 (Windows before 10 1903).
- `BUFSIZE` is 1024 bytes, so that a path of MAX_PATH characters fits in
  UTF-8 (up to 3 bytes per character). Structures shared with plugins keep
  their 256-byte strings (`PLUGIN_STRSIZE`). Windows still limits a path to
  MAX_PATH characters.
- Data and settings files are UTF-8. Shift_JIS files from older versions are
  converted on the first start (`WWWC/Charset.c`); the originals are kept in
  `<user>_sjis_backup` and `settings_sjis_backup`.
- Clipboard and drag & drop use `CF_UNICODETEXT` and wide `CF_HDROP` lists,
  because other applications read `CF_TEXT` in the system code page.
- Strings are compared with `strcmp` / `_stricmp` (identifiers, URLs, titles,
  ASCII names) or `PathCompare` (paths, ordinal and case-insensitive).
  `lstrcmp` / `lstrcmpi` compare by the user's locale and are used only to
  sort lists for display.
- Proxy (HTTP settings): none, the Windows settings (WinHTTP automatic
  proxy, including configuration scripts), or a given server
  (`[PROXY] Proxy` = 0 / 2 / 1 in wwwc.ini). A proxy set in the item
  properties takes precedence.
- Dark mode: Options > General > Theme (Light / Dark / Follow Windows), stored
  as `[GENERAL] DarkMode` = 0 / 1 / 2 in user.ini. The default is 2 (follow
  Windows). Requires Windows 10 1809 or later.
- HTTPS server certificates are verified. A server with a self-signed or
  otherwise invalid certificate can be allowed per item with "Ignore
  certificate errors" on the Options tab of the item properties. (1.1.3 always
  ignored certificate errors.)
- HTTP passwords (item authentication and proxy authentication) are stored
  encrypted with DPAPI (`CryptProtectData`, machine scope) as values starting
  with `DP2:` (UTF-8 password). Values saved in the old format are
  re-encrypted when they are read, and 1.2.0's `DP1:` values (Shift_JIS
  password) are still accepted. Encrypted values cannot be decrypted on another PC, so passwords must
  be entered again after moving the data to another PC. WWWC 1.1.3 cannot read
  them.
- Warnings that indicate pointer truncation (C4113, C4133, C4267, C4311,
  C4312) are treated as errors.

## Tests

The tests run against a built `WWWC.exe` / `wwwc.dll`. They need Python 3 and
PowerShell 7 (`pwsh`).

```bat
pwsh tests\run-tests.ps1 -BinDir x64\Release
pwsh tests\run-tests.ps1 -BinDir Release -Suite Http,Password
```

- Each test copies WWWC to `%TEMP%\wwwc-tests` (or `WWWC_TEST_ROOT`), prepares the data files,
  drives WWWC with window messages and checks the result. No mouse or keyboard
  input is used, but WWWC windows are shown, so run the tests in a desktop
  session.
- The HTTP tests use a local test server (`tests/server/test_server.py`) that
  listens only on 127.0.0.1 - 127.0.0.5. Nothing is sent to other hosts.
- Suites: `Parse` (loading Item.dat and user.ini), `Http` (check, redirects,
  errors, timeout, cancel, exit while checking), `Password` (encryption and
  migration of passwords), `Tools` (header view, source view, get title, open
  URL), `Unicode` (conversion of old data, pages in UTF-8 / Shift_JIS /
  EUC-JP / ISO-2022-JP, filters, passwords from older versions, long
  Japanese folder and item names).
- The script exits with code 1 when a test fails.

### AddressSanitizer

The same tests can run on an AddressSanitizer build, which stops WWWC at the
first memory error (buffer overflow, use after free) and fails the test:

```bat
MSBuild WWWC.sln -p:Configuration=Release -p:Platform=x64 -p:EnableASAN=true -p:OutDir=%CD%\asan-build\x64\ -p:IntDir=asan-build\x64\
copy "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" asan-build\x64\
pwsh tests\run-tests.ps1 -BinDir asan-build\x64
```

- The runner notices the ASan runtime DLL, writes the reports to
  `wwwc-tests\asan` and adds a summary to the failed test.
- `lstrcpy`, `lstrcat`, `lstrcpyn`, `lstrlen` and `wsprintf` are implemented in
  Windows and not instrumented, so an ASan build force-includes
  `tools/asan/asan_shims.h`, which replaces them with instrumented versions.
- WWWC allocates with `GlobalAlloc`; the runner enables
  `windows_hook_rtl_allocators` so that heap errors are found as well.

## CI

The GitHub Actions workflow (`.github/workflows/build.yml`) builds all four
configurations (Debug / Release, x86 / x64) on every push to `main` and every
pull request, and runs the tests on the Release builds.

- Warnings are errors (`-warnAsError`).
- The job "AddressSanitizer x64" runs the tests on an ASan build; on failure
  the reports are kept as the artifact `asan-reports`.
- If the runner does not have v145 (VS 2026), it builds with v143 (VS 2022)
  and shows a notice.
- The Release binaries are kept as artifacts (`wwwc-x86` / `wwwc-x64`) for 14
  days.

## Releasing

1. Update the version in `version.h`, add a section for it to `CHANGELOG.md`
   (the heading must be `## <version>`), and add the Japanese entry to the
   update history in `docs/readme.txt`.
2. Merge to `main`, then push a tag named `v<version>`:
   ```bat
   git tag v1.2.0
   git push origin v1.2.0
   ```
3. The workflow builds and tests the tag, checks that the tag matches
   `version.h`, and creates a **draft** GitHub release with
   `wwwc-<version>-x86.zip` and `wwwc-<version>-x64.zip`. Review the draft and
   publish it by hand.

Each zip contains `WWWC.exe`, `wwwc.dll`, the user documents from `docs/`,
`CHANGELOG.md` and the third-party licenses (`licenses/`). To make a zip
locally:

```bat
pwsh tools\make-package.ps1 -Platform x64 -BinDir x64\Release -OutDir dist
```

The binaries are not code-signed. Windows SmartScreen may warn when an
unsigned program downloaded from the internet is run for the first time.

## Writing plugins

WWWC.exe gets the plugin functions with `GetProcAddress` and calls them.

- **The 64-bit WWWC.exe can load only 64-bit DLLs.** Existing 32-bit plugins
  do not work with the 64-bit version.
- Rebuild plugins for the 64-bit version with `http_dll/wwwcdll.h` from this
  repository. These members of `struct TPITEM` are now pointer-sized
  (`LONG_PTR` / `UINT_PTR`). Their size does not change on x86, so 32-bit
  plugins stay compatible.
  - `hItem` (`LONG_PTR`)
  - `Soc1`, `Soc2` (`UINT_PTR`, hold a `SOCKET`)
  - `Param1` - `Param4` (`LONG_PTR`, may hold pointers)
- **Strings are UTF-8** (since 1.3.0): all `char *` strings that WWWC passes
  to plugins and reads from them, including `struct TPITEM`, are UTF-8.
  Plugins that assumed Shift_JIS must be updated. A plugin DLL runs in the
  WWWC process, so `CP_ACP` and the `A` APIs are UTF-8 there as well.
- WWWC.exe initializes Winsock and forwards `WM_WSOCK_GETHOST` /
  `WM_WSOCK_SELECT` to `XXX_Gethost` / `XXX_Select` for protocol DLLs that
  use asynchronous Winsock (the HTTP plugin itself uses WinHTTP).
- `WM_WWWC_GETINIPATH` takes the buffer size in `wParam` (since 1.3.1); 0
  means 256 bytes, as older plugins assume. The path can be longer than 255
  bytes in UTF-8.
- All functions are `CALLBACK` (`__stdcall`). Export them with a `.def` file so
  that the x86 export names are not decorated (see `http_dll/WWWC.def`).

Protocol DLL functions (`XXX_` is the "header" in the protocol settings):

```c
int    CALLBACK GetProtocolList(int cnt, struct TPPROTOCOLSET *tpProtocolSet);
int    CALLBACK XXX_GetInfo(struct TPPROTOCOLINFO *tpInfo);
int    CALLBACK XXX_Initialize(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Start(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Timer(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Cancel(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_ItemCheckEnd(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_InitItem(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_FreeItem(struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Gethost(HWND hWnd, WPARAM wParam, LPARAM lParam, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Select(HWND hWnd, WPARAM wParam, LPARAM lParam, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_ExecItem(HWND hWnd, char *Action, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Property(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_ProtocolProperty(HWND hWnd);
HANDLE CALLBACK XXX_GetItemText(struct TPITEM *tpItemInfo);
BOOL   CALLBACK XXX_CreateDropItem(struct TPITEM *tpItemInfo, char *fPath, char *iPath, char *ret);
int    CALLBACK XXX_DropFile(HWND hWnd, char *FileType, char *FilePath, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_EndNotify(void);
```

Tool DLL functions:

```c
int CALLBACK GetToolList(int cnt, struct TP_TOOLS *tpToolInfo);
int CALLBACK GetToolHideOption(char *FuncName);
int CALLBACK ToolFunctionName(HWND hWnd, struct TPITEM **ToolItemList, int ToolItemListCnt, int type, int CheckType);
```

The matching function pointer types on the WWWC.exe side are defined in
`WWWC/General.h` (`PROTOCOL_ITEM_FUNC` and others).

WWWC applies dark mode to plugin dialogs automatically (dialogs created on the
same thread as WWWC). Parts whose colors the plugin sets itself, such as rich
edit contents or owner-drawn controls, should check
`SendMessage(<WWWC main window>, WM_GETDARKMODE, 0, 0)`: it returns TRUE while
dark mode is on (an older WWWC returns 0).

## Third-party software

- [win32-darkmodelib](https://github.com/ozone10/win32-darkmodelib)
  (`third_party/darkmodelib`, git submodule): used for dark mode. MPL-2.0
  (some files MIT). The license texts are `LICENSE.md`, `LICENSE-MIT.md` and
  `docs/LICENSE-*.md` in the submodule; the release zips include them.

## Notes

- Source files (`.c` `.h` `.rc`) are **UTF-8 without BOM** with CRLF line
  endings, compiled with `/source-charset:utf-8 /execution-charset:utf-8`.
- The files in `docs/` are UTF-8 as well (the samples are used as
  configuration files as they are).
- For the license terms see `docs/readme.txt` (WWWC is freeware and may be
  redistributed freely; the MD5 code is under the RSA Data Security, Inc.
  license).
