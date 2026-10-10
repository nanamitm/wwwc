# Changelog

Changes from the original WWWC 1.1.3 by Ohno Tomoaki. The Japanese summary for
users is in `docs/readme.txt`.

## 1.5.1

### High DPI

- The toolbar images (16x16 pixel art) are enlarged with Scale2x / Scale3x
  and then reduced to the size of the DPI, instead of being stretched, so
  the edges stay sharp.
- Tray icons use the small icon size of the system DPI, and the check icon
  in the status bar follows the window DPI.
- The icon lists in the automatic check, protocol, tool and icon selection
  settings follow the DPI of the dialog.
- Image lists are 32-bit instead of 16-bit colors.

### Fixes

- Japanese text was garbled in the lists of the find window, the update
  message window and the settings (automatic check, protocol, tool), and in
  the main tree and list with a font chosen in the settings (since 1.3.0).
  List views and tree views convert ANSI text with the code page of the
  character set of their font (Shift_JIS for the Japanese dialog font), not
  with the process code page (UTF-8). Their fonts now use
  `DEFAULT_CHARSET`. The tests read list texts as UTF-16, as they are shown.
- After the second DPI change (for example moving the window between
  monitors twice) the standard toolbar buttons (cut, copy, paste, views)
  showed wrong or no images.

## 1.5.0

### New

- Per-Monitor DPI awareness (V2) instead of GDI scaling: text, icons,
  toolbar, columns and the tree width follow the scale of the monitor, also
  when the window moves to a monitor with another scale. Sizes in user.ini
  stay values at 96 DPI (100%), so the settings are the same on every
  monitor.

### Fixes

- Dark mode: the button area at the bottom of message boxes (for example
  the version information) was light.

### Tests

- New `Features` suite: FILE protocol, delete and undo, find, update message
  window, dark mode, scheduled check and a simulated DPI change.

## 1.4.0

### New

- HTTP settings: "Use the Windows proxy settings" (WinHTTP automatic proxy:
  the Windows settings including automatic detection and configuration
  scripts). The proxy is now chosen from three options: none, Windows
  settings, or a given server (`[PROXY] Proxy` = 0 / 2 / 1). With the
  Windows settings the proxy credentials are passed to WinHTTP, so they are
  sent only to a proxy, never to a site reached directly.

### Fixes

- The proxy set in the item properties ("use a different proxy") was not
  used: the request connected directly.
- Identifiers, URLs, titles, file names and ini keys are compared ordinally
  instead of by the user's locale (which ignores some characters and, under
  a Turkish locale, does not match "I" and "i").

### Removed

- The unused Winsock code in wwwc.dll (host name cache, `HostNoCache`).
  WWWC.exe still initializes Winsock and forwards `WM_WSOCK_GETHOST` /
  `WM_WSOCK_SELECT` for protocol plugins that use them.

## 1.3.1

### Fixes

- Every check of an item with a password overflowed a stack buffer: reading
  the option after the encrypted password (about 310 characters) copied it
  into a 256-byte buffer. Found by the new AddressSanitizer tests.
- Long Japanese folder and item names: with UTF-8 a 256-byte buffer holds
  only about 85 Japanese characters, so paths of nested folders hit the
  limit and failed, and long names could crash WWWC. Buffers are now 1024
  bytes (a MAX_PATH path in UTF-8); the list views no longer write more than
  the control's buffer size, and a cut name does not end in half a
  character.

### Development

- AddressSanitizer build and CI job running all tests on it.
- `WM_WWWC_GETINIPATH` takes the buffer size in `wParam`.

## 1.3.0

### New

- Unicode: WWWC handles all text as UTF-8 (the manifest sets the process
  code page to UTF-8), so page titles and other text with emoji, Chinese,
  Korean and other characters outside Shift_JIS are shown and saved correctly.
  Pages in UTF-8, Shift_JIS, EUC-JP and ISO-2022-JP are all converted to
  UTF-8.
- Clipboard and drag & drop exchange text and file names with other
  applications as Unicode (CF_UNICODETEXT, wide CF_HDROP).

### Changed

- Requires Windows 10 version 1903 or later. WWWC shows a message and exits
  on older systems.
- Data and settings files (Item.dat, Folder.ini, user.ini, general.ini,
  wwwc.ini, filter.txt) are stored in UTF-8. Files from older versions
  (Shift_JIS) are converted on the first start; the original files are kept
  in `<user>_sjis_backup` and `settings_sjis_backup`. Older versions cannot
  read the converted files correctly.
- Strings passed to plugins are UTF-8 (see "Writing plugins" in README.md).
- Passwords are encrypted as UTF-8 (`DP2:`). Passwords saved by older
  versions (the old format and 1.2.0's `DP1:`) are still read. Non-ASCII
  passwords are now sent to servers in UTF-8.
- The sample files and the manual in `docs/` are UTF-8.

### Removed

- DDE browser integration. No current browser provides the DDE services,
  and "open URL" waited about 8 seconds before opening the URL. It now opens
  the URL in the configured or default browser at once. "Get information
  from the browser" (tools and the button in the item properties) and the
  DDE settings are removed; existing tool settings show a message.

### Fixes

- A filter on a page that becomes longer when converted could overflow the
  receive buffer.

## 1.2.0

### New

- 64-bit (x64) build in addition to x86. The 64-bit WWWC.exe can load only
  64-bit plugin DLLs; see "Writing plugins" in README.md.
- Dark mode (win32-darkmodelib). Options > General > Theme: Light / Dark /
  Follow Windows (default). Requires Windows 10 1809 or later.
- Sharper text on high-DPI displays (GDI scaling, Windows 10 1703 or later).

### Security

- HTTPS server certificates are verified. A self-signed or otherwise invalid
  certificate can be allowed per item (item properties > Options > "Ignore
  certificate errors"). 1.1.3 always ignored certificate errors.
- HTTP passwords (item authentication and proxy authentication) are stored
  encrypted with DPAPI (machine scope). Passwords saved by 1.1.3 are
  re-encrypted when they are read. Encrypted passwords cannot be decrypted on
  another PC, and WWWC 1.1.3 cannot read them.
- Hardened the handling of server responses in wwwc.dll (dates, URLs with
  user and password, character set conversion, chunked data, titles and the
  source view).
- Hardened the loading of Item.dat, user.ini and filter.txt, dropped and
  pasted text, and long paths (several buffer overflows fixed).

### Fixes

- Item.dat whose last line has no line break: the last item was lost (and
  memory was overwritten). Files with CR-only line breaks now load.
- Checks are no longer stopped with TerminateThread; cancel and timeout close
  the connection and the thread ends by itself. Fixes a thread handle leak on
  every check and a race between the check thread and the item list.
- Pages in ISO-2022-JP (JIS) were not converted, so their titles were garbled.
- The Find dialog crashed on items with empty fields.
- The drop target read an uninitialized STGMEDIUM.
- Redirects keep their target URL until the next request.

### Development

- Sources converted from Shift_JIS to UTF-8 (string literals are still
  compiled as CP932).
- Visual Studio 2026 (v145) project settings; warnings that indicate pointer
  truncation are errors.
- Automated tests (`tests/`) and GitHub Actions build and test.
- Plugin function pointers are typed (`PROTOCOL_ITEM_FUNC` and others in
  `WWWC/General.h`); `struct TPITEM` uses pointer-sized members (`LONG_PTR`,
  `UINT_PTR`), which keeps the x86 layout unchanged.
