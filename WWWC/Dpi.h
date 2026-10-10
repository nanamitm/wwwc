/**************************************************************************

	WWWC

	Dpi.h

**************************************************************************/

#ifndef _INC_DPI_H
#define _INC_DPI_H

extern int g_Dpi;
extern int TvIconSizeBase;
extern int LvLIconSizeBase;
extern int LvSIconSizeBase;

int DpiScale(int v);
int DpiUnscale(int v);
int Dpi_ForWindow(HWND hWnd);
int DpiScaleFor(HWND hWnd, int v);
int DpiUnscaleFor(HWND hWnd, int v);
void Dpi_SetIconSizeBase(void);
void Dpi_ApplyIconSizes(void);
HFONT Dpi_CreateDefaultFont(void);
void Dpi_SetStatusBarFont(HWND hStatus);
void Dpi_Uninitialize(void);
void Dpi_ScaleToolbar(HWND hToolbar);

#endif
/* End of source */
