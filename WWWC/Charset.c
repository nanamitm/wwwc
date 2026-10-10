/**************************************************************************

	WWWC

	Charset.c

	文字コード (UTF-8) への対応
	WWWC はマニフェストで ANSI の文字コードを UTF-8 にしている (activeCodePage)。
	以前のバージョン (Shift_JIS) で保存されたデータファイルを UTF-8 に変換する。

**************************************************************************/


/**************************************************************************
	Include Files
**************************************************************************/

#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE

#include "General.h"
#include "Profile.h"
#include "Charset.h"


/**************************************************************************
	Define
**************************************************************************/

//変換済みを示すキー (general.ini / user.ini の [GENERAL])
#define CHARSET_KEY					"CharSetUTF8"
//変換前のファイルを保存するフォルダの名前に付ける文字列
#define BACKUP_SUFFIX				"_sjis_backup"

static const char *DataFileNames[] = { DATAFILENAME, FOLDERFILENAME, USER_INI, NULL };
static const char *AppFileNames[] = { GENERAL_INI, "wwwc.ini", "filter.txt", NULL };


/******************************************************************************

	Charset_Check

	プロセスの文字コードが UTF-8 になっているか確認する
	(activeCodePage は Windows 10 1903 以降で有効)

******************************************************************************/

BOOL Charset_Check(void)
{
	if(GetACP() == CP_UTF8){
		return TRUE;
	}
	MessageBoxW(NULL,
		L"WWWC を起動できません。\n\nこのバージョンの WWWC は Windows 10 バージョン 1903 以降が必要です。\n"
		L"(This version of WWWC requires Windows 10 version 1903 or later.)",
		L"WWWC", MB_OK | MB_ICONERROR);
	return FALSE;
}


/******************************************************************************

	IsUtf8Text

	UTF-8 として正しい文字列か (ASCII だけの場合も TRUE)

******************************************************************************/

static BOOL IsUtf8Text(const unsigned char *s, DWORD len)
{
	DWORD i = 0;
	int n, j;

	while(i < len){
		if(s[i] < 0x80){
			i++;
			continue;
		}
		if(s[i] >= 0xC2 && s[i] <= 0xDF){
			n = 1;
		}else if(s[i] >= 0xE0 && s[i] <= 0xEF){
			n = 2;
		}else if(s[i] >= 0xF0 && s[i] <= 0xF4){
			n = 3;
		}else{
			return FALSE;
		}
		if(i + n >= len){
			return FALSE;
		}
		for(j = 1; j <= n; j++){
			if((s[i + j] & 0xC0) != 0x80){
				return FALSE;
			}
		}
		i += n + 1;
	}
	return TRUE;
}


/******************************************************************************

	ConvertFile

	Shift_JIS のファイルを UTF-8 に変換する
	変換前の内容は BackupPath に保存する。ASCII だけのファイルと、既に UTF-8 の
	ファイルはそのまま。

******************************************************************************/

static void ConvertFile(const char *Path, const char *BackupPath)
{
	HANDLE hFile;
	DWORD size, ret;
	unsigned char *buf;
	WCHAR *wbuf;
	char *cbuf;
	char dir[BUFSIZE], *p;
	int wlen, clen;

	hFile = CreateFile(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE){
		return;
	}
	size = GetFileSize(hFile, NULL);
	if(size == INVALID_FILE_SIZE || size == 0 || (buf = (unsigned char *)GlobalAlloc(GMEM_FIXED, size)) == NULL){
		CloseHandle(hFile);
		return;
	}
	if(ReadFile(hFile, buf, size, &ret, NULL) == FALSE || ret != size){
		CloseHandle(hFile);
		GlobalFree(buf);
		return;
	}
	CloseHandle(hFile);

	if(IsUtf8Text(buf, size) == TRUE){
		GlobalFree(buf);
		return;
	}

	wlen = MultiByteToWideChar(932, 0, (char *)buf, size, NULL, 0);
	wbuf = (wlen > 0) ? (WCHAR *)GlobalAlloc(GMEM_FIXED, sizeof(WCHAR) * wlen) : NULL;
	if(wbuf == NULL){
		GlobalFree(buf);
		return;
	}
	MultiByteToWideChar(932, 0, (char *)buf, size, wbuf, wlen);
	clen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL, NULL);
	cbuf = (clen > 0) ? (char *)GlobalAlloc(GMEM_FIXED, clen) : NULL;
	if(cbuf == NULL){
		GlobalFree(wbuf);
		GlobalFree(buf);
		return;
	}
	WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, cbuf, clen, NULL, NULL);
	GlobalFree(wbuf);

	//変換前の内容を保存してから書き換える
	lstrcpyn(dir, BackupPath, BUFSIZE);
	for(p = dir + lstrlen(dir); p > dir && *p != '\\'; p--);
	if(p > dir){
		*p = '\0';
		mkDirStr(dir);
	}
	if(CopyFile(Path, BackupPath, FALSE) == TRUE){
		hFile = CreateFile(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile != INVALID_HANDLE_VALUE){
			WriteFile(hFile, cbuf, clen, &ret, NULL);
			CloseHandle(hFile);
		}
	}
	GlobalFree(cbuf);
	GlobalFree(buf);
}


/******************************************************************************

	ConvertTree

	フォルダ以下のデータファイルを変換する

******************************************************************************/

static void ConvertTree(const char *Dir, const char *BackupDir)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFind;
	char path[BUFSIZE], backup[BUFSIZE];
	int i;

	for(i = 0; DataFileNames[i] != NULL; i++){
		if(lstrlen(Dir) + lstrlen(DataFileNames[i]) + 2 >= BUFSIZE ||
			lstrlen(BackupDir) + lstrlen(DataFileNames[i]) + 2 >= BUFSIZE){
			continue;
		}
		wsprintf(path, "%s\\%s", Dir, DataFileNames[i]);
		wsprintf(backup, "%s\\%s", BackupDir, DataFileNames[i]);
		ConvertFile(path, backup);
	}

	if(lstrlen(Dir) + 3 >= BUFSIZE){
		return;
	}
	wsprintf(path, "%s\\*", Dir);
	if((hFind = FindFirstFile(path, &FindData)) == INVALID_HANDLE_VALUE){
		return;
	}
	do{
		if((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
			lstrcmp(FindData.cFileName, ".") == 0 || lstrcmp(FindData.cFileName, "..") == 0){
			continue;
		}
		if(lstrlen(Dir) + lstrlen(FindData.cFileName) + 2 >= BUFSIZE ||
			lstrlen(BackupDir) + lstrlen(FindData.cFileName) + 2 >= BUFSIZE){
			continue;
		}
		wsprintf(path, "%s\\%s", Dir, FindData.cFileName);
		wsprintf(backup, "%s\\%s", BackupDir, FindData.cFileName);
		ConvertTree(path, backup);
	}while(FindNextFile(hFind, &FindData) == TRUE);
	FindClose(hFind);
}


/******************************************************************************

	IsConverted / SetConverted

	変換済みかどうか (ini ファイルの [GENERAL] CharSetUTF8=1)

******************************************************************************/

static BOOL IsConverted(const char *IniFile)
{
	int ret;

	profile_initialize(IniFile, TRUE);
	ret = profile_get_int("GENERAL", CHARSET_KEY, 0, IniFile);
	profile_free();
	return (ret == 1) ? TRUE : FALSE;
}

static void SetConverted(const char *IniFile)
{
	profile_initialize(IniFile, TRUE);
	profile_write_int("GENERAL", CHARSET_KEY, 1, IniFile);
	profile_flush(IniFile);
	profile_free();
}


/******************************************************************************

	Charset_ConvertAppDir

	WWWC.exe のフォルダの設定ファイル (general.ini, wwwc.ini, filter.txt) と
	新規ユーザ用のアイテム (DefItem) を変換する
	プロトコル DLL (wwwc.dll) を読み込む前に呼ぶ

******************************************************************************/

void Charset_ConvertAppDir(const char *AppDir)
{
	char ini[BUFSIZE], path[BUFSIZE], backup[BUFSIZE], backupDir[BUFSIZE];
	int i;

	if(lstrlen(AppDir) + 32 >= BUFSIZE){
		return;
	}
	wsprintf(ini, "%s\\%s", AppDir, GENERAL_INI);
	if(IsConverted(ini) == TRUE){
		return;
	}
	wsprintf(backupDir, "%s\\settings%s", AppDir, BACKUP_SUFFIX);
	for(i = 0; AppFileNames[i] != NULL; i++){
		wsprintf(path, "%s\\%s", AppDir, AppFileNames[i]);
		wsprintf(backup, "%s\\%s", backupDir, AppFileNames[i]);
		ConvertFile(path, backup);
	}
	wsprintf(path, "%s\\DefItem", AppDir);
	wsprintf(backup, "%s\\DefItem%s", AppDir, BACKUP_SUFFIX);
	ConvertTree(path, backup);
	SetConverted(ini);
}


/******************************************************************************

	Charset_ConvertDataDir

	ユーザのフォルダ (user.ini, Item.dat, Folder.ini) を変換する
	変換前のファイルは「フォルダ名_sjis_backup」に保存する

******************************************************************************/

void Charset_ConvertDataDir(const char *Dir)
{
	char ini[BUFSIZE], backupDir[BUFSIZE];

	if(lstrlen(Dir) + 32 >= BUFSIZE){
		return;
	}
	wsprintf(ini, "%s\\%s", Dir, USER_INI);
	if(IsConverted(ini) == TRUE){
		return;
	}
	wsprintf(backupDir, "%s%s", Dir, BACKUP_SUFFIX);
	ConvertTree(Dir, backupDir);
	SetConverted(ini);
}
/* End of source */
