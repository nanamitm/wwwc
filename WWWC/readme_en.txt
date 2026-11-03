WWWC Ver 1.2.1
------------------------

* Introduction

WWWC is a web page update checker for Windows.
Register the URLs of the web pages you visit often, and WWWC notifies you
when the pages are updated.


Files
	- WWWC.exe		: WWWC
	- wwwc.dll		: Protocol DLL that checks HTTP items
	- wwwc.html		: Manual (Japanese)
	- wwwc_en.html		: Manual (English)
	- manual		: Images of the manuals
	- ini_sample.txt	: Description of the INI file (Japanese)
	- filter_sample.txt	: Description of the filter file (Japanese)
	- readme.txt		: Readme (Japanese)
	- readme_en.txt		: This file


* Installation

Run the installer (wwwc121.exe). The installer runs as administrator and
installs WWWC in the WWWC folder under Program Files by default.

The settings and the items are saved in %LOCALAPPDATA%\WWWC.
To uninstall WWWC, use "Programs and Features" in the Control Panel
("Apps" in Settings).


* Language

WWWC is displayed in Japanese if the display language of Windows is
Japanese, and in English otherwise.
To specify the language, set Language in the [GENERAL] section of user.ini
to ja (Japanese) or en (English). The change takes effect the next time
WWWC starts.


* Usage

1. Select "File" - "New" - "Add HTTP Item..." and enter the URL of the
   web page to check.
2. Select "Check" - "Check All Items" (Ctrl+F5) to check the items.
   Updated items are shown with the updated icon, and the update message
   is displayed.
3. Double-click an item to open the page in the browser.

You can set the auto check in "View" - "Options..." - "Auto Check".
For details, see wwwc_en.html. It is opened by "Help" - "Help" in the
menu.


* History

- Ver 1.2.0 -> Ver 1.2.1
	- The number of archive generations is now 1 to 999, and 0 keeps all.
	  (Archives are not saved when the free disk space is less than 5%.)
	- A new generation is not saved if the content of the page is the same
	  as the previous one.
	- The list of archive generations is now shown as a calendar by year.
	- Pages embedded in the page (iframes and frames) are now archived too.
	- Archives can now save the page as shown in a browser (Microsoft Edge etc.
	  run in the background), including the data loaded by JavaScript.
	  (Per-item setting, off by default)
	- Added "Archive Progress" to the "View" menu and the toolbar. It shows the page being
	  archived, the pages waiting to be archived and the history of results,
	  and archiving can be stopped from it.
	- Fixed a problem with invalid file names in the archive folder.
	- Fixed WWWC not responding until an archive was saved when the settings
	  were changed or WWWC was closed while the archive was being saved.
	  (On exit, the archive is completed after the window is closed.)

- Ver 1.1.3 -> Ver 1.2.0
	- Checked pages can be archived and viewed later.
	- Getting browser information now supports Edge, Chrome and Firefox.
	- Passwords are encrypted for each Windows user.
	  (Set PassProtect=0 in [HTTP] of wwwc.ini to share with other PCs.)
	- Improved the robustness of HTTP and external data processing.
	- Tools do not run batch files with unsafe characters in the arguments.
	- Added a confirmation before opening items of unregistered protocols.
	- Added English display and the English manual.
	- Added the installer.
	- Fixed security-related issues.


* Others

WWWC is free software. You may distribute it freely without permission.

--
Special thanks:
  Jun-ya Kato
  Masaru Tsuchiyama
  Shinobu
--

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
 */

------------------------

Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
	https://nakka.com/
	nakka@nakka.com
