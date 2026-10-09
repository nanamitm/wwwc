/**************************************************************************

	WWWC

	DarkMode.h

	darkmodelib (third_party/darkmodelib) を C から使うための薄いラッパ

**************************************************************************/

#ifndef _INC_DARKMODE_H
#define _INC_DARKMODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* DarkMode 設定値 (user.ini [GENERAL] DarkMode) */
#define DARKMODE_LIGHT				0					/* ライト (従来の表示) */
#define DARKMODE_DARK				1					/* ダーク */
#define DARKMODE_SYSTEM				2					/* Windows の設定に従う */

void DarkMode_Initialize(int mode);
void DarkMode_Uninitialize(void);
BOOL DarkMode_IsDark(void);
void DarkMode_ApplyMainWindow(HWND hWnd);
BOOL DarkMode_SetMode(HWND hWnd, int mode);
BOOL DarkMode_OnSettingChange(HWND hWnd, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif
/* End of source */
