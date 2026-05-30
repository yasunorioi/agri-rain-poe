# agri-rain-poe

M5Stack ATOM PoE Kit を使った、雨量センサー専用 MQTT パブリッシャー。
OGMS（`~/agri-relay`）と同じ MQTT トピック体系に乗る独立ノード。

- **作成**: 2026-05-23
- **状態**: 実機未検証（コードのみ）。後日 M5 Atom PoE Kit と SEN0575 を入手次第、配線・動作確認を行う。

---

## 1. 概要

DFRobot SEN0575（Gravity Rainfall Sensor）を UART/Modbus RTU で読み取り、
`agriha/{house}/sensor/SEN0575` に `rainfall_mm` を publish するだけの単機能ノード。
電源・通信ともに PoE 1 本で完結する。

OGMS 本体（RP2350B）は温室制御を担当し、本ノードは雨量計測のみを担う分業構成。

---

## 2. ハードウェア

| 部品 | 型番 | 備考 |
|:-----|:-----|:-----|
| 制御基板 | M5Stack ATOM PoE Kit | ATOM Lite (ESP32-PICO-D4) + PoE Base (W5500) |
| 雨量センサー | DFRobot SEN0575 | DIP スイッチを **UART モード** に設定 |
| PoE 給電 | IEEE 802.3af 対応 PoE スイッチ／インジェクター | 出力 5V / 1.5A |
| LAN ケーブル | Cat5e 以上 | |

### SEN0575 の DIP スイッチ

センサー裏面の DIP を **I2C → UART 側** に倒す。出荷時 I2C なので必ず切り替えること。
切り替えないと Modbus 応答が返らず、起動時のシリアルログに `SEN0575: not responding` が出続ける。

---

## 3. 配線

### SEN0575 ↔ M5 Atom PoE（Grove ポート）

| M5 GPIO | 役割 | SEN0575 ピン | 線色（DFRobot 付属ケーブル） |
|:--------|:-----|:-------------|:------------------------------|
| G26 | UART TX → センサー RX | SDA / RX | 緑 |
| G32 | UART RX ← センサー TX | SCL / TX | 青 |
| 5V  | 電源 | VCC | 赤 |
| GND | GND  | GND | 黒 |

> SEN0575 の SDA/SCL の表記は I2C モード時のもの。UART モードでは
> **SDA=RX、SCL=TX** として動作する（DFRobot wiki 参照）。

### W5500（ATOM PoE Base 内部、はんだ済み）

参考までに記録。実装側で配線する必要はない。

| ESP32 GPIO | W5500 ピン |
|:-----------|:-----------|
| GPIO22 | SCK  |
| GPIO23 | MISO |
| GPIO33 | MOSI |
| GPIO19 | CS   |

---

## 4. ビルド・書き込み

### 4-1. VSCode 環境

このリポジトリには `.vscode/` 配下に必要な設定をすでに同梱してある。
VSCode で開けば自動的に PlatformIO IDE 拡張のインストールを推奨してくる。

| ファイル | 内容 |
|:---------|:-----|
| `.vscode/extensions.json` | 推奨拡張: `platformio.platformio-ide`, `ms-vscode.cpptools` |
| `.vscode/settings.json` | C++17、タブ幅 2、PATH に `~/.platformio/penv/bin` を追加 |
| `.vscode/tasks.json` | Build / Upload / Monitor / Clean / mosquitto_sub のショートカット |

### 4-2. 初回セットアップ

PlatformIO Core 本体は別途必要（同梱されない）。
未インストールなら **PlatformIO IDE 拡張を初回起動するときに自動取得** される。
CLI を直接使う場合は次のいずれか:

```sh
# 既にインストール済みかチェック
~/.platformio/penv/bin/pio --version

# 入っていなければ公式 installer
python3 -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py)"
```

### 4-3. ビルド・書き込み手順

1. このリポジトリを VSCode で開く（推奨拡張のインストール提案を承諾）
2. `src/main.cpp` 冒頭の **コンパイル時定数** を環境に合わせて編集（[§5](#5-設定) 参照）
3. M5 Atom PoE Kit を USB-C で接続
4. **Ctrl+Shift+B** で「PIO: Build」、`Tasks: Run Task` → 「PIO: Upload (USB)」で書き込み
   （PlatformIO サイドバーのチェック／矢印アイコンでも可）
5. `Tasks: Run Task` → 「PIO: Monitor」で 115200 baud のシリアル監視

### 4-4. ビルド済み確認

開発機（実機なし）でビルドだけ通っていることは確認済み。

```
RAM:   7.0% (used 22984 bytes from 327680 bytes)
Flash: 25.0% (used 327265 bytes from 1310720 bytes)
```

依存ライブラリは PlatformIO が自動取得する:
ArduinoJson 7.4.3 / PubSubClient 2.8.0 / Ethernet 2.0.2 / FastLED 3.10.3。

### 4-5. 2 回目以降

PoE 給電のみで運用する想定なので、USB を外したまま運用したい。
将来的に ArduinoOTA を組み込めば LAN 越しに書き換えできる（現状未実装）。

---

## 5. 設定

すべて `src/main.cpp` 冒頭の `static const` で完結する。
編集して再書き込みするだけ。WebUI や NVS 設定は **意図的に持たない**
（hobby-scope の方針）。

| 定数 | デフォルト | 説明 |
|:-----|:-----------|:-----|
| `HOUSE_ID` | `"h01"` | MQTT トピックの `{house}` 部分。OGMS と揃える |
| `NODE_ID` | `"agri-rain-01"` | MQTT クライアント ID。ハウス内でユニークに |
| `MQTT_HOST` | `"192.168.100.1"` | ブローカー IP |
| `MQTT_PORT` | `1883` | |
| `PUBLISH_INTERVAL_MS` | `10000` | publish 周期（ms） |
| `SENSOR_POLL_MS` | `5000` | センサー読み取り周期（ms） |

### 静的 IP にしたい場合

現状は DHCP のみ。固定 IP が必要になったら `Ethernet.begin(mac, ip, dns, gw, subnet)`
へ差し替える（`ethernetBegin()` の中身を変更）。

---

## 6. MQTT 仕様

`~/agri-relay/docs/operation-manual.md` の仕様に準拠。

### Publish

| トピック | retained | ペイロード例 |
|:---------|:---------|:-------------|
| `agriha/{house}/sensor/SEN0575` | yes | `{"rainfall_mm":1.23,"raw_tips":15,"work_min":120,"node_id":"agri-rain-01","timestamp":0}` |
| `agriha/{house}/sensor/SEN0575/status` | yes | `"online"` / `"offline"` (LWT) |

### フィールド

| キー | 単位 | 由来 |
|:-----|:-----|:-----|
| `rainfall_mm` | mm | Modbus reg 0x0008-0x0009 (32bit) ÷ 10000 |
| `raw_tips` | 回 | Modbus reg 0x000A-0x000B 転倒回数生値 |
| `work_min` | 分 | Modbus reg 0x000C 通電時間 |
| `node_id` | — | 区別用。同じ house に複数ノードがある場合に便利 |
| `timestamp` | epoch s | 本ノードは NTP を持たないので常に 0。ブローカー側でタイムスタンプを付ける運用 |

### Subscribe

なし。本ノードは publish 専用。

---

## 7. ステータス LED（NeoPixel GPIO27）

| 色 | 状態 |
|:---|:-----|
| 白（薄） | ブート中 |
| 赤 | Ethernet リンクダウン or DHCP 取得失敗 |
| 黄 | リンク OK / MQTT 未接続 |
| マゼンタ | MQTT OK / SEN0575 未検出（PID mismatch or 無応答） |
| 緑 | 全て OK |
| 青（瞬間点滅） | publish 成功 |

トラブル切り分けは LED 色を見るだけで大半が片付くようにしてある。

---

## 8. SEN0575 Modbus プロトコル（メモ）

OGMS の `ogms.ino:766-869` 実装と完全に同一。再実装ではなく踏襲。

- スレーブアドレス: `0xC0`
- ボーレート: 9600 8N1
- 機能コード: `0x04`（Read Input Registers）
- 主要レジスタ:

| アドレス | 内容 | 備考 |
|:---------|:-----|:-----|
| 0x0000 | PID（下位）  | `(VID & 0xC000) << 2 \| PID` で 0x000100C0 になれば本物 |
| 0x0001 | VID（上位 2bit が PID 上位） | 〃 |
| 0x0008-0x0009 | 累積雨量 (uint32, LSW first) | 値 ÷ 10000 = mm |
| 0x000A-0x000B | 転倒生カウント (uint32, LSW first) | |
| 0x000C | 通電時間（分） | |

CRC は 0xFFFF 初期値の標準 Modbus CRC-16（多項式 0xA001）。

---

## 9. 後日の動作確認手順

1. **配線確認** — §3 の表通り、特に G26/G32 とセンサー TX/RX が交差しているか
2. **DIP スイッチ確認** — SEN0575 が UART モードになっているか（出荷時 I2C）
3. **PoE 給電** — LED が白 → 赤 → 黄 と遷移すれば Ethernet 段階クリア
4. **シリアルモニタ**（USB-C 接続時のみ）— `MAC ... / DHCP ... / SEN0575: PID=0x000100C0 ... DETECTED` を確認
5. **MQTT 受信確認** —
   ```sh
   mosquitto_sub -h <broker> -t 'agriha/h01/sensor/SEN0575' -v
   ```
   10 秒ごとに JSON が流れてくる
6. **雨量更新確認** — センサーのバケットを指で軽く転倒させ、`raw_tips` がインクリメントするか確認

---

## 10. 既知の制約・TODO

- **NTP 未対応**: `timestamp` は常に 0。必要ならブローカー側でタイムスタンプを付ける。
  本ノードに NTP を入れたければ `NTPClient` を足すが、scope が膨らむのでとりあえず保留。
- **OTA 未対応**: 書き換えは USB-C のみ。LAN 越しに更新したくなったら ArduinoOTA を足す。
- **upload_speed=115200 固定**: この個体（M5 Atom Lite + CH9102F USB-UART）は 230400 以上だとほぼ失敗するので 115200 に下げてある。書き込みは 30 秒前後かかるがこれで安定する。
- **OGMS とのトピック衝突注意**: OGMS 側も SEN0575 を排液計として `agriha/{house}/sensor/SEN0575` に publish するコードを持つ（`drain_mm` キー、`ogms.ino:1138-1149`）。
  同じ `house_id` で両方が走るなら片方を必ず無効化する。本ノード優先なら OGMS の SEN0575 ブロックをコメントアウト。
- **再接続のバックオフが固定 5 秒**: 大規模なブローカー障害時に再接続を試み続けるが、現実的には問題ないはず。

---

## 11. 参考リンク

- DFRobot SEN0575: https://wiki.dfrobot.com/SKU_SEN0575_Gravity_Rainfall_Sensor
- M5Stack ATOM PoE Kit: https://docs.m5stack.com/en/atom/atom_poe
- OGMS 操作マニュアル: `~/agri-relay/docs/operation-manual.md`
