# Unreleased

- Rebase development on nakkag/WWWC while retaining the old history on
  archive/pre-fork-main and preserving the existing tags.
- Integrate upstream archiving, browser UI Automation, English resources and installer.
- Merge upstream WWWC 1.2.1: archive the content shown in the browser with
  a progress window, Opera support and the Firefox tab fix in browser information,
  archive/update-check security fixes, and ReloadNotify for protocol DLLs.
  The new per-item browser-archive flag is stored in `Option2` field 15.
- The global proxy credentials are no longer sent to an item-specific proxy.
- Preserve the fork's x86/x64, UTF-8, dark mode, DPI and password compatibility.
- Search for existing data beside WWWC.exe before using LocalAppData, including
  installed portable=0 copies; manual copying is unnecessary when updating in place.
- Add default-startup integration tests and native directory-selection regression tests.
- A protocol whose DLL cannot be loaded no longer takes over every URL: its
  empty scheme matched all items, so checks and item actions failed.
- Pressing OK in the protocol settings during a check no longer swaps the
  protocol DLLs under the running check. If the protocols are unchanged they
  are kept; if they changed, the check is stopped first and the DLLs are
  notified and kept loaded until the new set is loaded.
- Scheduled checks at a set time (daily, weekly, monthly) check every minute
  since the previous timer tick, so a minute is neither skipped nor checked
  twice when the once-a-minute timer fires near a minute boundary or is
  delayed (up to 5 minutes; longer gaps such as sleep are still not caught up).
- Cutting an item while it is being checked and pasting it into another
  folder (or undoing a move) no longer leaves the freed item in the check
  list, where the check timer read freed memory; its check is cancelled.
- Undo no longer uses a folder that was deleted after the undo information
  was recorded (Shift+Delete, emptying the recycle bin); the find window
  searches from the root when its folder was deleted.
- Moving or deleting one of several items with the same title and URL
  removes that item and keeps the others, instead of the first one.
- A tool command line is no longer built in a 1024-byte buffer: item values
  such as a long comment (`%m`) or title (`%t`) overflowed the stack.
- Quotes in item values put into a tool command line (`%t`, `%m`, `%u` and
  the others) are replaced (`'`, or `%22` in URLs), so a title or URL can no
  longer close a quoted argument and add arguments or commands.
- The update message waits until its item properties, tools, open and drag
  and drop have finished before it refreshes or clears its list; a check
  that ended meanwhile freed the items they were using.
- Item.dat is written to a temporary file and then replaces the old file, so
  a failed or interrupted save no longer leaves an empty or partial Item.dat
  (the data of the folder was lost on the next start).
- An item whose state field is negative (for example `-1` in Item.dat or in
  pasted item text) no longer writes past the save buffer; it is saved as 0.
- Tabs and line breaks in item fields (for example a tab pasted into the
  title in the item properties) are saved as spaces instead of shifting the
  fields of the record.
- A folder that is being checked or shown in the properties can no longer
  be moved by dragging it in the tree (only copied); it stayed in the tree
  and was created again at the old place when it was saved.
- WWWC no longer closes when a Windows shutdown is cancelled by another
  application (WM_ENDSESSION with wParam FALSE).
- The folder path column (11) in the main list shows the folder of the item
  instead of crashing.
- A DPI change no longer leaks GDI objects: the update icons added for each
  protocol were not destroyed, and the bitmap of the check animation in the
  toolbar was not deleted when the toolbar was rebuilt.
- The icon selection returns the number of the icon in the file: when an
  icon could not be extracted, the icons after it were saved with the wrong
  number, and the previous (already destroyed) icon was added again.
- filter.txt: a filter on a last line without a line break is applied,
  lines separated by LF only no longer run into the next line, and files
  with CR-only line breaks are read (the filters were counted by LF).
- A backslash in a filter string followed by a character other than
  `\\`, `n` and `t` is kept (for example `C:\data`); it was removed with
  that character.
- The character set detection checks the upper bound of the second byte of
  EUC-JP (0xFE); a second byte 0xFF was taken as EUC-JP.
- Tests: Assert-Equal compares strings case-sensitively and arrays element by
  element, credentials are matched case-sensitively, the tests wait for the
  end of a check (Wait-CheckEnd) and for the copied URL on the clipboard
  instead of a fixed time, and a message box
  still open when WWWC is closed fails the test unless it is expected.
- The archive asks WWWC for the data folder with the size of its buffer
  (the path was limited to 255 bytes).
- Cancelling an archive no longer lets the download thread use the request
  handle after it was closed.
- The browser used for archiving inherits only its standard input and output
  handles.
- Every archived resource (such as SVG images) is marked as downloaded from
  the internet, not only frame pages.
- When JavaScript is not archived, only `data:image/` URLs are kept; other
  `data:` URLs (such as `data:text/html` links) are removed.
- The duplicate and queue size checks of an archive request are done in the
  same lock as adding it to the queue.
- The archive progress window compares long titles and URLs in full, so they
  no longer flicker.
- The uninstaller deletes the settings folder (%LOCALAPPDATA%\WWWC) with the
  user's own rights instead of the administrator's, so a link put into the
  folder while it is deleted cannot make it delete other files.
- When the uninstaller does not delete the files (a network folder or a folder
  that standard users can change), it says so and asks before removing WWWC
  from the list of apps, instead of reporting a complete uninstall.
- Updating to a different folder warns that the previous folder is not
  deleted.
- The installer treats a short write as a failure, and removes all the
  folders it created when the destination turns out to be unsafe.
- Getting pages from the browser checks only windows of known browsers; apps
  built on Electron (VS Code, Teams and others) are no longer searched, which
  was slow and turned on their accessibility support.
- Getting pages from the browser does not wait for a page in windows whose
  address bar is not a web page (such as a new tab), and no longer reads past
  the end of short address bar texts.
- The HTTP item properties keep a backslash in the comment; the escapes were
  decoded a second time, so `C:\new` became a line break.
- A `;;` or a trailing `;` typed in an option value of the item properties
  (meta values, proxy, user, referer, User-Agent) no longer shifts the later
  option fields (a referer ending in `;;1` turned on ignoring certificate
  errors).
- Long meta values are saved in full; the check options were cut at 1024
  characters and lost the MD5 setting.
- wwwc.dll no longer calls LoadLibrary or decrypts the proxy password in
  DllMain; RichEdit is loaded for the source view and the password when it is
  first used.
- The URL list frees the items it made when an allocation fails, and the FILE
  check and properties check their allocations.
- CI: a new push cancels an older run only for pull requests (runs for main
  and tags always finish); a failed test run uploads the failures, the test
  server's log and each test's data folder; the release notes take only the
  lines of the version's section (an empty section stops the release instead
  of taking in the headings around it).
- The installer also installs licenses\THIRD-PARTY-NOTICES.txt (where the
  source code of win32-darkmodelib is and which version is used), like the zip
  package. Both are written by tools\third-party-notices.ps1, which stops when
  the darkmodelib submodule is not a checkout of its own or has local changes.
- tools\make-package.ps1 requires PowerShell 7 (Windows PowerShell 5.1 made
  zip entries with backslashes) and reports a missing version clearly.
- Add regression tests for HTTP credentials and redirects: the proxy password
  goes on CONNECT for HTTPS (the test server can require proxy credentials and
  answers CONNECT), the item password is not sent to another server a
  redirect leads to, non-HTTP redirect targets are refused, 303/308 and
  scheme-relative redirects are followed, and a URL containing a quote is not
  passed to the configured browser.
- Copying or dragging an item whose drop file path is longer than 536 bytes
  (for example a long title) no longer overflows a stack buffer: the path
  buffers passed to the protocol DLL's CreateDropItem now have the BUFSIZE
  the DLLs write up to.
- Copy and cut close the clipboard even when the drop files cannot be created;
  the WWWC data and text are still put on the clipboard.
- The right-button drop menu only offers the operations the drag source
  allows, so an item being checked can no longer be moved (and duplicated)
  with "Move Here".

# Changelog

Changes from the original WWWC 1.1.3 by Ohno Tomoaki. The Japanese summary for
users is in `docs/readme.txt`.

## Unreleased

### Fixes

- Copying, moving, deleting or restoring an item whose combined text exceeds
  32 KiB no longer overflows the clipboard parser's stack buffer.
- Shift_JIS migration writes converted files through temporary files, keeps
  the first backup, and records completion only after all conversions succeed.
  If conversion fails, startup stops with a message so legacy data is not
  loaded or saved as UTF-8; starting again retries the conversion.
  Converted files keep their attributes, permissions and creation time, and
  temporary files left by an interrupted conversion are removed on the next
  start. If every file is already UTF-8 but the completion mark cannot be
  written (for example, in a read-only folder), WWWC still starts.
- HTTP/1.1 response bodies are no longer decoded as chunked data a second
  time. Received sizes use the actual bytes read and exclude the header's
  terminating NUL. Because the saved size changes, an HTTP item without a
  Content-Length header that compares the size or MD5 digest may be reported
  as updated once after upgrading.
- Decimal and hexadecimal HTML numeric character references decode to UTF-8,
  including supplementary characters such as emoji. Invalid references stay
  literal instead of corrupting or truncating a title.
- FILE protocol checks use the complete 64-bit file size, including files
  larger than 2 GiB or 4 GiB.

### Development

- Resource scripts use Windows SDK headers instead of requiring MFC headers.
- Regression tests cover long item records, failed migration and retry,
  HTTP/1.1 chunked responses, Unicode character references and sparse files
  larger than 4 GiB. The DPI test establishes a 96 DPI baseline independently
  of the desktop's initial scaling.

## 1.5.2

### Fixes

- Dark mode: dark toolbar images (black lines such as refresh and delete,
  the dark blue standard images such as cut and undo) were hard to see.
  Dark colors in the toolbar images are lightened (vivid colors such as red
  are kept), also at 96 DPI.
- The status bar font for another DPI is deleted at exit.

### Tests

- The runner writes the failures with their messages to
  `last-results.txt` in the test folder.
- The Parse tests wait until the items are listed.

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
