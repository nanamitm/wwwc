#ifndef WWWC_DATA_DIRECTORY_H
#define WWWC_DATA_DIRECTORY_H

/* Shared by the executable and the protocol DLL. The directory containing
   WWWC.exe is the portable data directory; the process working directory is
   intentionally unrelated (shortcuts may specify a different working folder). */
static BOOL WwwcDataFileExists(const char *dir, const char *name)
{
	char path[1024];
	DWORD attr;
	if(lstrlenA(dir) + lstrlenA(name) + 2 > sizeof(path)) return FALSE;
	wsprintfA(path, "%s\\%s", dir, name);
	attr = GetFileAttributesA(path);
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL WwwcHasLocalData(const char *dir)
{
	char path[1024], child[1024], user[1024];
	DWORD size = sizeof(user);
	WIN32_FIND_DATAA entry;
	HANDLE search;
	if(WwwcDataFileExists(dir, "general.ini") ||
		WwwcDataFileExists(dir, "wwwc.ini") || WwwcDataFileExists(dir, "filter.txt")) return TRUE;
	/* A user folder alone is sufficient for old portable installations. */
	if(GetUserNameA(user, &size) && lstrlenA(dir) + lstrlenA(user) + 2 <= sizeof(path)){
		DWORD attr;
		wsprintfA(path, "%s\\%s", dir, user);
		attr = GetFileAttributesA(path);
		if(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return TRUE;
	}
	/* Shared or /u: users may have a name different from the Windows account. */
	if(lstrlenA(dir) + 3 > sizeof(path)) return FALSE;
	wsprintfA(path, "%s\\*", dir);
	search = FindFirstFileA(path, &entry);
	if(search == INVALID_HANDLE_VALUE) return FALSE;
	do{
		int length = lstrlenA(entry.cFileName);
		if(!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			entry.cFileName[0] == '.' || lstrcmpiA(entry.cFileName, "DefItem") == 0) continue;
		if((length >= 12 && lstrcmpiA(entry.cFileName + length - 12, "_sjis_backup") == 0) ||
			(length >= 8 && lstrcmpiA(entry.cFileName + length - 8, ".archive") == 0)) continue;
		if(lstrlenA(dir) + lstrlenA(entry.cFileName) + 2 > sizeof(child)) continue;
		wsprintfA(child, "%s\\%s", dir, entry.cFileName);
		if(WwwcDataFileExists(child, "user.ini") || WwwcDataFileExists(child, "Folder.ini") ||
			WwwcDataFileExists(child, "Item.dat")){
			FindClose(search);
			return TRUE;
		}
	}while(FindNextFileA(search, &entry));
	FindClose(search);
	return FALSE;
}

/* Resolve the default without creating files. Existing portable data wins even
   when the installer has written portable=0. portable=1 remains portable. */
static BOOL WwwcDefaultDataDirectory(const char *exeDir, char *result, DWORD capacity)
{
	char ini[1024], local[1024];
	DWORD length;
	if(lstrlenA(exeDir) + lstrlenA("\\wwwc_app.ini") + 1 > sizeof(ini) ||
		(DWORD)lstrlenA(exeDir) >= capacity) return FALSE;
	lstrcpyA(result, exeDir);
	wsprintfA(ini, "%s\\wwwc_app.ini", exeDir);
	if(WwwcHasLocalData(exeDir) || GetPrivateProfileIntA("GENERAL", "portable", 1, ini) != 0) return TRUE;
	length = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local));
	if(length == 0 || length >= sizeof(local)) return FALSE;
	while(length && (local[length - 1] == '\\' || local[length - 1] == '/')) local[--length] = '\0';
	if(length + sizeof("\\WWWC") > capacity) return FALSE;
	wsprintfA(result, "%s\\WWWC", local);
	return TRUE;
}

#endif
