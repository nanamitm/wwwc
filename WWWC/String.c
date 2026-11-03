/**************************************************************************

	WWWC

	String.c

	Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
		http://www.nakka.com/
		nakka@nakka.com

**************************************************************************/


/**************************************************************************
	Include Files
**************************************************************************/

#define _INC_OLE
#include <windows.h>
#undef	_INC_OLE

#include "General.h"


/**************************************************************************
	Define
**************************************************************************/

#define to_lower(c)				((c >= TEXT('A') && c <= TEXT('Z')) ? (c - TEXT('A') + TEXT('a')) : c)

//リソースの文字列のID (resource.h の IDS_*) の範囲
#define RES_STR_BASE			50000
#define RES_STR_CNT				1000

/**************************************************************************
	Global Variables
**************************************************************************/

extern HINSTANCE g_hinst;
extern char DateFormat[];
extern char TimeFormat[];

static LANGID ResLangID;
static char *ResStrList[RES_STR_CNT];


/******************************************************************************

	AllocCopy

	バッファを確保して文字列のコピー

******************************************************************************/

char *AllocCopy(char *buf)
{
	char *ret;

	if(buf == NULL){
		return NULL;
	}
	ret = (char *)GlobalAlloc(GMEM_FIXED, lstrlen(buf) + 1);
	if(ret == NULL){
		return NULL;
	}
	lstrcpy(ret, buf);
	return ret;
}


/******************************************************************************

	Trim

	文字列の前後の空白, Tabを除去する

******************************************************************************/

BOOL Trim(char *buf)
{
	char *p, *r;

	//前後の空白を除いたポインタを取得
	for(p = buf;(*p == ' ' || *p == '\t') && *p != '\0';p++);
	for(r = buf + lstrlen(buf) - 1; r > p && (*r == ' ' || *r == '\t'); r--);
	*(r + 1) = '\0';

	//元の文字列にコピーを行う
	MoveMemory(buf, p, lstrlen(p) + 1);
	return TRUE;
}


/******************************************************************************

	iStrCpy

	文字列をコピーして最後の文字のアドレスを返す

******************************************************************************/

char *iStrCpy(char *ret, char *buf)
{
	if(buf == NULL){
		return ret;
	}
	while(*buf != '\0'){
		*(ret++) = *(buf++);
	}
	*ret = '\0';
	return ret;
}


/******************************************************************************

	lstrcmpn / lstrcmpni

	2つの文字列を先頭から len バイトまで比較する (lstrcmpni は ASCII の大文字と小文字を区別しない)
	HTML やヘッダの解析、識別子の比較に使うため、言語に依存しない比較にする
	(以前は CompareString を使っていた)

******************************************************************************/

int lstrcmpn(char *buf1, char *buf2, int len)
{
	return strncmp(buf1, buf2, (size_t)len);
}

int lstrcmpni(char *buf1, char *buf2, int len)
{
	return _strnicmp(buf1, buf2, (size_t)len);
}


/******************************************************************************

	IsTerminatedMem

	メモリ内の文字列がNULで終端されているかチェックする

******************************************************************************/

BOOL IsTerminatedMem(HANDLE hMem, char *buf)
{
	SIZE_T size, i;

	if(hMem == NULL || buf == NULL){
		return FALSE;
	}
	size = GlobalSize(hMem);
	for(i = 0; i < size; i++){
		if(*(buf + i) == '\0'){
			return TRUE;
		}
	}
	return FALSE;
}


/******************************************************************************

	str_match

	2つの文字列をワイルドカード(*)を使って比較を行う

******************************************************************************/

BOOL str_match(const TCHAR *ptn, const TCHAR *str)
{
	if (ptn == NULL || str == NULL) {
		return FALSE;
	}
	switch (*ptn) {
	case TEXT('\0'):
		return (*str == TEXT('\0'));
	case TEXT('*'):
		if (*(ptn + 1) == TEXT('\0')) {
			return TRUE;
		}
		if (str_match(ptn + 1, str) == TRUE) {
			return TRUE;
		}
		while (*str != TEXT('\0')) {
			str++;
			if (str_match(ptn + 1, str) == TRUE) {
				return TRUE;
			}
		}
		return FALSE;
	case TEXT('?'):
		return (*str != TEXT('\0')) && str_match(ptn + 1, str + 1);
	default:
		while (to_lower(*ptn) == to_lower(*str)) {
			if (*ptn == TEXT('\0')) {
				return TRUE;
			}
			ptn++;
			str++;
			if (*ptn == TEXT('*') || *ptn == TEXT('?')) {
				return str_match(ptn, str);
			}
		}
		return FALSE;
	}
}


/******************************************************************************

	strlistcmp

	フォーマット内の複数の文字列に指定の文字列が含まれているか調べる
	(フォーマットにはワイルドカード使用可)
	 Sep は区切り文字

******************************************************************************/

BOOL strlistcmp(char *buf, char *Format, char Sep)
{
	char *p, *r;
	char *tmp;

	if(*Format == '\0'){
		return TRUE;
	}

	tmp = (char *)GlobalAlloc(GPTR, lstrlen(Format) + 1);
	if(tmp == NULL){
		return FALSE;
	}

	p = Format;
	r = tmp;
	while(1){
		//区切り文字か文字列の終端だった場合
		if(*p == '\0' || *p == Sep){
			*r = '\0';

			Trim(tmp);

			if(str_match(tmp, buf) == TRUE){
				GlobalFree(tmp);
				return TRUE;
			}

			if(*p == '\0' || *(p++) == '\0'){
				GlobalFree(tmp);
				return FALSE;
			}

			//次の文字列を取得
			r = tmp;
		}else{
			*(r++) = *(p++);
		}
	}
}


/******************************************************************************

	FileNameCheck

	ファイル名にできない文字列が含まれていないかチェックを行う

******************************************************************************/

int FileNameCheck(char *buf)
{
	static const char *reserved[] = {
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
	};
	char base[8];
	char *p;
	int len, i;

	//空の名前、"." や ".." などの末尾が '.' または ' ' の名前は使用できない
	len = lstrlen(buf);
	if(len == 0 || *(buf + len - 1) == '.' || *(buf + len - 1) == ' '){
		return FALSE;
	}
	//デバイス名 (拡張子の有無にかかわらず) は使用できない
	for(p = buf, i = 0; *p != '\0' && *p != '.' && i < sizeof(base) - 1; p++, i++){
		base[i] = *p;
	}
	base[i] = '\0';
	if(*p == '\0' || *p == '.'){
		for(i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++){
			if(lstrcmpi(base, reserved[i]) == 0){
				return FALSE;
			}
		}
	}

	p = buf;

	while(*p != '\0'){
		if(IS_LEAD_BYTE(p)){
			//２バイトコードの場合
			p++;

		}else{
			//ファイル名にできない文字の場合は偽を返す
			if(*p == '\\' ||
				*p == '/' ||
				*p == ':' ||
				*p == ',' ||
				*p == ';' ||
				*p == '*' ||
				*p == '?' ||
				*p == '\"' ||
				*p == '<' ||
				*p == '>' ||
				*p == '|'){

				return FALSE;
			}
		}
		p++;
	}
	return TRUE;
}


/******************************************************************************

	FileNameConv

	ファイル名にできない文字を変換する

******************************************************************************/

void FileNameConv(char *buf, char NewChar)
{
	char *p;

	p = buf;

	while(*p != '\0'){
		if(IS_LEAD_BYTE(p)){
			//２バイトコードの場合
			p++;

		}else{
			//ファイル名にできない文字は指定の文字に変換
			if(*p == '\\' ||
				*p == '/' ||
				*p == ':' ||
				*p == ',' ||
				*p == ';' ||
				*p == '*' ||
				*p == '?' ||
				*p == '\"' ||
				*p == '<' ||
				*p == '>' ||
				*p == '|'){

				*p = NewChar;
			}
		}
		p++;
	}
}


/******************************************************************************

	EscToCode

	エスケープ文字をコードに変換

******************************************************************************/

void EscToCode(char *buf)
{
	char *p, *r;

	for(p = buf, r = buf; *p != '\0'; p++){
		if(IS_LEAD_BYTE(p)){
			*(r++) = *(p++);
			*(r++) = *p;
		}else if(*p == '\\' && *(p + 1) == 'n'){
			*(r++) = '\r';
			*(r++) = '\n';
			p++;
		}else if(*p == '\\' && *(p + 1) == '\\'){
			*(r++) = '\\';
			p++;
		}else{
			*(r++) = *p;
		}
	}
	*r = '\0';
}


/******************************************************************************

	CodeToEsc

	コードをエスケープ文字に変換

******************************************************************************/

char *CodeToEsc(char *buf)
{
	char *ret;
	char *p, *r;

	ret = (char *)GlobalAlloc(GPTR, (lstrlen(buf) * 2) + 1);
	if(ret == NULL){
		return NULL;
	}
	//改行、タブを変換
	for(p = buf, r = ret ;*p != '\0'; p++){
		if(IS_LEAD_BYTE(p)){
			*(r++) = *(p++);
			*(r++) = *p;
			continue;
		}
		switch(*p)
		{
		case '\r':
			break;

		case '\n':
			*(r++) = '\\';
			*(r++) = 'n';
			break;

		case '\t':
			*(r++) = ' ';
			break;

		case '\\':
			*(r++) = '\\';
			*(r++) = '\\';
			break;

		default:
			*(r++) = *p;
			break;
		}
	}
	*r = '\0';
	return ret;
}


/******************************************************************************

	GetNumString

	数字文字のみを取得する

******************************************************************************/

void GetNumString(char *buf, char *ret)
{
	char *p, *r;

	//数字文字のみを戻り値のバッファに設定
	for(p = buf, r = ret;*p != '\0';p++){
		if(IS_LEAD_BYTE(p)){
			//２バイトコードの場合
			p++;

		}else if(*p >= '0' && *p <= '9'){
			*(r++) = *p;
		}
	}
	*r = '\0';
}


/******************************************************************************

	GetDateTime

	現在の日時を指定のフォーマットで作成する

******************************************************************************/

void GetDateTime(char *fDay, char *fTime)
{
	char *p;

	//フォーマットが設定されてない場合はシステムのフォーマットを使用する
	//日付と時刻を連結して BUFSIZE に収まる長さにする
	p = (*DateFormat == '\0') ? NULL : DateFormat;
	GetDateFormat(0, 0, NULL, p, fDay, BUFSIZE / 2 - 1);

	p = (*TimeFormat == '\0') ? NULL : TimeFormat;
	GetTimeFormat(0, 0, NULL, p, fTime, BUFSIZE / 2 - 1);
}


/******************************************************************************

	GetIndexToString

	インデックスより情報を取得する

******************************************************************************/

char *GetIndexToString(struct TPITEM *tpItemInfo, int Index)
{
	char *p = NULL;

	switch(Index)
	{
	case 0:
		p = tpItemInfo->Title;
		break;

	case 1:
		p = tpItemInfo->CheckURL;
		break;

	case 2:
		p = tpItemInfo->Size;
		break;

	case 3:
		p = tpItemInfo->Date;
		break;

	case 4:
		p = tpItemInfo->CheckDate;
		break;

	case 5:
		p = tpItemInfo->OldSize;
		break;

	case 6:
		p = tpItemInfo->OldDate;
		break;

	case 7:
		p = tpItemInfo->ViewURL;
		break;

	case 8:
		p = tpItemInfo->Option1;
		break;

	case 9:
		p = tpItemInfo->Option2;
		break;

	case 10:
		p = tpItemInfo->Comment;
		break;

	case 12:
		p = tpItemInfo->ErrStatus;
		break;

	case 13:
		p = tpItemInfo->DLLData1;
		break;

	case 14:
		p = tpItemInfo->DLLData2;
		break;
	}
	return ((p != NULL) ? p : "");
}

/******************************************************************************

	TextMemToUnicode

	文字列 (UTF-8) のメモリを CF_UNICODETEXT のメモリに変換する (元のメモリは解放する)
	CF_TEXT は他のアプリケーションではシステムの文字コードとして扱われるため、
	クリップボードとドラッグ＆ドロップでは CF_UNICODETEXT を使う

******************************************************************************/

HANDLE TextMemToUnicode(HANDLE hText)
{
	HANDLE hMem;
	char *buf;
	WCHAR *wbuf;
	int len;

	if(hText == NULL){
		return NULL;
	}
	if((buf = GlobalLock(hText)) == NULL){
		GlobalFree(hText);
		return NULL;
	}
	len = MultiByteToWideChar(CP_ACP, 0, buf, -1, NULL, 0);
	hMem = GlobalAlloc(GHND, sizeof(WCHAR) * (len + 1));
	if(hMem != NULL){
		if((wbuf = GlobalLock(hMem)) != NULL){
			MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, len);
			GlobalUnlock(hMem);
		}
	}
	GlobalUnlock(hText);
	GlobalFree(hText);
	return hMem;
}


/******************************************************************************

	UnicodeMemToText

	CF_UNICODETEXT のメモリから文字列 (UTF-8) を作成する (戻り値は GlobalFree で解放)

******************************************************************************/

char *UnicodeMemToText(HANDLE hMem)
{
	WCHAR *wbuf;
	char *buf;
	int len;

	if(hMem == NULL || (wbuf = GlobalLock(hMem)) == NULL){
		return NULL;
	}
	{
		SIZE_T count = GlobalSize(hMem) / sizeof(WCHAR), n;
		for(n = 0; n < count && wbuf[n]; n++);
		if(n == count){ GlobalUnlock(hMem); return NULL; }
	}
	len = WideCharToMultiByte(CP_ACP, 0, wbuf, -1, NULL, 0, NULL, NULL);
	buf = (char *)GlobalAlloc(GPTR, len + 1);
	if(buf != NULL){
		WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, len, NULL, NULL);
	}
	GlobalUnlock(hMem);
	return buf;
}

/******************************************************************************

	CopyDisplayText

	表示用に文字列を size バイト (終端を含む) までコピーする
	収まらない場合は UTF-8 の文字の途中で切らない

******************************************************************************/

void CopyDisplayText(char *ret, const char *buf, int size)
{
	int len, i, need;

	if(size <= 0){
		return;
	}
	lstrcpyn(ret, (buf != NULL) ? buf : "", size);
	len = lstrlen(ret);
	if(buf == NULL || buf[len] == '\0'){
		return;
	}
	//切り詰めた場合は末尾の不完全な文字を除く
	for(i = len; i > 0 && ((unsigned char)ret[i - 1] & 0xC0) == 0x80; i--);
	if(i > 0 && (unsigned char)ret[i - 1] >= 0xC0){
		need = ((unsigned char)ret[i - 1] >= 0xF0) ? 4 : (((unsigned char)ret[i - 1] >= 0xE0) ? 3 : 2);
		if(len - (i - 1) < need){
			ret[i - 1] = '\0';
		}
	}
}

/******************************************************************************

	PathCompare

	パスやフォルダ名の比較 (0 で一致)
	ファイルシステムと同じく大文字と小文字を区別せず、言語 (ロケール) に依存しない。
	(lstrcmpi は言語に依存した比較のため、無視される文字があったり、トルコ語の
	 I と i のように大文字と小文字の対応が異なったりする)

******************************************************************************/

int PathCompare(const char *buf1, const char *buf2)
{
	WCHAR w1[BUFSIZE], w2[BUFSIZE];
	int ret;

	if(MultiByteToWideChar(CP_ACP, 0, buf1, -1, w1, BUFSIZE) == 0 ||
		MultiByteToWideChar(CP_ACP, 0, buf2, -1, w2, BUFSIZE) == 0){
		return strcmp(buf1, buf2);
	}
	ret = CompareStringOrdinal(w1, -1, w2, -1, TRUE);
	return (ret == 0) ? strcmp(buf1, buf2) : ret - CSTR_EQUAL;
}


/******************************************************************************

	SetResLanguage

	リソースの言語を設定 (lang が "ja" と "en" 以外の場合は Windows の UI 言語)

******************************************************************************/

void SetResLanguage(char *lang)
{
	LANGID langid;
	int i;

	if(lang != NULL && lstrcmpni(lang, "ja", 2) == 0){
		langid = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
	}else if(lang != NULL && lstrcmpni(lang, "en", 2) == 0){
		langid = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}else if(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE){
		langid = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
	}else{
		//日本語以外の環境では英語のリソースを使用する
		langid = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}
	if(langid == ResLangID){
		return;
	}
	//ダイアログとメニューはスレッドの UI 言語のリソースが使用される
	SetThreadUILanguage(langid);
	ResLangID = langid;

	//取得済みの文字列は参照されている可能性があるため解放せずに取得し直す
	for(i = 0; i < RES_STR_CNT; i++){
		ResStrList[i] = NULL;
	}
}


/******************************************************************************

	LoadResString

	設定した言語の文字列リソースを読み込む (戻り値は GlobalFree で解放)

******************************************************************************/

static char *LoadResString(UINT id)
{
	HRSRC hRes;
	HGLOBAL hMem;
	WCHAR *p, *end;
	char *ret;
	int len, size;
	UINT i;

	//文字列は 16 個ずつのブロックに格納されている
	hRes = FindResourceEx(g_hinst, RT_STRING, MAKEINTRESOURCE(id / 16 + 1), ResLangID);
	if(hRes == NULL){
		hRes = FindResource(g_hinst, MAKEINTRESOURCE(id / 16 + 1), RT_STRING);
	}
	if(hRes == NULL || (hMem = LoadResource(g_hinst, hRes)) == NULL ||
		(p = (WCHAR *)LockResource(hMem)) == NULL){
		return NULL;
	}
	end = p + SizeofResource(g_hinst, hRes) / sizeof(WCHAR);

	//ブロック内は文字数と文字列 (終端なし) の並び
	for(i = 0; i < id % 16 && p < end; i++){
		p += *p + 1;
	}
	if(p >= end || *p >= end - p){
		return NULL;
	}
	len = *(p++);
	size = (len > 0) ? WideCharToMultiByte(CP_ACP, 0, p, len, NULL, 0, NULL, NULL) : 0;
	ret = (char *)GlobalAlloc(GMEM_FIXED, size + 1);
	if(ret == NULL){
		return NULL;
	}
	if(size > 0){
		WideCharToMultiByte(CP_ACP, 0, p, len, ret, size, NULL, NULL);
	}
	*(ret + size) = '\0';
	return ret;
}


/******************************************************************************

	GetResStr

	リソースの文字列を取得 (取得した文字列は終了まで保持する)

******************************************************************************/

char *GetResStr(UINT id)
{
	if(id < RES_STR_BASE || id >= RES_STR_BASE + RES_STR_CNT){
		return "";
	}
	if(ResStrList[id - RES_STR_BASE] == NULL){
		ResStrList[id - RES_STR_BASE] = LoadResString(id);
		if(ResStrList[id - RES_STR_BASE] == NULL){
			return "";
		}
	}
	return ResStrList[id - RES_STR_BASE];
}
/* End of source */