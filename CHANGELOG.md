# Changelog

Changes from the original WWWC 1.1.3 by Ohno Tomoaki. The Japanese summary for
users is in `docs/readme.txt`.

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
