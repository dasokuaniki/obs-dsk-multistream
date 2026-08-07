# DSK Multistream 0.3.7 public beta guide

DSK Multistream is free and open-source software. This public beta is intended
for evaluation before a stable release. Download it only from
<https://dsk.dasoku.org/download> and compare the published SHA-256 value before
running the installer.

## Release status

- Google OAuth verification is approved for the bundled DSK Multistream desktop
  application and its YouTube Live scope.
- Development candidates are currently unsigned. They are not public release
  artifacts. Public downloads remain withheld until both the plugin DLL and the
  installer have valid Authenticode signatures from the DSK publisher.
- Do not disable Microsoft Defender, SmartScreen, Smart App Control, browser
  download protection, or other security software to install a candidate.

## Before installing

- Requires 64-bit OBS Studio 32 or later on Windows 10 version 1809 or later.
- Exit OBS Studio before installing, updating, or uninstalling the plugin.
- Twitch, YouTube, and Kick account connections are optional. Manual RTMP is
  available only when the destination has issued an RTMP/RTMPS URL and stream
  key directly to the user.
- TikTok login is not included in the public product. Do not publish or share a
  private TikTok RTMP URL or stream key.

## Updating and uninstalling

Run the newer installer to update the existing installation. Windows Settings >
Apps > Installed apps can uninstall DSK Multistream. Normal uninstall removes
the plugin and its installed files while preserving destinations and credentials
for reinstall. Choose complete removal in the uninstaller to remove only DSK
Multistream settings and `DSK Multistream/` Windows Credential Manager entries.
It does not remove DSK Comment Viewer or another DSK product.

## Reporting a beta issue

Send the OBS version, Windows version, the failed action, and relevant OBS log
lines to `support@dasoku.org`. Remove stream keys, OAuth codes, access or refresh
tokens, client secrets, and personal information before sending a log.

---

# DSK Multistream 0.3.7 公開ベータ案内

DSK Multistreamは無料のオープンソースソフトウェアです。この公開ベータは、
安定版公開前の評価を目的としています。インストーラーは
<https://dsk.dasoku.org/download> からのみ取得し、実行前に掲載されている
SHA-256と一致することを確認してください。

## 公開状態

- 同梱するDSK MultistreamデスクトップアプリとYouTube Liveスコープについて、
  Google OAuth verification is approved（GoogleのOAuth確認は承認済み）です。
- 現在の開発候補は未署名です。一般公開用の成果物ではありません。プラグインDLLと
  インストーラーの両方にDSK発行者の有効なAuthenticode署名が付くまで、公開
  ダウンロードは停止します。
- インストールのためにMicrosoft Defender、SmartScreen、Smart App Control、
  ブラウザーのダウンロード保護、その他のセキュリティ機能を無効にしないでください。

## インストール前の確認

- 64-bit版OBS Studio 32以降と、Windows 10 version 1809以降が必要です。
- インストール、更新、アンインストールの前にOBS Studioを終了してください。
- Twitch、YouTube、Kickのアカウント接続は任意です。Manual RTMPは、配信先から
  利用者本人へRTMP/RTMPS URLとストリームキーが発行されている場合だけ使えます。
- 公開版にTikTokログインは含みません。個人用のTikTok RTMP URLやストリームキーを
  公開・共有しないでください。

## 更新とアンインストール

新しいインストーラーを実行すると既存版を更新できます。Windowsの「設定 > アプリ >
インストールされているアプリ」からDSK Multistreamをアンインストールできます。
通常削除では、再インストールに備えて配信先設定と認証情報を保持します。
アンインストーラーで「完全削除」を選ぶと、DSK Multistream自身の設定と
Windows資格情報マネージャー内の `DSK Multistream/` 項目だけを削除します。
DSK Comment Viewerや他のDSK製品は削除しません。

## 不具合の報告

OBSとWindowsのバージョン、失敗した操作、関連するOBSログを
`support@dasoku.org` へ送ってください。送信前に、ストリームキー、OAuthコード、
アクセストークン、更新トークン、Client Secret、個人情報を削除してください。
