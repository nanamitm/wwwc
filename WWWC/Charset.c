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
#include <limits.h>
#include <ctype.h>


/**************************************************************************
	Define
**************************************************************************/

//変換済みを示すキー (general.ini / user.ini の [GENERAL])
#define CHARSET_KEY					"CharSetUTF8"
//変換前のファイルを保存するフォルダの名前に付ける文字列
#define BACKUP_SUFFIX				"_sjis_backup"
//変換中の一時ファイルの名前の先頭 (3文字)
#define TEMP_PREFIX					"wcu"
//これより古い一時ファイルは中断した変換の残りとみなす (10分, FILETIME の単位)
#define STALE_TEMP_AGE				(10ULL * 60 * 10000000)

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
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)s, (int)len, NULL, 0) > 0;
}

//元ファイルと同じディレクトリに一時ファイルを作る (最後に置き換える)
static BOOL CreateConversionTemp(const char *Path, char *TempPath)
{
	char dir[BUFSIZE], *p;

	if(lstrlen(Path) >= BUFSIZE){
		return FALSE;
	}
	lstrcpy(dir, Path);
	p = strrchr(dir, '\\');
	if(p == NULL){
		lstrcpy(dir, ".");
	}else{
		*(p + 1) = '\0';
	}
	return GetTempFileName(dir, TEMP_PREFIX, 0, TempPath) != 0;
}

//一時ファイルで元ファイルを置き換える
//既存のファイルは ReplaceFile で属性・アクセス権・作成日時を引き継ぐ
static BOOL ReplaceWithTemp(const char *TempPath, const char *Path)
{
	if(GetFileAttributes(Path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND){
		return MoveFileEx(TempPath, Path, MOVEFILE_WRITE_THROUGH);
	}
	return ReplaceFile(Path, TempPath, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, NULL, NULL);
}

//GetTempFileName が作る名前 (TEMP_PREFIX + 16進数4桁まで + ".tmp") か
static BOOL IsConversionTempName(const char *Name)
{
	int len = lstrlen(Name), i;

	if(len < 9 || len > 11 || _strnicmp(Name, TEMP_PREFIX, 3) != 0 || lstrcmpi(Name + len - 4, ".tmp") != 0){
		return FALSE;
	}
	for(i = 3; i < len - 4; i++){
		if(isxdigit((unsigned char)Name[i]) == 0){
			return FALSE;
		}
	}
	return TRUE;
}

//中断した変換で残った一時ファイルを削除する
//(同じフォルダを変換中の別のプロセスの一時ファイルは消さないよう、古いものだけ)
static void DeleteStaleTemps(const char *Dir)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFind;
	FILETIME ft;
	ULARGE_INTEGER now, t;
	char path[BUFSIZE];

	if(lstrlen(Dir) + 16 >= BUFSIZE){
		return;
	}
	wsprintf(path, "%s\\%s*.tmp", Dir, TEMP_PREFIX);
	if((hFind = FindFirstFile(path, &FindData)) == INVALID_HANDLE_VALUE){
		return;
	}
	GetSystemTimeAsFileTime(&ft);
	now.LowPart = ft.dwLowDateTime;
	now.HighPart = ft.dwHighDateTime;
	do{
		if((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
			IsConversionTempName(FindData.cFileName) == FALSE ||
			lstrlen(Dir) + lstrlen(FindData.cFileName) + 2 >= BUFSIZE){
			continue;
		}
		t.LowPart = FindData.ftLastWriteTime.dwLowDateTime;
		t.HighPart = FindData.ftLastWriteTime.dwHighDateTime;
		if(now.QuadPart < t.QuadPart + STALE_TEMP_AGE){
			continue;
		}
		wsprintf(path, "%s\\%s", Dir, FindData.cFileName);
		DeleteFile(path);
	}while(FindNextFile(hFind, &FindData) == TRUE);
	FindClose(hFind);
}

/******************************************************************************

	ConvertFile

	Shift_JIS のファイルを UTF-8 に変換する
	変換前の内容は BackupPath に保存する。ASCII だけのファイルと、既に UTF-8 の
	ファイルはそのまま。

******************************************************************************/

static BOOL ConvertFile(const char *Path, const char *BackupPath)
{
	HANDLE hFile;
	DWORD size, high, ret, err;
	unsigned char *buf;
	WCHAR *wbuf;
	char *cbuf;
	char dir[BUFSIZE], temp[BUFSIZE], *p;
	int wlen, clen;
	BOOL ok = FALSE;

	hFile = CreateFile(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE){
		err = GetLastError();
		return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
	}
	size = GetFileSize(hFile, &high);
	if(size == 0 && high == 0){
		CloseHandle(hFile);
		return TRUE;
	}
	if(high != 0 || size == INVALID_FILE_SIZE || size > INT_MAX || (buf = (unsigned char *)GlobalAlloc(GMEM_FIXED, size)) == NULL){
		CloseHandle(hFile);
		return FALSE;
	}
	if(ReadFile(hFile, buf, size, &ret, NULL) == FALSE || ret != size){
		CloseHandle(hFile);
		GlobalFree(buf);
		return FALSE;
	}
	CloseHandle(hFile);
	if(IsUtf8Text(buf, size) == TRUE){
		GlobalFree(buf);
		return TRUE;
	}

	wlen = MultiByteToWideChar(932, 0, (char *)buf, size, NULL, 0);
	wbuf = (wlen > 0) ? (WCHAR *)GlobalAlloc(GMEM_FIXED, sizeof(WCHAR) * wlen) : NULL;
	if(wbuf == NULL){
		GlobalFree(buf);
		return FALSE;
	}
	MultiByteToWideChar(932, 0, (char *)buf, size, wbuf, wlen);
	clen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL, NULL);
	cbuf = (clen > 0) ? (char *)GlobalAlloc(GMEM_FIXED, clen) : NULL;
	if(cbuf == NULL){
		GlobalFree(wbuf);
		GlobalFree(buf);
		return FALSE;
	}
	WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, cbuf, clen, NULL, NULL);
	GlobalFree(wbuf);

	//再試行時も最初のバックアップを残す
	lstrcpyn(dir, BackupPath, BUFSIZE);
	for(p = dir + lstrlen(dir); p > dir && *p != '\\'; p--);
	if(p > dir){
		*p = '\0';
		mkDirStr(dir);
	}
	if(CopyFile(Path, BackupPath, TRUE) == TRUE || GetLastError() == ERROR_FILE_EXISTS){
		if(CreateConversionTemp(Path, temp) == TRUE){
			hFile = CreateFile(temp, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(hFile != INVALID_HANDLE_VALUE){
				ok = WriteFile(hFile, cbuf, clen, &ret, NULL) && ret == (DWORD)clen && FlushFileBuffers(hFile);
				CloseHandle(hFile);
				if(ok == TRUE){
					ok = ReplaceWithTemp(temp, Path);
				}
			}
			if(ok == FALSE) DeleteFile(temp);
		}
	}
	GlobalFree(cbuf);
	GlobalFree(buf);
	return ok;
}

/******************************************************************************

	ConvertTree

	フォルダ以下のデータファイルを変換する

******************************************************************************/

static BOOL ConvertTree(const char *Dir, const char *BackupDir)
{
	WIN32_FIND_DATA FindData;
	HANDLE hFind;
	char path[BUFSIZE], backup[BUFSIZE];
	int i;
	BOOL ok = TRUE;
	DWORD err;

	DeleteStaleTemps(Dir);
	for(i = 0; DataFileNames[i] != NULL; i++){
		if(lstrlen(Dir) + lstrlen(DataFileNames[i]) + 2 >= BUFSIZE ||
			lstrlen(BackupDir) + lstrlen(DataFileNames[i]) + 2 >= BUFSIZE){
			return FALSE;
		}
		wsprintf(path, "%s\\%s", Dir, DataFileNames[i]);
		wsprintf(backup, "%s\\%s", BackupDir, DataFileNames[i]);
		if(ConvertFile(path, backup) == FALSE) ok = FALSE;
	}

	if(lstrlen(Dir) + 3 >= BUFSIZE){
		return FALSE;
	}
	wsprintf(path, "%s\\*", Dir);
	if((hFind = FindFirstFile(path, &FindData)) == INVALID_HANDLE_VALUE){
		err = GetLastError();
		return ok && (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
	}
	do{
		if((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
			strcmp(FindData.cFileName, ".") == 0 || strcmp(FindData.cFileName, "..") == 0){
			continue;
		}
		if(lstrlen(Dir) + lstrlen(FindData.cFileName) + 2 >= BUFSIZE ||
			lstrlen(BackupDir) + lstrlen(FindData.cFileName) + 2 >= BUFSIZE){
			ok = FALSE;
			continue;
		}
		wsprintf(path, "%s\\%s", Dir, FindData.cFileName);
		wsprintf(backup, "%s\\%s", BackupDir, FindData.cFileName);
		if(ConvertTree(path, backup) == FALSE) ok = FALSE;
	}while(FindNextFile(hFind, &FindData) == TRUE);
	if(GetLastError() != ERROR_NO_MORE_FILES) ok = FALSE;
	FindClose(hFind);
	return ok;
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

static BOOL SetConverted(const char *IniFile)
{
	char temp[BUFSIZE];
	BOOL ok;

	if(profile_initialize(IniFile, TRUE) == FALSE && GetFileAttributes(IniFile) != INVALID_FILE_ATTRIBUTES){
		profile_free();
		return FALSE;
	}
	ok = profile_write_int("GENERAL", CHARSET_KEY, 1, IniFile);
	if(ok == TRUE && CreateConversionTemp(IniFile, temp) == TRUE){
		ok = profile_flush(temp);
		if(ok == TRUE) ok = ReplaceWithTemp(temp, IniFile);
		if(ok == FALSE) DeleteFile(temp);
	}else{
		ok = FALSE;
	}
	profile_free();
	return ok;
}

/******************************************************************************

	Charset_ConvertAppDir

	WWWC.exe のフォルダの設定ファイル (general.ini, wwwc.ini, filter.txt) と
	新規ユーザ用のアイテム (DefItem) を変換する
	プロトコル DLL (wwwc.dll) を読み込む前に呼ぶ

******************************************************************************/

BOOL Charset_ConvertAppDir(const char *AppDir)
{
	char ini[BUFSIZE], path[BUFSIZE], backup[BUFSIZE], backupDir[BUFSIZE];
	int i;

	if(lstrlen(AppDir) + 32 >= BUFSIZE){
		return FALSE;
	}
	wsprintf(ini, "%s\\%s", AppDir, GENERAL_INI);
	if(IsConverted(ini) == TRUE){
		return TRUE;
	}
	DeleteStaleTemps(AppDir);
	wsprintf(backupDir, "%s\\settings%s", AppDir, BACKUP_SUFFIX);
	for(i = 0; AppFileNames[i] != NULL; i++){
		wsprintf(path, "%s\\%s", AppDir, AppFileNames[i]);
		wsprintf(backup, "%s\\%s", backupDir, AppFileNames[i]);
		if(ConvertFile(path, backup) == FALSE) return FALSE;
	}
	wsprintf(path, "%s\\DefItem", AppDir);
	wsprintf(backup, "%s\\DefItem%s", AppDir, BACKUP_SUFFIX);
	if(ConvertTree(path, backup) == FALSE) return FALSE;
	//変換は済んでいるので、記録できなくても (書き込めないフォルダなど) 起動は続ける
	//次回の起動で変換済みかを確かめ直す
	SetConverted(ini);
	return TRUE;
}


/******************************************************************************

	Charset_ConvertDataDir

	ユーザのフォルダ (user.ini, Item.dat, Folder.ini) を変換する
	変換前のファイルは「フォルダ名_sjis_backup」に保存する

******************************************************************************/

BOOL Charset_ConvertDataDir(const char *Dir)
{
	char ini[BUFSIZE], backupDir[BUFSIZE];

	if(lstrlen(Dir) + 32 >= BUFSIZE){
		return FALSE;
	}
	wsprintf(ini, "%s\\%s", Dir, USER_INI);
	if(IsConverted(ini) == TRUE){
		return TRUE;
	}
	wsprintf(backupDir, "%s%s", Dir, BACKUP_SUFFIX);
	if(ConvertTree(Dir, backupDir) == FALSE) return FALSE;
	//変換は済んでいるので、記録できなくても起動は続ける
	SetConverted(ini);
	return TRUE;
}
/* End of source */
