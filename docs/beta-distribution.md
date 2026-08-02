# DSK Multistream 0.3.4 limited beta guide

This build is for a limited beta of up to 20 invited users. Do not mirror or
redistribute the installer. DSK Multistream is free and open-source software;
the matching source is provided from the tagged public repository.

## Before installing

- Requires 64-bit OBS Studio 32 or later on Windows 10 version 1809 or later.
- Exit OBS Studio before installing, updating, or uninstalling the plugin.
- The installer and plugin are currently unsigned. Verify the published
  SHA-256 before running the installer. Do not disable Microsoft Defender,
  SmartScreen, the browser's download protection, or other security software.
- Google OAuth verification is still pending. Invited testers may see Google's
  unverified-app warning and must review the requested YouTube permission before
  continuing. If you do not accept it, YouTube login is unavailable; Twitch,
  Kick, and Manual RTMP can still be configured separately.
- TikTok is not a public login destination. Only use Manual RTMP when a service
  has issued an RTMP/RTMPS URL and stream key directly to your account.

## Uninstalling

Windows Settings > Apps > Installed apps can remove DSK Multistream. Normal
uninstall keeps destinations and credentials for reinstall. Choose complete removal
in the uninstaller to remove only DSK Multistream settings and Windows Credential
Manager entries. It does not remove DSK Comment Viewer or other DSK products.

## Reporting beta issues

Send the OBS version, Windows version, the action that failed, and relevant OBS
log lines to `support@dasoku.org`. Remove stream keys, OAuth codes, tokens,
client secrets, and personal information before sending a log.

---

# DSK Multistream 0.3.4 限定ベータ案内

このビルドは招待した最大20人向けの限定ベータです。インストーラーを転載・再配布
しないでください。DSK Multistreamは無料のオープンソースソフトウェアで、対応する
ソースはタグを固定した公開リポジトリから確認できます。

## インストール前

- 64-bit版OBS Studio 32以降とWindows 10 version 1809以降が必要です。
- インストール、更新、アンインストールの前にOBS Studioを終了してください。
- 現在のインストーラーとプラグインは未署名です。実行前に公開SHA-256を確認して
  ください。Microsoft Defender、SmartScreen、ブラウザのダウンロード保護などを
  無効にしないでください。
- Google OAuth審査は進行前です。招待テスターにはGoogleの未確認アプリ警告が表示
  される場合があります。YouTube権限の内容を確認し、同意できる場合だけ続行して
  ください。同意しない場合はYouTubeログインを使えませんが、Twitch、Kick、Manual
  RTMPは個別に設定できます。
- TikTokは公開ログイン先ではありません。サービスから自分のアカウント用の
  RTMP/RTMPS URLとストリームキーが発行された場合だけManual RTMPを使用してください。

## アンインストール

Windowsの「設定 > アプリ > インストールされているアプリ」から削除できます。通常
削除は再インストール用に配信先と認証情報を保持します。アンインストーラーで完全削除
を選ぶと、DSK Multistreamの設定とWindows資格情報だけを削除します。DSK Comment
Viewerや他のDSK製品は削除しません。

## ベータ不具合の報告

OBSバージョン、Windowsバージョン、失敗した操作、関連するOBSログ行を
`support@dasoku.org`へ送ってください。送信前にストリームキー、OAuthコード、
トークン、Client Secret、個人情報を削除してください。
