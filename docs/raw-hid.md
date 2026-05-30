# hitsuki46 RawHID 設定

hitsuki46 はホスト PC と RawHID で双方向通信し、Prospector ディスプレイ上で
レイヤー制御・時刻同期・AI 使用率表示に対応します。

実体は2つの汎用モジュールで、hitsuki46 はこれらを**有効化するだけ**です（キーボード固有の RawHID コードは持ちません）。

- [zmk-raw-hid](https://github.com/hrmt-lab/zmk-raw-hid) — トランスポート（USB/BLE で `raw_hid_received_event` を発火）
- [zmk-rawhid-app](https://github.com/hrmt-lab/zmk-rawhid-app) — アプリ層プロトコル（解析・状態保持・getter）

> **プロトコル仕様（パケットのバイトレイアウト等）・実装構造・他キーボードへの移植ガイドは
> [zmk-rawhid-app の README](https://github.com/hrmt-lab/zmk-rawhid-app) を参照してください。**
> 本ページは hitsuki46 固有の設定のみを記載します。

---

## 構成

```
[ホスト PC] ⇄ RawHID(USB/BLE) ⇄ [hitsuki46 ドングル(セントラル)]
  zmk-raw-hid: raw_hid_received_event を発火
  zmk-rawhid-app: 解析 → HELLO応答 / レイヤー制御 / 時刻保持 / AI使用率保持（getter公開）
  prospector-zmk-module-ring: getter を読んで AI Usage 画面に描画
```

west.yml にこの3モジュールを取り込み済み（`config/west.yml`）、ドングルのシールドは
`hitsuki46_dongle prospector_adapter raw_hid_adapter`（`build.yaml`）。

## 有効化している CONFIG（`boards/shields/hitsuki46/hitsuki46_dongle.conf`）

```ini
CONFIG_RAW_HID=y
CONFIG_RAWHID_APP=y
CONFIG_RAWHID_APP_LAYER_CONTROL=y
CONFIG_RAWHID_APP_TIME_SYNC=y
CONFIG_RAWHID_APP_AI_USAGE=y

# Prospector の AI Usage 画面（長押し F21 / 長押しタッチで切替）
CONFIG_PROSPECTOR_RING_AI_USAGE=y
CONFIG_PROSPECTOR_RING_AI_USAGE_TOGGLE_KEY=y
CONFIG_PROSPECTOR_RING_AI_USAGE_TOGGLE_KEYCODE=112
CONFIG_PROSPECTOR_RING_AI_USAGE_TOGGLE_TOUCH=y
```

## キーマップ

AI Usage 画面の切替キー（F21 = keycode 112）は Settings レイヤーに割り当て済み（`config/hitsuki46.keymap`）。

```dts
&kp F21
```

長押し（約 0.7 秒）または画面長押しタッチで Main ↔ AI Usage を切り替えます。
