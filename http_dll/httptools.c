/**************************************************************************

	WWWC (wwwc.dll)

	httptools.c

	Copyright (C) 1996-2003 by Nakashima Tomoaki. All rights reserved.
		http://www.nakka.com/
		nakka@nakka.com

**************************************************************************/


/**************************************************************************
	Include Files
**************************************************************************/

#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <commctrl.h>

#include "httptools.h"
#include "http.h"
#include "StrTbl.h"
#include "wwwcdll.h"
#include "resource.h"


/**************************************************************************
	Global Variables
**************************************************************************/

//外部参照
extern char app_path[];
extern HINSTANCE ghinst;

extern char BrowserPath[];


/******************************************************************************

	GetBrowserInfo, GetBrowserInfoList

	ブラウザから情報を取得 (DDE)
	DDE (WWW_GetWindowInfo, WWW_ListWindows) に対応したブラウザがないため廃止。
	以前のツール設定から呼ばれた場合はメッセージを表示する。

******************************************************************************/

static int BrowserInfoRemoved(HWND hWnd, int type)
{
	if(type == TOOL_EXEC_WINDOWMENU || type == TOOL_EXEC_PORP){
		MessageBox(hWnd, STR_ERR_MSG_DDE_REMOVED, STR_ERR_TITLE_DDE, MB_ICONINFORMATION | MB_OK);
	}
	return 0;
}

__declspec(dllexport) int CALLBACK GetBrowserInfo(HWND hWnd, struct TPITEM **ToolItemList,
												  int ToolItemListCnt, int type, int CheckType)
{
	return BrowserInfoRemoved(hWnd, type);
}

__declspec(dllexport) int CALLBACK GetBrowserInfoList(HWND hWnd, struct TPITEM **ToolItemList,
													  int ToolItemListCnt, int type, int CheckType)
{
	return BrowserInfoRemoved(hWnd, type);
}


/******************************************************************************

	SetBrowserProc

	ブラウザの場所の設定

******************************************************************************/

static INT_PTR CALLBACK SetBrowserProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	char buf[BUFSIZE];

	switch (uMsg)
	{
	case WM_INITDIALOG:
		SendMessage(GetDlgItem(hDlg, IDC_EDIT_PATH), WM_SETTEXT, 0, (LPARAM)BrowserPath);
		break;

	case WM_CLOSE:
		EndDialog(hDlg, FALSE);
		break;

	case WM_COMMAND:
		switch(wParam)
		{
		case IDC_BUTTON_BROWS:
			if(FileSelect(hDlg, "*.exe", STR_FILTER_EXESELECT, STR_TITLE_FILESELECT, buf, NULL, 1) == -1){
				break;
			}
			SendMessage(GetDlgItem(hDlg, IDC_EDIT_PATH), WM_SETTEXT, 0, (LPARAM)buf);
			break;

		case IDOK:
			SendMessage(GetDlgItem(hDlg, IDC_EDIT_PATH), WM_GETTEXT, BUFSIZE, (LPARAM)BrowserPath);
			WritePrivateProfileString("HTTP", "BrowserPath", BrowserPath, app_path);
			EndDialog(hDlg, TRUE);
			break;

		case IDCANCEL:
			EndDialog(hDlg, FALSE);
			break;
		}
		break;
	default:
		return FALSE;
	}
	return TRUE;
}


/******************************************************************************

	DDE_OpenURL

	ブラウザでURLを開く
	(関数名は以前のツール設定との互換のため。DDE は使わず、設定したブラウザか
	既定のブラウザで開く)

******************************************************************************/

__declspec(dllexport) int CALLBACK DDE_OpenURL(HWND hWnd, struct TPITEM **ToolItemList,
													  int ToolItemListCnt, int type, int CheckType)
{
	char *url;
	int rc = -1;
	int i;

	switch(type)
	{
	case TOOL_EXEC_ITEMMENU:
		for(i = 0; i < ToolItemListCnt; i++){
			url = ((*(ToolItemList + i))->ViewURL != NULL && *(*(ToolItemList + i))->ViewURL != '\0') ?
				(*(ToolItemList + i))->ViewURL : (*(ToolItemList + i))->CheckURL;
			if(url == NULL || *url == '\0'){
				continue;
			}
			if(*BrowserPath == '\0'){
				rc = ExecItem(hWnd, url, NULL);
			}else{
				//指定のブラウザの引数として渡す (空白を含むURLのため引用符で囲む)
				char *arg = (char *)GlobalAlloc(GMEM_FIXED, lstrlen(url) + 3);
				if(arg == NULL){
					continue;
				}
				*arg = '\"';
				lstrcpy(arg + 1, url);
				lstrcat(arg, "\"");
				rc = ExecItem(hWnd, BrowserPath, arg);
				GlobalFree(arg);
			}
		}
		break;

	case TOOL_EXEC_PORP:
		DialogBoxParam(ghinst, MAKEINTRESOURCE(IDD_DIALOG_SETBROWSER), hWnd, SetBrowserProc, 0);
		rc = 1;
		break;

	default:
		return 0;
	}
	return rc;
}
/* End of source */
