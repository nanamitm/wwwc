/**************************************************************************

	WWWC

	DarkMode.cpp

	darkmodelib (third_party/darkmodelib) を C から使うための薄いラッパ

**************************************************************************/

#include <windows.h>

#include "Darkmodelib.h"
#include "DarkMode.h"

static HHOOK hDialogHook = nullptr;
static bool DarkEnabled = false;

/*
 * DialogHookProc - スレッド内で作成されるダイアログにダークモードを適用する
 *
 * WWWC.exe と プロトコル DLL のダイアログ (プロパティシートのページ、メッセージボックスを含む)
 * は全てメインスレッドで作成されるため、WM_INITDIALOG の処理後にまとめて適用する。
 */
static LRESULT CALLBACK DialogHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) {
		const CWPRETSTRUCT *cwp = reinterpret_cast<const CWPRETSTRUCT *>(lParam);
		if (cwp->message == WM_INITDIALOG) {
			dmlib::setDarkWndNotifySafe(cwp->hwnd);
			if ((GetWindowLongPtr(cwp->hwnd, GWL_STYLE) & WS_CHILD) == 0) {
				dmlib::setDarkTitleBar(cwp->hwnd);
			}
		}
	}
	return CallNextHookEx(hDialogHook, nCode, wParam, lParam);
}

/*
 * DarkMode_Initialize - ライブラリの初期化
 */
void DarkMode_Initialize(int mode)
{
	dmlib::initDarkMode();
	switch (mode) {
	case DARKMODE_DARK:
		dmlib::setDarkModeConfigEx(static_cast<UINT>(dmlib::DarkModeType::dark));
		break;

	case DARKMODE_SYSTEM:
		dmlib::setDarkModeConfig();
		break;

	default:
		dmlib::setDarkModeConfigEx(static_cast<UINT>(dmlib::DarkModeType::light));
		break;
	}
	dmlib::setDefaultColors(true);

	DarkEnabled = (mode != DARKMODE_LIGHT) && dmlib::isEnabled();
	if (DarkEnabled && hDialogHook == nullptr) {
		hDialogHook = SetWindowsHookExW(WH_CALLWNDPROCRET, DialogHookProc, nullptr, GetCurrentThreadId());
	}
}

/*
 * DarkMode_Uninitialize - フックの解除
 */
void DarkMode_Uninitialize(void)
{
	if (hDialogHook != nullptr) {
		UnhookWindowsHookEx(hDialogHook);
		hDialogHook = nullptr;
	}
}

/*
 * DarkMode_IsDark - ダークモードで表示しているか
 */
BOOL DarkMode_IsDark(void)
{
	return DarkEnabled ? TRUE : FALSE;
}

/*
 * DarkMode_ApplyMainWindow - メインウィンドウと子コントロールに適用
 */
void DarkMode_ApplyMainWindow(HWND hWnd)
{
	if (!DarkEnabled) {
		return;
	}
	dmlib::setDarkWndNotifySafe(hWnd);
	dmlib::setWindowEraseBgSubclass(hWnd);
	dmlib::setWindowMenuBarSubclass(hWnd);
	dmlib::setDarkTitleBar(hWnd);
	RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}
/* End of source */
