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
#include "DarkMode.h"


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

static HFONT StatusFont = NULL;

void Dpi_SetStatusBarFont(HWND hStatus)
{
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

	Dpi_Uninitialize

	終了処理 (ステータスバーを破棄した後に呼ぶ)

******************************************************************************/

void Dpi_Uninitialize(void)
{
	if(StatusFont != NULL){
		DeleteObject(StatusFont);
		StatusFont = NULL;
	}
}


/******************************************************************************

	ツールバーの画像の拡大

	ツールバーの画像は 16x16 のドット絵しかないため、輪郭を保って拡大する
	Scale2x / Scale3x で 2 倍・3 倍 (それ以上は組み合わせ) にしてから、
	目的のサイズに面積平均で縮小する。画素は 0xAARRGGBB (乗算済みでない)。

******************************************************************************/

#define TB_SRC_SIZE			16				//ToolBar.c の TBICONSIZE_X

//Scale2x / Scale3x (https://www.scale2x.it/algorithm)
static DWORD *ScaleNx(const DWORD *src, int w, int n)
{
	DWORD *dst;
	DWORD A, B, C, D, E, F, G, H, I;
	DWORD o[9];
	int x, y, dx, dy, dw = w * n;

	dst = (DWORD *)LocalAlloc(LMEM_FIXED, sizeof(DWORD) * dw * dw);
	if(dst == NULL){
		return NULL;
	}
	for(y = 0; y < w; y++){
		const int ym = (y > 0) ? y - 1 : y;
		const int yp = (y < w - 1) ? y + 1 : y;
		for(x = 0; x < w; x++){
			const int xm = (x > 0) ? x - 1 : x;
			const int xp = (x < w - 1) ? x + 1 : x;
			A = src[ym * w + xm]; B = src[ym * w + x]; C = src[ym * w + xp];
			D = src[y * w + xm];  E = src[y * w + x];  F = src[y * w + xp];
			G = src[yp * w + xm]; H = src[yp * w + x]; I = src[yp * w + xp];
			if(B == H || D == F){
				o[0] = o[1] = o[2] = o[3] = o[4] = o[5] = o[6] = o[7] = o[8] = E;
			}else if(n == 2){
				o[0] = (D == B) ? D : E;
				o[1] = (B == F) ? F : E;
				o[2] = (D == H) ? D : E;
				o[3] = (H == F) ? F : E;
			}else{
				o[0] = (D == B) ? D : E;
				o[1] = ((D == B && E != C) || (B == F && E != A)) ? B : E;
				o[2] = (B == F) ? F : E;
				o[3] = ((D == B && E != G) || (D == H && E != A)) ? D : E;
				o[4] = E;
				o[5] = ((B == F && E != I) || (H == F && E != C)) ? F : E;
				o[6] = (D == H) ? D : E;
				o[7] = ((D == H && E != I) || (H == F && E != G)) ? H : E;
				o[8] = (H == F) ? F : E;
			}
			for(dy = 0; dy < n; dy++){
				for(dx = 0; dx < n; dx++){
					dst[(y * n + dy) * dw + x * n + dx] = o[dy * n + dx];
				}
			}
		}
	}
	return dst;
}

//面積平均で縮小して乗算済みの ARGB で書き込む (イメージリストに追加する 32bit の DIB)
static void Downscale(const DWORD *src, int sw, DWORD *dst, int dstride, int dw)
{
	const double r = (double)sw / dw;
	int x, y, sx, sy;

	for(y = 0; y < dw; y++){
		const double y0 = y * r, y1 = (y + 1) * r;
		for(x = 0; x < dw; x++){
			const double x0 = x * r, x1 = (x + 1) * r;
			double a = 0, cr = 0, cg = 0, cb = 0;
			for(sy = (int)y0; sy < sw && sy < y1; sy++){
				const double wy = min(sy + 1, y1) - max(sy, y0);
				for(sx = (int)x0; sx < sw && sx < x1; sx++){
					const DWORD p = src[sy * sw + sx];
					const double pa = (p >> 24) / 255.0 * wy * (min(sx + 1, x1) - max(sx, x0));
					a += pa;
					cr += ((p >> 16) & 0xFF) * pa;
					cg += ((p >> 8) & 0xFF) * pa;
					cb += (p & 0xFF) * pa;
				}
			}
			a /= r * r; cr /= r * r; cg /= r * r; cb /= r * r;
			dst[y * dstride + x] = ((DWORD)(a * 255 + 0.5) << 24) | ((DWORD)(cr + 0.5) << 16) |
				((DWORD)(cg + 0.5) << 8) | (DWORD)(cb + 0.5);
		}
	}
}

//ダークモードでは、暗い色の画素 (黒の輪郭や細い線、濃い青など) を明るくする
//(黒は明るい灰色、濃い青は明るい青になる。赤などの鮮やかな色はそのまま)
static void LightenForDark(DWORD *px)
{
#define TB_DARK_LIMIT		100			//これより暗い画素を明るくする (輝度 0 - 255)
#define TB_DARK_MAXCH		128			//これより明るい成分がある色は明るくしない
#define TB_DARK_TARGET		190			//明るくした後の輝度
	int i;

	for(i = 0; i < TB_SRC_SIZE * TB_SRC_SIZE; i++){
		const DWORD p = px[i];
		const int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
		const int lum = (r * 299 + g * 587 + b * 114) / 1000;

		if((p >> 24) < 128 || lum >= TB_DARK_LIMIT || max(r, max(g, b)) > TB_DARK_MAXCH){
			continue;
		}
		//白と混ぜて輝度を TB_DARK_TARGET にする (色合いは残る)
		px[i] = (p & 0xFF000000) |
			((DWORD)(r + (255 - r) * (TB_DARK_TARGET - lum) / (255 - lum)) << 16) |
			((DWORD)(g + (255 - g) * (TB_DARK_TARGET - lum) / (255 - lum)) << 8) |
			(DWORD)(b + (255 - b) * (TB_DARK_TARGET - lum) / (255 - lum));
	}
}

//16x16 の画像を size に拡大する
static void ScaleImage(const DWORD *src, int size, DWORD *dst, int dstride)
{
	DWORD *cur = (DWORD *)src;
	DWORD *next;
	int w = TB_SRC_SIZE;
	int n;

	while(w < size){
		//残りの倍率が 2 以下なら 2 倍、それ以上なら 3 倍
		n = (size <= w * 2) ? 2 : 3;
		next = ScaleNx(cur, w, n);
		if(next == NULL){
			break;
		}
		if(cur != src) LocalFree(cur);
		cur = next;
		w *= n;
	}
	Downscale(cur, w, dst, dstride, size);
	if(cur != src) LocalFree(cur);
}

//イメージリストの画像を黒と白の背景に描き、その差から透明度を含めて取り出す
static void GetImagePixels(HIMAGELIST himl, int index, HDC hdc, const DWORD *bits, DWORD *out)
{
	RECT rc = {0, 0, TB_SRC_SIZE, TB_SRC_SIZE};
	DWORD black[TB_SRC_SIZE * TB_SRC_SIZE];
	int i, a;

	FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
	ImageList_Draw(himl, index, hdc, 0, 0, ILD_TRANSPARENT);
	GdiFlush();
	CopyMemory(black, bits, sizeof(black));
	FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
	ImageList_Draw(himl, index, hdc, 0, 0, ILD_TRANSPARENT);
	GdiFlush();
	for(i = 0; i < TB_SRC_SIZE * TB_SRC_SIZE; i++){
		const DWORD b = black[i];
		a = 255 - (int)((bits[i] >> 8) & 0xFF) + (int)((b >> 8) & 0xFF);
		if(a <= 0){
			out[i] = 0;
		}else if(a >= 255){
			out[i] = 0xFF000000 | (b & 0xFFFFFF);
		}else{
			out[i] = ((DWORD)a << 24) |
				((DWORD)min(255, ((b >> 16) & 0xFF) * 255 / a) << 16) |
				((DWORD)min(255, ((b >> 8) & 0xFF) * 255 / a) << 8) |
				(DWORD)min(255, (b & 0xFF) * 255 / a);
		}
	}
}


/******************************************************************************

	Dpi_ScaleToolbar

	ツールバーのボタンの画像を DPI に合わせて拡大する
	ダークモードでは 96 DPI でも作り直して、暗い輪郭を明るくする
	(標準のボタン (切り取り、元に戻すなど) の画像も含む)

******************************************************************************/

void Dpi_ScaleToolbar(HWND hToolbar)
{
	HIMAGELIST src, dst;
	BITMAPINFO bmi;
	HBITMAP hWork = NULL, hStrip = NULL, hOld;
	HDC hdc, hWorkDC;
	DWORD *work, *strip;
	DWORD pixels[TB_SRC_SIZE * TB_SRC_SIZE];
	int size, cnt, i;
	BOOL dark = DarkMode_IsDark();

	if(hToolbar == NULL || (g_Dpi == USER_DEFAULT_SCREEN_DPI && dark == FALSE)){
		return;
	}
	src = (HIMAGELIST)SendMessage(hToolbar, TB_GETIMAGELIST, 0, 0);
	if(src == NULL || (cnt = ImageList_GetImageCount(src)) <= 0){
		return;
	}
	size = DpiScale(TB_SRC_SIZE);

	//作業用 (16x16) と結果 (size x size を横に並べたもの) の 32bit DIB
	ZeroMemory(&bmi, sizeof(BITMAPINFO));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = TB_SRC_SIZE;
	bmi.bmiHeader.biHeight = -TB_SRC_SIZE;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	hdc = GetDC(NULL);
	hWork = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&work, NULL, 0);
	bmi.bmiHeader.biWidth = size * cnt;
	bmi.bmiHeader.biHeight = -size;
	hStrip = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&strip, NULL, 0);
	hWorkDC = CreateCompatibleDC(hdc);
	ReleaseDC(NULL, hdc);
	dst = ImageList_Create(size, size, ILC_COLOR32, cnt, 0);

	if(hWork != NULL && hStrip != NULL && hWorkDC != NULL && dst != NULL){
		hOld = (HBITMAP)SelectObject(hWorkDC, hWork);
		for(i = 0; i < cnt; i++){
			GetImagePixels(src, i, hWorkDC, work, pixels);
			if(dark == TRUE){
				LightenForDark(pixels);
			}
			ScaleImage(pixels, size, strip + i * size, size * cnt);
		}
		SelectObject(hWorkDC, hOld);
		GdiFlush();
		ImageList_Add(dst, hStrip, NULL);

		//元のイメージリストはツールバーのもの (ツールバーを破棄するときに解放される)
		SendMessage(hToolbar, TB_SETIMAGELIST, 0, (LPARAM)dst);
		SendMessage(hToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(size + DpiScale(7), size + DpiScale(6)));
		SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);
		dst = NULL;
	}
	if(dst != NULL) ImageList_Destroy(dst);
	if(hWorkDC != NULL) DeleteDC(hWorkDC);
	if(hStrip != NULL) DeleteObject(hStrip);
	if(hWork != NULL) DeleteObject(hWork);
}
/* End of source */
