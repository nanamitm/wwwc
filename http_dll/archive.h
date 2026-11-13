/**************************************************************************

	WWWC (wwwc.dll)

	archive.h

	Copyright (C) 1996-2026 by Ohno Tomoaki. All rights reserved.
		https://www.nakka.com/
		nakka@nakka.com

**************************************************************************/

#ifndef _INC_ARCHIVE_H
#define _INC_ARCHIVE_H

/**************************************************************************
	Define
**************************************************************************/

#define ARCHIVE_ACTION				"openarchive"			/* アーカイブを開くメニューのアクション */
#define ARCHIVE_RES_PREFIX			"res/"				/* ページから見たリソースの場所 */
#define ARCHIVE_URL_MAX				4096				/* 扱うURLの最大長 */
#define ARCHIVE_GEN_MAX				999					/* 保存する世代数の上限 */
#define ARCHIVE_GEN_UNLIMITED			0					/* 世代数に上限を設けない */
#define ARCHIVE_BROWSER_WAIT			10000				/* ブラウザで読み込み後にスクリプトの実行を待つ時間 (ミリ秒) の既定値 */

/* リソースの種類 */
#define RES_LINK					0					/* 絶対URLにするのみ */
#define RES_IMAGE					1
#define RES_CSS						2
#define RES_JS						3
#define RES_FRAME					4					/* iframe, frame のページ */

/* 取得の段階 (進捗の表示用) */
#define ARCHIVE_PHASE_PAGE			0					/* ページの取得 */
#define ARCHIVE_PHASE_BROWSER		1					/* ブラウザで表示 */
#define ARCHIVE_PHASE_RES			2					/* 画像などの取得 */
#define ARCHIVE_PHASE_SAVE			3					/* 保存 */

/* 取得の結果 (進捗の表示用) */
#define ARCHIVE_RESULT_SAVED		0					/* 新しい世代を保存した */
#define ARCHIVE_RESULT_REPLACED		1					/* 最新の世代を置き換えた (画像などのみ変更) */
#define ARCHIVE_RESULT_SAME			2					/* 最新の世代と同じため保存しなかった */
#define ARCHIVE_RESULT_FAILED		3					/* ページを取得できなかった */
#define ARCHIVE_RESULT_NOTHTML		4					/* HTML とテキスト以外のため保存しなかった */
#define ARCHIVE_RESULT_DISKFULL		5					/* ディスクの空きが少ないため保存しなかった */
#define ARCHIVE_RESULT_ERROR		6					/* 保存できなかった */
#define ARCHIVE_RESULT_STOPPED		7					/* 中止した */

/* ブラウザで表示した内容の取得 (進捗の表示用) */
#define ARCHIVE_BROWSER_NONE		0					/* 使用しない */
#define ARCHIVE_BROWSER_USED		1					/* ブラウザで表示した内容を保存した */
#define ARCHIVE_BROWSER_FAILED		2					/* 使用できず取得したページを保存した */

/* ブラウザを使用できなかった理由 */
#define ARCHIVE_BERR_NONE			0
#define ARCHIVE_BERR_NOTFOUND		1					/* ブラウザが見つからない */
#define ARCHIVE_BERR_PROXY			2					/* プロキシの認証が必要 */
#define ARCHIVE_BERR_ARG			3					/* URL などをコマンドラインに渡せない */
#define ARCHIVE_BERR_START			4					/* 起動できない (BrowserCode は GetLastError の値) */
#define ARCHIVE_BERR_TIMEOUT		5					/* 時間内に終わらなかった */
#define ARCHIVE_BERR_EXIT			6					/* 終了コードが 0 以外 (BrowserCode) */
#define ARCHIVE_BERR_NOOUTPUT		7					/* 出力が無かった */
#define ARCHIVE_BERR_TOOLARGE		8					/* 出力が大きすぎる */
#define ARCHIVE_BERR_BOTCHECK		9					/* ボット対策の確認ページが表示された (取得したページを保存する) */

#define ARCHIVE_HISTORY_MAX			200					/* 進捗の表示で保持する履歴の数 */


/**************************************************************************
	Struct
**************************************************************************/

struct TPITEM;

/* 進捗の表示用のアイテムの情報 (Archive_GetStatus で作成したコピー) */
typedef struct _ARCHIVE_STATUS_ITEM {
	LONG Id;										/* 履歴の通し番号 */
	char *Title;
	char *Url;
	char Dir[MAX_PATH];
	int Phase;										/* ARCHIVE_PHASE_* (取得中のみ) */
	BOOL Cancel;									/* 中止している (取得中のみ) */
	int Result;										/* ARCHIVE_RESULT_* (履歴のみ) */
	DWORD Status;									/* ページの HTTP ステータス (0 は接続できなかった) */
	int Browser;									/* ARCHIVE_BROWSER_* */
	int BrowserError;								/* ARCHIVE_BERR_* (履歴のみ) */
	DWORD BrowserCode;								/* 終了コードなど (履歴のみ) */
	int ResCount;									/* 取得した画像などの数 */
	LONGLONG Total;									/* 取得した画像などのサイズ */
	DWORD Elapsed;									/* 取得にかかった時間 (ミリ秒) */
	SYSTEMTIME Time;								/* 取得を終えた日時 (履歴のみ) */
} ARCHIVE_STATUS_ITEM;

typedef struct _ARCHIVE_STATUS {
	BOOL Enabled;									/* アーカイブを使用する設定か */
	BOOL Running;									/* 取得中のページがあるか */
	ARCHIVE_STATUS_ITEM Now;						/* 取得中のページ */
	LONG Serial;									/* 取得待ちと履歴が変わると増える */
	BOOL Lists;										/* 取得待ちと履歴をコピーしたか */
	int QueueCnt;
	ARCHIVE_STATUS_ITEM *Queue;						/* 取得待ち (取得する順) */
	int HistoryCnt;
	ARCHIVE_STATUS_ITEM *History;					/* 履歴 (新しい順) */
} ARCHIVE_STATUS;

typedef struct _ARCHIVE_BUF {
	char *p;
	long len;
	long cap;
	BOOL err;
} ARCHIVE_BUF;

/* 絶対URLと種類からアーカイブしたファイル名を返す (アーカイブしない場合は NULL) */
typedef const char *(*ARCHIVE_RESOLVE)(void *ctx, const char *url, int type);


/**************************************************************************
	Function Prototypes
**************************************************************************/

//archivehtml.c
void Buf_Add(ARCHIVE_BUF *b, const char *s, long n);
void Buf_Str(ARCHIVE_BUF *b, const char *s);
void Buf_Free(ARCHIVE_BUF *b);
void Archive_Free(void *p);
BOOL Archive_ResolveUrl(const char *base, const char *rel, char *ret, int size);
char *Archive_RewriteCss(const char *css, long len, const char *base, const char *prefix,
					   ARCHIVE_RESOLVE cb, void *ctx, long *outlen);
char *Archive_RewriteHtml(const char *html, long len, const char *url, const char *charset, BOOL js, const char *prefix,
						ARCHIVE_RESOLVE cb, void *ctx, long *outlen);

//archive.c
void Archive_Init(void);
void Archive_End(void);
void Archive_EndWait(void);
void Archive_Reload(void);
void Archive_Restart(void);
int Archive_GetGeneration(const char *Option2);
BOOL Archive_FindBrowser(char *ret);
BOOL Archive_IsAuthItem(struct TPITEM *tpItemInfo);
void Archive_Request(HWND hWnd, struct TPITEM *tpItemInfo, BOOL Update);
BOOL Archive_Exists(HWND hWnd, struct TPITEM *tpItemInfo);
int Archive_Delete(HWND hWnd, struct TPITEM *tpItemInfo);
int Archive_Open(HWND hWnd, struct TPITEM *tpItemInfo);
int Archive_OpenDir(HWND hWnd, const char *dir);
BOOL Archive_GetRoot(HWND hWnd, char *ret);
HWND Archive_GetMainWindow(void);
void Archive_GetStatus(ARCHIVE_STATUS *st, LONG Serial);
void Archive_FreeStatus(ARCHIVE_STATUS *st);
void Archive_ClearHistory(void);
void Archive_Cancel(BOOL All);

//archiveprog.c
void ArchiveProgress_Show(HWND hWnd);
void ArchiveProgress_Close(BOOL Reload);
void ArchiveProgress_Reopen(void);

#endif
/* End of source */
