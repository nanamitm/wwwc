# WWWC

WWWC Ver 1.1.3 (Webページ更新チェッカ) のソースコードです。x86 (32bit) に加えて x64 (64bit) でもビルドできるようにリファクタリングしています。

オリジナル: Copyright (C) 1996-2018 by Ohno Tomoaki — https://www.nakka.com/

## 構成

```
WWWC.sln        Visual Studio ソリューション
WWWC/           本体 (WWWC.exe)
  res/          アイコン・ビットマップ等のリソース
http_dll/       HTTP/FILE プロトコル DLL (wwwc.dll)
  wwwcdll.h     プラグイン (プロトコル DLL / ツール DLL) 用の公開ヘッダ
docs/           オリジナルの readme / マニュアル / サンプル
tests/          自動テスト (run-tests.ps1)
.github/        GitHub Actions のワークフロー (ビルドとテスト)
```

## ビルド

必要なもの: Visual Studio 2026 (プラットフォームツールセット v145) と「C++ によるデスクトップ開発」ワークロード、Windows 10/11 SDK。

Visual Studio で `WWWC.sln` を開き、構成 (`Debug` / `Release`) とプラットフォーム (`x86` / `x64`) を選んでビルドします。コマンドラインの場合:

```bat
MSBuild WWWC.sln -p:Configuration=Release -p:Platform=x64
```

出力先:

| プラットフォーム | 出力フォルダ |
|---|---|
| x86 | `Debug\` / `Release\` |
| x64 | `x64\Debug\` / `x64\Release\` |

`WWWC.exe` と `wwwc.dll` を同じフォルダに置いて実行します。データの保存先は起動オプション `/d:"フォルダ"` で変更できます。

- Windows XP はサポート対象外です (v141_xp ツールセットは使用していません)。
- ダークモードに対応しています。オプションの「全般」タブの「表示テーマ」(ライト / ダーク / Windows の設定に従う) で切り替えます (user.ini の `[GENERAL] DarkMode` = 0 / 1 / 2、既定は 2 = Windows の設定に従う)。Windows 10 1809 以降で有効です。
- HTTPS のサーバ証明書は検証します。自己署名証明書のサーバなどは、アイテムのプロパティ「オプション」タブの「証明書のエラーを無視する」で個別に許可できます (オリジナルの 1.1.3 は常に無視していました)。
- HTTP のパスワード (アイテムの認証用、プロキシ認証用) は DPAPI (`CryptProtectData`, マシン単位) で暗号化して保存します (`DP1:` で始まる値)。以前の形式 (単純な変換) で保存されている値は、読み込み時に自動で暗号化し直します。暗号化した値は別の PC では復号できないため、データを別の PC に移した場合はパスワードを入力し直してください。また、オリジナルの 1.1.3 では読めません。
- ポインタの切り捨てにつながる警告 (C4113, C4133, C4267, C4311, C4312) はエラーとして扱います。

## テスト

ビルドした `WWWC.exe` / `wwwc.dll` に対して自動テストを実行します。Python 3 と PowerShell 7 (`pwsh`) が必要です。

```bat
pwsh tests\run-tests.ps1 -BinDir x64\Release
pwsh tests\run-tests.ps1 -BinDir Release -Suite Http,Password
```

- テストごとに `%TEMP%\wwwc-tests` に WWWC をコピーしてデータを用意し、ウィンドウメッセージで操作して結果を確認します。マウスやキーボードは使いませんが、WWWC のウィンドウが表示されるのでデスクトップのあるセッションで実行してください。
- HTTP のテストはローカルのテストサーバ (`tests/server/test_server.py`、127.0.0.1 - 127.0.0.5 のみで待ち受け) を使います。外部には接続しません。
- スイート: `Parse` (Item.dat / user.ini の読み込み)、`Http` (チェック、リダイレクト、エラー、タイムアウト、キャンセル、チェック中の終了)、`Password` (パスワードの暗号化と移行)、`Tools` (ヘッダ表示、ソース表示、タイトル取得)。
- 失敗したテストがあると終了コード 1 を返します。

## CI

GitHub Actions (`.github/workflows/build.yml`) で、`main` への push と pull request ごとに 4 構成 (Debug / Release × x86 / x64) をビルドし、Release の x86 / x64 でテストを実行します。

- 警告はエラーとして扱います (`-warnAsError`)。
- ランナーに v145 (VS 2026) がない場合は v143 (VS 2022) でビルドします (ジョブの通知に表示されます)。
- Release のバイナリはアーティファクト (`wwwc-x86` / `wwwc-x64`、14 日間保存) としてダウンロードできます。

## プラグイン (プロトコル DLL / ツール DLL) を作る場合

WWWC.exe はプラグインの関数を `GetProcAddress` で取得して呼び出します。

- **64bit 版の WWWC.exe が読み込めるのは 64bit の DLL だけです。** 既存の 32bit プラグインは 64bit 版では使えません。
- 64bit 版用のプラグインは、このリポジトリの `http_dll/wwwcdll.h` でビルドし直してください。`struct TPITEM` の次のメンバーは、ポインタと同じサイズ (`LONG_PTR` / `UINT_PTR`) に変わっています。32bit 版ではサイズが変わらないため、32bit プラグインの互換性は保たれます。
  - `hItem` (`LONG_PTR`)
  - `Soc1`, `Soc2` (`UINT_PTR`、`SOCKET` を格納)
  - `Param1` 〜 `Param4` (`LONG_PTR`、ポインタを格納してよい)
- 関数はすべて `CALLBACK` (`__stdcall`) です。x86 版でエクスポート名に装飾が付かないよう、`.def` ファイルでエクスポートしてください (`http_dll/WWWC.def` を参照)。

プロトコル DLL の関数 (`XXX_` はプロトコル設定の「ヘッダ」):

```c
int    CALLBACK GetProtocolList(int cnt, struct TPPROTOCOLSET *tpProtocolSet);
int    CALLBACK XXX_GetInfo(struct TPPROTOCOLINFO *tpInfo);
int    CALLBACK XXX_Initialize(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Start(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Timer(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Cancel(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_ItemCheckEnd(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_InitItem(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_FreeItem(struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Gethost(HWND hWnd, WPARAM wParam, LPARAM lParam, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Select(HWND hWnd, WPARAM wParam, LPARAM lParam, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_ExecItem(HWND hWnd, char *Action, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_Property(HWND hWnd, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_ProtocolProperty(HWND hWnd);
HANDLE CALLBACK XXX_GetItemText(struct TPITEM *tpItemInfo);
BOOL   CALLBACK XXX_CreateDropItem(struct TPITEM *tpItemInfo, char *fPath, char *iPath, char *ret);
int    CALLBACK XXX_DropFile(HWND hWnd, char *FileType, char *FilePath, struct TPITEM *tpItemInfo);
int    CALLBACK XXX_EndNotify(void);
```

ツール DLL の関数:

```c
int CALLBACK GetToolList(int cnt, struct TP_TOOLS *tpToolInfo);
int CALLBACK GetToolHideOption(char *FuncName);
int CALLBACK ツール関数名(HWND hWnd, struct TPITEM **ToolItemList, int ToolItemListCnt, int type, int CheckType);
```

WWWC.exe 側の対応する関数ポインタ型は `WWWC/General.h` (`PROTOCOL_ITEM_FUNC` など) で定義しています。

プラグインのダイアログ (WWWC と同じスレッドで作成されるもの) には、WWWC がダークモードを自動で適用します。リッチエディットの内容やオーナードローなど、プラグインが自分で色を決めている部分は、`SendMessage(WWWCのメインウィンドウ, WM_GETDARKMODE, 0, 0)` (TRUE ならダーク表示中、未対応の WWWC は 0) で判定して切り替えてください。

## サードパーティ

- [win32-darkmodelib](https://github.com/ozone10/win32-darkmodelib) (`third_party/darkmodelib`, git submodule): ダークモード表示に使用。MPL-2.0 (一部 MIT)。ライセンス本文は submodule 内の `LICENSE.md` / `LICENSE-MIT.md` / `docs/LICENSE-*.md` を参照。
  クローン時は `git clone --recursive`、または `git submodule update --init` を実行してください。

## 注意

- ソースファイル (`.c` `.h` `.rc`) の文字コードは **UTF-8 (BOM なし)**、改行は CRLF です。コンパイラには `/source-charset:utf-8 /execution-charset:.932` を指定しているため、文字列リテラルは従来どおり Shift_JIS (CP932) で埋め込まれます。
- `docs/` 以下のファイルはオリジナルのまま Shift_JIS です (サンプルはそのまま設定ファイルとして使われるため)。
- ライセンス条件は `docs/readme.txt` を参照してください (MD5 部分は RSA Data Security, Inc. のライセンス)。
