/**************************************************************************

	WWWC (wwwc.dll)

	archiveprog.c

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
#include <commctrl.h>
#include <shlwapi.h>
#include <stdlib.h>

#include "String.h"
#include "http.h"
#include "StrTbl.h"
#include "wwwcdll.h"
#include "archive.h"
#include "resource.h"


/**************************************************************************
	Define
**************************************************************************/

#define PROG_TIMER_ID			1
#define PROG_TIMER_INTERVAL		500
#define PROG_REOPEN_PROP		"WWWC_ArchiveProgressReopen"	/* 再読み込みの後に開き直す (本体のウィンドウに設定) */
#define PROG_INI_SECTION		"ARCHIVE"

/* コントロールの配置 (ダイアログの大きさに合わせる方向) */
#define AN_W					1						/* 幅を広げる */
#define AN_H					2						/* 高さを広げる */
#define AN_X					4						/* 右に移動する */
#define AN_Y					8						/* 下に移動する */
#define AN_SPLIT_H				16						/* 境界の移動の分だけ高さを変える (取得待ち) */
#define AN_SPLIT_Y				32						/* 境界の移動の分だけ下に移動して高さを減らす (履歴) */

#define PROG_SPLIT_MIN			40						/* 境界をドラッグしたときの一覧の最小の高さ (ダイアログ単位、見出しとスクロールバーと 2 行程度) */

typedef struct _PROG_ANCHOR {
	int id;
	int flags;
	RECT rc;
} PROG_ANCHOR;

/* 一覧の列 (幅はダイアログ単位) */
typedef struct _PROG_COLUMN {
	UINT str;
	int width;
} PROG_COLUMN;


/**************************************************************************
	Global Variables
**************************************************************************/

static HWND hProgressWnd;
static LONG ProgSerial;
static ARCHIVE_STATUS_ITEM *ProgHist;				/* 一覧に表示している履歴 (一覧と同じ順) */
static int ProgHistCnt;
static SIZE ProgInitClient;
static SIZE ProgMinTrack;
static int ProgSplit;								/* 取得待ちの一覧の高さの増分 (境界のドラッグ、ピクセル) */
static BOOL ProgDragging;
static int ProgDragY;
static int ProgDragSplit;
static int ProgQueueCnt;							/* 表示している取得待ちの数 */

static PROG_ANCHOR ProgAnchor[] = {
	{ IDC_STATIC_PROG_GROUP, AN_W },
	{ IDC_BUTTON_PROG_CANCEL, AN_X },
	{ IDC_BUTTON_PROG_CANCELALL, AN_X },
	{ IDC_STATIC_PROG_TITLE, AN_W },
	{ IDC_STATIC_PROG_URL, AN_W },
	{ IDC_STATIC_PROG_STATE, AN_W },
	{ IDC_STATIC_PROG_FILES, AN_W },
	{ IDC_LIST_PROG_QUEUE, AN_W | AN_SPLIT_H },
	{ IDC_STATIC_PROG_HISTORY, AN_SPLIT_Y },
	{ IDC_LIST_PROG_HISTORY, AN_W | AN_H | AN_SPLIT_Y },
	{ IDC_BUTTON_PROG_OPEN, AN_Y },
	{ IDC_BUTTON_PROG_CLEAR, AN_Y },
	{ IDOK, AN_X | AN_Y },
};

static const PROG_COLUMN QueueColumns[] = {
	{ IDS_STR_PROG_COL_NAME, 110 },
	{ IDS_STR_PROG_COL_URL, 200 },
};

static const PROG_COLUMN HistoryColumns[] = {
	{ IDS_STR_PROG_COL_TIME, 80 },
	{ IDS_STR_PROG_COL_NAME, 90 },
	{ IDS_STR_PROG_COL_RESULT, 180 },
	{ IDS_STR_PROG_COL_ELAPSED, 35 },
	{ IDS_STR_PROG_COL_FILES, 60 },
	{ IDS_STR_PROG_COL_URL, 200 },
};

//外部参照
extern HINSTANCE ghinst;
extern char app_path[];
extern char DateFormat[];


/**************************************************************************
	Helpers
**************************************************************************/

/* 経過時間 (ミリ秒) を秒で表す (小数点以下 1 桁) */
static void FormatSeconds(DWORD ms, char *ret)
{
	wsprintf(ret, "%lu.%lu", ms / 1000, (ms % 1000) / 100);
}

/* 取得したファイルの数とサイズ */
static void FormatFiles(int cnt, LONGLONG size, char *ret)
{
	char buf[64];

	StrFormatByteSize64A(size, buf, sizeof(buf));
	wsprintf(ret, STR_PROG_FILES, cnt, buf);
}

/* 結果の文字列 */
static void FormatResult(const ARCHIVE_STATUS_ITEM *s, char *ret)
{
	switch(s->Result)
	{
	case ARCHIVE_RESULT_SAVED:
		lstrcpy(ret, STR_PROG_RESULT_SAVED);
		break;
	case ARCHIVE_RESULT_REPLACED:
		lstrcpy(ret, STR_PROG_RESULT_REPLACED);
		break;
	case ARCHIVE_RESULT_SAME:
		lstrcpy(ret, STR_PROG_RESULT_SAME);
		break;
	case ARCHIVE_RESULT_FAILED:
		if(s->Status != 0){
			wsprintf(ret, STR_PROG_RESULT_FAILED_STATUS, s->Status);
		}else{
			lstrcpy(ret, STR_PROG_RESULT_FAILED);
		}
		break;
	case ARCHIVE_RESULT_NOTHTML:
		lstrcpy(ret, STR_PROG_RESULT_NOTHTML);
		break;
	case ARCHIVE_RESULT_DISKFULL:
		lstrcpy(ret, STR_PROG_RESULT_DISKFULL);
		break;
	case ARCHIVE_RESULT_STOPPED:
		lstrcpy(ret, STR_PROG_RESULT_STOPPED);
		break;
	default:
		lstrcpy(ret, STR_PROG_RESULT_ERROR);
		break;
	}
	if(s->Browser == ARCHIVE_BROWSER_USED){
		lstrcat(ret, STR_PROG_BROWSER_USED);
	}else if(s->Browser == ARCHIVE_BROWSER_FAILED){
		char reason[BUFSIZE], buf[BUFSIZE];

		//使用できなかった理由
		*reason = '\0';
		switch(s->BrowserError)
		{
		case ARCHIVE_BERR_NOTFOUND:
			lstrcpy(reason, STR_PROG_BERR_NOTFOUND);
			break;
		case ARCHIVE_BERR_PROXY:
			lstrcpy(reason, STR_PROG_BERR_PROXY);
			break;
		case ARCHIVE_BERR_ARG:
			lstrcpy(reason, STR_PROG_BERR_ARG);
			break;
		case ARCHIVE_BERR_START:
			wsprintf(reason, STR_PROG_BERR_START, s->BrowserCode);
			break;
		case ARCHIVE_BERR_TIMEOUT:
			lstrcpy(reason, STR_PROG_BERR_TIMEOUT);
			break;
		case ARCHIVE_BERR_EXIT:
			wsprintf(reason, STR_PROG_BERR_EXIT, s->BrowserCode);
			break;
		case ARCHIVE_BERR_NOOUTPUT:
			lstrcpy(reason, STR_PROG_BERR_NOOUTPUT);
			break;
		case ARCHIVE_BERR_TOOLARGE:
			lstrcpy(reason, STR_PROG_BERR_TOOLARGE);
			break;
		case ARCHIVE_BERR_BOTCHECK:
			lstrcpy(reason, STR_PROG_BERR_BOTCHECK);
			break;
		}
		if(*reason != '\0'){
			wsprintf(buf, STR_PROG_BROWSER_FAILED_REASON, reason);
			lstrcat(ret, buf);
		}else{
			lstrcat(ret, STR_PROG_BROWSER_FAILED);
		}
	}
}

/* 日時 (日付の形式は WWWC の設定に合わせる) */
static void FormatTime(const SYSTEMTIME *t, char *ret, int size)
{
	char date[BUFSIZE], time[BUFSIZE];

	if(GetDateFormat(LOCALE_USER_DEFAULT, 0, t, (*DateFormat != '\0') ? DateFormat : NULL, date, BUFSIZE - 1) == 0){
		*date = '\0';
	}
	if(GetTimeFormat(LOCALE_USER_DEFAULT, 0, t, "HH':'mm':'ss", time, BUFSIZE - 1) == 0){
		*time = '\0';
	}
	wnsprintf(ret, size, "%s %s", date, time);
}

/* 内容が変わった場合のみ文字列を設定する (ちらつき防止) */
static void SetTextIfChanged(HWND hDlg, int id, const char *text)
{
	char buf[ARCHIVE_URL_MAX + 1];

	GetDlgItemText(hDlg, id, buf, sizeof(buf));
	if(lstrcmp(buf, text) != 0){
		SetDlgItemText(hDlg, id, text);
	}
}

/* 一覧の列を作成する */
static void InitColumns(HWND hDlg, int id, const PROG_COLUMN *cols, int cnt)
{
	LV_COLUMN lvc;
	RECT rc;
	int i;

	ListView_SetExtendedListViewStyle(GetDlgItem(hDlg, id), LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
	for(i = 0; i < cnt; i++){
		SetRect(&rc, 0, 0, cols[i].width, 0);
		MapDialogRect(hDlg, &rc);
		ZeroMemory(&lvc, sizeof(lvc));
		lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		lvc.fmt = LVCFMT_LEFT;
		lvc.cx = rc.right;
		lvc.pszText = GetResStr(cols[i].str);
		lvc.iSubItem = i;
		ListView_InsertColumn(GetDlgItem(hDlg, id), i, &lvc);
	}
}

/* 一覧に行を追加 */
static int AddRow(HWND hList, int i, LPARAM lParam, const char *text)
{
	LV_ITEM lvi;

	ZeroMemory(&lvi, sizeof(lvi));
	lvi.mask = LVIF_TEXT | LVIF_PARAM;
	lvi.iItem = i;
	lvi.pszText = (char *)((text != NULL) ? text : "");
	lvi.lParam = lParam;
	return ListView_InsertItem(hList, &lvi);
}

static void SetCell(HWND hList, int i, int col, const char *text)
{
	ListView_SetItemText(hList, i, col, (char *)((text != NULL) ? text : ""));
}

/* 選択している履歴 */
static int GetSelHistory(HWND hDlg)
{
	int i;

	i = ListView_GetNextItem(GetDlgItem(hDlg, IDC_LIST_PROG_HISTORY), -1, LVNI_SELECTED);
	return (i >= 0 && i < ProgHistCnt) ? i : -1;
}

/* コントロールの有効、無効を切り替える (フォーカスのあるものを無効にする場合は「閉じる」に移す) */
static void EnableItem(HWND hDlg, int id, BOOL enable)
{
	HWND hItem = GetDlgItem(hDlg, id);

	if(enable == FALSE && GetFocus() == hItem){
		SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDOK), TRUE);
	}
	EnableWindow(hItem, enable);
}

static void EnableOpen(HWND hDlg)
{
	EnableItem(hDlg, IDC_BUTTON_PROG_OPEN, (GetSelHistory(hDlg) >= 0) ? TRUE : FALSE);
}

/* 中止のボタン (中止を指示した後は取得が終わるまで押せない) */
static void EnableCancel(HWND hDlg, ARCHIVE_STATUS *st)
{
	BOOL running = (st->Running == TRUE && st->Now.Cancel == FALSE) ? TRUE : FALSE;

	EnableItem(hDlg, IDC_BUTTON_PROG_CANCEL, running);
	EnableItem(hDlg, IDC_BUTTON_PROG_CANCELALL, (running == TRUE || ProgQueueCnt > 0) ? TRUE : FALSE);
}

/* 選択している履歴のアーカイブを開く */
static void OpenSelHistory(HWND hDlg)
{
	int i;

	if((i = GetSelHistory(hDlg)) < 0){
		return;
	}
	Archive_OpenDir(hDlg, ProgHist[i].Dir);
}


/**************************************************************************
	Update
**************************************************************************/

/* 取得中のページ */
static void SetCurrent(HWND hDlg, ARCHIVE_STATUS *st)
{
	char buf[BUFSIZE * 2], sec[32];
	const char *phase;

	if(st->Running == FALSE){
		SetTextIfChanged(hDlg, IDC_STATIC_PROG_TITLE, "");
		SetTextIfChanged(hDlg, IDC_STATIC_PROG_URL, "");
		SetTextIfChanged(hDlg, IDC_STATIC_PROG_STATE, (st->Enabled == TRUE) ? STR_PROG_IDLE : STR_PROG_DISABLED);
		SetTextIfChanged(hDlg, IDC_STATIC_PROG_FILES, "");
		return;
	}
	SetTextIfChanged(hDlg, IDC_STATIC_PROG_TITLE, (st->Now.Title != NULL) ? st->Now.Title : "");
	SetTextIfChanged(hDlg, IDC_STATIC_PROG_URL, (st->Now.Url != NULL) ? st->Now.Url : "");
	switch((st->Now.Cancel == TRUE) ? -1 : st->Now.Phase)
	{
	case -1:
		phase = STR_PROG_PHASE_CANCEL;
		break;
	case ARCHIVE_PHASE_BROWSER:
		phase = STR_PROG_PHASE_BROWSER;
		break;
	case ARCHIVE_PHASE_RES:
		phase = STR_PROG_PHASE_RES;
		break;
	case ARCHIVE_PHASE_SAVE:
		phase = STR_PROG_PHASE_SAVE;
		break;
	default:
		phase = STR_PROG_PHASE_PAGE;
		break;
	}
	FormatSeconds(st->Now.Elapsed, sec);
	wnsprintf(buf, sizeof(buf), STR_PROG_STATE, phase, sec);
	SetTextIfChanged(hDlg, IDC_STATIC_PROG_STATE, buf);
	FormatFiles(st->Now.ResCount, st->Now.Total, buf);
	SetTextIfChanged(hDlg, IDC_STATIC_PROG_FILES, buf);
}

/* 取得待ちの一覧 */
static void SetQueue(HWND hDlg, ARCHIVE_STATUS *st)
{
	HWND hList = GetDlgItem(hDlg, IDC_LIST_PROG_QUEUE);
	char buf[BUFSIZE];
	int i, n;

	SendMessage(hList, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(hList);
	for(i = 0; i < st->QueueCnt; i++){
		if((n = AddRow(hList, i, 0, st->Queue[i].Title)) < 0){
			break;
		}
		SetCell(hList, n, 1, st->Queue[i].Url);
	}
	SendMessage(hList, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hList, NULL, TRUE);
	wsprintf(buf, STR_PROG_QUEUE, st->QueueCnt);
	SetTextIfChanged(hDlg, IDC_STATIC_PROG_QUEUE, buf);
}

/* 履歴の一覧 (選択と表示位置を保つ) */
static void SetHistory(HWND hDlg, ARCHIVE_STATUS *st)
{
	HWND hList = GetDlgItem(hDlg, IDC_LIST_PROG_HISTORY);
	LV_ITEM lvi;
	RECT rc;
	char buf[BUFSIZE * 2], sec[32];
	LONG selId = 0, topId = 0;
	int i, n, top, sel;

	//変更前の選択と先頭の行
	if((sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED)) >= 0 && sel < ProgHistCnt){
		selId = ProgHist[sel].Id;
	}
	if((top = ListView_GetTopIndex(hList)) > 0 && top < ProgHistCnt){
		topId = ProgHist[top].Id;
	}

	SendMessage(hList, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(hList);
	//一覧と同じ順の履歴を保持する (アーカイブを開くときに使う)
	if(ProgHist != NULL){
		for(i = 0; i < ProgHistCnt; i++){
			HeapFree(GetProcessHeap(), 0, ProgHist[i].Title);
			HeapFree(GetProcessHeap(), 0, ProgHist[i].Url);
		}
		HeapFree(GetProcessHeap(), 0, ProgHist);
	}
	ProgHist = st->History;
	ProgHistCnt = st->HistoryCnt;
	st->History = NULL;
	st->HistoryCnt = 0;

	sel = top = -1;
	for(i = 0; i < ProgHistCnt; i++){
		FormatTime(&ProgHist[i].Time, buf, sizeof(buf));
		if((n = AddRow(hList, i, (LPARAM)ProgHist[i].Id, buf)) < 0){
			break;
		}
		SetCell(hList, n, 1, ProgHist[i].Title);
		FormatResult(&ProgHist[i], buf);
		SetCell(hList, n, 2, buf);
		FormatSeconds(ProgHist[i].Elapsed, sec);
		wsprintf(buf, STR_PROG_SECONDS, sec);
		SetCell(hList, n, 3, buf);
		FormatFiles(ProgHist[i].ResCount, ProgHist[i].Total, buf);
		SetCell(hList, n, 4, buf);
		SetCell(hList, n, 5, ProgHist[i].Url);
		if(selId != 0 && ProgHist[i].Id == selId){
			sel = n;
		}
		if(topId != 0 && ProgHist[i].Id == topId){
			top = n;
		}
	}
	if(sel >= 0){
		ZeroMemory(&lvi, sizeof(lvi));
		lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
		lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
		SendMessage(hList, LVM_SETITEMSTATE, sel, (LPARAM)&lvi);
	}
	//スクロールしていた場合は同じ行が先頭になるようにする (新しい履歴は先頭に追加される)
	if(top > 0 && ListView_GetItemRect(hList, 0, &rc, LVIR_BOUNDS) == TRUE){
		ListView_Scroll(hList, 0, top * (rc.bottom - rc.top));
	}
	SendMessage(hList, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hList, NULL, TRUE);

	wsprintf(buf, STR_PROG_HISTORY, ProgHistCnt);
	SetTextIfChanged(hDlg, IDC_STATIC_PROG_HISTORY, buf);
	EnableItem(hDlg, IDC_BUTTON_PROG_CLEAR, (ProgHistCnt > 0) ? TRUE : FALSE);
	EnableOpen(hDlg);
}

static void UpdateProgress(HWND hDlg)
{
	ARCHIVE_STATUS st;

	Archive_GetStatus(&st, ProgSerial);
	SetCurrent(hDlg, &st);
	if(st.Lists == TRUE){
		ProgSerial = st.Serial;
		ProgQueueCnt = st.QueueCnt;
		SetQueue(hDlg, &st);
		SetHistory(hDlg, &st);
	}
	EnableCancel(hDlg, &st);
	Archive_FreeStatus(&st);
}


/**************************************************************************
	Layout
**************************************************************************/

static void InitLayout(HWND hDlg)
{
	RECT rc;
	int i;

	GetClientRect(hDlg, &rc);
	ProgInitClient.cx = rc.right;
	ProgInitClient.cy = rc.bottom;
	GetWindowRect(hDlg, &rc);
	ProgMinTrack.cx = rc.right - rc.left;
	ProgMinTrack.cy = rc.bottom - rc.top;
	for(i = 0; i < sizeof(ProgAnchor) / sizeof(ProgAnchor[0]); i++){
		GetWindowRect(GetDlgItem(hDlg, ProgAnchor[i].id), &ProgAnchor[i].rc);
		MapWindowPoints(NULL, hDlg, (POINT *)&ProgAnchor[i].rc, 2);
	}
}

static RECT *GetAnchorRect(int id)
{
	int i;

	for(i = 0; i < sizeof(ProgAnchor) / sizeof(ProgAnchor[0]); i++){
		if(ProgAnchor[i].id == id){
			return &ProgAnchor[i].rc;
		}
	}
	return NULL;
}

/* 境界の移動量を、取得待ちと履歴の一覧が最小の高さより低くならない範囲にする (cy はクライアント領域の高さ) */
static int ClampSplit(HWND hDlg, int split, int cy)
{
	RECT rc, *q, *h;
	int minh, lo, hi;

	if((q = GetAnchorRect(IDC_LIST_PROG_QUEUE)) == NULL || (h = GetAnchorRect(IDC_LIST_PROG_HISTORY)) == NULL){
		return 0;
	}
	SetRect(&rc, 0, 0, 0, PROG_SPLIT_MIN);
	MapDialogRect(hDlg, &rc);
	minh = rc.bottom;
	lo = minh - (q->bottom - q->top);
	hi = (h->bottom - h->top) + (cy - ProgInitClient.cy) - minh;
	if(split > hi){
		split = hi;
	}
	if(split < lo){
		split = lo;
	}
	return split;
}

static void MoveControls(HWND hDlg, int cx, int cy)
{
	HDWP hdwp;
	RECT *r;
	int dx = cx - ProgInitClient.cx, dy = cy - ProgInitClient.cy;
	int i, f, s;

	//ウィンドウを小さくした場合は境界を一時的に動かす (ProgSplit は保つ)
	s = ClampSplit(hDlg, ProgSplit, cy);
	hdwp = BeginDeferWindowPos(sizeof(ProgAnchor) / sizeof(ProgAnchor[0]));
	for(i = 0; i < sizeof(ProgAnchor) / sizeof(ProgAnchor[0]) && hdwp != NULL; i++){
		r = &ProgAnchor[i].rc;
		f = ProgAnchor[i].flags;
		hdwp = DeferWindowPos(hdwp, GetDlgItem(hDlg, ProgAnchor[i].id), NULL,
			r->left + ((f & AN_X) ? dx : 0),
			r->top + ((f & AN_Y) ? dy : 0) + ((f & AN_SPLIT_Y) ? s : 0),
			(r->right - r->left) + ((f & AN_W) ? dx : 0),
			(r->bottom - r->top) + ((f & AN_H) ? dy : 0) + ((f & AN_SPLIT_H) ? s : 0) - (((f & AN_SPLIT_Y) && (f & AN_H)) ? s : 0),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if(hdwp != NULL){
		EndDeferWindowPos(hdwp);
	}
	InvalidateRect(hDlg, NULL, TRUE);
}

/* 取得待ちと履歴の一覧の間 (ドラッグで境界を移動できる場所) か */
static BOOL InSplitter(HWND hDlg, POINT pt)
{
	RECT q, h;

	GetWindowRect(GetDlgItem(hDlg, IDC_LIST_PROG_QUEUE), &q);
	GetWindowRect(GetDlgItem(hDlg, IDC_LIST_PROG_HISTORY), &h);
	MapWindowPoints(NULL, hDlg, (POINT *)&q, 2);
	MapWindowPoints(NULL, hDlg, (POINT *)&h, 2);
	return (pt.x >= q.left && pt.x < q.right && pt.y >= q.bottom && pt.y < h.top) ? TRUE : FALSE;
}

/* 境界をドラッグで移動する */
static void DragSplit(HWND hDlg, int y)
{
	RECT rc;

	GetClientRect(hDlg, &rc);
	ProgSplit = ClampSplit(hDlg, ProgDragSplit + (y - ProgDragY), rc.bottom);
	MoveControls(hDlg, rc.right, rc.bottom);
	UpdateWindow(hDlg);
}

/* 前回の位置と大きさ (画面の外になる場合は本体のウィンドウの中央) */
static void LoadPosition(HWND hDlg)
{
	RECT rc, mrc;
	HWND hMain;
	int w, h;

	rc.left = GetPrivateProfileInt(PROG_INI_SECTION, "ProgressLeft", 0, app_path);
	rc.top = GetPrivateProfileInt(PROG_INI_SECTION, "ProgressTop", 0, app_path);
	w = GetPrivateProfileInt(PROG_INI_SECTION, "ProgressWidth", 0, app_path);
	h = GetPrivateProfileInt(PROG_INI_SECTION, "ProgressHeight", 0, app_path);
	if(w >= ProgMinTrack.cx && h >= ProgMinTrack.cy){
		rc.right = rc.left + w;
		rc.bottom = rc.top + h;
		if(MonitorFromRect(&rc, MONITOR_DEFAULTTONULL) != NULL){
			SetWindowPos(hDlg, NULL, rc.left, rc.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
			return;
		}
	}
	hMain = Archive_GetMainWindow();
	if(hMain == NULL || IsWindowVisible(hMain) == FALSE || IsIconic(hMain) != 0 || GetWindowRect(hMain, &mrc) == FALSE){
		GetWindowRect(GetDesktopWindow(), &mrc);
	}
	GetWindowRect(hDlg, &rc);
	SetWindowPos(hDlg, NULL, mrc.left + ((mrc.right - mrc.left) - (rc.right - rc.left)) / 2,
		mrc.top + ((mrc.bottom - mrc.top) - (rc.bottom - rc.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void SavePosition(HWND hDlg)
{
	WINDOWPLACEMENT wp;
	MONITORINFO mi;
	RECT rc;
	char buf[32];

	if(*app_path == '\0'){
		return;
	}
	if(IsIconic(hDlg) == 0 && IsZoomed(hDlg) == 0){
		GetWindowRect(hDlg, &rc);
	}else{
		//最小化、最大化している場合は元の位置と大きさ (ワークエリアの座標のため画面の座標に直す)
		ZeroMemory(&wp, sizeof(wp));
		wp.length = sizeof(wp);
		if(GetWindowPlacement(hDlg, &wp) == FALSE){
			return;
		}
		rc = wp.rcNormalPosition;
		mi.cbSize = sizeof(mi);
		if(GetMonitorInfo(MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST), &mi) == TRUE){
			OffsetRect(&rc, mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top);
		}
	}
	wsprintf(buf, "%d", rc.left);
	WritePrivateProfileString(PROG_INI_SECTION, "ProgressLeft", buf, app_path);
	wsprintf(buf, "%d", rc.top);
	WritePrivateProfileString(PROG_INI_SECTION, "ProgressTop", buf, app_path);
	wsprintf(buf, "%d", rc.right - rc.left);
	WritePrivateProfileString(PROG_INI_SECTION, "ProgressWidth", buf, app_path);
	wsprintf(buf, "%d", rc.bottom - rc.top);
	WritePrivateProfileString(PROG_INI_SECTION, "ProgressHeight", buf, app_path);
	wsprintf(buf, "%d", ProgSplit);
	WritePrivateProfileString(PROG_INI_SECTION, "ProgressSplit", buf, app_path);
}


/******************************************************************************

	ProgressProc

	アーカイブの進捗状況のダイアログ (モードレス)

******************************************************************************/

static BOOL CALLBACK ProgressProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HWND hMain, hFocus;
	HICON hIcon;
	POINT pt;
	RECT rc;

	switch (uMsg)
	{
	case WM_INITDIALOG:
		hProgressWnd = hDlg;
		ProgSerial = -1;
		//本体と同じアイコン
		if((hMain = Archive_GetMainWindow()) != NULL){
			if((hIcon = (HICON)GetClassLongPtr(hMain, GCLP_HICON)) != NULL){
				SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			}
			if((hIcon = (HICON)GetClassLongPtr(hMain, GCLP_HICONSM)) != NULL){
				SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			}
		}
		InitColumns(hDlg, IDC_LIST_PROG_QUEUE, QueueColumns, sizeof(QueueColumns) / sizeof(QueueColumns[0]));
		InitColumns(hDlg, IDC_LIST_PROG_HISTORY, HistoryColumns, sizeof(HistoryColumns) / sizeof(HistoryColumns[0]));
		InitLayout(hDlg);
		//前回の境界の位置 (上に移動した場合は負の値のため文字列で読む)
		{
			char buf[32];
			GetPrivateProfileString(PROG_INI_SECTION, "ProgressSplit", "0", buf, sizeof(buf), app_path);
			ProgSplit = atoi(buf);
		}
		LoadPosition(hDlg);
		GetClientRect(hDlg, &rc);
		MoveControls(hDlg, rc.right, rc.bottom);
		UpdateProgress(hDlg);
		SetTimer(hDlg, PROG_TIMER_ID, PROG_TIMER_INTERVAL, NULL);
		break;

	case WM_SETCURSOR:
		//一覧の間では上下の矢印のカーソルにする
		GetCursorPos(&pt);
		ScreenToClient(hDlg, &pt);
		if(ProgDragging == TRUE || (LOWORD(lParam) == HTCLIENT && InSplitter(hDlg, pt) == TRUE)){
			SetCursor(LoadCursor(NULL, IDC_SIZENS));
			SetWindowLongPtr(hDlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
		return FALSE;

	case WM_LBUTTONDOWN:
		pt.x = (short)LOWORD(lParam);
		pt.y = (short)HIWORD(lParam);
		if(InSplitter(hDlg, pt) == TRUE){
			GetClientRect(hDlg, &rc);
			ProgDragging = TRUE;
			ProgDragY = pt.y;
			ProgDragSplit = ClampSplit(hDlg, ProgSplit, rc.bottom);
			SetCapture(hDlg);
		}
		break;

	case WM_MOUSEMOVE:
		if(ProgDragging == TRUE){
			DragSplit(hDlg, (short)HIWORD(lParam));
		}
		break;

	case WM_LBUTTONUP:
		if(ProgDragging == TRUE){
			DragSplit(hDlg, (short)HIWORD(lParam));
			ReleaseCapture();
		}
		break;

	case WM_CAPTURECHANGED:
		ProgDragging = FALSE;
		break;

	case WM_TIMER:
		if(wParam == PROG_TIMER_ID){
			UpdateProgress(hDlg);
		}
		break;

	case WM_GETMINMAXINFO:
		//ボタンが見切れる大きさには縮めない (コントロールの作成前は既定の処理)
		if(ProgMinTrack.cx == 0){
			return FALSE;
		}
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = ProgMinTrack.cx;
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = ProgMinTrack.cy;
		break;

	case WM_SIZE:
		if(wParam == SIZE_MINIMIZED || ProgInitClient.cx == 0){
			break;
		}
		MoveControls(hDlg, LOWORD(lParam), HIWORD(lParam));
		break;

	case WM_NOTIFY:
		if(((NMHDR *)lParam)->idFrom == IDC_LIST_PROG_HISTORY){
			switch(((NMHDR *)lParam)->code)
			{
			case LVN_ITEMCHANGED:
				EnableOpen(hDlg);
				break;

			case NM_DBLCLK:
				OpenSelHistory(hDlg);
				break;
			}
		}
		break;

	case WM_CLOSE:
		//モードレスのダイアログのため破棄する
		DestroyWindow(hDlg);
		break;

	case WM_DESTROY:
		KillTimer(hDlg, PROG_TIMER_ID);
		SavePosition(hDlg);
		if(ProgHist != NULL){
			int i;
			for(i = 0; i < ProgHistCnt; i++){
				HeapFree(GetProcessHeap(), 0, ProgHist[i].Title);
				HeapFree(GetProcessHeap(), 0, ProgHist[i].Url);
			}
			HeapFree(GetProcessHeap(), 0, ProgHist);
		}
		ProgHist = NULL;
		ProgHistCnt = 0;
		ProgInitClient.cx = ProgInitClient.cy = 0;
		ProgMinTrack.cx = ProgMinTrack.cy = 0;
		ProgDragging = FALSE;
		hProgressWnd = NULL;
		return FALSE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_BUTTON_PROG_OPEN:
			OpenSelHistory(hDlg);
			break;

		case IDC_BUTTON_PROG_CANCEL:
			//取得中のページを中止する (取得待ちは続ける)
			Archive_Cancel(FALSE);
			UpdateProgress(hDlg);
			break;

		case IDC_BUTTON_PROG_CANCELALL:
			//取得中のページを中止して取得待ちもすべて取り消す
			Archive_Cancel(TRUE);
			UpdateProgress(hDlg);
			break;

		case IDC_BUTTON_PROG_CLEAR:
			Archive_ClearHistory();
			UpdateProgress(hDlg);
			break;

		//本体のアクセラレータで送られるキー
		case ID_KEY_RETURN:
			hFocus = GetFocus();
			if(hFocus == GetDlgItem(hDlg, IDC_LIST_PROG_HISTORY)){
				OpenSelHistory(hDlg);
			}else if(hFocus != NULL && GetParent(hFocus) == hDlg &&
				(SendMessage(hFocus, WM_GETDLGCODE, 0, 0) & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON))){
				SendMessage(hFocus, BM_CLICK, 0, 0);
			}
			break;

		case IDOK:
		case IDCANCEL:
		case ID_KEY_ESC:
			SendMessage(hDlg, WM_CLOSE, 0, 0);
			break;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}


/******************************************************************************

	ArchiveProgress_Show

	アーカイブの進捗状況を表示する (既に開いている場合は前面に出す)

******************************************************************************/

void ArchiveProgress_Show(HWND hWnd)
{
	if(hProgressWnd != NULL){
		if(IsIconic(hProgressWnd) != 0){
			ShowWindow(hProgressWnd, SW_RESTORE);
		}
		SetForegroundWindow(hProgressWnd);
		return;
	}
	if(CreateDialogParam(ghinst, MAKEINTRESOURCE(IDD_DIALOG_ARCHIVE_PROGRESS), NULL, ProgressProc, 0) == NULL){
		return;
	}
	ShowWindow(hProgressWnd, SW_SHOW);
}


/******************************************************************************

	ArchiveProgress_Close

	終了と再読み込みの通知で閉じる (DLL が解放されるため)
	再読み込みの場合は ArchiveProgress_Reopen で開き直す

******************************************************************************/

void ArchiveProgress_Close(BOOL Reload)
{
	HWND hMain;

	if(hProgressWnd == NULL){
		return;
	}
	if(Reload == TRUE && (hMain = Archive_GetMainWindow()) != NULL){
		SetProp(hMain, PROG_REOPEN_PROP, (HANDLE)1);
	}
	DestroyWindow(hProgressWnd);
}


/******************************************************************************

	ArchiveProgress_Reopen

	再読み込みの前に開いていた場合は開き直す (前面には出さない)

******************************************************************************/

void ArchiveProgress_Reopen(void)
{
	HWND hMain;

	if((hMain = Archive_GetMainWindow()) == NULL || GetProp(hMain, PROG_REOPEN_PROP) == NULL){
		return;
	}
	RemoveProp(hMain, PROG_REOPEN_PROP);
	if(hProgressWnd != NULL){
		return;
	}
	if(CreateDialogParam(ghinst, MAKEINTRESOURCE(IDD_DIALOG_ARCHIVE_PROGRESS), NULL, ProgressProc, 0) == NULL){
		return;
	}
	ShowWindow(hProgressWnd, SW_SHOWNOACTIVATE);
}
/* End of source */
