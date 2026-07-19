# OBS multistream competitor research

調査日: 2026-06-02

## 調査対象

| 名前 | 種別 | ソース | ライセンス | 直接競合度 |
|---|---|---|---|---|
| Aitum Multistream | OBS マルチ配信プラグイン | https://github.com/Aitum/obs-aitum-multistream | GPL-2.0 | 高 |
| Multiple RTMP Outputs / obs-multi-rtmp | OBS 複数 RTMP 出力 | https://github.com/sorayuki/obs-multi-rtmp | GPL-2.0 | 高 |
| Aitum Vertical | OBS 縦型キャンバス | https://github.com/Aitum/obs-vertical-canvas | GPL-2.0 | 高 |
| Branch Output | ソース/シーン単位の配信/録画出力 | https://github.com/OPENSPHERE-Inc/branch-output | GPL-2.0 | 中-高 |
| Source Record | ソース単位出力フィルタ | https://github.com/exeldro/obs-source-record | GPL-2.0 | 中 |
| StreamUP | OBS 操作性改善ツールキット | https://github.com/StreamUPTips/obs-streamup | GPL-2.0 | UX 競合 |
| Stream Sprout | ローカル RTMP リレー | https://github.com/wimpysworld/stream-sprout | Apache-2.0 | 補完技術 |
| Restream Vertical OBS plugin | 縦横同時配信向け OBS プラグイン | https://support.restream.io/en/articles/11730141-stream-vertical-and-horizontal-at-the-same-time-from-obs | 不明/非公開 | サービス競合 |

## 競合別メモ

### Aitum Multistream

特徴:

- OBS Dock にメインキャンバス/縦キャンバスの出力一覧を表示する。
- Twitch, YouTube, TikTok, Facebook, Trovo, X, Kick, Other をウィザードで追加できる。
- `rtmp-services` の `services.json` / Twitch ingest cache を使って平台候補を出している。
- Aitum Vertical がある場合、グローバル `proc_handler` 経由で縦配信設定を読み書きする。
- WHIP URL は `whip_custom`、それ以外は `rtmp_custom` を使う。
- 通常モードではメイン配信出力の encoder を共有する。Advanced では個別 video/audio encoder を作成できる。

参考箇所:

- `competitors/obs-aitum-multistream/multistream.cpp`
- `competitors/obs-aitum-multistream/config-dialog.cpp`
- `competitors/obs-aitum-multistream/output-dialog.cpp`
- `competitors/obs-aitum-multistream/config-utils.cpp`

使える設計:

- メイン/縦キャンバスを同じ Dock に並べる構造。
- `rtmp-services` のサービス定義を利用する方式。
- URL から平台アイコンを推定する軽量 UX。
- `obs_service_create` -> `obs_output_create` -> encoder attach -> `obs_output_start` の基本フロー。
- Aitum Vertical 連携のような `proc_handler` による疎結合連携。

弱点/差別化余地:

- Stream Key を OBS data に直接保持しているように見える。OS credential store を使う余地がある。
- 配信前診断、帯域見積もり、平台別制約チェックは薄い。
- `obs_output_start(output)` の戻り値や停止コードの UX 化が弱い。
- TikTok は手動 RTMP 前提。API/資格/地域制限の診断はない。
- platform adapter 層は薄く、URL 文字列判定が中心。

### obs-multi-rtmp

特徴:

- かなり実戦的な複数 RTMP 出力プラグイン。
- output signal handler を持ち、`starting/start/reconnect/reconnect_success/stopping/stop` を UI に反映する。
- メイン配信/録画 encoder の共有、個別 encoder の作成、追加 audio track に対応。
- `obs_output_get_total_bytes` / `obs_output_get_total_frames` を使い統計を表示する。
- delay 設定、service/output release、force stop などの後始末が比較的丁寧。

参考箇所:

- `competitors/obs-multi-rtmp/src/push-widget.cpp`
- `competitors/obs-multi-rtmp/src/output-config.cpp`
- `competitors/obs-multi-rtmp/src/edit-widget.cpp`

使える設計:

- 出力ごとの状態機械。
- reconnect signal の UI 反映。
- encoder 共有/個別設定の切り替え。
- OBS 出力の統計取得。

弱点/差別化余地:

- 縦横キャンバスの統合は主目的ではない。
- 平台ごとの配信前チェック、OAuth/API、タイトル/カテゴリ同期はない。
- UX は機能寄りで、初心者向けの事故防止にはまだ余地がある。

### Aitum Vertical

特徴:

- 縦型キャンバス、縦シーン/ソース/トランジション Dock を追加する。
- 縦配信、縦録画、Backtrack、Virtual Camera などを持つ。
- `audio_wrapper_source` と `multi_canvas_source` を登録する。
- WebSocket vendor request を公開し、外部操作できる。
- Aitum Multistream から `aitum_vertical_get_stream_settings` / `aitum_vertical_set_stream_settings` 等で連携される。

参考箇所:

- `competitors/obs-vertical-canvas/vertical-canvas.cpp`
- `competitors/obs-vertical-canvas/multi-canvas-source.c`
- `competitors/obs-vertical-canvas/audio-wrapper-source.c`
- `competitors/obs-vertical-canvas/config-dialog.cpp`

使える設計:

- 縦キャンバスを OBS 内部 video として独立管理する発想。
- シーン/ソース/トランジション Dock を縦用に分ける UX。
- WebSocket/proc handler で他プラグインから操作可能にする設計。

弱点/差別化余地:

- 縦型特化。マルチ平台出力の高度診断とは別領域。
- Aitum Multistream と併用しないと配信コントロールセンターにはならない。

### Branch Output

特徴:

- Source/Scene の filter として出力を生やす。
- 1フィルタから複数ストリーミング、録画、Replay Buffer が可能。
- custom audio source、audio track、no audio、streaming/recording/both の音声割当が強い。
- crop、custom resolution、frame-rate divisor、status dock、hotkeys、proc handler を持つ。
- reconnect timeout、output signal、mutex、状態復旧が堅い。

参考箇所:

- `competitors/branch-output/src/plugin-main.cpp`
- `competitors/branch-output/src/plugin-streaming.cpp`
- `competitors/branch-output/src/UI/output-status-dock.cpp`
- `competitors/branch-output/API.md`

使える設計:

- 平台別に違う映像/音声を出すための filter/output モデル。
- crop/resolution/frame divisor の実装。
- status dock と一括/個別制御。
- reconnect が固着した時の timeout 処理。
- `proc_handler` による外部スクリプト連携。

弱点/差別化余地:

- 使いこなすにはフィルタ追加・シーン設計が必要で、初心者向けではない。
- 플랫폼/配信サービス向けのウィザードや API 連携は主目的ではない。
- 配信先管理というより source-level output。

### Source Record

特徴:

- Source/Scene に filter を追加して録画/Replay/配信できる。
- `rtmp_custom` / `whip_custom` を使う実装がある。
- WebSocket vendor request で source の stream start/stop を操作できる。

参考箇所:

- `competitors/obs-source-record/source-record.c`

使える設計:

- 小さめの C 実装として、OBS filter から output を作る基本を追いやすい。
- WebSocket 連携の最低限パターン。

弱点/差別化余地:

- UI/UX は強くない。
- Branch Output の方が現代的で機能が広い。

### StreamUP

特徴:

- 配信出力ではなく OBS 操作性改善プラグイン。
- toolbar、custom dock、multi-dock、hotkeys、WebSocket API、plugin update checker を統合。
- モジュール単位で有効/無効化し、無効なものは UI/イベント/ホットキーをロードしない設計。

参考箇所:

- `competitors/obs-streamup/streamup.cpp`
- `competitors/obs-streamup/ui/streamup-toolbar.cpp`
- `competitors/obs-streamup/multidock/`
- `competitors/obs-streamup/ui/settings-manager.cpp`

使える設計:

- 配信コントロール Dock を toolbar 化する発想。
- 設定ウィザード、モジュール ON/OFF、プラグイン更新チェック。
- WebSocket vendor command を厚く用意する設計。

弱点/差別化余地:

- マルチ配信の core output 実装は持たない。
- ただし UX 競合として水準が高い。

### Stream Sprout

特徴:

- OBS プラグインではなく、FFmpeg を使うローカル RTMP リレー。
- `ffmpeg -listen 1 -i <local rtmp> -c:v copy -c:a copy -f tee -use_fifo 1 ...` で複数宛先にコピーする。
- tee muxer に `onfail=ignore` を付け、1宛先失敗時も他宛先継続を狙う。
- YAML で server と services を設定。

参考箇所:

- `competitors/stream-sprout/stream-sprout`
- `competitors/stream-sprout/stream-sprout.yaml.example`

使える設計:

- ローカルリレー/クラウドリレーの最小プロトタイプ。
- `onfail=ignore` による失敗先切り離し。
- archive 同時保存。

弱点/差別化余地:

- RTMP のみ、bash/FFmpeg 依存、UI なし。
- Windows 向けにはそのまま使いづらい。
- 宛先別状態や GUI 診断は弱い。

## 差別化方針

競合はそれぞれ強いが、統合されていない。

狙うべき位置:

> OBS 内で完結する、横/縦/平台別出力を統合管理する配信コントロールセンター。

競合に対する差別化:

- Aitum Multistream より診断と安全性を厚くする。
- obs-multi-rtmp より平台別 UX と縦横統合を強くする。
- Aitum Vertical よりマルチ平台出力を主役にする。
- Branch Output より初心者向けウィザードと一括管理を強くする。
- StreamUP のような toolbar/customizable dock を取り入れる。
- Stream Sprout 的なローカル/クラウドリレーを選択肢として持つ。

## 実装で採用したい部品

### すぐ採用

- OBS Dock + Settings dialog。
- Main Canvas / Vertical Canvas の 2 セクション構造。
- `rtmp-services` の `services.json` 読み込み。
- Platform presets を JSON 定義化。
- `obs_service_create("rtmp_custom")` / `obs_service_create("whip_custom")`。
- `obs_output_get_signal_handler` で output 状態を UI に反映。
- `obs_output_get_total_bytes` / dropped frames / active / reconnecting の統計。
- Stream key 入力は `OBS_TEXT_PASSWORD` ではなく、最終的に OS credential store へ分離。

### 中期採用

- Aitum Vertical 互換/連携。インストール済みなら proc handler で連携、未インストールなら自前 vertical canvas を提供。
- Branch Output 的な scene/source-level branch output。
- Crop/resolution/frame-rate divisor。
- WebSocket vendor API。
- Status Dock と toolbar。

### 後期採用

- YouTube Live API による liveBroadcast/liveStream 作成。
- Twitch OAuth による stream key / ingest 取得。
- Kick API は実機確認後。
- TikTok は公式/Partner access が確認できるまで手動 RTMP のみ。
- Local relay / Cloud relay。

## ライセンス方針

OBS プラグイン本体は GPL-2.0 互換で進めるのが現実的。

理由:

- 主要競合の OBS プラグインは GPL-2.0 が多い。
- OBS 本体との結合、OBS フォーラム配布、既存コード参照を考えるとクローズド本体は難しい。
- 収益化は plugin 本体ではなく cloud relay、team sync、diagnostics、support、managed presets で行う方が安全。

コードを直接流用する場合:

- GPL 表記、著作権表示、改変履歴を残す。
- どのファイル由来かを明示する。
- 可能なら最初は設計だけ参考にし、実装は新規で書く。

## 現時点の推奨アーキテクチャ

```text
OBS Multiplatform Live Plugin
  UI
    Dock
    Settings dialog
    Preflight dialog
    Status/Diagnostics dock
    Optional toolbar
  Core
    OutputManager
    OutputStateMachine
    EncoderProfileManager
    PlatformPresetRegistry
    CredentialVault
    DiagnosticsEngine
  Canvas
    MainCanvasOutput
    VerticalCanvasOutput
    Scene/SourceBranchOutput
  Platforms
    CustomRTMP
    TwitchAdapter
    YouTubeAdapter
    KickAdapter
    TikTokManualRTMPAdapter
  Integrations
    AitumVerticalBridge
    ObsWebsocketVendorApi
    LocalRelayConnector
    CloudRelayConnector
```

## 次に見るべきこと

- Aitum Multistream の output start failure handling をもっと詳しく確認する。
- Branch Output の output status dock UI と stats 更新周期を見る。
- Aitum Vertical の `aitum_vertical_get_video` / stream settings proc handler の仕様を整理する。
- OBS plugin template で最小 Dock を立ち上げる。
- Platform preset JSON の初期設計を作る。
