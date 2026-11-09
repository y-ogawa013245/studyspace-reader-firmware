#pragma once

// ============================================================================
// StudySpace 単体カードリーダー設定
// ハードウェア構成:
//  - 本体: M5Stack ATOMS3 Lite (ESP32-S3 / SKU: C124)
//  - 拡張ベース: M5Stack Atomic Port ABC Base (SKU: A130)
//  - PORT A (I2C): M5Stack NFC Universal Unit (ST25R3916 / SKU: U216)
//  - PORT B (PWM): M5Stack Unit Buzzer (パッシブブザー / SKU: U132)
// ============================================================================

struct WifiCredential {
    const char* ssid;
    const char* password;
};

// 1. 事前登録Wi-Fiリスト（複数環境対応・WiFiMulti自動判別）
static const WifiCredential DEFAULT_WIFI_LIST[] = {
    { "StudySpace_Naha",  "password1234" },
    { "StudySpace_Oroku", "password5678" },
    { "YOUR_DEV_WIFI",    "your_dev_pass" },
};
static const int DEFAULT_WIFI_COUNT = sizeof(DEFAULT_WIFI_LIST) / sizeof(DEFAULT_WIFI_LIST[0]);

// 2. デフォルトのバックエンドAPI URL
#define DEFAULT_SERVER_URL "https://entry.studyspace.okinawa"

// 3. デフォルトの拠点・設置場所名（空文字の場合はデバイスIDから自動命名）
#define DEFAULT_TERMINAL_NAME ""

// 4. スマホ設定モード（アクセスポイント）のWi-Fi名プレフィックス
#define SETUP_AP_SSID_PREFIX "StudySpace-Setup-"

// 5. 端末識別子（空文字の場合はMACアドレスから "ATOMS3_XXXXXXXXXXXX" を自動生成）
#define CUSTOM_DEVICE_ID ""

// ============================================================================
// 6. ハードウェアピン定義 (ATOMS3 Lite + Atomic Port ABC Base A130 公式ピンマップ準拠)
// ============================================================================
// 【Atomic Port ABC Base (A130) + ATOMS3 Lite ピン対応表】
//  - Base PORT A (赤 / I2C)  : Yellow(SDA) = GPIO 38, White(SCL) = GPIO 39
//  - 本体側面 Grove (I2C)     : Yellow(SDA) = GPIO 2,  White(SCL) = GPIO 1
//  - Base PORT B (黒 / GPIO) : Yellow(Sig1)= GPIO 8,  White(Sig2)= GPIO 7
//  - Base PORT C (青 / UART) : Yellow      = GPIO 5,  White      = GPIO 6
//
// ※NFC Unit (0x50) は起動時に Base PORT A (G38/G39) を優先スキャンし、
//   未検出の場合は本体側面 Grove (G2/G1) へ自動フォールバックします。
#define PORTA_BASE_SDA_PIN  38
#define PORTA_BASE_SCL_PIN  39

#define PORTA_SIDE_SDA_PIN  2
#define PORTA_SIDE_SCL_PIN  1

// Unit Buzzer (U132) 接続先: Atomic Port ABC Base の PORT B (黒)
// Unit Buzzer の信号線は Grove ケーブルの Yellow 線 (Pin 1) であり、
// Atomic Port ABC Base の PORT B では GPIO 8 (またはロット/配線により GPIO 7) に接続されます。
// ※実機未検証項目: 実機にて音が鳴らない場合は BUZZER_PIN を 7 に変更してください。
#define BUZZER_PIN          8
#define BUZZER_LEDC_CHANNEL 0
#define BUZZER_LEDC_RES_BIT 10

// ブザー音声通知の初期有効フラグ (true: 有効 / false: ブザー未接続・消音モード)
// ※スマホWeb設定画面 (http://192.168.4.1) からも拠点ごとに ON/OFF を切り替え可能です。
#define DEFAULT_BUZZER_ENABLED true

// 本体フルカラーRGB LED (WS2812 / NeoPixel): GPIO 35
#define RGB_LED_PIN         35

// 本体フロントボタン: GPIO 41
#define BTN_PIN             41

// 同一カードの連続二重タッチ防止クールダウン時間（ミリ秒）
#define TOUCH_COOLDOWN_MS   3000
