# DSK Multistream - Architecture

> Historical pre-implementation design. It contains the retired `OBS Main` target mode and is not the current runtime contract. Use `README.md` and `docs/dsk-runtime-state-spec.md` for implemented behavior.

作成日: 2026-06-02

## 設計方針

Lite 版は C++ OBS native plugin として実装する。

外部 companion app、クラウドリレー、platform API は初期実装に入れない。

設計原則:

- 配信先ごとに `obs_output_t` を作る。
- 横型は OBS 標準のメインキャンバスを使う。
- 縦型は DSK が 9:16 の Vertical Layout を持つ。
- OBS メイン encoder 共有だけでなく DSK 管理 encoder も初期設計に入れる。
- v0.1 では配信先ごとの完全個別 encoder ではなく、出力グループ単位の encoder 共有にする。
- 配信先ごとの状態を独立管理する。
- UI と OBS output 操作を分離する。
- 将来 Full 版へ移行できる内部構造にする。

## 全体構成

```text
obs-dsk-multistream
  plugin-main
    module load/unload
    locale
    frontend event callback
    dock registration

  ui
    MainDock
    TargetListWidget
    TargetEditDialog
    VerticalLayoutEditor
    VerticalPreviewWidget
    PreflightDialog

  core
    OutputManager
    OutputTarget
    OutputSession
    OutputStateTracker
    EncoderProfileManager
    LayoutManager
    VerticalLayout
    PlatformPresetRegistry
    SettingsStore
    Diagnostics

  obs
    ObsOutputAdapter
    ObsServiceFactory
    ObsEncoderResolver
    ObsSignalBridge

  data
    presets.json
    locale/*.ini
```

## モジュール責務

### plugin-main

責務:

- `OBS_DECLARE_MODULE`
- `obs_module_load`
- `obs_module_unload`
- `obs_frontend_add_dock_by_id`
- `obs_frontend_add_event_callback`
- `obs_frontend_add_save_callback`

持たない責務:

- 配信開始ロジック。
- 設定画面ロジック。
- platform 判定。

### MainDock

責務:

- 配信先一覧を表示する。
- `すべて開始` ボタンを表示する。
- `すべて停止` ボタンを表示する。
- 配信先ごとの個別 `開始/停止` ボタンを表示する。
- 設定画面を開く。
- `OutputManager` から状態変更通知を受けて UI 更新する。

持たない責務:

- `obs_output_t` を直接操作しない。
- Stream Key を直接保持しない。

### TargetEditDialog

責務:

- 配信先を追加/編集する。
- platform preset を選ぶ。
- server URL と Stream Key を入力する。
- 入力値を `OutputTarget` に変換する。

### VerticalLayoutEditor

責務:

- 9:16 の縦型レイアウトを編集する。
- 既存 OBS シーンまたはソースを縦レイアウトへ追加する。
- ソースごとの transform を編集する。
- テンプレートを適用する。
- `VerticalPreviewWidget` で結果を確認する。

初期編集機能:

- source/scene add。
- remove。
- move up/down。
- position x/y。
- scale。
- crop。
- fit/fill。
- safe area overlay。

持たない責務:

- OBS 標準の横シーン編集はしない。
- platform API 連携はしない。

### LayoutManager

責務:

- Horizontal layout と Vertical layout の対応を管理する。
- Horizontal layout は OBS Main Canvas を参照する。
- Vertical layout は DSK private scene/view として管理する。
- Vertical layout の保存/読み込みを行う。

初期 layout:

```text
Horizontal
  - type: obs_main_canvas
  - OBS 標準の program output を使う

Vertical
  - type: dsk_vertical_layout
  - resolution: 1080x1920
  - private obs_scene_t
  - private obs_view_t
```

Vertical layout 保存データ案:

```json
{
  "verticalLayout": {
    "width": 1080,
    "height": 1920,
    "items": [
      {
        "sourceName": "Game Scene",
        "x": 0,
        "y": 360,
        "scale": 1.0,
        "crop": {"left": 420, "top": 0, "right": 420, "bottom": 0},
        "fitMode": "fill"
      }
    ]
  }
}
```

### PlatformPresetRegistry

責務:

- platform preset を読み込む。
- platform ごとの初期 server URL、説明、推奨ビットレート、警告文を返す。

初期プリセット:

```json
[
  {
    "id": "twitch",
    "name": "Twitch",
    "defaultServer": "rtmp://live.twitch.tv/app",
    "recommendedVideoKbps": 6000,
    "notes": ["CBR", "2秒キーフレーム推奨"]
  },
  {
    "id": "youtube",
    "name": "YouTube",
    "defaultServer": "rtmps://a.rtmps.youtube.com/live2",
    "recommendedVideoKbps": 6000,
    "notes": ["YouTube高画質配信はFull版の個別エンコードで対応予定"]
  },
  {
    "id": "kick",
    "name": "Kick",
    "defaultServer": "rtmps://fa723fc1b171.global-contribute.live-video.net",
    "recommendedVideoKbps": 6000,
    "notes": ["最大8,000kbps目安"]
  },
  {
    "id": "custom",
    "name": "Custom RTMP",
    "defaultServer": "",
    "recommendedVideoKbps": 6000,
    "notes": []
  }
]
```

### SettingsStore

責務:

- OBS profile config または plugin config に設定を保存する。
- `OutputTarget` の配列を JSON として保存/読み込みする。
- migration version を持つ。

初期方針:

- v0.1 では Stream Key を OBS data に保存する場合も UI ではマスクする。
- v0.2 以降で OS credential store へ移す。
- ログには Stream Key を絶対に出さない。

保存データ案:

```json
{
  "schemaVersion": 1,
  "targets": [
    {
      "id": "uuid",
      "enabled": true,
      "name": "Twitch",
      "platform": "twitch",
      "server": "rtmp://live.twitch.tv/app",
      "key": "*** stored secret ***",
      "followMainStream": true,
      "autoReconnect": true
    }
  ]
}
```

### OutputManager

責務:

- 配信先ごとの `OutputSession` を作成/破棄する。
- 一括開始/停止。
- 個別開始/停止。
- OBS frontend event に追従する。
- UI へ状態変更を通知する。

主な API:

```cpp
class OutputManager {
public:
    void loadTargets(std::vector<OutputTarget> targets);
    bool startAll(StartMode mode);
    void stopAll(StopMode mode);
    bool startTarget(const std::string& targetId);
    void stopTarget(const std::string& targetId);
    std::vector<OutputStatus> statuses() const;
};
```

操作仕様:

- `startAll`: enabled target をすべて開始する。1件が失敗しても他 target の開始を続ける。
- `stopAll`: active / starting / reconnecting target をすべて停止する。
- `startTarget`: 指定 target だけ開始する。他 target の状態は変えない。
- `stopTarget`: 指定 target だけ停止する。他 target の状態は変えない。
- `startAll` は OBS メイン配信を自動開始しない。
- `startTarget` は OBS Main group かつ OBS メイン配信が未開始なら警告して拒否する。

### OutputSession

責務:

- 1つの配信先に対応する。
- `obs_service_t` と `obs_output_t` の lifetime を管理する。
- OBS メイン配信 encoder を取得して attach する。
- signal handler を登録/解除する。
- 状態を `OutputStateTracker` に渡す。

開始フロー:

```text
validate target
  -> resolve encoder group
  -> if OBS Main group: get main streaming output and encoders
  -> if DSK group: create/reuse DSK-managed video/audio encoder for the group
  -> create service rtmp_custom
  -> create output preferred type or rtmp_output
  -> apply reconnect settings
  -> attach service
  -> attach encoders
  -> connect output signals
  -> obs_output_start
```

停止フロー:

```text
if active:
  obs_output_stop
else if reconnecting/stuck:
  obs_output_force_stop after timeout
disconnect signals
release service/output refs
```

### ObsEncoderResolver

責務:

- OBS Main group の場合、OBS メイン配信出力から video/audio encoder を取得する。
- DSK group の場合、`EncoderProfileManager` が作成した encoder を返す。
- OBS Main group でメイン配信出力が動いていない場合に明確なエラーを返す。

Lite の制約:

- 配信先ごとの完全個別 encoder は作成しない。
- 初期 encoder group は `OBS Main`、`DSK Horizontal`、`DSK Vertical` の 3つ。
- 複数 audio track は扱わない。track 0 のみ。

### EncoderProfileManager

責務:

- 出力グループごとの encoder lifetime を管理する。
- DSK Horizontal Encoder を作成/再利用/破棄する。
- DSK Vertical Encoder を作成/再利用/破棄する。
- encoder 設定を profile として保存する。

初期 profile:

```text
OBS Main
  - OBS メイン配信 output の encoder を共有
  - OBS メイン配信中のみ利用可能

DSK Horizontal
  - 1920x1080 または OBS canvas と同じ解像度
  - 30/60fps
  - H.264 CBR
  - 6000kbps 初期値

DSK Vertical
  - 1080x1920
  - 30/60fps
  - H.264 CBR
  - 4000-6000kbps 初期値
  - video source は LayoutManager の Vertical private view
```

設計判断:

- v0.1 で必要なのは「各 platform 完全個別 encoder」ではなく「横型と縦型を分けられる encoder group」。
- YouTube 専用高画質などの platform 完全個別 encoder は v0.2+。
- DSK group を使う target は OBS メイン配信が停止中でも開始できる。

### ObsSignalBridge

監視する signal:

- `starting`
- `start`
- `reconnect`
- `reconnect_success`
- `stopping`
- `stop`

将来見る候補:

- `activate`
- `deactivate`

### Diagnostics

責務:

- 開始前チェック。
- エラー文の変換。
- OBS log に出す詳細と UI に出す短文を分ける。

Lite のチェック:

- target enabled か。
- server URL が空でないか。
- stream key が空でないか。
- URL scheme が `rtmp://` または `rtmps://` か。
- OBS メイン配信が開始済みか。
- 目安帯域が過剰でないか。

## 状態機械

```text
Idle
  -> Starting
  -> Live
  -> Reconnecting
  -> Stopping
  -> Idle
  -> Error
```

状態説明:

- `Idle`: 出力未作成または停止済み。
- `Starting`: `obs_output_start` 呼び出し済み、start signal 待ち。
- `Live`: 配信中。
- `Reconnecting`: OBS output が reconnect 中。
- `Stopping`: stop/force_stop 中。
- `Error`: start 失敗または stop code が異常。

## エラー分類

```text
ConfigurationError
  MissingServer
  MissingStreamKey
  UnsupportedUrlScheme

ObsStateError
  MainOutputNotActive
  MainVideoEncoderNotFound
  MainAudioEncoderNotFound

OutputError
  ServiceCreateFailed
  OutputCreateFailed
  OutputStartFailed
  OutputStoppedWithCode
```

UI では短く出す。

例:

- `YouTube: Stream Key が未入力です`
- `Kick: OBS のメイン配信を開始してから追加出力を開始してください`
- `Twitch: 接続が切れました。再接続中です`

OBS log では詳細を出す。

## 参考にする競合実装

### Aitum Multistream から学ぶ

- Dock に Main Canvas / Vertical Canvas の構造を置く設計。
- `obs_service_create` / `obs_output_create` / encoder attach の基本フロー。
- `rtmp-services` の services 読み込み。

ただし Lite では Aitum Vertical 外部プラグイン連携は入れない。DSK 自前の Vertical Layout は v0.1 に入れる。

### obs-multi-rtmp から学ぶ

- output signal handler。
- reconnect UI。
- `obs_output_get_total_bytes` 等の統計。
- release/force stop の後始末。

### Branch Output から学ぶ

- reconnect timeout。
- status dock。
- 将来の source/scene branch output。

Lite では filter output は入れない。

### StreamUP から学ぶ

- 設定 UI と Dock の使いやすさ。
- 将来の toolbar / WebSocket API。

Lite では toolbar は入れない。

## 初期ファイル構成案

```text
CMakeLists.txt
buildspec.json
src/
  plugin-main.cpp
  plugin-support.c.in
  plugin-support.h
  ui/
    main-dock.hpp
    main-dock.cpp
    target-edit-dialog.hpp
    target-edit-dialog.cpp
    preflight-dialog.hpp
    preflight-dialog.cpp
  core/
    output-manager.hpp
    output-manager.cpp
    output-session.hpp
    output-session.cpp
    output-target.hpp
    output-state.hpp
    platform-preset-registry.hpp
    platform-preset-registry.cpp
    settings-store.hpp
    settings-store.cpp
    diagnostics.hpp
    diagnostics.cpp
  obs/
    obs-encoder-resolver.hpp
    obs-encoder-resolver.cpp
    obs-signal-bridge.hpp
    obs-signal-bridge.cpp
data/
  locale/
    en-US.ini
    ja-JP.ini
  presets/
    platforms.json
```

## 実装順

1. OBS plugin template で空 Dock を表示。
2. `SettingsStore` と `OutputTarget` を実装。
3. `LayoutManager` と縦型 private scene/view を実装。
4. 縦型プレビューと基本レイアウト編集 UI。
5. 配信先追加/編集 UI。
6. `OutputSession` の start/stop。
7. output signal -> UI state 更新。
8. 一括開始/停止。
9. preflight checks。
10. ログとマスク処理。
11. パッケージング。

## 技術的な未決事項

| 論点 | 初期判断 | 実装前確認 |
|---|---|---|
| OBS 対応バージョン | OBS Studio 32.1.2 以上 | template と CI で確認 |
| Stream Key 保存 | v0.1 は既存競合プラグインにならい OBS config、UI/log はマスク | v0.2 以降で credential store 検討 |
| メイン配信未開始時の挙動 | OBS Main group は開始不可。DSK Horizontal/Vertical group は開始可 | UI で group ごとの挙動を明示 |
| YouTube 推奨ビットレート | Lite では警告のみ | Full 版で個別 encoder |
| TikTok | Custom RTMP のみ | 専用 UI 文言を慎重にする |
| WHIP | MVP 外 | Aitum の実装は参考に残す |
