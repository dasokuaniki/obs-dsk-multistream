# DSK Multistream - Implementation Backlog

> Historical implementation backlog. Completed and retired items, including the old `OBS Main` target mode, are retained only as design history.

作成日: 2026-06-02

## Milestone 0: 開発土台

目的: OBS にロードされ、Dock が出るだけの最小プラグインを作る。

### M0-1 OBS plugin template 導入

作業:

- OBS plugin template 由来の CMake 構成を作る。
- plugin name を `obs-dsk-multistream` にする。
- Windows x64 を最初の対応対象にする。

受け入れ条件:

- CMake configure が通る。
- OBS が plugin をロードできる。
- OBS log に plugin version が出る。

### M0-2 空 Dock 表示

作業:

- `obs_frontend_add_dock_by_id` で Dock を登録する。
- Dock title を `DSK Multistream` にする。
- 空状態メッセージと Add ボタンを表示する。

受け入れ条件:

- OBS 起動後に Dock が表示される。
- Dock の表示/非表示が OBS layout に保存される。
- plugin unload 時にクラッシュしない。

### M0-3 locale 構成

作業:

- `en-US.ini`
- `ja-JP.ini`

受け入れ条件:

- 日本語 OBS で日本語表示できる。
- 英語 OBS で英語表示できる。

## Milestone 1: 配信先管理

目的: 配信先を追加/編集/削除し、OBS 再起動後も保持する。

### M1-1 OutputTarget モデル

項目:

- `id`
- `enabled`
- `name`
- `platform`
- `server`
- `streamKey`
- `encoderGroup`
- `followMainStream`
- `autoReconnect`

受け入れ条件:

- UUID つき target を作れる。
- validation error を返せる。

### M1-2 SettingsStore

作業:

- plugin config または profile config に JSON 保存する。
- schema version を保存する。
- load/save/migrate の入口を用意する。

受け入れ条件:

- target を追加後、OBS 再起動で復元される。
- 不正 JSON でも plugin が落ちず、空設定として起動する。
- Stream Key は log に出ない。

### M1-3 PlatformPresetRegistry

作業:

- `data/presets/platforms.json` を読む。
- Twitch / YouTube / Kick / Custom RTMP を初期定義する。
- icon は初期実装では省略可。

受け入れ条件:

- Add dialog で platform を選べる。
- platform 選択で server URL が自動入力される。

### M1-4 TargetEditDialog

作業:

- platform 選択。
- name 入力。
- server URL 入力。
- stream key 入力。
- enabled。
- follow main stream。

受け入れ条件:

- 必須項目が未入力なら保存できない。
- Stream Key は password field。
- 編集時も Stream Key はマスクされる。

### M1-5 MainDock target list

作業:

- 配信先カードを表示。
- enabled toggle。
- edit/delete。
- status badge。

受け入れ条件:

- target 追加/編集/削除が即 UI に反映される。
- 削除時は確認ダイアログを出す。

## Milestone 1.5: 縦型レイアウト

目的: TikTok/Shorts 系に訴求できる 9:16 レイアウトを OBS 内で作れるようにする。

### M1.5-1 LayoutManager

作業:

- Horizontal layout は OBS Main Canvas として扱う。
- Vertical layout 用に private `obs_scene_t` / `obs_view_t` を作る。
- Vertical layout の解像度を `1080x1920` にする。
- layout 設定を保存/読み込みする。

受け入れ条件:

- OBS 起動後に縦レイアウトが復元される。
- 縦レイアウト用 private scene/view を作成できる。
- plugin unload 時に scene/view を安全に解放できる。

### M1.5-2 VerticalLayoutEditor

作業:

- 既存 OBS シーンまたはソースを縦レイアウトへ追加する。
- ソースの位置、スケール、クロップを編集する。
- fit/fill を切り替える。
- レイヤー順を変更する。
- safe area overlay を表示する。

受け入れ条件:

- Dock 内で 9:16 プレビューを見ながら編集できる。
- 縦レイアウトに複数ソースを配置できる。
- `Game + Camera` のような縦向け構図を作れる。
- 編集内容が OBS 再起動後も保持される。

### M1.5-3 Vertical templates

作業:

- `Full Screen`
- `Game + Camera`
- `Camera First`
- `Center Crop`

受け入れ条件:

- 既存 OBS シーンを選ぶだけで初期縦レイアウトを生成できる。
- テンプレート適用後も手動調整できる。

## Milestone 2: 出力開始/停止

目的: OBS メイン encoder 共有、または DSK 管理 encoder group を使って、追加 RTMP 出力を開始/停止できる。

現在の実装状態:

- `OBS Main` output path は実装済み。
- `DSK Horizontal` independent x264/AAC output path は実装済み。
- `DSK Vertical` layout editor と private scene builder は実装済み。
- `DSK Vertical` は OBS 32.x canvas/video output に接続する実装を追加済み。

### M2-1 ObsEncoderResolver

作業:

- `OBS Main` group の場合、`obs_frontend_get_streaming_output` を取得する。
- `OBS Main` group の場合、main output active を確認する。
- `OBS Main` group の場合、video encoder を取得する。
- `OBS Main` group の場合、audio encoder 0 を取得する。
- `DSK Horizontal` / `DSK Vertical` group の場合、`EncoderProfileManager` の encoder を取得する。

受け入れ条件:

- `OBS Main` group でメイン配信未開始時は `MainOutputNotActive` を返す。
- `DSK Horizontal` / `DSK Vertical` group は OBS メイン配信未開始でも encoder を返せる。
- encoder が取れない時はクラッシュせず error を返す。

### M2-1.5 EncoderProfileManager

作業:

- `OBS Main` profile を扱う。
- `DSK Horizontal` profile を作る。
- `DSK Vertical` profile を作る。
- DSK-managed video/audio encoder を作成/再利用/破棄する。
- profile 設定を保存する。

受け入れ条件:

- `DSK Horizontal` group の target を OBS メイン配信なしで開始できる。
- `DSK Vertical` group 用 encoder を作成できる。
- 同じ group の複数 target は同じ encoder を共有する。
- group の最後の target 停止後、encoder を安全に解放できる。

### M2-2 ObsServiceFactory

作業:

- `rtmp_custom` service を作る。
- server / key を設定する。
- service preferred output type を取得する。

受け入れ条件:

- Twitch/YouTube/Kick/Custom の service を作れる。
- service 作成失敗時に error を返す。

### M2-3 OutputSession start

作業:

- `obs_output_create`
- `obs_output_set_service`
- `obs_output_set_reconnect_settings`
- `obs_output_set_video_encoder`
- `obs_output_set_audio_encoder`
- `obs_output_start`

受け入れ条件:

- 1 target を個別開始できる。
- start 失敗時に UI が Error になる。
- OBS log に target 名と error が出る。

### M2-4 OutputSession stop/release

作業:

- active 時は `obs_output_stop`。
- reconnect 固着時は timeout 後 `obs_output_force_stop`。
- signal disconnect。
- service/output release。

受け入れ条件:

- 個別停止できる。
- 停止後に再開始できる。
- target 削除時に output leak しない。

### M2-5 OutputManager

作業:

- `startTarget`
- `stopTarget`
- `startAll`
- `stopAll`

受け入れ条件:

- enabled target だけ一括開始する。
- 一部 target が失敗しても他 target の開始を続ける。
- 一括停止で全 target を止める。
- 個別開始で指定 target だけ開始し、他 target の状態を変えない。
- 個別停止で指定 target だけ停止し、他 target の配信を継続する。
- `すべて開始` / `すべて停止` / 個別 `開始/停止` が Dock から操作できる。

## Milestone 3: 状態表示と再接続

目的: 失敗した宛先が分かる。

### M3-1 ObsSignalBridge

監視 signal:

- `starting`
- `start`
- `reconnect`
- `reconnect_success`
- `stopping`
- `stop`

受け入れ条件:

- signal ごとに OutputState が変わる。
- UI thread で安全に Dock が更新される。

### M3-2 OutputStatus UI

表示:

- status badge。
- sent bytes。
- elapsed time。
- reconnecting。
- last error。

受け入れ条件:

- 配信開始後、状態が Live になる。
- 再接続中は Reconnecting になる。
- stop code が異常なら Error 表示。

### M3-3 軽量統計

作業:

- `obs_output_get_total_bytes`
- `obs_output_get_total_frames`
- 必要なら dropped frames / congestion を調査。

受け入れ条件:

- target ごとに送信量が更新される。
- 更新周期は 1 秒程度。
- UI 更新で OBS が重くならない。

## Milestone 4: 配信前チェック

目的: 配信事故を減らす。

### M4-1 DiagnosticsEngine

チェック:

- enabled target が 1件以上ある。
- server URL がある。
- Stream Key がある。
- URL scheme が RTMP/RTMPS。
- main output active。
- 合計推奨ビットレート表示。

受け入れ条件:

- OBS streaming start 前に warning/error 一覧が出る。
- error がある場合は開始しない。
- warning はユーザーが続行できる。

### M4-2 Platform warning

表示:

- YouTube は高画質には個別 encoder が必要になる場合がある。
- TikTok は Custom RTMP のみで、キーがない場合は使えない。
- Twitch は同時配信ガイドライン確認が必要。

受け入れ条件:

- platform ごとの注意が Add/Edit または Preflight に表示される。
- 長文で邪魔にならない。

## Milestone 5: 品質と配布準備

目的: 使える alpha にする。

### M5-1 ログマスク

作業:

- Stream Key を含む文字列を log に出さない。
- server URL と key の結合文字列を作らない。

受け入れ条件:

- OBS log に Stream Key が出ない。
- error message に key が混ざらない。

### M5-2 Manual test matrix

対象:

- OBS Studio 32.1.2 以上
- Windows x64
- Twitch test
- YouTube test
- Kick test
- Custom local RTMP test

受け入れ条件:

- 手順書に従って同じ結果を再現できる。

### M5-3 Package

作業:

- zip artifact。
- Windows installer は v0.2 以降でも可。

受け入れ条件:

- 手動配置で OBS が plugin を認識する。
- README に導入手順がある。

## 優先しないタスク

以下は Lite v0.1 では扱わない。

- 配信先ごとの完全個別 encoder UI。
- OAuth。
- API 連携。
- cloud/local relay。
- custom toolbar。
- WebSocket API。
- macOS/Linux package。

## v0.1 Definition of Done

- OBS Dock が表示される。
- Twitch / YouTube / Kick / Custom RTMP を追加できる。
- 設定が保存される。
- OBS メイン配信開始後、追加出力を `すべて開始` / `すべて停止` / 個別開始 / 個別停止できる。
- DSK Horizontal group の出力は OBS メイン配信なしでも開始できる。
- DSK Vertical Layout Editor で 9:16 レイアウトを作れる。
- DSK Vertical group の target が Vertical Layout を送信できる。
- 出力ごとの Live / Reconnecting / Error / Idle が分かる。
- 一部出力失敗で plugin 全体が落ちない。
- Stream Key が UI と log で保護される。
- Windows x64 で手動導入できる。

## v0.2 候補

- OS credential store。
- WHIP 対応。
- SRT 対応。
- platform icon。
- status dock の詳細化。
- local FFmpeg relay 実験。
- Aitum Vertical 検出と案内。

## v1.0 候補

- 配信先ごとの完全個別 encoder。
- 高度な縦型キャンバス編集 UI。
- YouTube Live API。
- Twitch OAuth。
- Kick API。
- WebSocket vendor API。
- Windows installer。
- macOS/Linux。
