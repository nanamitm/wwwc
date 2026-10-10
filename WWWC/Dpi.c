/**************************************************************************

	WWWC

	Dpi.c

	高 DPI への対応 (Per-Monitor V2)
	サイズの設定 (アイコン、ツリーの幅、カラムの幅など) は 96 DPI での値として
	保存し、表示するときに DPI に合わせて拡大する。

**************************************************************************/


/**************************************************************************
	Include Files
**************************************************************************/

#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <commctrl.h>

#include "General.h"
#include "Dpi.h"


/**************************************************************************
	Global Variables
**************************************************************************/

int g_Dpi = USER_DEFAULT_SCREEN_DPI;		//メインウィンドウの DPI

//アイコンのサイズ (96 DPI での値、user.ini に保存する値)
int TvIconSizeBase = SICONSIZE;
int LvLIconSizeBase = LICONSIZE;
int LvSIconSizeBase = SICONSIZE;

//外部参照
extern HWND WWWCWnd;
extern int TvIconSize;
extern int LvLIconSize;
extern int LvSIconSize;


/******************************************************************************

	DpiScale / DpiUnscale

	96 DPI での値とメインウィンドウの DPI での値の変換

******************************************************************************/

int DpiScale(int v)
{
	return MulDiv(v, g_Dpi, USER_DEFAULT_SCREEN_DPI);
}

int DpiUnscale(int v)
{
	return MulDiv(v, USER_DEFAULT_SCREEN_DPI, g_Dpi);
}


/******************************************************************************

	Dpi_ForWindow

	ウィンドウの DPI
	メインウィンドウとその子ウィンドウは g_Dpi (WM_DPICHANGED で受け取った値)

******************************************************************************/

int Dpi_ForWindow(HWND hWnd)
{
	UINT dpi;

	if(hWnd != NULL && WWWCWnd != NULL && (hWnd == WWWCWnd || IsChild(WWWCWnd, hWnd) == TRUE)){
		return g_Dpi;
	}
	dpi = (hWnd != NULL) ? GetDpiForWindow(hWnd) : 0;
	return (dpi != 0) ? (int)dpi : USER_DEFAULT_SCREEN_DPI;
}

int DpiScaleFor(HWND hWnd, int v)
{
	return MulDiv(v, Dpi_ForWindow(hWnd), USER_DEFAULT_SCREEN_DPI);
}

int DpiUnscaleFor(HWND hWnd, int v)
{
	return MulDiv(v, USER_DEFAULT_SCREEN_DPI, Dpi_ForWindow(hWnd));
}


/******************************************************************************

	Dpi_SetIconSizeBase / Dpi_ApplyIconSizes

	設定から読んだアイコンのサイズを 96 DPI での値として保存し、
	TvIconSize などを DPI に合わせたサイズにする

******************************************************************************/

void Dpi_SetIconSizeBase(void)
{
	TvIconSizeBase = TvIconSize;
	LvLIconSizeBase = LvLIconSize;
	LvSIconSizeBase = LvSIconSize;
	Dpi_ApplyIconSizes();
}

void Dpi_ApplyIconSizes(void)
{
	TvIconSize = DpiScale(TvIconSizeBase);
	LvLIconSize = DpiScale(LvLIconSizeBase);
	LvSIconSize = DpiScale(LvSIconSizeBase);
}


/******************************************************************************

	Dpi_CreateDefaultFont

	フォントが設定されていない場合にツリーとリストで使うフォント
	(アイコンのタイトルのフォント)。システムの DPI では NULL (コントロールの既定)

******************************************************************************/

HFONT Dpi_CreateDefaultFont(void)
{
	LOGFONTW lf;

	if((UINT)g_Dpi == GetDpiForSystem()){
		return NULL;
	}
	//SystemParametersInfoForDpi は LOGFONTW のみ
	if(SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(LOGFONTW), &lf, 0, g_Dpi) == FALSE){
		return NULL;
	}
	return CreateFontIndirectW(&lf);
}


/******************************************************************************

	Dpi_SetStatusBarFont

	ステータスバーのフォントを DPI に合わせる。システムの DPI では既定のフォント

******************************************************************************/

void Dpi_SetStatusBarFont(HWND hStatus)
{
	static HFONT StatusFont = NULL;
	NONCLIENTMETRICSW ncm;
	HFONT hFont = NULL;

	if(hStatus == NULL){
		return;
	}
	if((UINT)g_Dpi != GetDpiForSystem()){
		ZeroMemory(&ncm, sizeof(NONCLIENTMETRICSW));
		ncm.cbSize = sizeof(NONCLIENTMETRICSW);
		if(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0, g_Dpi) == TRUE){
			hFont = CreateFontIndirectW(&ncm.lfStatusFont);
		}
	}
	if(hFont == NULL && StatusFont == NULL){
		return;
	}
	SendMessage(hStatus, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
	if(StatusFont != NULL){
		DeleteObject(StatusFont);
	}
	StatusFont = hFont;
}


/******************************************************************************

	Dpi_ScaleToolbar

	ツールバーのボタンの画像を DPI に合わせて拡大する
	(ツールバーの画像は 16x16 のビットマップしかないため拡大して使う)

******************************************************************************/

void Dpi_ScaleToolbar(HWND hToolbar)
{
	HIMAGELIST src, dst;
	HICON hIcon, hScaled;
	int size, cnt, i;

	if(hToolbar == NULL || g_Dpi == USER_DEFAULT_SCREEN_DPI){
		return;
	}
	src = (HIMAGELIST)SendMessage(hToolbar, TB_GETIMAGELIST, 0, 0);
	if(src == NULL || (cnt = ImageList_GetImageCount(src)) <= 0){
		return;
	}
	size = DpiScale(16);				//ToolBar.c の TBICONSIZE_X
	dst = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, cnt, 0);
	if(dst == NULL){
		return;
	}
	for(i = 0; i < cnt; i++){
		hIcon = ImageList_GetIcon(src, i, ILD_NORMAL);
		hScaled = (hIcon != NULL) ? (HICON)CopyImage(hIcon, IMAGE_ICON, size, size, 0) : NULL;
		ImageList_AddIcon(dst, (hScaled != NULL) ? hScaled : hIcon);
		if(hScaled != NULL) DestroyIcon(hScaled);
		if(hIcon != NULL) DestroyIcon(hIcon);
	}
	//元のイメージリストはツールバーのもの (ツールバーを破棄するときに解放される)
	SendMessage(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)dst);
	SendMessage(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(size + DpiScale(7), size + DpiScale(6)));
	SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);
}
/* End of source */
