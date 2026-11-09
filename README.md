# StudySpace 単体カードリーダー ファームウェア (`firmware/`)

M5Stack ATOMS3 Lite に Atomic Port ABC Base（A130）を装着し、NFC Universal Unit（ST25R3916 / U216）と Unit Buzzer（U132）を組み合わせて、PCレス（単体）で自習室の入退室打刻およびLED・音声通知を行うためのマイコン用プログラムです。

---

## 1. ハードウェア構成・配線・GPIOピンマップ

### 採用ハードウェア一覧
| 役割 | 製品名 | 型番 / SKU | 接続先 |
| :--- | :--- | :--- | :--- |
| **マイコン本体** | [M5Stack ATOMS3 Lite](https://docs.m5stack.com/en/core/AtomS3-Lite) | C124 (Switch Science #8778) | Atomic Port ABC Base 上部に装着 |
| **拡張ベース** | [M5Stack Atomic Port ABC Base](https://docs.m5stack.com/ja/atom/Atomic_Port_ABC_Base) | A130 | ATOMS3 Lite 背面ピンソケットへ装着 |
| **NFCリーダー** | [M5Stack NFC Universal Unit (ST25R3916)](https://www.switch-science.com/products/11128) | U216 (Switch Science #11128) | **PORT A（赤・I²C）** ※本体側面Groveも可 |
| **音声ブザー** | [M5Stack Unit Buzzer](https://docs.m5stack.com/ja/unit/Unit_Buzzer) | U132 | **PORT B（黒・PWM）** |

### 公式回路図・ピンマップに基づくGPIO対応表（ATOMS3 Lite + Atomic Port ABC Base）
| ポート / 機能 | Grove信号線（色） | ATOMS3 Lite 側 GPIO | ファームウェア上の定数 (`config.h`) | 備考 |
| :--- | :--- | :--- | :--- | :--- |
| **Base PORT A（赤：I²C）** | Yellow (SDA) / White (SCL) | **GPIO 38 (SDA) / GPIO 39 (SCL)** | `PORTA_BASE_SDA_PIN = 38`, `PORTA_BASE_SCL_PIN = 39` | 起動時に `0x50` を優先スキャン |
| **本体側面 PORT A（I²C）** | Yellow (SDA) / White (SCL) | **GPIO 2 (SDA) / GPIO 1 (SCL)** | `PORTA_SIDE_SDA_PIN = 2`, `PORTA_SIDE_SCL_PIN = 1` | Base PORT A 未検出時に自動フォールバック |
| **Base PORT B（黒：GPIO/PWM）** | Yellow (Signal 1) / White (Signal 2) | **GPIO 8 (Yellow) / GPIO 7 (White)** | `BUZZER_PIN = 8` | Unit Buzzer (U132) は Yellow 信号線を使用 |
| **Base PORT C（青：UART）** | Yellow / White | **GPIO 5 / GPIO 6** | （未使用） | 予備ポート |
| **本体フルカラーLED** | 内蔵 WS2812 | **GPIO 35** | `RGB_LED_PIN = 35` | ステータス・打刻結果表示 |
| **本体フロントボタン** | 内蔵タクトスイッチ | **GPIO 41** | `BTN_PIN = 41` | 短押し：Ping送信 / 2秒長押し：Web設定モード |

---

## 2. 音声通知仕様・アーキテクチャ上の設計方針

### 音声通知パターン（`buzzer_notifier.h`）
Unit Buzzer（U132）の推奨共振周波数（4000Hz前後）に合わせ、以下の4パターンを定義しています：

| イベント | 音声通知要件 | 実装シーケンス（周波数・持続時間） | 本体RGB LED表示 |
| :--- | :--- | :--- | :--- |
| **カード読み取り** | 短い単音 | `4000Hz (60ms)` | 黄色 点灯（通信開始） |
| **入室成功** | 上昇する2音 | `3200Hz (90ms) -> 休符(30ms) -> 4000Hz (150ms)` | **緑色 点灯（1.2秒）** |
| **退室成功** | 下降する2音 | `4000Hz (90ms) -> 休符(30ms) -> 3000Hz (150ms)` | **水色（シアン）点灯（1.2秒）** |
| **未登録カード・API通信失敗** | 低い警告音を2回 | `1000Hz (140ms) -> 休符(80ms) -> 1000Hz (180ms)` | **赤色 点灯（1.0秒）** |

### 安全設計およびソフトウェア分離方針
1. **PWM駆動の徹底・直流HIGH固定の回避**
   - Unit Buzzer（U132）はパッシブブザー＋NPNトランジスタ駆動回路であるため、直流HIGHを入力し続けると過電流・発熱の恐れがあります。
   - 起動直後に `digitalWrite(BUZZER_PIN, LOW)` で初期化した上で、鳴動時は `ledcWriteTone()` による矩形波（デューティ比50%）のみで駆動し、鳴動終了時・リセット時は即座にデューティ比 `0`（LOW）へ落とします。
2. **Arduino-ESP32 v2.x / v3.x 両対応の LEDC API**
   - `ESP_ARDUINO_VERSION` マクロにより、Arduino-ESP32 v3.x 系（`ledcAttach` / ピン指定型）と v2.x 系（`ledcSetup` + `ledcAttachPin` / チャンネル指定型）の両方でそのままコンパイル・動作します。
3. **非ブロッキング制御とモジュール分離**
   - 音声通知処理は [`buzzer_notifier.h`](atoms3_nfc_reader/buzzer_notifier.h)（`BuzzerNotifier` クラス）に独立させ、NFC読み取り関数（`pollNfcCardIdm()`）から完全に分離しています。
   - `millis()` ベースのステートマシンにより、`loop()` をブロックせずに複数音シーケンスを再生します。

---

## 3. ⚠️ 実機未検証項目（実機が届き次第確認する項目）

以下の項目はドキュメントおよび回路図ベースで実装しており、**実機到着後の検証対象（実機未検証）** です：
- [ ] **Atomic Port ABC Base (A130) の PORT B ピン極性確認**:
  - 公式ピンマップ上、AtomS3 装着時の PORT B は `GPIO 8` および `GPIO 7` に割り当てられます。Unit Buzzer（U132）の信号線（Yellow）が `GPIO 8` で鳴動するか実機で確認してください（万一鳴らない場合は `config.h` の `#define BUZZER_PIN 8` を `7` に変更してください）。
- [ ] **Atomic Port ABC Base (A130) の PORT A (G38/G39) における ST25R3916 I²C 通信確認**:
  - Base 側の PORT A（`G38`/`G39`）および本体側面 PORT A（`G2`/`G1`）の自動検出・フォールバック動作の実機確認。
- [ ] **Unit Buzzer (U132) の音量・周波数バランスの聴感確認**:
  - 4000Hz / 3200Hz / 3000Hz / 1000Hz の各トーンが自習室環境で聞き取りやすい音量・音程であるかの実機確認。

---

## 4. スマホ・PCからのWeb設定モード（拠点名・Wi-Fi・サーバーURL登録）

1. 登録済みWi-Fiが見つからない場所で電源を入れた時、または **ATOMS3 Liteの本体ボタン（G41）を2秒以上長押し** した時に、リーダー自身が **`StudySpace-Setup-XXXX`** というWi-Fiを発信します（LEDが**青白色**に点灯）。
2. スマホやPCでそのWi-Fiに繋ぎ、ブラウザで **`http://192.168.4.1`** を開きます。
3. 画面から **「① 拠点・設置場所名」「② Wi-Fi SSID」「③ Wi-Fi パスワード」「④ 接続先サーバーURL」** を入力して保存すると、本体メモリ（NVS）に保存され、再起動後にサーバーへ拠点名が自動登録されます。

---

## 5. Arduino IDE での書き込み手順

1. [Arduino公式サイト](https://www.arduino.cc/en/software) から **Arduino IDE (2.x)** をインストールして開きます。
2. **`ファイル` ＞ `基本設定`** の「追加のボードマネージャのURL」に以下を追加します：
   ```text
   https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
   ```
3. **ボードマネージャ** で **`M5Stack`** をインストールし、ボードとして **`M5Stack AtomS3`** を選択します。
4. **ライブラリマネージャ** で **`M5Unified`**、**`M5UnitUnified`**、**`M5Unit-NFC`** の3つをインストールします。
5. `firmware/atoms3_nfc_reader/atoms3_nfc_reader.ino` を開いて書き込みます。
