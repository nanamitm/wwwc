/**************************************************************************

	WWWC (wwwc.dll)

	archive.c

	Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
		https://www.nakka.com/
		nakka@nakka.com

**************************************************************************/


/**************************************************************************
	Include Files
**************************************************************************/

#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <winhttp.h>
#include <shlwapi.h>
#include <stdlib.h>

#include "String.h"
#include "http.h"
#include "StrTbl.h"
#include "wwwcdll.h"
#include "global.h"
#include "md5.h"
#include "archive.h"
#include "resource.h"


/**************************************************************************
	Define
**************************************************************************/

#define ARCHIVE_DIR_EXT			".archive"				/* ユーザディレクトリ名に付加する */
#define ARCHIVE_RES_DIR			"res"
#define ARCHIVE_RES_DAT			"res.dat"
#define ARCHIVE_LATEST			"latest.dat"
#define ARCHIVE_INDEX				"index.html"
#define ARCHIVE_URLFILE			"url.txt"
#define ARCHIVE_INDEX_YEAR			"index-"				/* 年ごとの一覧ページ (index-YYYY.html) */

#define ARCHIVE_KEYLEN			16						/* キーとリソース名のハッシュの桁数 */
#define ARCHIVE_MAX_RES			300						/* 1ページで取得するリソースの最大数 */
#define ARCHIVE_MAX_RESSIZE		(10 * 1024 * 1024)		/* リソース1つの最大サイズ */
#define ARCHIVE_MAX_TOTAL			(100 * 1024 * 1024)		/* 1ページで取得するリソースの合計の最大 */
#define ARCHIVE_MAX_REDIRECT		10
#define ARCHIVE_MAX_QUEUE			200
#define ARCHIVE_CSS_DEPTH			3						/* @import をたどる深さ */
#define ARCHIVE_FRAME_DEPTH		3						/* iframe の中の iframe をたどる深さ */
#define ARCHIVE_MAX_DATFILE		(4 * 1024 * 1024)
#define ARCHIVE_MAX_GENFILES		200000
#define ARCHIVE_CLEAN_GENS			100						/* 世代数がこれ以下なら毎回使われていないリソースを探す */
#define ARCHIVE_DISK_RESERVE		5						/* ディスクの空きがこの割合 (%) 未満のときは保存しない */
#define ARCHIVE_DIR_MARGIN		40						/* ディレクトリ名に付加するファイル名の分 */
#define ARCHIVE_END_WAIT			5000
#define ARCHIVE_FINISH_WAIT		30000					/* 終了時に取得待ちのアーカイブを処理する時間 */
#define ARCHIVE_BROWSER_DIR		"WWWC.browser"			/* ブラウザのプロファイル (一時フォルダに作成) */
#define ARCHIVE_BROWSER_TIMEOUT	60000					/* タイムアウト未設定時にブラウザがページの読み込みを打ち切る時間 */
#define ARCHIVE_BROWSER_MARGIN	30000					/* ブラウザの起動と終了にかかる時間 (タイムアウトに加える) */
#define ARCHIVE_BROWSER_MAXWAIT	600000					/* スクリプトの実行を待つ時間の上限 */
#define ARCHIVE_BROWSER_POLL		50
#define ARCHIVE_BROWSER_RETRY		5000					/* ブラウザがこの時間内に失敗した場合は一度だけやり直す */

#define WWWC_WNDCLASS			"WWWCWindowClass"

typedef struct _ARCHIVE_MEMO {
	char *url;
	int type;
	char *file;
	struct _ARCHIVE_MEMO *next;
} ARCHIVE_MEMO;

typedef struct _ARCHIVE_ENTRY {
	char *url;
	char *etag;
	char *lastmod;
	char *file;
	struct _ARCHIVE_ENTRY *next;
} ARCHIVE_ENTRY;

typedef struct _ARCHIVE_JOB {
	struct _ARCHIVE_JOB *next;
	char *Url;
	char *Title;
	char Dir[MAX_PATH];
	char Key[ARCHIVE_KEYLEN + 1];
	WCHAR *ProxyName;
	WCHAR *ProxyUser;
	WCHAR *ProxyPass;
	WCHAR *UserAgent;
	BOOL Image;
	BOOL Css;
	BOOL Js;
	BOOL Browser;									/* ブラウザで表示した内容 (スクリプト実行後の DOM) を保存する */
	int Generation;
	DWORD TimeOutMs;

	HINTERNET hSession;
	ARCHIVE_MEMO *Memo;
	ARCHIVE_ENTRY *Entries;
	volatile LONG ResCount;
	volatile LONGLONG Total;
	int CssDepth;
	int FrameDepth;

	//進捗の表示用 (取得中は ArchiveCs の外で更新する)
	volatile LONG Phase;							/* ARCHIVE_PHASE_* */
	int Result;										/* ARCHIVE_RESULT_* */
	DWORD Status;									/* ページの HTTP ステータス */
	volatile LONG BrowserState;						/* ARCHIVE_BROWSER_* */
	int BrowserError;								/* ARCHIVE_BERR_* */
	DWORD BrowserCode;
	DWORD StartTick;

	//中止 (進捗状況のダイアログから)
	volatile LONG Cancel;							/* このアイテムの取得を中止する */
	HINTERNET ActiveReq;							/* 受信中のリクエスト (ArchiveCs で保護、中止のときに閉じる) */
} ARCHIVE_JOB;

/* 進捗の表示用の履歴 */
typedef struct _ARCHIVE_HIST {
	LONG Id;
	char *Title;
	char *Url;
	char Dir[MAX_PATH];
	int Result;
	DWORD Status;
	int Browser;
	int BrowserError;
	DWORD BrowserCode;
	int ResCount;
	LONGLONG Total;
	DWORD Elapsed;
	SYSTEMTIME Time;
} ARCHIVE_HIST;

typedef struct _ARCHIVE_RESP {
	DWORD Status;
	char *Body;
	long Len;
	char Type[128];
	char Charset[64];
	char ETag[256];
	char LastMod[128];
	char *FinalUrl;
} ARCHIVE_RESP;


/**************************************************************************
	Global Variables
**************************************************************************/

static CRITICAL_SECTION ArchiveCs;
static BOOL ArchiveCsInit;
static HANDLE ArchiveEvent;
static HANDLE ArchiveThread;
static ARCHIVE_JOB *ArchiveQueue;
static int ArchiveQueueCnt;
static volatile LONG ArchiveStop;
static BOOL ArchiveRestart;						/* 停止後に再開する */
static volatile LONG ArchiveEnding;				/* 終了通知の後 (新しい要求を受け付けない) */
static volatile LONG ArchiveBusy;
static char ArchiveRunningKey[ARCHIVE_KEYLEN + 1];		/* 取得中のアイテムのキー (ArchiveCs で保護) */
static ARCHIVE_JOB *ArchiveRunningJob;				/* 取得中のアイテム (ArchiveCs で保護、進捗の表示用) */
static ARCHIVE_HIST ArchiveHist[ARCHIVE_HISTORY_MAX];	/* 履歴 (ArchiveCs で保護、ArchiveHistTop が最新) */
static int ArchiveHistCnt;
static int ArchiveHistTop;
static LONG ArchiveHistId;
static volatile LONG ArchiveSerial;					/* 取得待ちと履歴が変わると増やす */
static char ArchiveUserDir[MAX_PATH];

//外部参照
extern int TimeOut;
extern int Proxy;
extern char pServer[];
extern int pPort;
extern int pUsePass;
extern char pUser[];
extern char pPass[];
extern int ArchiveGlobal;
extern char ArchiveBrowser[];
extern int ArchiveBrowserWait;


/**************************************************************************
	Helpers
**************************************************************************/

static char *ArchiveStrDup(const char *s)
{
	char *ret;

	if(s == NULL){
		return NULL;
	}
	ret = (char *)HeapAlloc(GetProcessHeap(), 0, lstrlen(s) + 1);
	if(ret != NULL){
		lstrcpy(ret, s);
	}
	return ret;
}

static WCHAR *WideDup(const char *s)
{
	WCHAR *ret;
	int len;

	if(s == NULL || (len = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0)) <= 0){
		return NULL;
	}
	ret = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
	if(ret != NULL && MultiByteToWideChar(CP_ACP, 0, s, -1, ret, len) == 0){
		HeapFree(GetProcessHeap(), 0, ret);
		ret = NULL;
	}
	return ret;
}

static char *NarrowDup(const WCHAR *s)
{
	char *ret;
	int len;

	if(s == NULL || (len = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL)) <= 0){
		return NULL;
	}
	ret = (char *)HeapAlloc(GetProcessHeap(), 0, len);
	if(ret != NULL && WideCharToMultiByte(CP_ACP, 0, s, -1, ret, len, NULL, NULL) == 0){
		HeapFree(GetProcessHeap(), 0, ret);
		ret = NULL;
	}
	return ret;
}

static void MemFree(void *p)
{
	if(p != NULL){
		HeapFree(GetProcessHeap(), 0, p);
	}
}

static void WideFree(WCHAR *p)
{
	if(p != NULL){
		SecureZeroMemory(p, lstrlenW(p) * sizeof(WCHAR));
		HeapFree(GetProcessHeap(), 0, p);
	}
}

/* MD5 の16進表記 (digits 桁) */
static void HashHex(const void *data, long len, char *ret, int digits)
{
	MD5_CTX context;
	unsigned char digest[16];
	int i;

	MD5Init(&context);
	MD5Update(&context, (unsigned char *)data, len);
	MD5Final(digest, &context);
	for(i = 0; i < 16 && i * 2 < digits; i++){
		wsprintf(ret + i * 2, "%02x", digest[i]);
	}
	ret[digits] = '\0';
}

/* 制御文字を含まない文字列か */
static BOOL IsSafeText(const char *s)
{
	for(; *s != '\0'; s++){
		if((unsigned char)*s < 0x20 || *s == 0x7F){
			return FALSE;
		}
	}
	return TRUE;
}

static BOOL IsHttp(const char *url)
{
	return (url != NULL && (lstrcmpni((char *)url, "http://", 7) == 0 || lstrcmpni((char *)url, "https://", 8) == 0)) ? TRUE : FALSE;
}

/* ファイルの内容を読み込む */
static char *ReadAll(const char *path, long max, long *len)
{
	HANDLE hFile;
	DWORD size, ret;
	char *buf;

	hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE){
		return NULL;
	}
	size = GetFileSize(hFile, NULL);
	if(size == INVALID_FILE_SIZE || size > (DWORD)max){
		CloseHandle(hFile);
		return NULL;
	}
	buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
	if(buf == NULL || ReadFile(hFile, buf, size, &ret, NULL) == FALSE || ret != size){
		MemFree(buf);
		CloseHandle(hFile);
		return NULL;
	}
	CloseHandle(hFile);
	buf[size] = '\0';
	if(len != NULL){
		*len = size;
	}
	return buf;
}

/* 一時ファイルに書き込んでから置き換える */
static BOOL WriteAll(const char *path, const char *data, long len)
{
	char tmp[MAX_PATH];
	HANDLE hFile;
	DWORD ret;
	BOOL rc;

	if(lstrlen(path) + 4 >= MAX_PATH){
		return FALSE;
	}
	wsprintf(tmp, "%s.tmp", path);
	hFile = CreateFile(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE){
		return FALSE;
	}
	rc = (len == 0 || (WriteFile(hFile, data, len, &ret, NULL) == TRUE && ret == (DWORD)len)) ? TRUE : FALSE;
	CloseHandle(hFile);
	if(rc == FALSE || MoveFileEx(tmp, path, MOVEFILE_REPLACE_EXISTING) == FALSE){
		DeleteFile(tmp);
		return FALSE;
	}
	return TRUE;
}

/* インターネットから取得したファイルであることを記録する (Zone.Identifier) */
static void SetZoneIdentifier(const char *path, const char *url)
{
	char stream[MAX_PATH + 32];
	ARCHIVE_BUF b;
	HANDLE hFile;
	DWORD ret;

	if(lstrlen(path) + 20 >= sizeof(stream)){
		return;
	}
	wsprintf(stream, "%s:Zone.Identifier", path);
	ZeroMemory(&b, sizeof(ARCHIVE_BUF));
	Buf_Str(&b, "[ZoneTransfer]\r\nZoneId=3\r\n");
	if(url != NULL && IsSafeText(url) == TRUE && lstrlen(url) < ARCHIVE_URL_MAX){
		Buf_Str(&b, "HostUrl=");
		Buf_Str(&b, url);
		Buf_Str(&b, "\r\n");
	}
	if(b.err == FALSE){
		hFile = CreateFile(stream, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(hFile != INVALID_HANDLE_VALUE){
			WriteFile(hFile, b.p, b.len, &ret, NULL);
			CloseHandle(hFile);
		}
	}
	Buf_Free(&b);
}

static BOOL MemEqual(const char *a, const char *b, long len)
{
	long i;

	for(i = 0; i < len; i++){
		if(a[i] != b[i]){
			return FALSE;
		}
	}
	return TRUE;
}

static BOOL FileExists(const char *path)
{
	DWORD attr = GetFileAttributes(path);
	return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) ? TRUE : FALSE;
}

static BOOL EnsureDir(const char *path)
{
	DWORD attr;

	if(CreateDirectory(path, NULL) == TRUE){
		return TRUE;
	}
	attr = GetFileAttributes(path);
	return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) ? TRUE : FALSE;
}

/* 保存先のディスクの空きが少ないか (空きが全体の ARCHIVE_DISK_RESERVE % 未満) */
static BOOL IsDiskFull(const char *dir)
{
	ULARGE_INTEGER avail, total, freebytes;

	if(GetDiskFreeSpaceEx(dir, &avail, &total, &freebytes) == FALSE || total.QuadPart == 0){
		return FALSE;
	}
	return (avail.QuadPart < total.QuadPart / 100 * ARCHIVE_DISK_RESERVE) ? TRUE : FALSE;
}

/* 先頭 n 文字の比較 */
static int PrefixCmp(const char *a, const char *b, int n)
{
	int i;

	for(i = 0; i < n; i++){
		if(a[i] != b[i] || a[i] == '\0'){
			return (unsigned char)a[i] - (unsigned char)b[i];
		}
	}
	return 0;
}

/* 数字の文字列を数値にする */
static int DigitsToInt(const char *s, int n)
{
	int i, ret = 0;

	for(i = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++){
		ret = ret * 10 + (s[i] - '0');
	}
	return ret;
}

/* 世代のファイル名か (YYYYMMDD-HHMMSS[-n].html または .txt、n は 3 桁まで) */
static BOOL IsGenName(const char *name)
{
	const char *p;
	int i;

	for(i = 0; i < 15; i++){
		if(name[i] == '\0' || (i == 8 && name[i] != '-') || (i != 8 && !(name[i] >= '0' && name[i] <= '9'))){
			return FALSE;
		}
	}
	p = name + 15;
	if(*p == '-'){
		for(p++; *p >= '0' && *p <= '9'; p++);
		//パスのバッファに収まる長さのものだけを扱う (NewGenName が付ける番号は 2 桁まで)
		if(p - (name + 16) < 1 || p - (name + 16) > 3){
			return FALSE;
		}
	}
	return (lstrcmpi(p, ".html") == 0 || lstrcmpi(p, ".txt") == 0) ? TRUE : FALSE;
}

static int CompareName(const void *a, const void *b)
{
	return lstrcmp(*(const char **)a, *(const char **)b);
}

/* 世代のファイル名の一覧 (古い順) */
static int ListGens(const char *dir, char ***list)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind;
	char path[MAX_PATH];
	char **ret = NULL, **tmp;
	int cnt = 0, cap = 0;

	*list = NULL;
	if(lstrlen(dir) + 3 >= MAX_PATH){
		return 0;
	}
	wsprintf(path, "%s\\*", dir);
	hFind = FindFirstFile(path, &fd);
	if(hFind == INVALID_HANDLE_VALUE){
		return 0;
	}
	do{
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || IsGenName(fd.cFileName) == FALSE){
			continue;
		}
		if(cnt >= ARCHIVE_MAX_GENFILES){
			break;
		}
		if(cnt >= cap){
			cap = (cap == 0) ? 16 : cap * 2;
			tmp = (ret == NULL) ? (char **)HeapAlloc(GetProcessHeap(), 0, sizeof(char *) * cap) :
				(char **)HeapReAlloc(GetProcessHeap(), 0, ret, sizeof(char *) * cap);
			if(tmp == NULL){
				break;
			}
			ret = tmp;
		}
		if((ret[cnt] = ArchiveStrDup(fd.cFileName)) != NULL){
			cnt++;
		}
	}while(FindNextFile(hFind, &fd) == TRUE);
	FindClose(hFind);
	if(cnt > 1){
		qsort(ret, cnt, sizeof(char *), CompareName);
	}
	*list = ret;
	return cnt;
}

static void FreeList(char **list, int cnt)
{
	int i;

	if(list == NULL){
		return;
	}
	for(i = 0; i < cnt; i++){
		MemFree(list[i]);
	}
	MemFree(list);
}


/**************************************************************************
	Paths
**************************************************************************/

/* WWWC のメインウィンドウを取得 */
static BOOL CALLBACK FindMainProc(HWND hWnd, LPARAM lParam)
{
	char cls[64];

	if(GetClassName(hWnd, cls, sizeof(cls)) > 0 && lstrcmp(cls, WWWC_WNDCLASS) == 0){
		*(HWND *)lParam = hWnd;
		return FALSE;
	}
	return TRUE;
}

static HWND GetMainWindow(HWND hWnd)
{
	HWND ret = NULL;
	char cls[64];

	if(hWnd != NULL){
		if(GetClassName(hWnd, cls, sizeof(cls)) > 0 && lstrcmp(cls, WWWC_WNDCLASS) == 0){
			return hWnd;
		}
		ret = GetAncestor(hWnd, GA_ROOTOWNER);
		if(ret != NULL && GetClassName(ret, cls, sizeof(cls)) > 0 && lstrcmp(cls, WWWC_WNDCLASS) == 0){
			return ret;
		}
		ret = NULL;
	}
	EnumThreadWindows(GetCurrentThreadId(), FindMainProc, (LPARAM)&ret);
	return ret;
}

/* WWWC 本体のウィンドウ (メインスレッドで呼ぶ) */
HWND Archive_GetMainWindow(void)
{
	return GetMainWindow(NULL);
}

/******************************************************************************

	Archive_GetRoot

	アーカイブを保存するディレクトリ (ユーザディレクトリと同じ場所の ユーザ名.archive)
	ユーザディレクトリの中はフォルダのツリーとして扱われるため使用しない

******************************************************************************/

BOOL Archive_GetRoot(HWND hWnd, char *ret)
{
	char buf[MAX_PATH * 2];
	char *p;
	HWND mWnd;

	*ret = '\0';
	if((mWnd = GetMainWindow(hWnd)) != NULL){
		*buf = '\0';
		SendMessage(mWnd, WM_WWWC_GETINIPATH, 0, (LPARAM)buf);
		buf[MAX_PATH - 1] = '\0';
		//末尾の \ を除く
		for(p = buf + lstrlen(buf); p > buf && *(p - 1) == '\\'; p--);
		*p = '\0';
		if(*buf != '\0'){
			lstrcpy(ArchiveUserDir, buf);
		}
	}
	if(*ArchiveUserDir == '\0' || lstrlen(ArchiveUserDir) + lstrlen(ARCHIVE_DIR_EXT) >= MAX_PATH){
		return FALSE;
	}
	wsprintf(ret, "%s%s", ArchiveUserDir, ARCHIVE_DIR_EXT);
	return TRUE;
}

/* アイテムのアーカイブのディレクトリ */
static BOOL GetItemDir(HWND hWnd, const char *url, char *ret, char *key)
{
	char root[MAX_PATH];
	char k[ARCHIVE_KEYLEN + 1];

	if(url == NULL || IsHttp(url) == FALSE || Archive_GetRoot(hWnd, root) == FALSE){
		return FALSE;
	}
	HashHex(url, lstrlen(url), k, ARCHIVE_KEYLEN);
	if(lstrlen(root) + 1 + ARCHIVE_KEYLEN + ARCHIVE_DIR_MARGIN >= MAX_PATH){
		return FALSE;
	}
	wsprintf(ret, "%s\\%s", root, k);
	if(key != NULL){
		lstrcpy(key, k);
	}
	return TRUE;
}


/**************************************************************************
	HTTP
**************************************************************************/

static void FreeResp(ARCHIVE_RESP *r)
{
	MemFree(r->Body);
	MemFree(r->FinalUrl);
	ZeroMemory(r, sizeof(ARCHIVE_RESP));
}

/* ヘッダの値を取得 */
static void QueryHeader(HINTERNET hReq, DWORD info, char *ret, int size)
{
	WCHAR *w;
	DWORD len = 0;
	char *s;

	*ret = '\0';
	if(WinHttpQueryHeaders(hReq, info, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &len, WINHTTP_NO_HEADER_INDEX) == FALSE &&
		GetLastError() != ERROR_INSUFFICIENT_BUFFER){
		return;
	}
	if(len == 0 || len > 64 * 1024){
		return;
	}
	w = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len + sizeof(WCHAR));
	if(w == NULL){
		return;
	}
	if(WinHttpQueryHeaders(hReq, info, WINHTTP_HEADER_NAME_BY_INDEX, w, &len, WINHTTP_NO_HEADER_INDEX) == TRUE &&
		(s = NarrowDup(w)) != NULL){
		if(lstrlen(s) < size && IsSafeText(s) == TRUE){
			lstrcpy(ret, s);
		}
		MemFree(s);
	}
	MemFree(w);
}

/* Content-Type から種類と文字コードを取得 */
static void ParseContentType(ARCHIVE_RESP *r)
{
	char *p, *c;
	int i;

	for(p = r->Type; *p != '\0'; p++){
		*p = (*p >= 'A' && *p <= 'Z') ? *p - 'A' + 'a' : *p;
	}
	*r->Charset = '\0';
	if((p = StrStr(r->Type, ";")) != NULL){
		c = StrStr(p, "charset=");
		if(c != NULL){
			c += 8;
			if(*c == '"') c++;
			for(i = 0; c[i] != '\0' && c[i] != '"' && c[i] != ';' && c[i] != ' ' && i < sizeof(r->Charset) - 1; i++){
				r->Charset[i] = c[i];
			}
			r->Charset[i] = '\0';
		}
		*p = '\0';
	}
	for(p = r->Type + lstrlen(r->Type); p > r->Type && *(p - 1) == ' '; p--);
	*p = '\0';
}

/* 取得を止めるか (終了などで全体を停止した場合と、このアイテムを中止した場合) */
static BOOL IsJobStopped(ARCHIVE_JOB *job)
{
	return (ArchiveStop != 0 || job->Cancel != 0) ? TRUE : FALSE;
}

/* 受信するリクエストを登録する (中止のときに別のスレッドから閉じる) */
static void SetActiveRequest(ARCHIVE_JOB *job, HINTERNET hReq)
{
	EnterCriticalSection(&ArchiveCs);
	job->ActiveReq = hReq;
	LeaveCriticalSection(&ArchiveCs);
}

/* 登録したリクエストを閉じる (中止で既に閉じた場合は何もしない) */
static void CloseActiveRequest(ARCHIVE_JOB *job)
{
	HINTERNET h;

	EnterCriticalSection(&ArchiveCs);
	h = job->ActiveReq;
	job->ActiveReq = NULL;
	LeaveCriticalSection(&ArchiveCs);
	if(h != NULL){
		WinHttpCloseHandle(h);
	}
}

/******************************************************************************

	ArchiveFetch

	URLを取得する (リダイレクトはここで処理し、認証情報は同じ接続先にのみ送る)

******************************************************************************/

static BOOL ArchiveFetch(ARCHIVE_JOB *job, const char *url, const char *lastmod, const char *etag, long maxsize, ARCHIVE_RESP *r)
{
	URL_COMPONENTS uc;
	HINTERNET hConnect = NULL, hReq = NULL;
	WCHAR host[256];
	WCHAR *wurl = NULL, *path = NULL, *header = NULL;
	ARCHIVE_BUF hb, body;
	char *cur, *next, loc[ARCHIVE_URL_MAX + 1];
	DWORD status, size, len, dwRead, flags;
	int hop;
	BOOL ret = FALSE;

	ZeroMemory(r, sizeof(ARCHIVE_RESP));
	cur = ArchiveStrDup(url);
	for(hop = 0; cur != NULL && hop <= ARCHIVE_MAX_REDIRECT && IsJobStopped(job) == FALSE; hop++){
		ZeroMemory(&hb, sizeof(ARCHIVE_BUF));
		ZeroMemory(&body, sizeof(ARCHIVE_BUF));
		wurl = WideDup(cur);
		if(wurl == NULL){
			break;
		}
		len = lstrlenW(wurl) + 1;
		path = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
		if(path == NULL){
			break;
		}
		ZeroMemory(&uc, sizeof(uc));
		uc.dwStructSize = sizeof(uc);
		uc.lpszHostName = host;
		uc.dwHostNameLength = 256;
		uc.lpszUrlPath = path;
		uc.dwUrlPathLength = len;
		if(WinHttpCrackUrl(wurl, 0, 0, &uc) == FALSE){
			break;
		}
		{
			WCHAR *f;
			for(f = path; *f != L'\0'; f++){
				if(*f == L'#'){
					*f = L'\0';
					break;
				}
			}
		}
		hConnect = WinHttpConnect(job->hSession, host, uc.nPort, 0);
		if(hConnect == NULL){
			break;
		}
		hReq = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			(uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
		if(hReq == NULL){
			break;
		}
		//中止のときは別のスレッドから閉じる (閉じるのは CloseActiveRequest のみで行う)
		SetActiveRequest(job, hReq);
		if(IsJobStopped(job) == TRUE){
			break;
		}
		flags = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &flags, sizeof(flags));
		//Windows のログオン情報を自動で送信しない
		flags = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
		WinHttpSetOption(hReq, WINHTTP_OPTION_AUTOLOGON_POLICY, &flags, sizeof(flags));
		if(job->ProxyUser != NULL && job->ProxyPass != NULL){
			WinHttpSetOption(hReq, WINHTTP_OPTION_PROXY_USERNAME, job->ProxyUser, lstrlenW(job->ProxyUser));
			WinHttpSetOption(hReq, WINHTTP_OPTION_PROXY_PASSWORD, job->ProxyPass, lstrlenW(job->ProxyPass));
		}
		//送信ヘッダ
		Buf_Str(&hb, "Accept: */*");
		if(hop == 0 && etag != NULL && *etag != '\0' && IsSafeText(etag) == TRUE){
			Buf_Str(&hb, "\r\nIf-None-Match: ");
			Buf_Str(&hb, etag);
		}
		if(hop == 0 && lastmod != NULL && *lastmod != '\0' && IsSafeText(lastmod) == TRUE){
			Buf_Str(&hb, "\r\nIf-Modified-Since: ");
			Buf_Str(&hb, lastmod);
		}
		header = (hb.err == FALSE) ? WideDup(hb.p) : NULL;
		if(hb.p != NULL){
			SecureZeroMemory(hb.p, hb.len);
		}
		Buf_Free(&hb);
		if(header == NULL){
			break;
		}
		if(WinHttpSendRequest(hReq, header, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
			WinHttpReceiveResponse(hReq, NULL) == FALSE){
			break;
		}
		size = sizeof(status);
		if(WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			&status, &size, WINHTTP_NO_HEADER_INDEX) == FALSE){
			break;
		}
		if(status == 301 || status == 302 || status == 303 || status == 307 || status == 308){
			//リダイレクト (http, https のみ)
			QueryHeader(hReq, WINHTTP_QUERY_LOCATION, loc, sizeof(loc));
			next = (*loc != '\0') ? CreateMoveURL(cur, loc) : NULL;
			MemFree(cur);
			cur = NULL;
			if(next != NULL){
				if(lstrlen(next) <= ARCHIVE_URL_MAX){
					cur = ArchiveStrDup(next);
				}
				GlobalFree(next);
			}
			CloseActiveRequest(job);
			WinHttpCloseHandle(hConnect);
			hReq = hConnect = NULL;
			WideFree(header);
			MemFree(wurl);
			MemFree(path);
			header = wurl = path = NULL;
			continue;
		}
		r->Status = status;
		if(status == 200){
			QueryHeader(hReq, WINHTTP_QUERY_CONTENT_TYPE, r->Type, sizeof(r->Type));
			QueryHeader(hReq, WINHTTP_QUERY_ETAG, r->ETag, sizeof(r->ETag));
			QueryHeader(hReq, WINHTTP_QUERY_LAST_MODIFIED, r->LastMod, sizeof(r->LastMod));
			ParseContentType(r);
			//本文
			while(IsJobStopped(job) == FALSE){
				size = 0;
				if(WinHttpQueryDataAvailable(hReq, &size) == FALSE){
					body.err = TRUE;
					break;
				}
				if(size == 0){
					break;
				}
				if(body.len + (LONGLONG)size > maxsize){
					body.err = TRUE;
					break;
				}
				{
					char *rb = (char *)HeapAlloc(GetProcessHeap(), 0, size);
					if(rb == NULL){
						body.err = TRUE;
						break;
					}
					dwRead = 0;
					if(WinHttpReadData(hReq, rb, size, &dwRead) == FALSE){
						HeapFree(GetProcessHeap(), 0, rb);
						body.err = TRUE;
						break;
					}
					Buf_Add(&body, rb, (long)dwRead);
					HeapFree(GetProcessHeap(), 0, rb);
					if(dwRead == 0){
						break;
					}
				}
			}
			if(body.err == TRUE || IsJobStopped(job) == TRUE){
				Buf_Free(&body);
				break;
			}
			if(body.p == NULL){
				//本文が空
				body.p = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1);
				if(body.p == NULL){
					break;
				}
			}
			r->Body = body.p;
			r->Len = body.len;
			body.p = NULL;
		}
		r->FinalUrl = cur;
		cur = NULL;
		ret = TRUE;
		break;
	}
	if(hReq != NULL) CloseActiveRequest(job);
	if(hConnect != NULL) WinHttpCloseHandle(hConnect);
	WideFree(header);
	MemFree(wurl);
	MemFree(path);
	MemFree(cur);
	if(ret == FALSE){
		FreeResp(r);
	}
	return ret;
}


/**************************************************************************
	Browser (ブラウザで表示した内容の取得)
**************************************************************************/

/* App Paths に登録されたブラウザのパスを取得 */
static BOOL RegAppPath(HKEY root, REGSAM wow, const char *exe, char *ret, int size)
{
	HKEY hKey;
	char key[MAX_PATH], val[MAX_PATH];
	DWORD type, len = sizeof(val) - 1;
	LONG rc;

	wsprintf(key, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", exe);
	if(RegOpenKeyEx(root, key, 0, KEY_QUERY_VALUE | wow, &hKey) != ERROR_SUCCESS){
		return FALSE;
	}
	ZeroMemory(val, sizeof(val));
	rc = RegQueryValueEx(hKey, NULL, NULL, &type, (LPBYTE)val, &len);
	RegCloseKey(hKey);
	if(rc != ERROR_SUCCESS || !(type == REG_SZ || type == REG_EXPAND_SZ) || *val == '\0'){
		return FALSE;
	}
	if(type == REG_EXPAND_SZ){
		if(ExpandEnvironmentStrings(val, ret, size) == 0){
			return FALSE;
		}
	}else{
		lstrcpyn(ret, val, size);
	}
	return FileExists(ret);
}


/******************************************************************************

	Archive_FindBrowser

	ブラウザで表示した内容の取得に使うブラウザ (Chromium 系) を探す
	設定が空の場合は Microsoft Edge、Google Chrome の順に探す (ret は MAX_PATH)

******************************************************************************/

BOOL Archive_FindBrowser(char *ret)
{
	static const char *exes[] = { "msedge.exe", "chrome.exe" };
	int i;

	if(*ArchiveBrowser != '\0'){
		lstrcpyn(ret, ArchiveBrowser, MAX_PATH);
		return FileExists(ret);
	}
	for(i = 0; i < sizeof(exes) / sizeof(exes[0]); i++){
		if(RegAppPath(HKEY_LOCAL_MACHINE, 0, exes[i], ret, MAX_PATH) == TRUE ||
			RegAppPath(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, exes[i], ret, MAX_PATH) == TRUE ||
			RegAppPath(HKEY_CURRENT_USER, 0, exes[i], ret, MAX_PATH) == TRUE){
			return TRUE;
		}
	}
	*ret = '\0';
	return FALSE;
}

/* コマンドラインの引数に使える文字列か (引用符で囲むため " と \ は使えない) */
static BOOL IsArgText(const char *s)
{
	for(; *s != '\0'; s++){
		if((unsigned char)*s < 0x20 || (unsigned char)*s >= 0x7F || *s == '"' || *s == '\\'){
			return FALSE;
		}
	}
	return TRUE;
}

/* コマンドラインの引数に使えるパスか (引用符の中の \ は末尾以外ならそのまま扱われる) */
static BOOL IsPathArg(const char *s)
{
	const char *p;

	if(*s == '\0' || s[lstrlen(s) - 1] == '\\'){
		return FALSE;
	}
	for(p = s; *p != '\0'; p++){
		if((unsigned char)*p < 0x20 || *p == '"'){
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * ブラウザに渡す環境変数 (互換性レイヤーの指定 __COMPAT_LAYER を除く、HeapFree で解放)
 * エクスプローラから起動した WWWC には互換性アシスタントが __COMPAT_LAYER を付けることがあり、
 * それを引き継いだ Edge は互換性レイヤーを外して自分を起動し直すため、標準出力に DOM が出力されない
 */
static WCHAR *MakeBrowserEnv(void)
{
	static const WCHAR key[] = L"__COMPAT_LAYER=";
	WCHAR *env, *p, *ret, *r;
	int len, klen = lstrlenW(key);
	SIZE_T total = 0;

	if((env = GetEnvironmentStringsW()) == NULL){
		return NULL;
	}
	for(p = env; *p != L'\0'; p += lstrlenW(p) + 1){
		total += lstrlenW(p) + 1;
	}
	ret = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (total + 2) * sizeof(WCHAR));
	if(ret != NULL){
		r = ret;
		for(p = env; *p != L'\0'; p += len + 1){
			len = lstrlenW(p);
			if(len >= klen && CompareStringOrdinal(p, klen, key, klen, TRUE) == CSTR_EQUAL){
				continue;
			}
			CopyMemory(r, p, (len + 1) * sizeof(WCHAR));
			r += len + 1;
		}
		//終端 (空の場合も 2 つの NUL にする)
		*(r++) = L'\0';
		*r = L'\0';
	}
	FreeEnvironmentStringsW(env);
	return ret;
}

/* ブラウザの子プロセスをまとめて終了するためのジョブ */
static HANDLE CreateKillJob(void)
{
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl;
	HANDLE hJob;

	hJob = CreateJobObject(NULL, NULL);
	if(hJob == NULL){
		return NULL;
	}
	ZeroMemory(&jl, sizeof(jl));
	jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if(SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jl, sizeof(jl)) == FALSE){
		CloseHandle(hJob);
		return NULL;
	}
	return hJob;
}

/* data の中に s (NUL 終端) があるか */
static BOOL MemContains(const char *data, long len, const char *s)
{
	long n = lstrlen(s), i;

	if(data == NULL || n == 0 || len < n){
		return FALSE;
	}
	for(i = 0; i <= len - n; i++){
		if(data[i] == *s && MemEqual(data + i, s, n) == TRUE){
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * ボット対策の確認ページ (ブラウザの確認中に表示されるページ) か
 * 通常のページにも読み込まれるスクリプト (challenge-platform) ではなく、確認ページにのみある記述で判断する
 */
static BOOL IsBotCheckPage(const char *data, long len)
{
	static const char *marks[] = {
		"_cf_chl_opt",						/* Cloudflare の確認ページの設定 */
		"id=\"challenge-error-text\"",		/* Cloudflare の確認ページの本文 */
	};
	int i;

	for(i = 0; i < sizeof(marks) / sizeof(marks[0]); i++){
		if(MemContains(data, len, marks[i]) == TRUE){
			return TRUE;
		}
	}
	return FALSE;
}

/* フォルダを中身ごと削除する (path は作業用のバッファで、終了時は元に戻す。削除できないものがあれば FALSE) */
static BOOL DeleteTreeW(WCHAR *path, int size)
{
	WIN32_FIND_DATAW fd;
	HANDLE hFind;
	int len = lstrlenW(path);
	BOOL ret = TRUE;

	if(len + 3 >= size){
		return FALSE;
	}
	lstrcpyW(path + len, L"\\*");
	hFind = FindFirstFileW(path, &fd);
	path[len] = L'\0';
	if(hFind != INVALID_HANDLE_VALUE){
		do{
			if(lstrcmpW(fd.cFileName, L".") == 0 || lstrcmpW(fd.cFileName, L"..") == 0){
				continue;
			}
			if(len + 1 + lstrlenW(fd.cFileName) + 1 >= size){
				ret = FALSE;
				continue;
			}
			path[len] = L'\\';
			lstrcpyW(path + len + 1, fd.cFileName);
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
				//リンクの先はたどらない
				if(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT){
					if(RemoveDirectoryW(path) == FALSE){
						ret = FALSE;
					}
				}else if(DeleteTreeW(path, size) == FALSE){
					ret = FALSE;
				}
			}else{
				if(fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY){
					SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
				}
				if(DeleteFileW(path) == FALSE){
					ret = FALSE;
				}
			}
			path[len] = L'\0';
		}while(FindNextFileW(hFind, &fd));
		FindClose(hFind);
	}
	if(RemoveDirectoryW(path) == FALSE){
		ret = FALSE;
	}
	return ret;
}

/* フォルダを中身ごと削除する (MAX_PATH を超える深さにも対応するため \\?\ を付ける) */
static BOOL DeleteDirTree(const char *dir)
{
	WCHAR *path;
	int n;
	BOOL ret = FALSE;

	path = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, 32768 * sizeof(WCHAR));
	if(path == NULL){
		return FALSE;
	}
	//ローカルのフルパスのみ \\?\ を付ける (UNC はそのまま)
	n = (dir[0] != '\\') ? 4 : 0;
	if(n > 0){
		lstrcpyW(path, L"\\\\?\\");
	}
	if(MultiByteToWideChar(CP_ACP, 0, dir, -1, path + n, 32768 - n) > 0){
		ret = DeleteTreeW(path, 32768);
	}
	HeapFree(GetProcessHeap(), 0, path);
	return ret;
}

/* ブラウザのプロファイルの親のフォルダ (%TEMP%\WWWC.browser) */
static BOOL GetBrowserRoot(char *ret)
{
	DWORD n;

	n = GetTempPath(MAX_PATH - lstrlen(ARCHIVE_BROWSER_DIR) - 40, ret);
	if(n == 0 || n >= (DWORD)(MAX_PATH - lstrlen(ARCHIVE_BROWSER_DIR) - 40)){
		return FALSE;
	}
	if(ret[lstrlen(ret) - 1] != '\\'){
		lstrcat(ret, "\\");
	}
	lstrcat(ret, ARCHIVE_BROWSER_DIR);
	return EnsureDir(ret);
}

/*
 * 前回までのプロファイルを削除する
 * 起動ごとのプロファイル (p<プロセスID>-<番号>) は、実行中の他のプロセスのものは残す
 * それ以外 (以前の版が共有で使っていたプロファイル) はすべて削除する
 */
static void CleanBrowserProfiles(const char *root)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind, hProcess;
	char path[MAX_PATH];
	const char *p;
	DWORD pid;
	BOOL del;

	if(lstrlen(root) + 3 >= MAX_PATH){
		return;
	}
	wsprintf(path, "%s\\*", root);
	if((hFind = FindFirstFile(path, &fd)) == INVALID_HANDLE_VALUE){
		return;
	}
	do{
		if(lstrcmp(fd.cFileName, ".") == 0 || lstrcmp(fd.cFileName, "..") == 0 ||
			lstrlen(root) + 1 + lstrlen(fd.cFileName) >= MAX_PATH){
			continue;
		}
		del = TRUE;
		if(fd.cFileName[0] == 'p' && fd.cFileName[1] >= '0' && fd.cFileName[1] <= '9'){
			for(pid = 0, p = fd.cFileName + 1; *p >= '0' && *p <= '9'; p++){
				pid = pid * 10 + (*p - '0');
			}
			if(*p == '-' && pid != GetCurrentProcessId() &&
				(hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid)) != NULL){
				//他のプロセスが実行中の場合は使用中の可能性があるため残す
				del = (WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT) ? FALSE : TRUE;
				CloseHandle(hProcess);
			}
		}
		if(del == FALSE){
			continue;
		}
		wsprintf(path, "%s\\%s", root, fd.cFileName);
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			DeleteDirTree(path);
		}else{
			DeleteFile(path);
		}
	}while(FindNextFile(hFind, &fd));
	FindClose(hFind);
}

/*
 * ブラウザをヘッドレスで起動してページを表示し、スクリプトの実行後の DOM (UTF-8) を取得する
 * ブラウザが無い場合や失敗した場合は FALSE を返す (通常の取得にする、理由は job->BrowserError)
 * プロファイルは起動ごとに新しく作成して終了後に削除する
 * (同じプロファイルのブラウザが動いていると、起動したブラウザがそちらに処理を渡してすぐに終了するため)
 */
static BOOL BrowserFetch(ARCHIVE_JOB *job, const char *url, char **body, long *len)
{
	SECURITY_ATTRIBUTES sa;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ARCHIVE_BUF cmd, out;
	HANDLE hRead = NULL, hWrite = NULL, hNul = NULL, hJob = NULL;
	char exe[MAX_PATH], root[MAX_PATH], dir[MAX_PATH], num[32];
	char *proxy = NULL, *ua = NULL;
	char *rb = NULL;
	WCHAR *env = NULL;
	DWORD avail, dwRead, start, limit, timeout, code;
	int wait, i;
	BOOL ret = FALSE, exited = FALSE, killed = FALSE, made = FALSE;

	*body = NULL;
	*len = 0;
	job->BrowserError = ARCHIVE_BERR_NONE;
	job->BrowserCode = 0;
	if(Archive_FindBrowser(exe) == FALSE){
		job->BrowserError = ARCHIVE_BERR_NOTFOUND;
		return FALSE;
	}
	//プロキシの認証はブラウザに渡せない
	if(job->ProxyUser != NULL){
		job->BrowserError = ARCHIVE_BERR_PROXY;
		return FALSE;
	}
	if(IsPathArg(exe) == FALSE || IsArgText(url) == FALSE){
		job->BrowserError = ARCHIVE_BERR_ARG;
		return FALSE;
	}
	//プロファイルは一時フォルダに起動ごとに作成する (起動中の他のブラウザとは別に起動する)
	if(GetBrowserRoot(root) == FALSE){
		job->BrowserError = ARCHIVE_BERR_START;
		job->BrowserCode = GetLastError();
		return FALSE;
	}
	CleanBrowserProfiles(root);
	for(i = 0; i < 100 && made == FALSE; i++){
		wsprintf(dir, "%s\\p%lu-%lu", root, GetCurrentProcessId(), GetTickCount() + i);
		made = CreateDirectory(dir, NULL);
	}
	if(made == FALSE || IsPathArg(dir) == FALSE){
		job->BrowserError = (made == FALSE) ? ARCHIVE_BERR_START : ARCHIVE_BERR_ARG;
		job->BrowserCode = (made == FALSE) ? GetLastError() : 0;
		if(made == TRUE){
			RemoveDirectory(dir);
		}
		return FALSE;
	}
	timeout = (job->TimeOutMs > 0) ? job->TimeOutMs : ARCHIVE_BROWSER_TIMEOUT;
	wait = (ArchiveBrowserWait < 0) ? 0 : ((ArchiveBrowserWait > ARCHIVE_BROWSER_MAXWAIT) ? ARCHIVE_BROWSER_MAXWAIT : ArchiveBrowserWait);

	//コマンドライン
	ZeroMemory(&cmd, sizeof(ARCHIVE_BUF));
	ZeroMemory(&out, sizeof(ARCHIVE_BUF));
	Buf_Str(&cmd, "\"");
	Buf_Str(&cmd, exe);
	Buf_Str(&cmd, "\" --headless=new --disable-gpu --no-first-run --no-default-browser-check --disable-extensions"
		" --disable-sync --disable-background-networking --no-pings --mute-audio --hide-scrollbars --window-size=1280,1024");
	Buf_Str(&cmd, " --user-data-dir=\"");
	Buf_Str(&cmd, dir);
	Buf_Str(&cmd, "\"");
	if(wait > 0){
		//読み込み後にスクリプトのタイマーなどを進める仮想時間 (待ち時間ではなく実行できる分だけ進む)
		wsprintf(num, " --virtual-time-budget=%d", wait);
		Buf_Str(&cmd, num);
	}
	wsprintf(num, " --timeout=%lu", timeout);
	Buf_Str(&cmd, num);
	if(job->ProxyName != NULL){
		proxy = NarrowDup(job->ProxyName);
		if(proxy == NULL || IsArgText(proxy) == FALSE){
			job->BrowserError = ARCHIVE_BERR_ARG;
			goto end;
		}
		Buf_Str(&cmd, " --proxy-server=\"");
		Buf_Str(&cmd, proxy);
		Buf_Str(&cmd, "\"");
	}else{
		Buf_Str(&cmd, " --no-proxy-server");
	}
	if(job->UserAgent != NULL){
		ua = NarrowDup(job->UserAgent);
		if(ua != NULL && IsArgText(ua) == TRUE){
			Buf_Str(&cmd, " --user-agent=\"");
			Buf_Str(&cmd, ua);
			Buf_Str(&cmd, "\"");
		}
	}
	Buf_Str(&cmd, " --dump-dom \"");
	Buf_Str(&cmd, url);
	Buf_Str(&cmd, "\"");
	if(cmd.err == TRUE){
		job->BrowserError = ARCHIVE_BERR_ARG;
		goto end;
	}

	//標準出力をパイプで受け取る (標準入力とエラー出力は NUL)
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if(CreatePipe(&hRead, &hWrite, &sa, 0) == FALSE){
		job->BrowserError = ARCHIVE_BERR_START;
		job->BrowserCode = GetLastError();
		goto end;
	}
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
	hNul = CreateFile("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
	if(hNul == INVALID_HANDLE_VALUE){
		job->BrowserError = ARCHIVE_BERR_START;
		job->BrowserCode = GetLastError();
		hNul = NULL;
		goto end;
	}
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = hNul;
	si.hStdOutput = hWrite;
	si.hStdError = hNul;
	ZeroMemory(&pi, sizeof(pi));
	hJob = CreateKillJob();
	//互換性レイヤーの指定を除いた環境変数で起動する (作成できない場合はそのまま引き継ぐ)
	env = MakeBrowserEnv();
	if(CreateProcess(NULL, cmd.p, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | ((env != NULL) ? CREATE_UNICODE_ENVIRONMENT : 0),
		env, NULL, &si, &pi) == FALSE){
		job->BrowserError = ARCHIVE_BERR_START;
		job->BrowserCode = GetLastError();
		goto end;
	}
	if(hJob != NULL){
		AssignProcessToJobObject(hJob, pi.hProcess);
	}
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	CloseHandle(hWrite);
	hWrite = NULL;

	//出力を読みながら終了を待つ (停止要求とタイムアウトで打ち切る)
	rb = (char *)HeapAlloc(GetProcessHeap(), 0, 65536);
	if(rb == NULL){
		goto end;
	}
	start = GetTickCount();
	limit = timeout + ARCHIVE_BROWSER_MARGIN;
	while(out.err == FALSE && killed == FALSE){
		avail = 0;
		if(PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) == FALSE){
			//出力が閉じられた (この後プロセスの終了を待つ)
			break;
		}
		if(avail > 0){
			dwRead = 0;
			if(ReadFile(hRead, rb, 65536, &dwRead, NULL) == FALSE || dwRead == 0){
				break;
			}
			if(out.len + (LONGLONG)dwRead > MAX_RECVSIZE){
				out.err = TRUE;
				break;
			}
			Buf_Add(&out, rb, (long)dwRead);
			continue;
		}
		if(exited == TRUE){
			break;
		}
		if(WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0){
			//終了後に残っている出力を読み切る
			exited = TRUE;
			continue;
		}
		if(IsJobStopped(job) == TRUE || GetTickCount() - start > limit){
			TerminateProcess(pi.hProcess, 1);
			killed = TRUE;
			break;
		}
		Sleep(ARCHIVE_BROWSER_POLL);
	}
	//プロセスの終了を待つ
	while(exited == FALSE && killed == FALSE && out.err == FALSE){
		if(WaitForSingleObject(pi.hProcess, ARCHIVE_BROWSER_POLL) == WAIT_OBJECT_0){
			exited = TRUE;
		}else if(IsJobStopped(job) == TRUE || GetTickCount() - start > limit){
			TerminateProcess(pi.hProcess, 1);
			killed = TRUE;
		}
	}
	code = 0;
	if(exited == TRUE){
		GetExitCodeProcess(pi.hProcess, &code);
	}
	if(out.err == TRUE){
		job->BrowserError = ARCHIVE_BERR_TOOLARGE;
	}else if(killed == TRUE){
		//停止の要求で終了させた場合は中止になるため理由は付けない
		job->BrowserError = (IsJobStopped(job) == TRUE) ? ARCHIVE_BERR_NONE : ARCHIVE_BERR_TIMEOUT;
	}else if(code != 0){
		job->BrowserError = ARCHIVE_BERR_EXIT;
		job->BrowserCode = code;
	}else if(out.len == 0){
		job->BrowserError = ARCHIVE_BERR_NOOUTPUT;
	}else if(exited == TRUE && IsJobStopped(job) == FALSE){
		*body = out.p;
		*len = out.len;
		out.p = NULL;
		ret = TRUE;
	}
	CloseHandle(pi.hProcess);

end:
	//残っている子プロセスを終了させてからプロファイルを削除する (削除できなかったものは次回に削除する)
	if(hJob != NULL) CloseHandle(hJob);
	if(hRead != NULL) CloseHandle(hRead);
	if(hWrite != NULL) CloseHandle(hWrite);
	if(hNul != NULL) CloseHandle(hNul);
	if(rb != NULL) HeapFree(GetProcessHeap(), 0, rb);
	if(env != NULL) HeapFree(GetProcessHeap(), 0, env);
	MemFree(proxy);
	MemFree(ua);
	Buf_Free(&cmd);
	Buf_Free(&out);
	for(i = 0; i < 20 && DeleteDirTree(dir) == FALSE; i++){
		Sleep(100);
	}
	return ret;
}


/**************************************************************************
	Resource index (res.dat)
**************************************************************************/

static void FreeEntries(ARCHIVE_ENTRY *e)
{
	ARCHIVE_ENTRY *n;

	for(; e != NULL; e = n){
		n = e->next;
		MemFree(e->url);
		MemFree(e->etag);
		MemFree(e->lastmod);
		MemFree(e->file);
		MemFree(e);
	}
}

static ARCHIVE_ENTRY *FindEntry(ARCHIVE_JOB *job, const char *url)
{
	ARCHIVE_ENTRY *e;

	for(e = job->Entries; e != NULL; e = e->next){
		if(lstrcmp(e->url, url) == 0){
			return e;
		}
	}
	return NULL;
}

/* リソースのファイル名か (16桁の16進 + 拡張子) */
static BOOL IsResName(const char *name)
{
	const char *p;
	int i;

	for(i = 0; i < ARCHIVE_KEYLEN; i++){
		if(!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))){
			return FALSE;
		}
	}
	if(name[ARCHIVE_KEYLEN] != '.'){
		return FALSE;
	}
	for(p = name + ARCHIVE_KEYLEN + 1, i = 0; *p != '\0'; p++, i++){
		if(!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) || i >= 4){
			return FALSE;
		}
	}
	return (i >= 2) ? TRUE : FALSE;
}

static void SetEntry(ARCHIVE_JOB *job, const char *url, const char *etag, const char *lastmod, const char *file)
{
	ARCHIVE_ENTRY *e;

	e = FindEntry(job, url);
	if(e == NULL){
		e = (ARCHIVE_ENTRY *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ARCHIVE_ENTRY));
		if(e == NULL || (e->url = ArchiveStrDup(url)) == NULL){
			MemFree(e);
			return;
		}
		e->next = job->Entries;
		job->Entries = e;
	}
	MemFree(e->etag);
	MemFree(e->lastmod);
	MemFree(e->file);
	e->etag = ArchiveStrDup(etag);
	e->lastmod = ArchiveStrDup(lastmod);
	e->file = ArchiveStrDup(file);
}

static void LoadEntries(ARCHIVE_JOB *job)
{
	char path[MAX_PATH];
	char *buf, *p, *line, *f[4];
	long len;
	int i;

	wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_RES_DAT);
	if((buf = ReadAll(path, ARCHIVE_MAX_DATFILE, &len)) == NULL){
		return;
	}
	for(p = buf; *p != '\0';){
		line = p;
		for(; *p != '\0' && *p != '\r' && *p != '\n'; p++);
		if(*p != '\0'){
			*(p++) = '\0';
		}
		for(; *p == '\r' || *p == '\n'; p++);
		f[0] = line;
		for(i = 1; i < 4; i++){
			f[i] = StrChr(f[i - 1], '\t');
			if(f[i] == NULL){
				break;
			}
			*(f[i]++) = '\0';
		}
		if(i < 4 || IsHttp(f[0]) == FALSE || IsResName(f[3]) == FALSE || lstrlen(f[0]) > ARCHIVE_URL_MAX ||
			lstrlen(f[1]) >= 256 || lstrlen(f[2]) >= 128){
			continue;
		}
		SetEntry(job, f[0], f[1], f[2], f[3]);
	}
	MemFree(buf);
}

static void SaveEntries(ARCHIVE_JOB *job)
{
	ARCHIVE_ENTRY *e;
	ARCHIVE_BUF b;
	char path[MAX_PATH];

	ZeroMemory(&b, sizeof(ARCHIVE_BUF));
	for(e = job->Entries; e != NULL; e = e->next){
		if(e->file == NULL){
			continue;
		}
		Buf_Str(&b, e->url);
		Buf_Str(&b, "\t");
		Buf_Str(&b, (e->etag != NULL) ? e->etag : "");
		Buf_Str(&b, "\t");
		Buf_Str(&b, (e->lastmod != NULL) ? e->lastmod : "");
		Buf_Str(&b, "\t");
		Buf_Str(&b, e->file);
		Buf_Str(&b, "\r\n");
	}
	wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_RES_DAT);
	if(b.err == FALSE){
		WriteAll(path, (b.p != NULL) ? b.p : "", b.len);
	}
	Buf_Free(&b);
}


/**************************************************************************
	Resources
**************************************************************************/

/* 保存するリソースの拡張子 (保存しない種類は NULL) */
static const char *ResExt(int type, ARCHIVE_RESP *r, const char *url)
{
	static const struct { const char *type; const char *ext; } img[] = {
		{"image/png", "png"}, {"image/apng", "png"}, {"image/jpeg", "jpg"}, {"image/jpg", "jpg"}, {"image/pjpeg", "jpg"},
		{"image/gif", "gif"}, {"image/webp", "webp"}, {"image/svg+xml", "svg"}, {"image/x-icon", "ico"},
		{"image/vnd.microsoft.icon", "ico"}, {"image/bmp", "bmp"}, {"image/avif", "avif"},
	};
	static const char *js[] = {
		"application/javascript", "text/javascript", "application/x-javascript", "application/ecmascript", "text/ecmascript",
	};
	int i;

	switch(type)
	{
	case RES_IMAGE:
		for(i = 0; i < sizeof(img) / sizeof(img[0]); i++){
			if(lstrcmp(r->Type, img[i].type) == 0){
				return img[i].ext;
			}
		}
		break;

	case RES_CSS:
		if(lstrcmp(r->Type, "text/css") == 0){
			return "css";
		}
		break;

	case RES_JS:
		for(i = 0; i < sizeof(js) / sizeof(js[0]); i++){
			if(lstrcmp(r->Type, js[i]) == 0){
				return "js";
			}
		}
		break;

	case RES_FRAME:
		//UTF-16 のページは書き換えられないため保存しない
		if((lstrcmp(r->Type, "text/html") == 0 || lstrcmp(r->Type, "application/xhtml+xml") == 0) &&
			!(r->Len >= 2 && (((unsigned char)r->Body[0] == 0xFF && (unsigned char)r->Body[1] == 0xFE) ||
			((unsigned char)r->Body[0] == 0xFE && (unsigned char)r->Body[1] == 0xFF)))){
			return "html";
		}
		break;
	}
	return NULL;
}

static const char *AddMemo(ARCHIVE_JOB *job, const char *url, int type, const char *file)
{
	ARCHIVE_MEMO *m;

	m = (ARCHIVE_MEMO *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ARCHIVE_MEMO));
	if(m == NULL || (m->url = ArchiveStrDup(url)) == NULL){
		MemFree(m);
		return NULL;
	}
	m->type = type;
	m->file = ArchiveStrDup(file);
	m->next = job->Memo;
	job->Memo = m;
	return m->file;
}

/******************************************************************************

	ResolveRes

	リソースを取得して保存し、ファイル名を返す (書き換え処理から呼ばれる)

******************************************************************************/

static const char *ResolveRes(void *ctx, const char *url, int type)
{
	ARCHIVE_JOB *job = (ARCHIVE_JOB *)ctx;
	ARCHIVE_MEMO *m;
	ARCHIVE_ENTRY *e;
	ARCHIVE_RESP r;
	char path[MAX_PATH];
	char name[ARCHIVE_KEYLEN + 8];
	char *data, *css = NULL;
	const char *ext, *file = NULL;
	long len;
	BOOL cond, rewrite;

	if(IsJobStopped(job) == TRUE || (type == RES_IMAGE && job->Image == FALSE) ||
		(type == RES_CSS && job->Css == FALSE) || (type == RES_JS && job->Js == FALSE) ||
		(type != RES_IMAGE && type != RES_CSS && type != RES_JS && type != RES_FRAME)){
		return NULL;
	}
	for(m = job->Memo; m != NULL; m = m->next){
		if(m->type == type && lstrcmp(m->url, url) == 0){
			return m->file;
		}
	}
	//深い iframe は元のURLのままにする (自身を読み込むページなどの無限の再帰を防ぐ)
	if(type == RES_FRAME && job->FrameDepth >= ARCHIVE_FRAME_DEPTH){
		return NULL;
	}
	if(job->ResCount >= ARCHIVE_MAX_RES || job->Total >= ARCHIVE_MAX_TOTAL){
		return NULL;
	}
	InterlockedIncrement(&job->ResCount);
	InterlockedExchange(&job->Phase, ARCHIVE_PHASE_RES);

	//前回取得したものがある場合は変更があったときのみ取得する
	//(CSS と iframe のページは中のURLを処理するため毎回取得)
	rewrite = (type == RES_CSS || type == RES_FRAME) ? TRUE : FALSE;
	e = FindEntry(job, url);
	cond = FALSE;
	if(rewrite == FALSE && e != NULL && e->file != NULL){
		wsprintf(path, "%s\\%s\\%s", job->Dir, ARCHIVE_RES_DIR, e->file);
		cond = FileExists(path);
	}
	if(ArchiveFetch(job, url, (cond == TRUE) ? e->lastmod : NULL, (cond == TRUE) ? e->etag : NULL,
		ARCHIVE_MAX_RESSIZE, &r) == FALSE){
		return AddMemo(job, url, type, NULL);
	}
	if(r.Status == 304 && cond == TRUE){
		file = e->file;
	}else if(r.Status == 200 && (ext = ResExt(type, &r, url)) != NULL){
		data = r.Body;
		len = r.Len;
		if(type == RES_CSS){
			//CSS内のURLを書き換える (深い @import は絶対URLにするのみ)
			job->CssDepth++;
			css = Archive_RewriteCss(r.Body, r.Len, r.FinalUrl, "",
				(job->CssDepth < ARCHIVE_CSS_DEPTH) ? ResolveRes : NULL, job, &len);
			job->CssDepth--;
			if(css == NULL){
				FreeResp(&r);
				return AddMemo(job, url, type, NULL);
			}
			data = css;
		}else if(type == RES_FRAME){
			//iframe のページの中のURLを書き換える (res 内に保存するためリソースは同じ場所)
			job->FrameDepth++;
			css = Archive_RewriteHtml(r.Body, r.Len, r.FinalUrl, r.Charset, job->Js, "", ResolveRes, job, &len);
			job->FrameDepth--;
			if(css == NULL || IsJobStopped(job) == TRUE){
				Archive_Free(css);
				FreeResp(&r);
				return AddMemo(job, url, type, NULL);
			}
			data = css;
		}
		HashHex(data, len, name, ARCHIVE_KEYLEN);
		lstrcat(name, ".");
		lstrcat(name, ext);
		wsprintf(path, "%s\\%s\\%s", job->Dir, ARCHIVE_RES_DIR, name);
		if(FileExists(path) == TRUE || WriteAll(path, data, len) == TRUE){
			if(type == RES_FRAME){
				SetZoneIdentifier(path, url);
			}
			InterlockedExchangeAdd64(&job->Total, len);
			SetEntry(job, url, (rewrite == TRUE) ? "" : r.ETag, (rewrite == TRUE) ? "" : r.LastMod, name);
			file = name;
		}
		Archive_Free(css);
	}
	file = AddMemo(job, url, type, file);
	FreeResp(&r);
	return file;
}


/**************************************************************************
	Generations
**************************************************************************/

/* 参照されているリソースの印を付ける */
static void MarkRefs(const char *data, long len, char **names, BOOL *mark, int cnt)
{
	const char *p, *end = data + len;
	int i, n;

	for(p = data; p + ARCHIVE_KEYLEN + 3 <= end; p++){
		for(n = 0; n < ARCHIVE_KEYLEN && p + n < end && ((p[n] >= '0' && p[n] <= '9') || (p[n] >= 'a' && p[n] <= 'f')); n++);
		if(n < ARCHIVE_KEYLEN || p[ARCHIVE_KEYLEN] != '.'){
			continue;
		}
		for(i = 0; i < cnt; i++){
			if(mark[i] == FALSE && lstrcmpn((char *)p, names[i], ARCHIVE_KEYLEN + 1) == 0){
				mark[i] = TRUE;
			}
		}
		p += ARCHIVE_KEYLEN;
	}
}

/* 使われていないリソースを削除する */
static void CleanResources(ARCHIVE_JOB *job, char **gens, int gcnt)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind;
	ARCHIVE_ENTRY *e;
	char path[MAX_PATH];
	char **names = NULL, **tmp;
	BOOL *mark = NULL, *scanned = NULL, again;
	char *buf;
	long len;
	int cnt = 0, cap = 0, i;

	wsprintf(path, "%s\\%s\\*", job->Dir, ARCHIVE_RES_DIR);
	hFind = FindFirstFile(path, &fd);
	if(hFind == INVALID_HANDLE_VALUE){
		return;
	}
	do{
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || IsResName(fd.cFileName) == FALSE){
			continue;
		}
		if(cnt >= cap){
			cap = (cap == 0) ? 64 : cap * 2;
			tmp = (names == NULL) ? (char **)HeapAlloc(GetProcessHeap(), 0, sizeof(char *) * cap) :
				(char **)HeapReAlloc(GetProcessHeap(), 0, names, sizeof(char *) * cap);
			if(tmp == NULL){
				break;
			}
			names = tmp;
		}
		if((names[cnt] = ArchiveStrDup(fd.cFileName)) != NULL){
			cnt++;
		}
	}while(FindNextFile(hFind, &fd) == TRUE);
	FindClose(hFind);
	if(cnt == 0){
		MemFree(names);
		return;
	}
	mark = (BOOL *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BOOL) * cnt);
	scanned = (BOOL *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BOOL) * cnt);
	if(mark == NULL || scanned == NULL){
		goto end;
	}
	//世代のページから参照されているもの
	for(i = 0; i < gcnt; i++){
		wsprintf(path, "%s\\%s", job->Dir, gens[i]);
		if((buf = ReadAll(path, MAX_RECVSIZE * 2, &len)) == NULL){
			//読めない場合は削除しない
			goto end;
		}
		MarkRefs(buf, len, names, mark, cnt);
		MemFree(buf);
	}
	//CSS と iframe のページから参照されているもの
	do{
		again = FALSE;
		for(i = 0; i < cnt; i++){
			if(mark[i] == TRUE && scanned[i] == FALSE){
				scanned[i] = TRUE;
				if(lstrcmpi(names[i] + ARCHIVE_KEYLEN, ".css") != 0 && lstrcmpi(names[i] + ARCHIVE_KEYLEN, ".html") != 0){
					continue;
				}
				wsprintf(path, "%s\\%s\\%s", job->Dir, ARCHIVE_RES_DIR, names[i]);
				if((buf = ReadAll(path, MAX_RECVSIZE * 2, &len)) == NULL){
					//読めない場合は中から参照しているものがわからないため削除しない
					goto end;
				}
				MarkRefs(buf, len, names, mark, cnt);
				MemFree(buf);
				again = TRUE;
			}
		}
	}while(again == TRUE);
	for(i = 0; i < cnt; i++){
		if(mark[i] == TRUE){
			continue;
		}
		wsprintf(path, "%s\\%s\\%s", job->Dir, ARCHIVE_RES_DIR, names[i]);
		DeleteFile(path);
		for(e = job->Entries; e != NULL; e = e->next){
			if(e->file != NULL && lstrcmp(e->file, names[i]) == 0){
				MemFree(e->file);
				e->file = NULL;
			}
		}
	}
end:
	MemFree(mark);
	MemFree(scanned);
	FreeList(names, cnt);
}

/* HTMLの特殊文字を変換して追加 (2バイト文字は対で追加し、末尾の先行バイトだけのものは捨てる) */
static void AddHtmlText(ARCHIVE_BUF *b, const char *s)
{
	for(; *s != '\0'; s++){
		if(IsDBCSLeadByte((BYTE)*s) == TRUE){
			if(*(s + 1) == '\0'){
				break;
			}
			Buf_Add(b, s, 2);
			s++;
			continue;
		}
		switch(*s)
		{
		case '&': Buf_Str(b, "&amp;"); break;
		case '<': Buf_Str(b, "&lt;"); break;
		case '>': Buf_Str(b, "&gt;"); break;
		case '"': Buf_Str(b, "&quot;"); break;
		default:
			if((unsigned char)*s >= 0x20 || *s == '\t'){
				Buf_Add(b, s, 1);
			}
			break;
		}
	}
}

/* UTF-8 に変換して書き込む */
static void WriteUtf8(const char *path, ARCHIVE_BUF *b)
{
	WCHAR *w;
	char *u;
	int len;

	if(b->err == TRUE || b->p == NULL || (w = WideDup(b->p)) == NULL){
		return;
	}
	len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if(len > 0 && (u = (char *)HeapAlloc(GetProcessHeap(), 0, len)) != NULL){
		WideCharToMultiByte(CP_UTF8, 0, w, -1, u, len, NULL, NULL);
		WriteAll(path, u, len - 1);
		MemFree(u);
	}
	MemFree(w);
}

/* 空白で区切られた文字列の n 番目の語 */
static void GetWord(const char *s, int n, char *ret, int size)
{
	int i;

	for(; n > 0 && *s != '\0'; s++){
		if(*s == ' '){
			n--;
		}
	}
	for(i = 0; *s != '\0' && *s != ' ' && i < size - 1; i++, s++){
		ret[i] = *s;
	}
	ret[i] = '\0';
}

/* YYYYMMDD-HHMMSS を表示用にする */
static void GenToDate(const char *gen, char *ret)
{
	wsprintf(ret, "%.4s/%.2s/%.2s %.2s:%.2s:%.2s", gen, gen + 4, gen + 6, gen + 9, gen + 11, gen + 13);
}

static int DaysInMonth(int y, int m)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)){
		return 29;
	}
	return days[m - 1];
}

/* 曜日 (0 が日曜) */
static int DayOfWeek(int y, int m, int d)
{
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	if(m < 3){
		y--;
	}
	return ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 7) % 7;
}

/* 年の一覧ページのファイル名 (最新の年は index.html) */
static void YearPageName(int y, BOOL latest, char *ret)
{
	if(latest == TRUE){
		lstrcpy(ret, ARCHIVE_INDEX);
	}else{
		wsprintf(ret, "%s%04d.html", ARCHIVE_INDEX_YEAR, y);
	}
}

/* 年の一覧ページのファイル名か (index-YYYY.html) */
static BOOL IsYearPageName(const char *name)
{
	int len = lstrlen(ARCHIVE_INDEX_YEAR);
	int i;

	if(lstrcmpni((char *)name, ARCHIVE_INDEX_YEAR, len) != 0){
		return FALSE;
	}
	for(i = 0; i < 4; i++){
		if(!(name[len + i] >= '0' && name[len + i] <= '9')){
			return FALSE;
		}
	}
	return (lstrcmpi(name + len + 4, ".html") == 0) ? TRUE : FALSE;
}

/* 一覧に無い年のページを削除する */
static void DeleteYearPages(ARCHIVE_JOB *job, const int *years, int ycnt)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind;
	char path[MAX_PATH];
	int i, y;

	wsprintf(path, "%s\\%s*.html", job->Dir, ARCHIVE_INDEX_YEAR);
	hFind = FindFirstFile(path, &fd);
	if(hFind == INVALID_HANDLE_VALUE){
		return;
	}
	do{
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || IsYearPageName(fd.cFileName) == FALSE){
			continue;
		}
		y = DigitsToInt(fd.cFileName + lstrlen(ARCHIVE_INDEX_YEAR), 4);
		for(i = 0; i < ycnt && years[i] != y; i++);
		if(i >= ycnt){
			wsprintf(path, "%s\\%s", job->Dir, fd.cFileName);
			DeleteFile(path);
		}
	}while(FindNextFile(hFind, &fd) == TRUE);
	FindClose(hFind);
}

/******************************************************************************

	WriteYearPage

	1年分のカレンダーのページを作成する (UTF-8)
	years[yi] の年のページで、その年の世代は gens の start から end の前まで (古い順)

******************************************************************************/

static void WriteYearPage(ARCHIVE_JOB *job, char **gens, int gcnt, const int *years, int ycnt, int yi, int start, int end)
{
	ARCHIVE_BUF b;
	char path[MAX_PATH], file[32], key[16], date[64], word[64];
	const char *title;
	int y, m, d, i, idx, dow, days;

	y = years[yi];
	title = (job->Title != NULL) ? job->Title : job->Url;
	ZeroMemory(&b, sizeof(ARCHIVE_BUF));
	Buf_Str(&b, "<!DOCTYPE html>\r\n<html><head><meta charset=\"utf-8\">\r\n"
		"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'\">\r\n<title>");
	Buf_Str(&b, STR_ARCHIVE_INDEX_TITLE);
	AddHtmlText(&b, title);
	wsprintf(date, " - %d", y);
	Buf_Str(&b, date);
	Buf_Str(&b, "</title>\r\n<style>"
		"body{font-family:sans-serif;margin:2em;color:#222}"
		"h1{font-size:1.4em;margin:0 0 .3em}"
		"p{margin:.3em 0}"
		"nav{margin:1.2em 0;font-size:1.1em;line-height:1.8}"
		"nav a,nav span{margin:0 .3em;text-decoration:none}"
		"nav .cur{font-weight:bold}"
		"nav .dis{color:#bbb}"
		".months{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:1em}"
		"table{border-collapse:collapse;width:100%;table-layout:fixed}"
		"caption{font-weight:bold;margin-bottom:.3em;text-align:left}"
		"th,td{border:1px solid #ddd;vertical-align:top;padding:2px;font-size:.8em;height:1.8em}"
		"th{background:#f3f3f3;font-weight:normal}"
		"th.sun{color:#c33}th.sat{color:#36c}"
		"td .d{display:block;color:#999}"
		"td.has{background:#eef6ff}td.has .d{color:#333}"
		"td a{display:block;white-space:nowrap;text-decoration:none}"
		"td a:hover{text-decoration:underline}"
		"td a.latest{font-weight:bold}"
		"</style>\r\n</head><body>\r\n<h1>");
	AddHtmlText(&b, title);
	Buf_Str(&b, "</h1>\r\n<p>");
	AddHtmlText(&b, job->Url);
	Buf_Str(&b, "</p>\r\n");
	//最新のアーカイブ
	GenToDate(gens[gcnt - 1], date);
	Buf_Str(&b, "<p class=\"latest\"><a href=\"");
	AddHtmlText(&b, gens[gcnt - 1]);
	Buf_Str(&b, "\">");
	AddHtmlText(&b, date);
	Buf_Str(&b, STR_ARCHIVE_INDEX_LATEST);
	Buf_Str(&b, "</a></p>\r\n");
	//年の切り替え (最初のアーカイブの年から最後のアーカイブの年まで)
	Buf_Str(&b, "<nav>");
	if(yi > 0){
		YearPageName(years[yi - 1], FALSE, file);
		Buf_Str(&b, "<a class=\"pn\" href=\"");
		AddHtmlText(&b, file);
		Buf_Str(&b, "\">&laquo;</a>");
	}else{
		Buf_Str(&b, "<span class=\"pn dis\">&laquo;</span>");
	}
	for(i = 0; i < ycnt; i++){
		wsprintf(date, "%d", years[i]);
		if(i == yi){
			Buf_Str(&b, "<span class=\"cur\">");
			Buf_Str(&b, date);
			Buf_Str(&b, "</span>");
		}else{
			YearPageName(years[i], (i == ycnt - 1) ? TRUE : FALSE, file);
			Buf_Str(&b, "<a href=\"");
			AddHtmlText(&b, file);
			Buf_Str(&b, "\">");
			Buf_Str(&b, date);
			Buf_Str(&b, "</a>");
		}
	}
	if(yi < ycnt - 1){
		YearPageName(years[yi + 1], (yi + 1 == ycnt - 1) ? TRUE : FALSE, file);
		Buf_Str(&b, "<a class=\"pn\" href=\"");
		AddHtmlText(&b, file);
		Buf_Str(&b, "\">&raquo;</a>");
	}else{
		Buf_Str(&b, "<span class=\"pn dis\">&raquo;</span>");
	}
	Buf_Str(&b, "</nav>\r\n<div class=\"months\">\r\n");
	//月ごとのカレンダー
	idx = start;
	for(m = 1; m <= 12; m++){
		GetWord(STR_ARCHIVE_INDEX_MONTHS, m - 1, word, sizeof(word));
		Buf_Str(&b, "<table><caption>");
		AddHtmlText(&b, word);
		Buf_Str(&b, "</caption>\r\n<tr>");
		for(i = 0; i < 7; i++){
			GetWord(STR_ARCHIVE_INDEX_WEEK, i, word, sizeof(word));
			Buf_Str(&b, (i == 0) ? "<th class=\"sun\">" : ((i == 6) ? "<th class=\"sat\">" : "<th>"));
			AddHtmlText(&b, word);
			Buf_Str(&b, "</th>");
		}
		Buf_Str(&b, "</tr>\r\n<tr>");
		dow = DayOfWeek(y, m, 1);
		for(i = 0; i < dow; i++){
			Buf_Str(&b, "<td></td>");
		}
		days = DaysInMonth(y, m);
		for(d = 1; d <= days; d++){
			wsprintf(key, "%04d%02d%02d", y, m, d);
			//世代は古い順のため、この日より前のものは読み飛ばす
			for(; idx < end && PrefixCmp(gens[idx], key, 8) < 0; idx++);
			if(idx < end && PrefixCmp(gens[idx], key, 8) == 0){
				wsprintf(date, "<td class=\"has\"><span class=\"d\">%d</span>", d);
				Buf_Str(&b, date);
				for(; idx < end && PrefixCmp(gens[idx], key, 8) == 0; idx++){
					GenToDate(gens[idx], date);
					Buf_Str(&b, (idx == gcnt - 1) ? "<a class=\"latest\" href=\"" : "<a href=\"");
					AddHtmlText(&b, gens[idx]);
					Buf_Str(&b, "\" title=\"");
					AddHtmlText(&b, date);
					Buf_Str(&b, "\">");
					Buf_Add(&b, date + 11, 5);
					Buf_Str(&b, "</a>");
				}
				Buf_Str(&b, "</td>");
			}else{
				wsprintf(date, "<td><span class=\"d\">%d</span></td>", d);
				Buf_Str(&b, date);
			}
			if((dow + d) % 7 == 0 && d < days){
				Buf_Str(&b, "</tr>\r\n<tr>");
			}
		}
		for(i = (dow + days) % 7; i != 0 && i < 7; i++){
			Buf_Str(&b, "<td></td>");
		}
		Buf_Str(&b, "</tr></table>\r\n");
	}
	Buf_Str(&b, "</div>\r\n</body></html>\r\n");
	YearPageName(y, (yi == ycnt - 1) ? TRUE : FALSE, file);
	wsprintf(path, "%s\\%s", job->Dir, file);
	WriteUtf8(path, &b);
	Buf_Free(&b);
}

/******************************************************************************

	WriteIndex

	世代の一覧ページを作成する
	1年ごとのカレンダーにし、最新の年を index.html、他の年を index-YYYY.html にする

******************************************************************************/

static void WriteIndex(ARCHIVE_JOB *job, char **gens, int gcnt)
{
	char path[MAX_PATH];
	int *years = NULL, *starts = NULL;
	int ycnt = 0, i, y;

	if(gcnt <= 1){
		wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_INDEX);
		DeleteFile(path);
		DeleteYearPages(job, NULL, 0);
		return;
	}
	//年ごとの範囲 (gens は古い順)
	for(i = 0, y = -1; i < gcnt; i++){
		if(DigitsToInt(gens[i], 4) != y){
			y = DigitsToInt(gens[i], 4);
			ycnt++;
		}
	}
	years = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int) * ycnt);
	starts = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int) * ycnt);
	if(years == NULL || starts == NULL){
		MemFree(years);
		MemFree(starts);
		return;
	}
	for(i = 0, y = -1, ycnt = 0; i < gcnt; i++){
		if(DigitsToInt(gens[i], 4) != y){
			y = DigitsToInt(gens[i], 4);
			years[ycnt] = y;
			starts[ycnt] = i;
			ycnt++;
		}
	}
	for(i = 0; i < ycnt; i++){
		WriteYearPage(job, gens, gcnt, years, ycnt, i, starts[i], (i + 1 < ycnt) ? starts[i + 1] : gcnt);
	}
	//最新の年は index.html のため、index-YYYY.html は残りの年の分だけにする
	DeleteYearPages(job, years, ycnt - 1);
	MemFree(years);
	MemFree(starts);
}

/* 新しい世代のファイル名 */
static BOOL NewGenName(ARCHIVE_JOB *job, const char *ext, char *ret)
{
	SYSTEMTIME st;
	char path[MAX_PATH];
	int i;

	GetLocalTime(&st);
	for(i = 1; i < 100; i++){
		if(i == 1){
			wsprintf(ret, "%04d%02d%02d-%02d%02d%02d.%s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ext);
		}else{
			wsprintf(ret, "%04d%02d%02d-%02d%02d%02d-%d.%s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, i, ext);
		}
		wsprintf(path, "%s\\%s", job->Dir, ret);
		if(FileExists(path) == FALSE){
			return TRUE;
		}
	}
	return FALSE;
}


/******************************************************************************

	RunJob

	ページを取得してアーカイブする (スレッドで実行)

******************************************************************************/

static void RunJob(ARCHIVE_JOB *job)
{
	ARCHIVE_RESP r;
	char path[MAX_PATH], gen[64];
	char md5[33], oldmd5[33], oldgen[64];
	char *out = NULL, *latest, *old, *p, *dom;
	const char *data;
	char **gens = NULL;
	const char *ext;
	long outlen = 0, oldlen, datalen, domlen;
	int gcnt = 0, i, deleted = 0;
	BOOL html, utf16, write = TRUE, found, replace = FALSE;

	ZeroMemory(&r, sizeof(ARCHIVE_RESP));
	job->Result = ARCHIVE_RESULT_ERROR;
	InterlockedExchange(&job->Phase, ARCHIVE_PHASE_PAGE);
	//Archive フォルダ、アイテムのフォルダ、リソースのフォルダの順に作成する
	lstrcpy(path, job->Dir);
	for(p = path + lstrlen(path); p > path && *p != '\\'; p--);
	*p = '\0';
	if(*path == '\0' || EnsureDir(path) == FALSE || EnsureDir(job->Dir) == FALSE){
		return;
	}
	wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_RES_DIR);
	if(EnsureDir(path) == FALSE){
		return;
	}
	//ディスクの空きが少ない場合は取得しない
	if(IsDiskFull(job->Dir) == TRUE){
		job->Result = ARCHIVE_RESULT_DISKFULL;
		return;
	}
	job->hSession = WinHttpOpen((job->UserAgent != NULL) ? job->UserAgent : USER_AGENT, (job->ProxyName != NULL) ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
		(job->ProxyName != NULL) ? job->ProxyName : WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(job->hSession == NULL){
		job->Result = ARCHIVE_RESULT_FAILED;
		return;
	}
	if(job->TimeOutMs > 0){
		WinHttpSetTimeouts(job->hSession, job->TimeOutMs, job->TimeOutMs, job->TimeOutMs, job->TimeOutMs);
	}
	LoadEntries(job);

	//ページの取得 (成功した場合のみアーカイブする)
	if(ArchiveFetch(job, job->Url, NULL, NULL, MAX_RECVSIZE, &r) == FALSE || r.Status != 200 || IsJobStopped(job) == TRUE){
		job->Result = ARCHIVE_RESULT_FAILED;
		job->Status = r.Status;
		goto end;
	}
	job->Status = r.Status;
	if(lstrcmp(r.Type, "text/html") == 0 || lstrcmp(r.Type, "application/xhtml+xml") == 0){
		html = TRUE;
		ext = "html";
	}else if(lstrcmp(r.Type, "text/plain") == 0){
		html = FALSE;
		ext = "txt";
	}else{
		job->Result = ARCHIVE_RESULT_NOTHTML;
		goto end;
	}
	//UTF-16 は書き換えずに保存する
	utf16 = (r.Len >= 2 && (((unsigned char)r.Body[0] == 0xFF && (unsigned char)r.Body[1] == 0xFE) ||
		((unsigned char)r.Body[0] == 0xFE && (unsigned char)r.Body[1] == 0xFF))) ? TRUE : FALSE;
	//ブラウザで表示した内容 (スクリプトの実行後の DOM、UTF-8) に置き換える
	//(ブラウザが無い場合や失敗した場合は取得したページをそのまま使う)
	if(html == TRUE && utf16 == FALSE && job->Browser == TRUE){
		BOOL got;
		DWORD tick;

		InterlockedExchange(&job->Phase, ARCHIVE_PHASE_BROWSER);
		tick = GetTickCount();
		got = BrowserFetch(job, r.FinalUrl, &dom, &domlen);
		//起動してすぐに出力なしで終了した場合は一度だけやり直す
		if(got == FALSE && IsJobStopped(job) == FALSE && GetTickCount() - tick < ARCHIVE_BROWSER_RETRY &&
			(job->BrowserError == ARCHIVE_BERR_NOOUTPUT || job->BrowserError == ARCHIVE_BERR_EXIT)){
			got = BrowserFetch(job, r.FinalUrl, &dom, &domlen);
		}
		//ブラウザではボット対策の確認ページになり、取得したページはそうでない場合は取得したページを使う
		//(確認ページを通り抜ける処理はしない)
		if(got == TRUE && IsBotCheckPage(dom, domlen) == TRUE && IsBotCheckPage(r.Body, r.Len) == FALSE){
			MemFree(dom);
			got = FALSE;
			job->BrowserError = ARCHIVE_BERR_BOTCHECK;
		}
		if(got == TRUE){
			MemFree(r.Body);
			r.Body = dom;
			r.Len = domlen;
			lstrcpy(r.Charset, "utf-8");
			InterlockedExchange(&job->BrowserState, ARCHIVE_BROWSER_USED);
		}else{
			InterlockedExchange(&job->BrowserState, ARCHIVE_BROWSER_FAILED);
		}
	}
	if(IsJobStopped(job) == TRUE){
		goto end;
	}
	HashHex(r.Body, r.Len, md5, 32);

	if(html == TRUE && utf16 == FALSE){
		out = Archive_RewriteHtml(r.Body, r.Len, r.FinalUrl, r.Charset, job->Js, ARCHIVE_RES_PREFIX, ResolveRes, job, &outlen);
		if(out == NULL || IsJobStopped(job) == TRUE){
			//このアイテムを中止した場合は取得した画像などのうち使われていないものを削除する (終了時は待たせないため削除しない)
			if(job->Cancel != 0 && ArchiveStop == 0){
				goto clean;
			}
			goto end;
		}
	}
	InterlockedExchange(&job->Phase, ARCHIVE_PHASE_SAVE);

	//前回の世代と比較する
	*oldmd5 = *oldgen = '\0';
	wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_LATEST);
	if((latest = ReadAll(path, 1024, NULL)) != NULL){
		if(lstrlen(latest) > 33 && latest[32] == '\t'){
			lstrcpyn(oldmd5, latest, 33);
			lstrcpyn(oldgen, latest + 33, sizeof(oldgen));
			for(p = oldgen; *p != '\0' && *p != '\r' && *p != '\n'; p++);
			*p = '\0';
		}
		MemFree(latest);
	}
	gcnt = ListGens(job->Dir, &gens);
	found = FALSE;
	for(i = 0; i < gcnt; i++){
		if(lstrcmpi(gens[i], oldgen) == 0){
			found = TRUE;
		}
	}
	data = (out != NULL) ? out : r.Body;
	datalen = (out != NULL) ? outlen : r.Len;
	if(gcnt > 0){
		//最新の世代と内容が同じ場合は保存しない (チェックで更新と判断しても内容が変わらないことがある)
		wsprintf(path, "%s\\%s", job->Dir, gens[gcnt - 1]);
		if((old = ReadAll(path, MAX_RECVSIZE * 2, &oldlen)) != NULL){
			if(oldlen == datalen && MemEqual(old, data, datalen) == TRUE){
				write = FALSE;
			}
			MemFree(old);
		}
	}
	if(write == TRUE){
		if(found == TRUE && IsGenName(oldgen) == TRUE && lstrcmp(oldmd5, md5) == 0){
			//ページは同じでリソースが変わった場合は同じ世代を置き換える
			lstrcpy(gen, oldgen);
			replace = TRUE;
		}else if(NewGenName(job, ext, gen) == FALSE){
			goto clean;
		}
		if(IsDiskFull(job->Dir) == TRUE){
			job->Result = ARCHIVE_RESULT_DISKFULL;
			goto clean;
		}
		wsprintf(path, "%s\\%s", job->Dir, gen);
		if(WriteAll(path, data, datalen) == FALSE){
			goto clean;
		}
		SetZoneIdentifier(path, job->Url);
		{
			char line[128];
			wsprintf(line, "%s\t%s\r\n", md5, gen);
			wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_LATEST);
			WriteAll(path, line, lstrlen(line));
		}
		job->Result = (replace == TRUE) ? ARCHIVE_RESULT_REPLACED : ARCHIVE_RESULT_SAVED;
	}else{
		job->Result = ARCHIVE_RESULT_SAME;
	}

	//世代数を超えたものを削除 (0 は上限なし)
	FreeList(gens, gcnt);
	gcnt = ListGens(job->Dir, &gens);
	if(job->Generation > ARCHIVE_GEN_UNLIMITED){
		for(i = 0; i < gcnt - job->Generation; i++){
			wsprintf(path, "%s\\%s", job->Dir, gens[i]);
			if(DeleteFile(path) == TRUE){
				deleted++;
			}
		}
		if(deleted > 0){
			FreeList(gens, gcnt);
			gcnt = ListGens(job->Dir, &gens);
		}
	}
	WriteIndex(job, gens, gcnt);
	//使われていないリソースの削除 (すべての世代のページを読むため、世代が多い場合は削除か置き換えがあったときのみ)
	if(deleted > 0 || replace == TRUE || gcnt <= ARCHIVE_CLEAN_GENS){
		CleanResources(job, gens, gcnt);
	}
	SaveEntries(job);

	//元のURL (確認用)
	{
		ARCHIVE_BUF b;
		ZeroMemory(&b, sizeof(ARCHIVE_BUF));
		Buf_Str(&b, job->Url);
		Buf_Str(&b, "\r\n");
		if(job->Title != NULL && IsSafeText(job->Title) == TRUE){
			Buf_Str(&b, job->Title);
			Buf_Str(&b, "\r\n");
		}
		wsprintf(path, "%s\\%s", job->Dir, ARCHIVE_URLFILE);
		if(b.err == FALSE){
			WriteAll(path, b.p, b.len);
		}
		Buf_Free(&b);
	}
	goto end;

clean:
	//ページを保存できなかった場合は、取得して置いたリソースのうち使われていないものを削除する
	FreeList(gens, gcnt);
	gcnt = ListGens(job->Dir, &gens);
	CleanResources(job, gens, gcnt);
	SaveEntries(job);
end:
	//保存する前に停止した場合は中止
	if(IsJobStopped(job) == TRUE && job->Result != ARCHIVE_RESULT_SAVED &&
		job->Result != ARCHIVE_RESULT_REPLACED && job->Result != ARCHIVE_RESULT_SAME){
		job->Result = ARCHIVE_RESULT_STOPPED;
	}
	FreeList(gens, gcnt);
	Archive_Free(out);
	FreeResp(&r);
	FreeEntries(job->Entries);
	job->Entries = NULL;
	WinHttpCloseHandle(job->hSession);
	job->hSession = NULL;
}


/**************************************************************************
	Queue
**************************************************************************/

static void FreeJob(ARCHIVE_JOB *job)
{
	ARCHIVE_MEMO *m, *n;

	for(m = job->Memo; m != NULL; m = n){
		n = m->next;
		MemFree(m->url);
		MemFree(m->file);
		MemFree(m);
	}
	FreeEntries(job->Entries);
	MemFree(job->Url);
	MemFree(job->Title);
	WideFree(job->ProxyName);
	WideFree(job->ProxyUser);
	WideFree(job->ProxyPass);
	WideFree(job->UserAgent);
	MemFree(job);
}

/* 履歴を解放 (ArchiveCs の中で呼ぶ) */
static void FreeHist(ARCHIVE_HIST *h)
{
	MemFree(h->Title);
	MemFree(h->Url);
	ZeroMemory(h, sizeof(ARCHIVE_HIST));
}

/******************************************************************************

	Archive_Cancel

	取得中のページを中止する (All が TRUE の場合は取得待ちもすべて取り消す)
	進捗状況のダイアログから呼ばれる (メインスレッド)

******************************************************************************/

static void CancelRunningJob(void);

void Archive_Cancel(BOOL All)
{
	ARCHIVE_JOB *job, *next = NULL;

	if(ArchiveCsInit == FALSE){
		return;
	}
	EnterCriticalSection(&ArchiveCs);
	CancelRunningJob();
	if(All == TRUE){
		next = ArchiveQueue;
		ArchiveQueue = NULL;
		ArchiveQueueCnt = 0;
	}
	InterlockedIncrement(&ArchiveSerial);
	LeaveCriticalSection(&ArchiveCs);
	for(job = next; job != NULL; job = next){
		next = job->next;
		FreeJob(job);
	}
}

/* 取得中のアイテムを中止する (ArchiveCs の中で呼ぶ、受信中のリクエストを閉じて待機中の受信を終わらせる) */
static void CancelRunningJob(void)
{
	HINTERNET h;

	if(ArchiveRunningJob == NULL){
		return;
	}
	InterlockedExchange(&ArchiveRunningJob->Cancel, 1);
	h = ArchiveRunningJob->ActiveReq;
	ArchiveRunningJob->ActiveReq = NULL;
	if(h != NULL){
		WinHttpCloseHandle(h);
	}
}

/* 取得を終えたアイテムを履歴に追加 (ArchiveCs の中で呼ぶ、古いものから削除する) */
static void AddHist(ARCHIVE_JOB *job)
{
	ARCHIVE_HIST *h;

	ArchiveHistTop = (ArchiveHistTop + 1) % ARCHIVE_HISTORY_MAX;
	h = &ArchiveHist[ArchiveHistTop];
	FreeHist(h);
	if(ArchiveHistCnt < ARCHIVE_HISTORY_MAX){
		ArchiveHistCnt++;
	}
	h->Id = ++ArchiveHistId;
	h->Title = ArchiveStrDup(job->Title);
	h->Url = ArchiveStrDup(job->Url);
	lstrcpy(h->Dir, job->Dir);
	h->Result = job->Result;
	h->Status = job->Status;
	h->Browser = job->BrowserState;
	h->BrowserError = job->BrowserError;
	h->BrowserCode = job->BrowserCode;
	h->ResCount = job->ResCount;
	h->Total = job->Total;
	h->Elapsed = GetTickCount() - job->StartTick;
	GetLocalTime(&h->Time);
}

static DWORD WINAPI ArchiveThreadProc(LPVOID lpParam)
{
	HMODULE hModule = (HMODULE)lpParam;
	ARCHIVE_JOB *job;

	while(ArchiveStop == 0){
		EnterCriticalSection(&ArchiveCs);
		job = ArchiveQueue;
		if(job != NULL){
			ArchiveQueue = job->next;
			ArchiveQueueCnt--;
			//取り出しと同時に処理中にする (終了時の待機で空と判定されないよう)
			InterlockedExchange(&ArchiveBusy, 1);
			lstrcpy(ArchiveRunningKey, job->Key);
			job->StartTick = GetTickCount();
			ArchiveRunningJob = job;
			InterlockedIncrement(&ArchiveSerial);
		}
		LeaveCriticalSection(&ArchiveCs);
		if(job == NULL){
			//終了通知の後は取得待ちが無くなった時点で終了する
			if(ArchiveEnding == TRUE){
				break;
			}
			WaitForSingleObject(ArchiveEvent, INFINITE);
			continue;
		}
		RunJob(job);
		EnterCriticalSection(&ArchiveCs);
		AddHist(job);
		ArchiveRunningJob = NULL;
		*ArchiveRunningKey = '\0';
		InterlockedIncrement(&ArchiveSerial);
		LeaveCriticalSection(&ArchiveCs);
		FreeJob(job);
		InterlockedExchange(&ArchiveBusy, 0);
	}
	FreeLibraryAndExitThread(hModule, 0);
	return 0;
}


/******************************************************************************

	Archive_Init

	初期化

******************************************************************************/

void Archive_Init(void)
{
	if(ArchiveCsInit == TRUE){
		return;
	}
	InitializeCriticalSection(&ArchiveCs);
	ArchiveEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	ArchiveCsInit = TRUE;
}


/******************************************************************************

	Archive_End

	終了の通知 (待たない)
	新しい要求を受け付けないようにし、取得中のものはスレッドが続行して取得待ちが
	無くなった時点で終了する。完了を待つのは Archive_EndWait で行う

******************************************************************************/

void Archive_End(void)
{
	if(ArchiveCsInit == FALSE){
		return;
	}
	InterlockedExchange(&ArchiveEnding, 1);
	Archive_Reload();
}


/******************************************************************************

	Archive_EndWait

	終了前にアーカイブの完了を待つ (取得待ちのものを一定時間まで処理し、残りは中止する)
	ウィンドウを閉じた後に呼ばれるため待っても画面は止まらない

******************************************************************************/

void Archive_EndWait(void)
{
	ARCHIVE_JOB *job, *next;

	DWORD start;
	BOOL empty;

	if(ArchiveCsInit == FALSE){
		return;
	}
	InterlockedExchange(&ArchiveEnding, 1);
	//チェック後に終了する場合などのため、取得待ちのものを一定時間まで処理する
	if(ArchiveThread != NULL){
		start = GetTickCount();
		while(GetTickCount() - start < ARCHIVE_FINISH_WAIT){
			EnterCriticalSection(&ArchiveCs);
			empty = (ArchiveQueue == NULL) ? TRUE : FALSE;
			LeaveCriticalSection(&ArchiveCs);
			if(empty == TRUE && ArchiveBusy == 0){
				break;
			}
			if(WaitForSingleObject(ArchiveThread, 100) != WAIT_TIMEOUT){
				break;
			}
		}
	}
	InterlockedExchange(&ArchiveStop, 1);
	EnterCriticalSection(&ArchiveCs);
	//受信中のリクエストを閉じて、タイムアウトを待たずにスレッドを終了させる
	CancelRunningJob();
	job = ArchiveQueue;
	ArchiveQueue = NULL;
	ArchiveQueueCnt = 0;
	InterlockedIncrement(&ArchiveSerial);
	LeaveCriticalSection(&ArchiveCs);
	for(; job != NULL; job = next){
		next = job->next;
		FreeJob(job);
	}
	if(ArchiveThread != NULL){
		SetEvent(ArchiveEvent);
		//スレッドはDLLの参照を持っているため、待機時間内に終わらない場合もDLLは解放されない
		if(WaitForSingleObject(ArchiveThread, ARCHIVE_END_WAIT) != WAIT_TIMEOUT){
			CloseHandle(ArchiveThread);
			ArchiveThread = NULL;
		}
	}
	ArchiveRestart = FALSE;
}


/******************************************************************************

	Archive_Reload

	プロトコルの再読み込み (設定の変更) の通知
	取得中の場合はスレッドが DLL の参照を持ったまま続行し、待機中の場合だけ
	スレッドを終了して DLL を解放できるようにする (終了まで待たないため UI は止まらない)

******************************************************************************/

void Archive_Reload(void)
{
	BOOL idle;

	if(ArchiveCsInit == FALSE || ArchiveThread == NULL){
		return;
	}
	EnterCriticalSection(&ArchiveCs);
	idle = (ArchiveQueue == NULL && ArchiveBusy == 0) ? TRUE : FALSE;
	LeaveCriticalSection(&ArchiveCs);
	if(idle == FALSE){
		return;
	}
	InterlockedExchange(&ArchiveStop, 1);
	SetEvent(ArchiveEvent);
	if(WaitForSingleObject(ArchiveThread, ARCHIVE_END_WAIT) != WAIT_TIMEOUT){
		CloseHandle(ArchiveThread);
		ArchiveThread = NULL;
	}
	//DLL が解放されなかった場合は次の Archive_Restart で再開する
	ArchiveRestart = TRUE;
}


/******************************************************************************

	ResumeArchive

	停止したアーカイブの取得を再開する (スレッドが終了していない場合は再開しない)

******************************************************************************/

static BOOL ResumeArchive(void)
{
	if(ArchiveStop == 0){
		return TRUE;
	}
	if(ArchiveRestart == FALSE){
		return FALSE;
	}
	if(ArchiveThread != NULL){
		if(WaitForSingleObject(ArchiveThread, 0) == WAIT_TIMEOUT){
			return FALSE;
		}
		CloseHandle(ArchiveThread);
		ArchiveThread = NULL;
	}
	EnterCriticalSection(&ArchiveCs);
	*ArchiveRunningKey = '\0';
	LeaveCriticalSection(&ArchiveCs);
	InterlockedExchange(&ArchiveBusy, 0);
	InterlockedExchange(&ArchiveStop, 0);
	InterlockedExchange(&ArchiveEnding, 0);
	ArchiveRestart = FALSE;
	return TRUE;
}


/******************************************************************************

	Archive_Restart

	プロトコルの再読み込みで終了通知の後に呼ばれた場合にアーカイブの取得を再開する

******************************************************************************/

void Archive_Restart(void)
{
	if(ArchiveCsInit == FALSE){
		return;
	}
	if(ArchiveStop == 0){
		//終了通知の後に読み込み直された場合は要求を受け付ける (スレッドは続行している)
		InterlockedExchange(&ArchiveEnding, 0);
		return;
	}
	ArchiveRestart = TRUE;
	ResumeArchive();
}


/******************************************************************************

	Archive_GetGeneration

	保存する世代数 (0 は上限なし、未設定は 1)

******************************************************************************/

int Archive_GetGeneration(const char *Option2)
{
	char buf[BUFSIZE];
	int gen;

	if(GetOptionString((char *)Option2, buf, OP2_ARCHIVE_GEN) == FALSE || !(*buf >= '0' && *buf <= '9')){
		return 1;
	}
	gen = atoi(buf);
	return (gen < ARCHIVE_GEN_UNLIMITED) ? 1 : ((gen > ARCHIVE_GEN_MAX) ? ARCHIVE_GEN_MAX : gen);
}


/******************************************************************************

	Archive_IsAuthItem

	認証を設定したアイテムか (認証が必要なページはアーカイブしない)

******************************************************************************/

BOOL Archive_IsAuthItem(struct TPITEM *tpItemInfo)
{
	char *sv, *user;
	BOOL ret = FALSE;

	if(tpItemInfo == NULL){
		return FALSE;
	}
	if(GetOptionInt(tpItemInfo->Option2, OP2_USEPASS) == 1){
		return TRUE;
	}
	if(tpItemInfo->CheckURL == NULL){
		return FALSE;
	}
	//URL にユーザ名を含む場合
	sv = (char *)GlobalAlloc(GPTR, lstrlen(tpItemInfo->CheckURL) + 1);
	user = (char *)GlobalAlloc(GPTR, lstrlen(tpItemInfo->CheckURL) + 1);
	if(sv == NULL || user == NULL){
		ret = TRUE;
	}else if(GetURL(tpItemInfo->CheckURL, sv, NULL, 80, user, NULL) != -1 && *user != '\0'){
		ret = TRUE;
	}
	if(sv != NULL) GlobalFree(sv);
	if(user != NULL) GlobalFree(user);
	return ret;
}

/* アイテムのプロキシ */
static void GetProxy(struct TPITEM *tpItemInfo, ARCHIVE_JOB *job)
{
	char ItemProxy[BUFSIZE], ItemPort[BUFSIZE], sv[BUFSIZE], proxy[BUFSIZE * 2];
	char *px = NULL;
	int pt = 0, port, flag;

	flag = GetOptionInt(tpItemInfo->Option2, OP2_NOPROXY);
	if(flag == 0 && GetOptionInt(tpItemInfo->Option2, OP2_SETPROXY) == 1){
		GetOptionString(tpItemInfo->Option2, ItemProxy, OP2_PROXY);
		GetOptionString(tpItemInfo->Option2, ItemPort, OP2_PORT);
		px = ItemProxy;
		pt = atoi(ItemPort);
	}else if(flag == 0 && Proxy == 1){
		px = pServer;
		pt = pPort;
	}
	if(px == NULL || *px == '\0'){
		return;
	}
	port = GetURL(px, sv, NULL, pt, NULL, NULL);
	if(port == -1){
		lstrcpyn(sv, px, BUFSIZE);
		port = pt;
	}
	wsprintf(proxy, "http://%s:%d", sv, port);
	job->ProxyName = WideDup(proxy);
	if(pUsePass == 1){
		job->ProxyUser = WideDup(pUser);
		job->ProxyPass = WideDup(pPass);
	}
}

/* アーカイブしたページがあるか */
static BOOL HasPage(const char *dir)
{
	char **gens;
	int cnt;

	cnt = ListGens(dir, &gens);
	FreeList(gens, cnt);
	return (cnt > 0) ? TRUE : FALSE;
}


/******************************************************************************

	Archive_Request

	チェックに成功したページのアーカイブを要求する (メインスレッドで実行)

******************************************************************************/

void Archive_Request(HWND hWnd, struct TPITEM *tpItemInfo, BOOL Update)
{
	ARCHIVE_JOB *job, *j;
	char dir[MAX_PATH];
	char key[ARCHIVE_KEYLEN + 1];
	char ua[BUFSIZE];
	HMODULE hModule;
	DWORD id;

	if(ArchiveGlobal == 0 || ArchiveCsInit == FALSE || ArchiveEnding != 0 || ResumeArchive() == FALSE ||
		tpItemInfo == NULL || tpItemInfo->Param3 != 0 ||
		GetOptionInt(tpItemInfo->Option2, OP2_NOARCHIVE) == 1 || Archive_IsAuthItem(tpItemInfo) == TRUE ||
		IsWebURL(tpItemInfo->CheckURL) == FALSE || lstrlen(tpItemInfo->CheckURL) > ARCHIVE_URL_MAX){
		return;
	}
	if(GetItemDir(hWnd, tpItemInfo->CheckURL, dir, key) == FALSE){
		return;
	}
	//更新が無い場合はアーカイブが無いときのみ
	if(Update == FALSE && HasPage(dir) == TRUE){
		return;
	}

	EnterCriticalSection(&ArchiveCs);
	for(j = ArchiveQueue; j != NULL; j = j->next){
		if(lstrcmp(j->Key, key) == 0){
			break;
		}
	}
	LeaveCriticalSection(&ArchiveCs);
	if(j != NULL || ArchiveQueueCnt >= ARCHIVE_MAX_QUEUE){
		return;
	}

	job = (ARCHIVE_JOB *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ARCHIVE_JOB));
	if(job == NULL){
		return;
	}
	job->Url = ArchiveStrDup(tpItemInfo->CheckURL);
	job->Title = ArchiveStrDup(tpItemInfo->Title);
	lstrcpy(job->Dir, dir);
	lstrcpy(job->Key, key);
	job->Image = (GetOptionInt(tpItemInfo->Option2, OP2_ARCHIVE_NOIMAGE) == 0) ? TRUE : FALSE;
	job->Css = (GetOptionInt(tpItemInfo->Option2, OP2_ARCHIVE_NOCSS) == 0) ? TRUE : FALSE;
	job->Js = (GetOptionInt(tpItemInfo->Option2, OP2_ARCHIVE_JS) == 1) ? TRUE : FALSE;
	job->Browser = (GetOptionInt(tpItemInfo->Option2, OP2_ARCHIVE_BROWSER) == 1) ? TRUE : FALSE;
	job->Generation = Archive_GetGeneration(tpItemInfo->Option2);
	job->TimeOutMs = (TimeOut > 0) ? TimeOut * 1000 : 0;
	GetProxy(tpItemInfo, job);
	//チェックと同じ User-Agent を使う
	if(GetOptionString(tpItemInfo->Option2, ua, OP2_USERAGENT) == TRUE){
		NormalizeUserAgent(ua);
		if(*ua != '\0'){
			job->UserAgent = WideDup(ua);
		}
	}
	if(job->Url == NULL){
		FreeJob(job);
		return;
	}

	EnterCriticalSection(&ArchiveCs);
	job->next = NULL;
	if(ArchiveQueue == NULL){
		ArchiveQueue = job;
	}else{
		for(j = ArchiveQueue; j->next != NULL; j = j->next);
		j->next = job;
	}
	ArchiveQueueCnt++;
	InterlockedIncrement(&ArchiveSerial);
	LeaveCriticalSection(&ArchiveCs);

	if(ArchiveThread == NULL){
		//スレッドの実行中にDLLが解放されないよう参照を取得する
		if(GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)ArchiveThreadProc, &hModule) == FALSE){
			return;
		}
		ArchiveThread = CreateThread(NULL, 0, ArchiveThreadProc, (LPVOID)hModule, 0, &id);
		if(ArchiveThread == NULL){
			FreeLibrary(hModule);
			return;
		}
		SetThreadPriority(ArchiveThread, THREAD_PRIORITY_BELOW_NORMAL);
	}
	SetEvent(ArchiveEvent);
}


/******************************************************************************

	Archive_Exists

	アイテムのアーカイブがあるか

******************************************************************************/

BOOL Archive_Exists(HWND hWnd, struct TPITEM *tpItemInfo)
{
	char dir[MAX_PATH];

	if(tpItemInfo == NULL || GetItemDir(hWnd, tpItemInfo->CheckURL, dir, NULL) == FALSE){
		return FALSE;
	}
	return HasPage(dir);
}


/* ディレクトリ内のファイルを削除する (サブディレクトリは削除しない) */
static void DeleteFiles(const char *dir)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind;
	char path[MAX_PATH];

	if(lstrlen(dir) + 3 >= MAX_PATH){
		return;
	}
	wsprintf(path, "%s\\*", dir);
	hFind = FindFirstFile(path, &fd);
	if(hFind == INVALID_HANDLE_VALUE){
		return;
	}
	do{
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || lstrlen(dir) + 1 + lstrlen(fd.cFileName) >= MAX_PATH){
			continue;
		}
		wsprintf(path, "%s\\%s", dir, fd.cFileName);
		if((fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0){
			SetFileAttributes(path, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
		}
		DeleteFile(path);
	}while(FindNextFile(hFind, &fd) == TRUE);
	FindClose(hFind);
}


/******************************************************************************

	Archive_Delete

	アイテムのアーカイブを削除する
	(戻り値 1: 削除した 0: アーカイブが無い -1: 取得中 -2: 削除できなかった)

******************************************************************************/

int Archive_Delete(HWND hWnd, struct TPITEM *tpItemInfo)
{
	ARCHIVE_JOB *job, *prev, *next, *removed = NULL;
	char dir[MAX_PATH], path[MAX_PATH];
	char key[ARCHIVE_KEYLEN + 1];
	DWORD attr;

	if(tpItemInfo == NULL || GetItemDir(hWnd, tpItemInfo->CheckURL, dir, key) == FALSE){
		return 0;
	}
	attr = GetFileAttributes(dir);
	if(attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0){
		return 0;
	}
	if(ArchiveCsInit == TRUE){
		EnterCriticalSection(&ArchiveCs);
		//取得中のものは削除しない
		if(lstrcmp(ArchiveRunningKey, key) == 0){
			LeaveCriticalSection(&ArchiveCs);
			return -1;
		}
		//取得待ちのものは取り消す
		for(prev = NULL, job = ArchiveQueue; job != NULL; job = next){
			next = job->next;
			if(lstrcmp(job->Key, key) == 0){
				if(prev == NULL){
					ArchiveQueue = next;
				}else{
					prev->next = next;
				}
				ArchiveQueueCnt--;
				InterlockedIncrement(&ArchiveSerial);
				job->next = removed;
				removed = job;
			}else{
				prev = job;
			}
		}
		LeaveCriticalSection(&ArchiveCs);
		for(job = removed; job != NULL; job = next){
			next = job->next;
			FreeJob(job);
		}
	}

	//リソース、ページの順に削除する (作成したもの以外のサブディレクトリは削除しない)
	wsprintf(path, "%s\\%s", dir, ARCHIVE_RES_DIR);
	DeleteFiles(path);
	RemoveDirectory(path);
	DeleteFiles(dir);
	RemoveDirectory(dir);
	return (GetFileAttributes(dir) == INVALID_FILE_ATTRIBUTES) ? 1 : -2;
}


/******************************************************************************

	Archive_Open

	アイテムのアーカイブを開く

******************************************************************************/

int Archive_Open(HWND hWnd, struct TPITEM *tpItemInfo)
{
	char dir[MAX_PATH];

	if(tpItemInfo == NULL || GetItemDir(hWnd, tpItemInfo->CheckURL, dir, NULL) == FALSE){
		MessageBox(hWnd, STR_ERR_MSG_ARCHIVE_NONE, STR_ERR_TITLE_ARCHIVE, MB_ICONEXCLAMATION);
		return -1;
	}
	return Archive_OpenDir(hWnd, dir);
}


/******************************************************************************

	Archive_OpenDir

	アイテムのフォルダのアーカイブを開く (複数の世代がある場合は一覧を開く)

******************************************************************************/

int Archive_OpenDir(HWND hWnd, const char *dir)
{
	char path[MAX_PATH];
	char **gens;
	int cnt, rc;

	if(dir == NULL || *dir == '\0' || (cnt = ListGens(dir, &gens)) <= 0){
		MessageBox(hWnd, STR_ERR_MSG_ARCHIVE_NONE, STR_ERR_TITLE_ARCHIVE, MB_ICONEXCLAMATION);
		return -1;
	}
	wsprintf(path, "%s\\%s", dir, ARCHIVE_INDEX);
	if(cnt <= 1 || FileExists(path) == FALSE){
		wsprintf(path, "%s\\%s", dir, gens[cnt - 1]);
	}
	FreeList(gens, cnt);
	rc = ExecItem(hWnd, path, NULL);
	return (rc == -1) ? -1 : 0;
}


/******************************************************************************

	Archive_GetStatus

	進捗の表示用に取得中のページと、取得待ちと履歴 (Serial から変わった場合のみ) をコピーする
	Archive_FreeStatus で解放する

******************************************************************************/

static void CopyJobStatus(ARCHIVE_JOB *job, ARCHIVE_STATUS_ITEM *s)
{
	s->Title = ArchiveStrDup(job->Title);
	s->Url = ArchiveStrDup(job->Url);
	lstrcpy(s->Dir, job->Dir);
	s->Phase = job->Phase;
	s->Cancel = (job->Cancel != 0) ? TRUE : FALSE;
	s->Browser = job->BrowserState;
	s->ResCount = job->ResCount;
	s->Total = InterlockedCompareExchange64((LONGLONG volatile *)&job->Total, 0, 0);
	s->Elapsed = GetTickCount() - job->StartTick;
}

void Archive_GetStatus(ARCHIVE_STATUS *st, LONG Serial)
{
	ARCHIVE_JOB *job;
	ARCHIVE_HIST *h;
	int i, n;

	ZeroMemory(st, sizeof(ARCHIVE_STATUS));
	st->Enabled = (ArchiveGlobal != 0) ? TRUE : FALSE;
	if(ArchiveCsInit == FALSE){
		return;
	}
	EnterCriticalSection(&ArchiveCs);
	st->Serial = ArchiveSerial;
	if(ArchiveRunningJob != NULL){
		st->Running = TRUE;
		CopyJobStatus(ArchiveRunningJob, &st->Now);
	}
	if(st->Serial != Serial){
		st->Lists = TRUE;
		if(ArchiveQueueCnt > 0){
			st->Queue = (ARCHIVE_STATUS_ITEM *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ARCHIVE_STATUS_ITEM) * ArchiveQueueCnt);
			if(st->Queue != NULL){
				for(n = 0, job = ArchiveQueue; job != NULL && n < ArchiveQueueCnt; job = job->next, n++){
					st->Queue[n].Title = ArchiveStrDup(job->Title);
					st->Queue[n].Url = ArchiveStrDup(job->Url);
					lstrcpy(st->Queue[n].Dir, job->Dir);
				}
				st->QueueCnt = n;
			}
		}
		if(ArchiveHistCnt > 0){
			st->History = (ARCHIVE_STATUS_ITEM *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ARCHIVE_STATUS_ITEM) * ArchiveHistCnt);
			if(st->History != NULL){
				for(i = 0; i < ArchiveHistCnt; i++){
					h = &ArchiveHist[(ArchiveHistTop - i + ARCHIVE_HISTORY_MAX) % ARCHIVE_HISTORY_MAX];
					st->History[i].Id = h->Id;
					st->History[i].Title = ArchiveStrDup(h->Title);
					st->History[i].Url = ArchiveStrDup(h->Url);
					lstrcpy(st->History[i].Dir, h->Dir);
					st->History[i].Result = h->Result;
					st->History[i].Status = h->Status;
					st->History[i].Browser = h->Browser;
					st->History[i].BrowserError = h->BrowserError;
					st->History[i].BrowserCode = h->BrowserCode;
					st->History[i].ResCount = h->ResCount;
					st->History[i].Total = h->Total;
					st->History[i].Elapsed = h->Elapsed;
					st->History[i].Time = h->Time;
				}
				st->HistoryCnt = ArchiveHistCnt;
			}
		}
	}
	LeaveCriticalSection(&ArchiveCs);
}


/******************************************************************************

	Archive_FreeStatus

	Archive_GetStatus で作成した情報を解放する

******************************************************************************/

static void FreeStatusItems(ARCHIVE_STATUS_ITEM *items, int cnt)
{
	int i;

	if(items == NULL){
		return;
	}
	for(i = 0; i < cnt; i++){
		MemFree(items[i].Title);
		MemFree(items[i].Url);
	}
	MemFree(items);
}

void Archive_FreeStatus(ARCHIVE_STATUS *st)
{
	MemFree(st->Now.Title);
	MemFree(st->Now.Url);
	FreeStatusItems(st->Queue, st->QueueCnt);
	FreeStatusItems(st->History, st->HistoryCnt);
	ZeroMemory(st, sizeof(ARCHIVE_STATUS));
}


/******************************************************************************

	Archive_ClearHistory

	進捗の表示の履歴を消去する

******************************************************************************/

void Archive_ClearHistory(void)
{
	int i;

	if(ArchiveCsInit == FALSE){
		return;
	}
	EnterCriticalSection(&ArchiveCs);
	for(i = 0; i < ARCHIVE_HISTORY_MAX; i++){
		FreeHist(&ArchiveHist[i]);
	}
	ArchiveHistCnt = 0;
	ArchiveHistTop = 0;
	InterlockedIncrement(&ArchiveSerial);
	LeaveCriticalSection(&ArchiveCs);
}
/* End of source */
