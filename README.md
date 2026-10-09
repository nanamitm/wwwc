# WWWC

WWWC Ver 1.1.3 (Webページ更新チェッカ) のソースコード。64bit (x64) ビルド対応のリファクタリング作業用リポジトリ。

オリジナル: Copyright (C) 1996-2018 by Ohno Tomoaki — https://www.nakka.com/

## 構成

```
WWWC.sln        Visual Studio ソリューション
WWWC/           本体 (WWWC.exe)
  res/          アイコン・ビットマップ等のリソース
http_dll/       HTTP/FILE プロトコル DLL (wwwc.dll)
docs/           オリジナルの readme / マニュアル / サンプル
```

## 注意

- ソースファイル (`.c` `.h` `.rc`) の文字コードは **UTF-8 (BOM なし)**、改行は CRLF です。コンパイラには `/source-charset:utf-8 /execution-charset:.932` を指定しているため、文字列リテラルは従来どおり Shift_JIS (CP932) で埋め込まれます。
- `docs/` 以下のファイルはオリジナルのまま Shift_JIS です (サンプルはそのまま設定ファイルとして使われるため)。
- ライセンス条件は `docs/readme.txt` を参照してください (MD5 部分は RSA Data Security, Inc. のライセンス)。
