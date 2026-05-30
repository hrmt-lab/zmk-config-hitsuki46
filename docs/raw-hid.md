# hitsuki46 RawHID 仕様・実装ガイド

ホスト PC と ZMK ドングル（セントラル）を **RawHID** で双方向通信し、レイヤー制御・時刻同期・AI 使用率表示などを実現するための仕様と実装メモ。
他キーボードへ移植する際の参考になるよう、プロトコルだけでなく実装構造も記載する。

- 対象: hitsuki46 ドングル（`xiao_ble`、ZMK セントラル）+ Prospector ディスプレイ
- 依存モジュール: [zmk-raw-hid](https://github.com/hrmt-lab/zmk-raw-hid)（RawHID トランスポート）、[prospector-zmk-module-ring](https://github.com/hrmt-lab/prospector-zmk-module-ring)（表示）

---

## 1. 全体構成（データフロー）

```
[ホスト PC アプリ]
   │  32 byte HID レポート（USB or BLE）
   ▼
[zmk-raw-hid モジュール]
   ├ USB:  src/usb_hid.c  set_report_cb()      ← OUTPUT/FEATURE レポート受信
   └ BLE:  src/hog.c      write_hids_raw_hid_report()  ← GATT 書き込み受信
   │  raise_raw_hid_received_event({data, length})
   ▼
[hitsuki46 ハンドラ]  src/hitsuki46_raw_hid.c
   ZMK_SUBSCRIPTION(..., raw_hid_received_event)
   parse_packet() で検証 → type で分岐
   ├ HOST_HELLO → DEVICE_HELLO 返信（raise_raw_hid_sent_event）
   ├ APP_LAYER  → src/hitsuki46_raw_hid_layer_control.c
   ├ TIME_SYNC  → src/hitsuki46_raw_hid_time_sync.c（状態保持＋getter）
   └ AI_USAGE   → src/hitsuki46_raw_hid_ai_usage.c（状態保持＋getter）
                      │
                      ▼
   [表示] prospector-ring の各ウィジェットが getter を呼んで描画
          例: boards/.../layouts/ring/ai_usage.c
```

- ホスト→デバイスは `raw_hid_received_event`、デバイス→ホストは `raw_hid_sent_event`（zmk-raw-hid が USB/BLE 両方に送信）。
- **キーボード固有のパケット解析・状態保持は hitsuki46 側**に置き、表示モジュール（ring）は getter 経由で参照する疎結合構成。

---

## 2. パケット共通仕様

- RawHID レポートサイズは **32 byte 固定**（`CONFIG_RAW_HID_REPORT_SIZE=32`）。
- すべてリトルエンディアン。

| offset | 内容 |
|---|---|
| 0..1 | magic `"HL"`（`0x48 0x4C`） |
| 2 | version `0x01` |
| 3 | packet type |
| 4..31 | type ごとのペイロード（未使用域は 0） |

### パケット型一覧

| 値 | 名称 | 方向 | 用途 |
|---|---|---|---|
| `0x01` | HOST_HELLO | H→D | 疎通確認 probe |
| `0x02` | DEVICE_HELLO | D→H | probe 応答 |
| `0x03` | ERROR | — | 予約 |
| `0x04` | PING | — | 予約 |
| `0x05` | PONG | — | 予約 |
| `0x10` | AI_USAGE | H→D | AI 使用率 |
| `0x20` | TIME_SYNC | H→D | 時刻同期 |
| `0x30` | APP_LAYER | H→D | レイヤー制御 |

> 注: この番号体系はホスト仕様（v1）に合わせたもの。旧実装の HELLO=0x10 / SET_LAYER=0x01 等とは非互換。

### 検証（最低限）

`parse_packet()`（`src/hitsuki46_raw_hid.c`）で:
- magic == `"HL"`、version == `0x01`、type が既知、length == 32。
- 各 type の **reserved バイトが全て 0**。
- 範囲チェック（layer ≤ 31、provider ∈ {1,2}、weekday ∈ 1..7 など）。
- 不正なら `ZMK_EV_EVENT_BUBBLE` で無視（他リスナへ伝播）。

---

## 3. パケット詳細

### HOST_HELLO / DEVICE_HELLO

| offset | 内容 |
|---|---|
| 4..6 | reserved (0) |
| 7 | seq（u8 wrapping counter） |
| 8..31 | reserved (0) |

ホストが HOST_HELLO(0x01) を送ると、デバイスは **同じ seq** を載せた DEVICE_HELLO(0x02) を返す。

### APP_LAYER (`0x30`) — レイヤー制御

| offset | 内容 |
|---|---|
| 4 | action（1=set, 2=clear） |
| 5 | layer（set 時 0..31、clear 時 0） |
| 6 | reserved (0) |
| 7 | seq |
| 8..31 | reserved (0) |

- set: ホスト指定レイヤーを有効化、clear: 解除。
- `hitsuki46_raw_hid_layer_control.c` が **ホストが有効化したレイヤーを1枚だけ追跡**し、別レイヤー指定時は前のを解除する（managed_layer 方式）。`zmk_keymap_layer_activate/deactivate` を使用。

### TIME_SYNC (`0x20`) — 時刻同期

| offset | 内容 |
|---|---|
| 4..7 | unix_time_sec（u32 LE, UTC） |
| 8..9 | tz_offset_min（i16 LE） |
| 10 | weekday（1=Mon .. 7=Sun） |
| 11 | format_hint（0:HM 1:HMS 2:Y-M-D 3:M-D 4:datetime 5:weekday+HM） |
| 12 | clock_mode（0:24h, 1:12h） |
| 13..31 | reserved (0) |

- `hitsuki46_raw_hid_time_sync.c` が「受信時刻 + 受信時の `k_uptime`」を基準に保持し、表示時に経過分を加算して現在ローカル時刻を算出（civil-from-days で年月日も計算）。
- getter: `hitsuki46_raw_hid_time_sync_format(buf, len)`（整形文字列、未同期なら false）、`hitsuki46_raw_hid_time_sync_wants_seconds()`（秒表示要否＝更新間隔判断用）。

### AI_USAGE (`0x10`) — AI 使用率

| offset | 内容 |
|---|---|
| 4 | provider（1=codex, 2=claude_code） |
| 5 | flags（後述） |
| 6..7 | five_hour_used_bp（u16 LE, basis points） |
| 8..9 | seven_day_used_bp（u16 LE） |
| 10..13 | five_hour_reset_unix（u32 LE） |
| 14..17 | seven_day_reset_unix（u32 LE） |
| 18..21 | updated_unix（u32 LE） |
| 22 | error_code |
| 23..31 | reserved (0) |

- **basis points**: `1234` = 12.34%、`10000` = 100.00%。受信時に 0..10000 へ clamp。
- provider ごとに最新状態を保持（`hitsuki46_raw_hid_ai_usage.c`、2 スロット、`K_MUTEX` 保護）。未知 provider は無視。

#### flags ビット

| bit | 意味 |
|---|---|
| 0 | five_hour_valid |
| 1 | seven_day_valid |
| 2 | estimated（推定値） |
| 3 | local_history_source |
| 4 | quota_source |
| 5 | stale（前回成功値のまま取得失敗） |
| 6 | fallback_limit |
| 7 | error_present |

#### error_code

`0` none / `1` source_disabled / `2` missing_credentials / `3` expired_credentials / `4` auth_failed / `5` rate_limited / `6` fetch_failed / `7` parse_failed / `8` no_usage_data / `9` missing_limit

#### 表示ルール（window=5h/7d ごと）

```
window_valid=0 → error_present ? "ERR"（バー無し） : "--"（バー無し）
window_valid=1 → 値表示 + 接尾辞:  stale→"*"  estimated→"e"  それ以外→""
                 （valid=1 では error_present でも値+接尾辞を優先。例 stale → "72%*"）
```
バー高は `used%`（reset 時刻は現バージョン非表示）。バー色は provider 固定（Claude `#CC7E5A` / Codex `#7B6FE8`）。

---

## 4. 実装構造（移植の参考）

### 4.1 ファイル構成（hitsuki46 側）

| ファイル | 役割 |
|---|---|
| `src/hitsuki46_raw_hid.{c,h}` | パケット定義・検証・ディスパッチ、HELLO 応答 |
| `src/hitsuki46_raw_hid_layer_control.c` | APP_LAYER ハンドラ |
| `src/hitsuki46_raw_hid_time_sync.c` + `include/hitsuki46/raw_hid_time_sync.h` | TIME_SYNC 保持・getter |
| `src/hitsuki46_raw_hid_ai_usage.c` + `include/hitsuki46/raw_hid_ai_usage.h` | AI_USAGE 保持・getter |
| `CMakeLists.txt` / `Kconfig` | CONFIG ガードでソース選択 |
| `boards/shields/hitsuki46/hitsuki46_dongle.conf` | 機能の有効化 |

### 4.2 受信リスナ

```c
ZMK_LISTENER(hitsuki46_raw_hid, hitsuki46_raw_hid_received_listener);
ZMK_SUBSCRIPTION(hitsuki46_raw_hid, raw_hid_received_event);
```
リスナ内で `parse_packet()` → `switch(packet.type)`。各ハンドラ呼び出しは `#if IS_ENABLED(CONFIG_...)` でガードし、機能単位で外せるようにする。

### 4.3 状態保持 + getter（疎結合の肝）

`time_sync` / `ai_usage` は同じパターン:
1. `K_MUTEX` 付きの static 状態を持つ。
2. ハンドラ（受信スレッド）が状態を更新。
3. `..._get()` getter で表示側へ snapshot を渡す。
4. **ヘッダに `#else` の inline スタブを用意**し、CONFIG 無効時でも他モジュールがビルド可能にする（例: `raw_hid_ai_usage.h`）:

```c
#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_AI_USAGE)
bool hitsuki46_raw_hid_ai_usage_get(uint8_t provider,
                                    struct hitsuki46_ai_usage_provider *out);
#else
static inline bool hitsuki46_raw_hid_ai_usage_get(uint8_t provider,
                                                  struct hitsuki46_ai_usage_provider *out) {
    if (out) out->present = false;
    return false;
}
#endif
```

`include/` は `zephyr_include_directories(include)`（CMakeLists）でグローバル公開されるため、表示モジュール（ring）から `<hitsuki46/raw_hid_ai_usage.h>` を include できる。

### 4.4 表示側（prospector-ring）

- `boards/.../layouts/ring/ai_usage.c` が `#if IS_ENABLED(CONFIG_HITSUKI46_RAW_HID_AI_USAGE)` のもとで getter を呼ぶ（無効ビルドでは「データ無し」にフォールバックし ring 単体でもビルド可能）。
- 値の取得は `k_work_delayable`（`zmk_display_work_q()`）で周期ポーリング（`uptime_info.c` と同パターン）。LVGL 操作は表示スレッドで実行。
- 画面切替（Main↔AI Usage）トリガ:
  - **キー長押し**: `keycode_state_changed` を購読し、押下で `k_work_schedule`、離鍵で `k_work_cancel_delayable`。閾値経過で **押下中に** トグル（`ai_usage.c`）。
  - **タッチ長押し**: `ring_touch.c` で CST816S の `INPUT_BTN_TOUCH` 押下/解放を拾い、同じタイマー方式でトグル。長押しで切替後の「離した時のタップ」は抑制。
  - 共通閾値 `RING_AI_USAGE_LONGPRESS_MS`（既定 700ms、`ai_usage.h`）。

### 4.5 移植時のチェックリスト

- [ ] west.yml に `zmk-raw-hid` と表示モジュールを追加、`build.yaml` のドングルに `raw_hid_adapter`（と表示シールド）を併設。
- [ ] `CONFIG_RAW_HID=y` と 32 byte レポート設定。
- [ ] 受信リスナ + `parse_packet`（magic/version/type/reserved 検証）を実装。
- [ ] 必要なハンドラ（HELLO 応答／APP_LAYER／TIME_SYNC／AI_USAGE）を CONFIG ガードで追加。
- [ ] 状態保持は mutex + getter + `#else` スタブで疎結合化。
- [ ] 表示は getter をポーリング、UI は表示スレッドで更新。

---

## 5. 関連 CONFIG

| CONFIG | 場所 | 説明 |
|---|---|---|
| `CONFIG_RAW_HID` | zmk-raw-hid | RawHID 有効化（依存で `USB_DEVICE_HID`） |
| `CONFIG_HITSUKI46_RAW_HID_LAYER_CONTROL` | hitsuki46 | APP_LAYER ハンドラ |
| `CONFIG_HITSUKI46_RAW_HID_TIME_SYNC` | hitsuki46 | TIME_SYNC ハンドラ＋getter |
| `CONFIG_HITSUKI46_RAW_HID_AI_USAGE` | hitsuki46 | AI_USAGE ハンドラ＋getter |
| `CONFIG_PROSPECTOR_RING_AI_USAGE` | prospector-ring | AI Usage 画面のコンパイル |
| `CONFIG_PROSPECTOR_RING_AI_USAGE_TOGGLE_KEY` / `..._KEYCODE` | prospector-ring | キー長押し切替（既定 F21=112） |
| `CONFIG_PROSPECTOR_RING_AI_USAGE_TOGGLE_TOUCH` | prospector-ring | タッチ長押し切替 |

hitsuki46 ドングルでの有効化は `boards/shields/hitsuki46/hitsuki46_dongle.conf` を参照。

### キーマップへの切替キー割り当て

AI Usage 画面のキー切替を使う場合、キーマップの任意レイヤーに切替キーコード（既定 F21）を割り当てる:

```dts
&kp F21
```

（hitsuki46 では Settings レイヤーに配置済み。）

---

## 6. 参考

- ホスト側パケット仕様（AI Usage）: 本リポジトリ作成時のホスト仕様に準拠。
- 表示 UI 仕様: prospector-zmk-module-ring `docs/ring-ai-usage-ui-spec.md`。
