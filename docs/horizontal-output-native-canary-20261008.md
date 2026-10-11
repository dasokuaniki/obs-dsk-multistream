# 独立横型出力 — 2026-10-08 ネイティブ検証

状態: 開発用。隔離OBSで送信・描画の一部を実確認済み。本番への導入・署名・配布・公開は未実施。

## 対象と隔離

- テストDLL: `build/horizontal-dev-20261008/test-payload-verified/obs-dsk-multistream/bin/64bit/obs-dsk-multistream.dll`
- SHA-256: `5583B83ACFD679FC04F851E6850E7FEFDC9D7DB966E2B203FC115DBAE7AF729A`、2,483,712 bytes、未署名。
- E2E hooks ON、bundled OAuth OFF。0.4.1の版数表示を引き継ぐ開発ビルドであり、公開済み署名版とは別物。
- 実行環境: インストール済みOBS 33.0.0-beta5の実行ファイル・標準モジュールを `build/horizontal-canary-20261008/obs` へ複製。ビルドSDKは32.1.2。ほかのOBS版での動作保証はこの試験からは導けない。
- `-IsolatedPortable` により設定は同ディレクトリの `config/obs-studio` に限定。テスト専用プロファイル・シーンコレクションを使用した。本番APPDATAの44設定ファイルのフィンガープリントは試験前後で一致した。
- 合成色映像、x264、60 fps、127.0.0.1のRTMP受信器を使用。実配信先・実アカウントの開始操作はしていない。受信FLVのデコード検証は自宅サーバーのFFmpegで実施。
- 稼働中のComment Viewerが読み取り連携される通常のDock初期化は残したが、Comment Viewer本体・設定・認証・ポートは変更していない。

## 確認結果

| 対象 | 受信結果 | 判定 |
| --- | --- | --- |
| OBS Main (`dsk-horizontal`) + DSK Horizontal | 各1920×1080。Main RGB 253/123/44、Horizontal RGB 44/138/253 | 別映像の同時送信成功 |
| DSK Horizontal + DSK Vertical | 横1920×1080 RGB 44/138/253、縦1080×1920 RGB 252/138/44 | サイズ・内容の分離成功 |
| Horizontalソース表示OFF | 約92.104秒からHorizontalのみ黒。93秒のRGB 0/0/0、Verticalは252/138/44を維持 | プレビューと送信内容のライブ更新・分離成功 |
| 両Dockの初回表示 | 横は青の16:9、縦は橙の9:16。修正後はツールバーの重複描画なし、初期ソース選択なし | ネイティブ画面で確認 |
| ツールバー幅・字体変更 | 実Qtウィジェットの160/300/700 px、28 px字体・500 pxで各5部品が範囲内・非重複 | 回帰テスト成功 |
| ローカル出力停止・OBS終了 | 最終Main+Horizontal各719フレーム、最終Horizontal+Vertical各5698フレーム。通常停止・正常プロセス終了 | クラッシュ再現なし。ただし未解放メモリ1件は残る |

デコード検査は `tests/check-canvas-media.py` を使用。既知の単色フレームを16×16 RGBへ縮小し、全ピクセルの最大誤差8以内と解像度を検査した。エンコードに伴う色差を許容する検査であり、接続ログ・解像度だけで成功判定していない。

## 試験中に見つけて修正した点

1. E2E横型ソースの固定名がOBS Mainの同名ソースと衝突し、両方が同じ色になった。これは試験フィクスチャの不備だった。横型フィクスチャを一意名にし、実際にOBSが返すソース名を使用した。修正前の失敗媒体も保存している。
2. 初回Dock表示で広幅・狭幅のツールバー描画が残り、広幅モードでは文字が収まらなかった。固定280 pxではなくテーマ・字体で測定した最小幅を使い、列stretch・layoutの再配置と初回show後のlayout確定を行うよう修正した。文字が収まらない300 pxの実ウィジェットでRED→GREENを確認した。縦型・横型で共有するため両方に影響する修正。
3. RTMPハーネスにbuild内portable設定ルートの境界検証と媒体保存を追加。プロセス稼働チェックを設定変更前に行い、タイムアウトでOBSを強制終了しないようにした。8件のパス隔離検査を追加した。境界と既存の祖先/config reparse pointの検査であり、任意の敵対的な子階層リンクを網羅するものではない。

## 証拠の場所

Windowsの媒体・受信ログ: `build/horizontal-canary-20261008/`

- `red-source-name/received-20261008-230250-*.flv` — 同名ソース衝突の失敗。
- `green-main-horizontal/received-20261008-230603-*.flv` — フィクスチャ修正後のMain/Horizontal。
- `green-horizontal-vertical/received-20261008-230835-*.flv` — Horizontal/Vertical。
- `green-ui-horizontal-vertical/received-20261008-231837-*.flv` — 最新DLLの初回表示・ライブ表示OFF検査。表示OFF前の90秒フレームは黒ではなく、黒化は92.104秒から。90秒での黒色期待の失敗を機能不良とは扱っていない。
- `green-final-main-horizontal/received-20261008-233451-*.flv` — 最新DLLでの最終Main/Horizontal確認。

OBSログ: `build/horizontal-canary-20261008/obs/config/obs-studio/logs/` の `2026-10-08 23-18-39.txt` および `2026-10-08 23-34-53.txt`。

自宅サーバーの合成媒体・検査器: `/home/dasoku/codex-work/multistream-horizontal-evidence-20261008/`。実ユーザーの配信媒体は含まない。

Windows全体ビルド成功、最終CTest 28/28成功。PowerShellハーネス構文検査成功、staged payloadの8ファイルがmanifestのサイズ・SHA-256と一致。

## 未確定事項・残りのゲート

- 成功したネイティブcanaryの終了ログすべてで `Number of memory leaks: 1`。DSK、OBS本体、Browserその他モジュールのどれに由来するか未確定。正常終了した事実だけで無視しない。
- DSKなしの通常終了比較は、非表示ウィンドウおよびnative UI helperの失敗により完了できなかった。2プロセスは、所有テスト実行ファイル・自動開始引数なし・起動完了・出力開始記録なし・DSK未ロードを確認して回収した。これは正常終了成功やリークなしの比較証拠ではない。その後テスト用DLLを戻し、OBSプロセス0件を確認した。
- 最終起動ログに永続Verticalキャンバスの同名context自動改名警告がある。繰り返しテスト環境の残存状態との関係は未確定。再起動・永続キャンバス所有権/再利用検査に含める。
- NativeのDock境界ドラッグによる縮小は不確定。Qt単体のgeometry成功と実OBSでの操作成功を混同しない。
- Nativeのシーン切替、OBS Link、CRUD/並び替え、profile/collection再ロード、同一横型のエンコーダー共有、Start All、Twitch Dual Format、hardware encoder、音声/AV同期、長時間・本番APIは今回未完了。
- 公開用OAuth構成・署名・新規導入/更新/削除・Defender・配布互換性は別のリリースゲート。既存の署名済み0.4.1、NAS、公開サイト、更新URL、Git履歴は変更していない。

残りの受入項目は [開発試験計画](horizontal-output-test-plan.md) を参照。
