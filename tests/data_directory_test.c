#include <windows.h>
#include <stdio.h>
#include "../shared/DataDirectory.h"

static int failures, cases;
static void expect(BOOL value, const char *name)
{
	cases++;
	if(!value){ failures++; printf("FAIL %s\n", name); }
}

int main(void)
{
	char temp[MAX_PATH], root[1024], exe[1024], appdata[1024], want[1024], got[1024];
	char path[1024], user[1024];
	DWORD count = sizeof(user);
	const char *files[] = {"general.ini", "wwwc.ini", "filter.txt"};
	int i;
	GetTempPathA(sizeof(temp), temp);
	wsprintfA(root, "%swwwc-directory-%lu", temp, GetCurrentProcessId());
	CreateDirectoryA(root, NULL);
	wsprintfA(exe, "%s\\bin", root); CreateDirectoryA(exe, NULL);
	wsprintfA(appdata, "%s\\local", root); CreateDirectoryA(appdata, NULL);
	SetEnvironmentVariableA("LOCALAPPDATA", appdata);
	wsprintfA(want, "%s\\WWWC", appdata);
	expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(exe, got), "zip without marker stays portable");
	wsprintfA(path, "%s\\wwwc_app.ini", exe);
	WritePrivateProfileStringA("GENERAL", "portable", "0", path);
	expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(want, got), "new install falls back to LocalAppData");
	for(i=0; i<3; i++){
		HANDLE file;
		wsprintfA(path, "%s\\%s", exe, files[i]);
		file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		CloseHandle(file);
		expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(exe, got), files[i]);
		DeleteFileA(path);
	}
	GetUserNameA(user, &count);
	wsprintfA(path, "%s\\%s", exe, user); CreateDirectoryA(path, NULL);
	expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(exe, got), "old username folder without INI wins");
	RemoveDirectoryA(path);
	wsprintfA(path, "%s\\shared-user", exe); CreateDirectoryA(path, NULL);
	{
		char ini[1024]; HANDLE file;
		wsprintfA(ini, "%s\\Folder.ini", path);
		file = CreateFileA(ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); CloseHandle(file);
		expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(exe, got), "shared username with Folder.ini wins");
		DeleteFileA(ini);
	}
	RemoveDirectoryA(path);
	wsprintfA(path, "%s\\DefItem", exe); CreateDirectoryA(path, NULL);
	{
		char ini[1024]; HANDLE file;
		wsprintfA(ini, "%s\\Folder.ini", path);
		file = CreateFileA(ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); CloseHandle(file);
		expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(want, got), "default item template is not existing user data");
		DeleteFileA(ini);
	}
	RemoveDirectoryA(path);
	SetCurrentDirectoryA(appdata);
	expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(want, got), "shortcut working directory is irrelevant");
	SetEnvironmentVariableA("LOCALAPPDATA", NULL);
	expect(!WwwcDefaultDataDirectory(exe, got, sizeof(got)), "missing LocalAppData fails explicitly");
	wsprintfA(path, "%s\\wwwc_app.ini", exe);
	WritePrivateProfileStringA("GENERAL", "portable", "1", path);
	expect(WwwcDefaultDataDirectory(exe, got, sizeof(got)) && !lstrcmpA(exe, got), "explicit portable works without LocalAppData");
	expect(!WwwcDefaultDataDirectory(exe, got, 4), "short output buffer rejected");
	DeleteFileA(path);
	SetCurrentDirectoryA(temp); RemoveDirectoryA(appdata); RemoveDirectoryA(exe); RemoveDirectoryA(root);
	printf("%d passed, %d failed\n", cases-failures, failures);
	return failures ? 1 : 0;
}
