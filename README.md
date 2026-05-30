# agri-rain-poe

M5Stack ATOM PoE Kit + DFRobot SEN0575 を使った雨量センサーノード。
v0.2 から MQTT に加えて UECS-CCM multicast にも投げられるようになり、
ブラウザから MQTT / CCM 設定ができる Web UI を備えています。

OGMS（`~/agri-relay`）や ccm_rp2350_relay と同じパッケージ規約で動作。

## ハードウェア

- **MCU**: M5Stack ATOM Lite (ESP32-PICO-D4)
- **PoE / Ethernet**: M5Stack ATOM PoE Base (W5500 on SPI)
- **センサー**: DFRobot SEN0575 Gravity Rainfall Sensor (DIP = UART)
  - M5 G26 (TX) → SEN0575 SDA/RX
  - M5 G32 (RX) ← SEN0575 SCL/TX

## 機能

- DHCP / PoE プラグアンドプレイ（cable 後挿しでも DHCP 自動再取得）
- **MQTT publisher** — `<topic prefix>` に JSON、`<topic prefix>/status` を LWT
- **UECS-CCM multicast publisher** — `WRainfallAmt.cMC`（UDP 224.0.0.1:16520）
- **Web UI** 3 ページ（Dashboard / Config / About）+ JSON API
- **ArduinoOTA** over Ethernet（hostname `agri-rain-XX`）
- **状態 LED**:
  | 色 | 意味 |
  |---|---|
  | 青 | 起動中 |
  | 赤 | リンク断 / DHCP 失敗 |
  | マゼンタ | センサー未検出 |
  | 黄 | MQTT 未接続 |
  | 緑 | 正常 |
  | 白点滅 | 送信瞬間 |

## 設定（NVS 永続化）

`Preferences` ネームスペース `rain-cfg`。Web UI の `/config` から編集:

- Node ID, hostname
- MQTT: host / port / user / pass / topic prefix / interval
- UECS-CCM: enable, interval, room, region, order, priority
  - region のデフォルトは `41`（`ccm_rp2350_relay` 慣例の sensor 11 + weather 30）

## ビルド・書き込み

```
pio run -e m5atom-poe -t upload                                       # USB-C
pio run -e m5atom-poe -t upload --upload-port agri-rain-01.local      # OTA
```

## 関連プロジェクト

- [`ccm_rp2350_relay`](https://github.com/yasunorioi/ccm_rp2350_relay)
- [`OGMS`](https://github.com/yasunorioi/OGMS)
- [`agri-env-poe`](https://github.com/yasunorioi/agri-env-poe)
