# Pre-Implementation Checklist

> Historical checklist. It is not a current specification and still mentions the retired `OBS Main` target mode.

作成日: 2026-06-02

## 現在の結論

まずは Lite 版を作る。

プロダクト名の仮称:

`DSK Multistream`

目的:

- OBS から Twitch / YouTube / Kick / Custom RTMP へ軽量に同時配信する。
- OBS メイン配信 encoder 共有に加え、DSK 管理の横型/縦型 encoder group を持てるようにする。
- 横型は OBS 標準キャンバスを使い、縦型は DSK Vertical Layout Editor で作る。
- OAuth、API、クラウドリレーは後回しにする。

## 実装開始前に確定していること

### スコープ

- [x] Twitch 対応。
- [x] YouTube 対応。
- [x] Kick 対応。
- [x] Custom RTMP / RTMPS 対応。
- [x] TikTok は Custom RTMP 扱い。
- [x] 縦型レイアウト編集は v0.1 に入れる。
- [x] OAuth / platform API は v0.1 外。
- [x] クラウド/ローカルリレーは v0.1 外。

### 技術方針

- [x] C++ OBS native plugin。
- [x] OBS Dock UI。
- [x] `obs_service_create("rtmp_custom")`。
- [x] `obs_output_create` で target ごとの output を作る。
- [x] OBS メイン配信 output の video/audio encoder を共有できる。
- [x] DSK Horizontal encoder group を初期設計に入れる。
- [x] DSK Vertical encoder group を初期設計に入れる。
- [x] Horizontal layout は OBS Main Canvas を使う。
- [x] Vertical layout は DSK private scene/view として持つ。
- [x] output signal handler で状態管理。
- [x] platform preset は JSON 化する。

### 競合から参考にするもの

- [x] Aitum Multistream: Dock 構成、service/output 作成フロー。
- [x] obs-multi-rtmp: signal handler、reconnect UI、統計。
- [x] Branch Output: reconnect timeout、status dock、将来 branch output。
- [x] StreamUP: 将来の toolbar / settings UX。
- [x] Stream Sprout: 将来の relay。

## 実装開始前に決めるべき残件

### 1. ライセンス

推奨:

- GPL-2.0-or-later。

理由:

- OBS 系競合が GPL-2.0 多数。
- OBS plugin として配布するなら GPL 互換が現実的。
- 既存 GPL 実装を参考にしやすい。

確認:

- [x] GPL-2.0-or-later にする。

### 2. プラグイン名

候補:

- `obs-dsk-multistream`

推奨:

- repository / module name: `obs-dsk-multistream`
- UI name: `DSK Multistream`

確認:

- [x] 最終名称は `DSK Multistream`。

### 3. 対応 OBS バージョン

推奨:

- v0.1: OBS 32.1.2 以上、Windows x64。
- macOS/Linux は v0.2 以降。

確認:

- [x] 2026-06-02 時点の最新安定版 OBS Studio 32.1.2 を主ターゲットにする。
- [x] 古い OBS 30/31 は alpha では非対応。
- [ ] Windows x64 のみで alpha を切るか。

### 4. Stream Key 保存方式

選択肢:

1. v0.1 は OBS config に保存、UI/log はマスク。
2. 初回から Windows Credential Manager を使う。

推奨:

- v0.1 は既存競合プラグインにならい OBS config に保存し、UI/log はマスクする。
- v0.2 で OS credential store に移行。

理由:

- 最初から OS 別 credential store を入れると軽量版の初速が落ちる。
- ただし log マスクは v0.1 必須。

確認:

- [x] v0.1 では OBS config 保存を許容する。Stream Key は password field、ログマスク必須。

### 5. メイン配信未開始時の挙動

選択肢:

1. 追加出力開始を拒否する。
2. plugin 側で OBS メイン配信も自動開始する。

推奨:

- `OBS Main` group の target は、OBS メイン配信未開始なら警告して拒否する。
- `DSK Horizontal` / `DSK Vertical` group の target は、OBS メイン配信未開始でも開始できる。
- `すべて開始` は group ごとの条件を見て、開始できる target は開始し、開始できない target は警告に出す。

理由:

- OBS Main group は encoder 共有の都合で main output が必要。
- DSK group は自前 encoder を持つため、YouTube だけ配信や縦型だけ配信も可能にできる。
- `すべて開始` で OBS メイン配信を勝手に開始すると事故りやすい。

確認:

- [x] `すべて開始` で OBS メイン配信は勝手に開始しない。
- [x] `OBS Main` group は OBS メイン配信中のみ開始可能。
- [x] `DSK Horizontal` / `DSK Vertical` group は OBS メイン配信なしでも開始可能。

### 5.1 開始/停止操作

確定:

- [x] Dock に `すべて開始` を置く。
- [x] Dock に `すべて停止` を置く。
- [x] 配信先ごとに個別 `開始/停止` を置く。
- [x] 個別停止は他の配信先に影響させない。
- [x] `すべて開始` は一部失敗しても他の配信先の開始を継続する。
- [x] `すべて停止` は開始中/配信中/再接続中の全 target を停止対象にする。

### 6. YouTube の画質警告

推奨:

- Lite では YouTube もメイン encoder 共有。
- YouTube target には「高画質最適化は Full 版予定」と warning を出す。

確認:

- [ ] warning 文言。

### 7. プリセット更新

v0.1:

- 同梱 JSON のみ。

v0.2:

- GitHub Releases などからリモート更新。

確認:

- [ ] v0.1 ではリモート更新なしでよいか。

## 実装時の最初の作業順

1. OBS plugin template を導入。
2. `DSK Multistream` Dock を出す。
3. `OutputTarget` / `SettingsStore`。
4. `LayoutManager` と DSK Vertical Layout Editor。
5. Add/Edit dialog。
6. `OutputSession` で 1 target 開始。
7. signal handler。
8. 一括開始/停止。
9. preflight。
10. log mask。
11. 手動テスト。

## 重要な実装メモ

### output lifecycle

`obs_output_t` と `obs_service_t` は必ず target session 単位で ownership を明確にする。

停止時:

- signal disconnect。
- active なら stop。
- reconnecting 固着なら timeout 後 force stop。
- service を output から外す。
- release。

### UI thread

OBS output signal は UI thread とは限らない。

UI 更新は Qt の queued invocation 等で main thread に戻す。

### Stream Key

禁止:

- `blog(LOG_INFO, "... %s", streamKey)`。
- server URL と key を結合した文字列をログに出す。
- エラーダイアログに key を含める。

### URL validation

Lite v0.1:

- `rtmp://`
- `rtmps://`

その他は warning/error。

WHIP/SRT は将来対応。

## Alpha テスト手順案

### ローカル疎通

目的:

- 本番平台を使わず output が開始できるか見る。

候補:

- ローカル RTMP server。
- FFmpeg/NGINX RTMP。
- 将来 Stream Sprout 連携。

### 実平台テスト

最小:

- Twitch 1件。
- YouTube 1件。
- Kick 1件。

確認:

- 1平台だけ key を間違えた場合、他平台が継続する。
- OBS メイン配信停止時、追加出力も止まる。
- target 個別停止で他 target が継続する。
- 再接続状態が UI に出る。

## 実装開始の Go / No-Go

Go 条件:

- [x] ライセンス決定。GPL-2.0-or-later。
- [x] plugin name 決定。`DSK Multistream` / `obs-dsk-multistream`。
- [x] v0.1 の Stream Key 保存方式決定。既存競合プラグインにならい OBS config 保存、UI/log はマスク。
- [x] OBS 対応バージョン決定。v0.1 は OBS Studio 32.1.2 以上。
- [x] OBS streaming start 時の挙動決定。DSK dock の On target だけ開始する。

上記が決まれば実装に入れる。
