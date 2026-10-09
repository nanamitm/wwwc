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
static int CurrentMode = DARKMODE_LIGHT;

/*
 * DialogHookProc - スレッド内で作成されるダイアログにダークモードを適用する
 *
 * WWWC.exe と プロトコル DLL のダイアログ (プロパティシートのページ、メッセージボックスを含む)
 * は全てメインスレッドで作成されるため、WM_INITDIALOG の処理後にまとめて適用する。
 */
static LRESULT CALLBACK DialogHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && DarkEnabled) {
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
 * ConfigMode - ライブラリの表示モードを設定する
 */
static void ConfigMode(int mode)
{
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

	CurrentMode = mode;
	DarkEnabled = (mode != DARKMODE_LIGHT) && dmlib::isEnabled();
	if (DarkEnabled && hDialogHook == nullptr) {
		hDialogHook = SetWindowsHookExW(WH_CALLWNDPROCRET, DialogHookProc, nullptr, GetCurrentThreadId());
	}
}

/*
 * DarkMode_Initialize - ライブラリの初期化 (ウィンドウ作成前に呼ぶ)
 */
void DarkMode_Initialize(int mode)
{
	dmlib::initDarkMode();
	ConfigMode(mode);
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

/*
 * DarkMode_SetMode - 表示モードを切り替えてメインウィンドウに反映する
 *
 * 戻り値: ダーク表示かどうかが変わった場合は TRUE
 */
BOOL DarkMode_SetMode(HWND hWnd, int mode)
{
	const bool wasDark = DarkEnabled;

	ConfigMode(mode);
	if (DarkEnabled) {
		DarkMode_ApplyMainWindow(hWnd);
	} else if (wasDark) {
		//ライトに戻す (既に開いているダイアログは開き直したときに反映)
		dmlib::setDarkTitleBar(hWnd);
		dmlib::setChildCtrlsTheme(hWnd);
		RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
	return (wasDark != DarkEnabled) ? TRUE : FALSE;
}

/*
 * DarkMode_OnSettingChange - WM_SETTINGCHANGE の処理 (Windows の設定に従う場合)
 *
 * 戻り値: ダーク表示かどうかが変わった場合は TRUE
 */
BOOL DarkMode_OnSettingChange(HWND hWnd, LPARAM lParam)
{
	if (CurrentMode != DARKMODE_SYSTEM || !dmlib::handleSettingChange(lParam)) {
		return FALSE;
	}
	return DarkMode_SetMode(hWnd, DARKMODE_SYSTEM);
}
/* End of source */
