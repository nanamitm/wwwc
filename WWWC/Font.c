/**************************************************************************

	WWWC

	Font.c

	Copyright (C) 1996-2003 by Nakashima Tomoaki. All rights reserved.
		http://www.nakka.com/
		nakka@nakka.com

**************************************************************************/

/**************************************************************************
	Include Files
**************************************************************************/

#define _INC_OLE
#include <windows.h>
#undef	_INC_OLE

#include <commctrl.h>

#include "General.h"


/******************************************************************************

	CreateListFont

	フォントを作成する

******************************************************************************/

HFONT CreateListFont(char *FontName, int FontSize, int CharSet)
{
	LOGFONT lf;

	ZeroMemory(&lf, sizeof(LOGFONT));

	//メインウィンドウの DPI で作成する
	lf.lfHeight = -MulDiv(FontSize, g_Dpi, 72);

	lf.lfWidth = 0;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfWeight = 0;
	lf.lfItalic = FALSE;
	lf.lfUnderline = FALSE;
	lf.lfStrikeOut = FALSE;
	//リストビューとツリービューはフォントの文字セットのコードページで文字列を変換する
	//ため、DEFAULT_CHARSET (プロセスのコードページ = UTF-8) にする
	lf.lfCharSet = DEFAULT_CHARSET;
	(void)CharSet;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	lstrcpy(lf.lfFaceName, FontName);
	return CreateFontIndirect((CONST LOGFONT *)&lf);
}


/******************************************************************************

	ダイアログのリストビューとツリービューのフォント

	リストビューとツリービュー (comctl32) は、ANSI の文字列をフォントの文字セットの
	コードページで変換する。ダイアログのフォントは日本語の文字セット (Shift_JIS) の
	ため、UTF-8 の文字列が化ける。WM_INITDIALOG の前に、文字セットを DEFAULT_CHARSET
	にしたフォントに差し替える (プロセスのコードページ = UTF-8 で変換される)。

******************************************************************************/

static HHOOK hCodePageHook = NULL;

static LRESULT CALLBACK CodePageFontSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	if(uMsg == WM_NCDESTROY){
		RemoveWindowSubclass(hWnd, CodePageFontSubclass, uIdSubclass);
		DeleteObject((HFONT)dwRefData);
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static BOOL CALLBACK SetCodePageFontProc(HWND hWnd, LPARAM lParam)
{
	char cls[32];
	LOGFONT lf;
	HFONT hFont;

	(void)lParam;
	if(GetClassName(hWnd, cls, sizeof(cls)) == 0 ||
		(lstrcmpi(cls, WC_LISTVIEWA) != 0 && lstrcmpi(cls, WC_TREEVIEWA) != 0)){
		return TRUE;
	}
	hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
	if(hFont == NULL || GetObject(hFont, sizeof(LOGFONT), &lf) == 0 || lf.lfCharSet == DEFAULT_CHARSET){
		return TRUE;
	}
	lf.lfCharSet = DEFAULT_CHARSET;
	hFont = CreateFontIndirect(&lf);
	if(hFont == NULL){
		return TRUE;
	}
	if(SetWindowSubclass(hWnd, CodePageFontSubclass, 1, (DWORD_PTR)hFont) == FALSE){
		DeleteObject(hFont);
		return TRUE;
	}
	SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
	return TRUE;
}

static LRESULT CALLBACK CodePageHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if(nCode == HC_ACTION && ((CWPSTRUCT *)lParam)->message == WM_INITDIALOG){
		EnumChildWindows(((CWPSTRUCT *)lParam)->hwnd, SetCodePageFontProc, 0);
	}
	return CallNextHookEx(hCodePageHook, nCode, wParam, lParam);
}

void Font_InstallDialogHook(void)
{
	if(hCodePageHook == NULL){
		hCodePageHook = SetWindowsHookEx(WH_CALLWNDPROC, CodePageHookProc, NULL, GetCurrentThreadId());
	}
}

void Font_UninstallDialogHook(void)
{
	if(hCodePageHook != NULL){
		UnhookWindowsHookEx(hCodePageHook);
		hCodePageHook = NULL;
	}
}
/* End of source */
