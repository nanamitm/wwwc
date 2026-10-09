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

- ソースファイルの文字コードは **Shift_JIS (CP932)**、改行は CRLF です。
- ライセンス条件は `docs/readme.txt` を参照してください (MD5 部分は RSA Data Security, Inc. のライセンス)。
