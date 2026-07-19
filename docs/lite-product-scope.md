# DSK Multistream - Product Scope

> Historical pre-implementation scope. It contains the retired `OBS Main` target mode and is not the current product behavior. Use `README.md` for the implemented scope.

作成日: 2026-06-02

## 方針

最初に作るのは「全部入り」ではなく、軽くて迷わない同時配信プラグイン。
ただし縦型配信は「クロップできるだけ」では訴求にならないため、初期から縦専用レイアウトを作れる場所を持つ。

狙う位置は以下。

> DSK Multistream: 設定5分、軽くて落ちにくい同時配信。

競合に対する立ち位置:

- Aitum Multistream より軽い。
- Branch Output より分かりやすい。
- obs-multi-rtmp より現代的な UI とプリセットを持つ。
- Restream のようなクラウド依存を必須にしない。

## ターゲットユーザー

- OBS で Twitch / YouTube / Kick / TikTok 向けに横型と縦型を同時配信したい個人配信者。
- 横配信は OBS の既存シーンを使い、縦配信だけ別レイアウトで作りたいユーザー。
- 既存の obs-multi-rtmp は古く感じるが、Aitum 系は大きく感じるユーザー。
- 有料クラウドリレーではなく、自分の PC と回線で直接配信したいユーザー。

## MVP で入れるもの

### 配信先

- Twitch
- YouTube
- Kick
- Custom RTMP / RTMPS
- TikTok は Custom RTMP 扱い

TikTok は明示的な専用対応にしない。RTMP URL と Stream Key を持っている場合のみ Custom RTMP として使える。

### 出力方式

- 配信先ごとに `obs_output_t` を作る。
- encoder は出力グループ単位で共有する。
- 初期グループ:
  - OBS Main Encoder: OBS メイン配信 encoder を共有する軽量モード。
  - DSK Horizontal Encoder: DSK が横型用 encoder を 1本作り、横型配信先で共有する。
  - DSK Vertical Encoder: DSK が縦型用 encoder を 1本作り、縦型配信先で共有する。
- 各出力は `rtmp_custom` service を基本にする。
- RTMPS は URL として扱う。
- WHIP / SRT / FTL は MVP 外。

### UI

- OBS Dock。
- 横型は OBS 標準のメインキャンバス/シーンを使う。
- 縦型は DSK Vertical Layout Editor で作る。
- 縦型プレビュー。
- 配信先一覧。
- 配信先追加/編集/削除。
- `すべて開始` ボタン。
- `すべて停止` ボタン。
- 配信先ごとの `開始/停止` ボタン。
- 状態表示:
  - 未設定
  - 待機中
  - 開始中
  - 配信中
  - 再接続中
  - 停止中
  - エラー
- 配信前の軽量チェック。

### 縦型レイアウト

v0.1 から入れる。

- 9:16 キャンバス。
- 初期解像度 `1080x1920`。
- 既存 OBS シーンまたはソースを縦レイアウトへ追加できる。
- ソースごとに位置、拡大縮小、クロップを調整できる。
- 縦型プレビューを Dock 内に表示する。
- 横 OBS シーンとは別に保存する。
- 縦型 target は `DSK Vertical Encoder` に割り当てる。

初期テンプレート:

- `Full Screen`: 1ソースを縦画面いっぱいに表示。
- `Game + Camera`: ゲーム画面を中央、カメラを上または下に配置。
- `Camera First`: カメラを大きく、ゲーム/資料を小さく配置。
- `Center Crop`: 横画面を中央クロップしてすぐ始められる避難用。

### 設定

- 配信先名。
- platform。
- server URL。
- stream key。
- 出力グループ:
  - OBS Main
  - DSK Horizontal
  - DSK Vertical
- 有効/無効。
- メイン配信開始に追従するか。
- 再接続設定。

### プリセット

- Twitch 標準。
- YouTube RTMPS 標準。
- Kick 標準。
- Custom RTMP。

プリセットはソースコードにベタ書きせず、将来 JSON 更新可能な形にする。

### 診断

Lite では重い診断は入れない。ただし最低限は入れる。

- Stream Key 未入力。
- Server URL 未入力。
- RTMP/RTMPS 以外の URL 警告。
- 合計想定アップロード帯域の表示。
- OBS メイン配信出力が動いていない状態で encoder 共有出力を開始しようとした場合の警告。
- 出力失敗時の OBS stop code 表示。
- OBS log への詳細出力。

## MVP で入れないもの

- 플랫폼別の個別エンコード。
- YouTube Live API。
- Twitch OAuth。
- Kick API。
- TikTok API。
- チャット統合。
- 配信タイトル/カテゴリ同期。
- 予約配信作成。
- クラウドリレー。
- ローカル FFmpeg リレー。
- ソース/シーン単位の高度な Branch Output。
- 詳細な GPU/CPU 負荷予測。
- インストール済みプラグイン管理。
- カスタム toolbar。

これらは Full / Pro 版候補として残す。

## 成功条件

v0.1 が成功と言える条件:

- Twitch / YouTube / Kick / Custom RTMP の 3-4 宛先に同時送信できる。
- OBS メイン配信 encoder 共有でも動く。
- DSK Horizontal Encoder で OBS メイン配信なしの横型追加配信ができる。
- DSK Vertical Layout Editor で 9:16 レイアウトを作れる。
- DSK Vertical Encoder で縦型配信先へ送信できる。
- Dock から `すべて開始`、`すべて停止`、配信先ごとの個別開始/停止ができる。
- 1宛先が切断されても、他宛先の配信を止めない。
- どの宛先が失敗したか Dock で分かる。
- 設定が OBS 再起動後も保持される。
- Stream Key が UI 上で常時マスクされる。
- OBS Studio 32.1.2 以上でクラッシュせずにロード/アンロードできる。

## 非目標

Lite 版は「最高画質」や「平台別完全最適化」を狙わない。

YouTube は Twitch/Kick より高いビットレートを推奨する場合があるが、MVP では配信先ごとの完全個別 encoder までは入れない。まずは `OBS Main`、`DSK Horizontal`、`DSK Vertical` の 3 グループで扱う。

## ネーミング候補

- DSK Multistream

正式名称は `DSK Multistream` とする。

実装名:

- repository / module name: `obs-dsk-multistream`
- UI name: `DSK Multistream`

## 将来の Full 版に残す拡張軸

- 高度な Vertical Canvas。
- Platform API。
- OAuth。
- 配信前診断の高度化。
- ローカル/クラウドリレー。
- ソース別/平台別レイアウト。
- WebSocket vendor API。
- Toolbar。
- Team/Creator 設定同期。
