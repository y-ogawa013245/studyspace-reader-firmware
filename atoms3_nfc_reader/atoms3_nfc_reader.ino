/**
 * StudySpace 単体ICカードリーダー ファームウェア
 * （複数環境Wi-Fi・スマホWeb設定モード・非ブロッキングLEDC音声通知 対応版）
 *
 * 対象ハードウェア構成:
 *  - マイコン本体 : M5Stack ATOMS3 Lite (ESP32-S3 / SKU: C124)
 *  - 拡張ベース   : M5Stack Atomic Port ABC Base (SKU: A130)
 *  - PORT A (I2C) : M5Stack NFC Universal Unit (ST25R3916 / SKU: U216)
 *  - PORT B (PWM) : M5Stack Unit Buzzer (パッシブブザー / SKU: U132)
 *
 * 設計上の重要ポイント:
 *  - NFC読み取り処理と音声通知処理 (BuzzerNotifier: buzzer_notifier.h) を完全に分離
 *  - ブザー駆動は LEDC PWM (Arduino-ESP32 v2.x/v3.x 両対応) による矩形波のみを使用し、直流HIGH固定を防止
 *  - ブザー再生および打刻結果LED表示は millis() ベースの非ブロッキング制御で実装
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// M5Stack公式統合ライブラリ & Unit NFC (ST25R3916) ライブラリ
#define USING_UNIT_NFC
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5Unit-NFC.h>

#include "config.h"
#include "buzzer_notifier.h"

// NFC・ハードウェア制御インスタンス（音声通知とは分離）
m5::UnitUnified Units;
M5UnitNFC nfcUnit;
m5::nfc::NFCLayerF nfcF(nfcUnit); // FeliCa (NFC-F: Suica/PASMO/学生証等)
m5::nfc::NFCLayerA nfcA(nfcUnit); // ISO14443A (NFC-A: MIFARE/NTAG等)
m5::nfc::PICC picc{};

// 非ブロッキング音声通知モジュール (PORT B: Unit Buzzer U132)
BuzzerNotifier buzzer;

// ネットワーク・不揮発メモリ保存用インスタンス
WiFiMulti wifiMulti;
Preferences prefs;
WebServer webServer(80);
DNSServer dnsServer;

// ランタイム設定値（NVSまたはconfig.hからロード）
String deviceId = "";
String savedSsid = "";
String savedPass = "";
String serverBaseUrl = DEFAULT_SERVER_URL;
String terminalName = DEFAULT_TERMINAL_NAME;
bool nameNeedSync = false;
bool buzzerEnabled = DEFAULT_BUZZER_ENABLED;

// 動作モード管理
bool isSetupPortalMode = false;

// 連続タッチ防止用ステート
String lastIdm = "";
unsigned long lastTouchTime = 0;

// 非ブロッキングLED状態管理
unsigned long ledHoldUntilMs = 0;
bool ledHolding = false;

// ============================================================================
// 本体RGB LED (GPIO 35) 非ブロッキング制御ヘルパー
// ============================================================================
void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(RGB_LED_PIN, r, g, b);
}

void setIdleLed() {
    setLedColor(0, 4, 8);
}

/**
 * 指定時間だけLEDを指定色に保持し、時間経過後に updateLedState() で待機色へ戻す（非ブロッキング）
 */
void showTimedLed(uint8_t r, uint8_t g, uint8_t b, unsigned long durationMs) {
    setLedColor(r, g, b);
    ledHoldUntilMs = millis() + durationMs;
    ledHolding = true;
}

void updateLedState() {
    if (ledHolding && (long)(millis() - ledHoldUntilMs) >= 0) {
        ledHolding = false;
        setIdleLed();
    }
}

// 起動時・設定モード移行時のみ使用する短時間点滅
void blinkLedStartup(uint8_t r, uint8_t g, uint8_t b, int times, int durationMs) {
    for (int i = 0; i < times; i++) {
        setLedColor(r, g, b);
        delay(durationMs);
        setLedColor(0, 0, 0);
        delay(durationMs);
    }
}

// ============================================================================
// デバイスID生成（MACアドレスベース）
// ============================================================================
String buildDeviceId() {
    if (strlen(CUSTOM_DEVICE_ID) > 0) {
        return String(CUSTOM_DEVICE_ID);
    }
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    return "ATOMS3_" + mac;
}

// ============================================================================
// 本体メモリ（Preferences / NVS）から設定を読み込み
// ============================================================================
void loadSettingsFromNvs() {
    prefs.begin("studyspace", true);
    savedSsid     = prefs.getString("ssid", "");
    savedPass     = prefs.getString("pass", "");
    serverBaseUrl = prefs.getString("server_url", DEFAULT_SERVER_URL);
    terminalName  = prefs.getString("term_name", DEFAULT_TERMINAL_NAME);
    nameNeedSync  = prefs.getBool("name_sync", false);
    buzzerEnabled = prefs.getBool("buzzer_on", DEFAULT_BUZZER_ENABLED);
    prefs.end();

    if (serverBaseUrl.endsWith("/")) {
        serverBaseUrl.remove(serverBaseUrl.length() - 1);
    }

    Serial.println("[CONFIG] Loaded from NVS:");
    Serial.println("  - Terminal Name : " + (terminalName.length() ? terminalName : "(未指定)"));
    Serial.println("  - Saved SSID    : " + (savedSsid.length() ? savedSsid : "(未指定)"));
    Serial.println("  - Server URL    : " + serverBaseUrl);
    Serial.println("  - Buzzer Sound  : " + String(buzzerEnabled ? "ON (有効)" : "OFF (ブザー未装着/消音)"));
}

// ============================================================================
// エスケープ補助関数
// ============================================================================
String escapeHtml(const String& input) {
    String s = input;
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    return s;
}

String escapeJson(const String& input) {
    String s = input;
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    return s;
}

// ============================================================================
// USBシリアル経由での初期設定コマンド受信 (PCインストーラー setup.exe / setup.command 連携用)
// フォーマット: STUDYSPACE_SET|<term_name>|<ssid>|<pass>|<server_url>|<buzzer_on(1/0)>
// ============================================================================
void checkSerialConfigCommand() {
    if (!Serial.available()) return;

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("STUDYSPACE_SET|")) {
        if (line == "STUDYSPACE_INFO") {
            Serial.println("STUDYSPACE_READY|" + deviceId + "|" + terminalName + "|" + serverBaseUrl);
        }
        return;
    }

    // パイプ区切りで5項目をパース
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    int p4 = line.indexOf('|', p3 + 1);
    int p5 = line.indexOf('|', p4 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0) {
        Serial.println("STUDYSPACE_ERR|Invalid format");
        return;
    }

    String newTermName  = line.substring(p1 + 1, p2);
    String newSsid      = line.substring(p2 + 1, p3);
    String newPass      = line.substring(p3 + 1, p4);
    String newServerUrl = line.substring(p4 + 1, p5);
    String newBuzzerStr = line.substring(p5 + 1);

    newTermName.trim();
    newSsid.trim();
    newServerUrl.trim();
    if (newServerUrl.endsWith("/")) {
        newServerUrl.remove(newServerUrl.length() - 1);
    }
    bool newBuzzerOn = (newBuzzerStr != "0");

    prefs.begin("studyspace", false);
    prefs.putString("term_name", newTermName);
    prefs.putString("ssid", newSsid);
    prefs.putString("pass", newPass);
    if (newServerUrl.length() > 0) {
        prefs.putString("server_url", newServerUrl);
    }
    prefs.putBool("buzzer_on", newBuzzerOn);
    prefs.putBool("name_sync", true);
    prefs.end();

    Serial.println("STUDYSPACE_OK|" + deviceId + "|" + newTermName);
    buzzer.stop();
    blinkLedStartup(0, 50, 0, 3, 100);
    delay(500);
    ESP.restart();
}

// ============================================================================
// スマホ・PC向け Web設定モード（Captive Portal: http://192.168.4.1）
// ============================================================================
void handlePortalRoot() {
    int n = WiFi.scanNetworks();
    String ssidOptions = "";
    for (int i = 0; i < n; ++i) {
        String s = WiFi.SSID(i);
        if (s.length() > 0) {
            String escaped = escapeHtml(s);
            ssidOptions += "<option value=\"" + escaped + "\">" + escaped + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
        }
    }

    String html = R"rawliteral(<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StudySpace リーダー設定</title>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; }
  .card { max-width: 440px; margin: 0 auto; background: #1e293b; border-radius: 14px; padding: 24px; box-shadow: 0 10px 25px rgba(0,0,0,0.4); border: 1px solid rgba(56,189,248,0.25); }
  h1 { font-size: 1.25rem; margin: 0 0 6px; color: #38bdf8; }
  .sub { font-size: 0.82rem; color: #94a3b8; margin-bottom: 20px; line-height: 1.5; }
  label { display: block; font-size: 0.84rem; font-weight: 600; color: #cbd5e1; margin: 14px 0 6px; }
  input, select { width: 100%; box-sizing: border-box; padding: 11px 12px; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: #fff; font-size: 0.95rem; }
  .hint { font-size: 0.76rem; color: #64748b; margin-top: 4px; }
  button { width: 100%; margin-top: 22px; padding: 13px; border: none; border-radius: 10px; background: linear-gradient(135deg, #10b981, #059669); color: #fff; font-size: 1rem; font-weight: 700; cursor: pointer; }
  .badge { display: inline-block; background: rgba(56,189,248,0.15); color: #38bdf8; padding: 4px 10px; border-radius: 6px; font-family: monospace; font-size: 0.8rem; margin-bottom: 12px; }
</style>
</head>
<body>
<div class="card">
  <div class="badge">ID: )rawliteral" + escapeHtml(deviceId) + R"rawliteral(</div>
  <h1>StudySpace カードリーダー設定</h1>
  <p class="sub">設置する拠点名（校舎・教室名）とWi-Fi・接続先サーバーを設定します。保存すると本体メモリに記憶され、サーバーへ拠点名が自動登録されます。</p>

  <form method="POST" action="/save">
    <label>① 拠点・設置場所名（所属名）</label>
    <input type="text" name="term_name" value=")rawliteral" + escapeHtml(terminalName) + R"rawliteral(" placeholder="例: 那覇校 1F自習室 / 小禄校 受付" required>
    <div class="hint">※ここで入力した名前がマスターPCの管理画面＆打刻ログの所属名として登録されます。</div>

    <label>② Wi-Fi SSID（周囲の電波から選択 または 手入力）</label>
    <select onchange="if(this.value) document.getElementById('ssid_input').value=this.value;">
      <option value="">▼ 検出されたWi-Fiから選ぶ...</option>
      )rawliteral" + ssidOptions + R"rawliteral(
    </select>
    <input type="text" id="ssid_input" name="ssid" value=")rawliteral" + escapeHtml(savedSsid) + R"rawliteral(" placeholder="Wi-FiのSSIDを入力（2.4GHz帯）" style="margin-top:8px;">
    <div class="hint">※空欄のままにした場合は config.h に事前登録されたWi-Fiリストを使用します。</div>

    <label>③ Wi-Fi パスワード</label>
    <input type="password" name="pass" value=")rawliteral" + escapeHtml(savedPass) + R"rawliteral(" placeholder="Wi-Fiパスワードを入力">

    <label>④ 接続先サーバーURL</label>
    <input type="url" name="server_url" value=")rawliteral" + escapeHtml(serverBaseUrl) + R"rawliteral(" placeholder="https://entry.studyspace.okinawa" required>
    <div class="hint">本番例: https://entry.studyspace.okinawa ／ テスト例: http://192.168.1.10:8000</div>

    <label>⑤ 音声通知（Unit Buzzer の有無）</label>
    <select name="buzzer_on">
      <option value="1" )rawliteral" + String(buzzerEnabled ? "selected" : "") + R"rawliteral(>有効（Unit Buzzer で音を鳴らす）</option>
      <option value="0" )rawliteral" + String(!buzzerEnabled ? "selected" : "") + R"rawliteral(>無効（ブザー未装着・LEDのみで稼働）</option>
    </select>
    <div class="hint">※ブザーを繋がない拠点や、静かな自習室で消音にしたい場合は「無効」を選択してください。</div>

    <button type="submit">設定を保存して再起動</button>
  </form>
</div>
</body>
</html>)rawliteral";

    webServer.send(200, "text/html; charset=utf-8", html);
}

void handlePortalSave() {
    String newTermName  = webServer.arg("term_name");
    String newSsid      = webServer.arg("ssid");
    String newPass      = webServer.arg("pass");
    String newServerUrl = webServer.arg("server_url");
    bool newBuzzerOn    = (webServer.arg("buzzer_on") != "0");

    newTermName.trim();
    newSsid.trim();
    newServerUrl.trim();
    if (newServerUrl.endsWith("/")) {
        newServerUrl.remove(newServerUrl.length() - 1);
    }

    prefs.begin("studyspace", false);
    prefs.putString("term_name", newTermName);
    prefs.putString("ssid", newSsid);
    prefs.putString("pass", newPass);
    if (newServerUrl.length() > 0) {
        prefs.putString("server_url", newServerUrl);
    }
    prefs.putBool("buzzer_on", newBuzzerOn);
    prefs.putBool("name_sync", true);
    prefs.end();

    String doneHtml = R"rawliteral(<!DOCTYPE html>
<html lang="ja">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>保存完了</title>
<style>body{font-family:sans-serif;background:#0f172a;color:#f8fafc;padding:30px;text-align:center;}.box{max-width:400px;margin:0 auto;background:#1e293b;padding:24px;border-radius:12px;border:1px solid #10b981;}</style>
</head><body><div class="box">
<h2 style="color:#34d399;">✅ 設定を保存しました！</h2>
<p>拠点名: <strong>)rawliteral" + escapeHtml(newTermName) + R"rawliteral(</strong></p>
<p style="font-size:0.88rem;color:#94a3b8;">リーダーを再起動してWi-Fiへ接続し、サーバーへ拠点名を登録します。</p>
</div></body></html>)rawliteral";

    webServer.send(200, "text/html; charset=utf-8", doneHtml);
    buzzer.stop();
    delay(1500);
    ESP.restart();
}

void startSetupPortal() {
    buzzer.stop();
    isSetupPortalMode = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);

    String apSsid = String(SETUP_AP_SSID_PREFIX) + deviceId.substring(deviceId.length() - 4);
    WiFi.softAP(apSsid.c_str());

    IPAddress apIp = WiFi.softAPIP();
    dnsServer.start(53, "*", apIp);

    webServer.on("/", HTTP_GET, handlePortalRoot);
    webServer.on("/save", HTTP_POST, handlePortalSave);
    webServer.onNotFound([]() {
        webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        webServer.send(302, "text/plain", "");
    });
    webServer.begin();

    setLedColor(20, 35, 60);

    Serial.println("\n==========================================");
    Serial.println(" [SETUP MODE] スマホWeb設定モード起動中");
    Serial.println("  1. スマホのWi-Fiで「" + apSsid + "」に接続");
    Serial.println("  2. ブラウザで http://" + apIp.toString() + " を開く");
    Serial.println("==========================================");
}

// ============================================================================
// PORT A (I2C) 自動検出＆ST25R3916 (Unit NFC) 初期化
// Atomic Port ABC Base の PORT A (G38/G39) と 本体側面 PORT A (G2/G1) を順に試行
// ============================================================================
bool initNfcUnit() {
    // 1. Atomic Port ABC Base (A130) の PORT A (SDA=G38, SCL=G39) を優先試行
    Serial.printf("[NFC] Trying Atomic Port ABC Base PORT A (SDA=G%d, SCL=G%d)...\n", PORTA_BASE_SDA_PIN, PORTA_BASE_SCL_PIN);
    Wire.end();
    Wire.begin(PORTA_BASE_SDA_PIN, PORTA_BASE_SCL_PIN);
    Wire.beginTransmission(0x50);
    if (Wire.endTransmission() == 0) {
        Units.add(nfcUnit, Wire);
        if (Units.begin()) {
            Serial.println("[OK] ST25R3916 Unit NFC connected on Base PORT A (G38/G39).");
            return true;
        }
    }

    // 2. 本体側面 Grove ポート (SDA=G2, SCL=G1) へフォールバック試行
    Serial.printf("[NFC] Trying ATOMS3 Lite Side Grove Port (SDA=G%d, SCL=G%d)...\n", PORTA_SIDE_SDA_PIN, PORTA_SIDE_SCL_PIN);
    Wire.end();
    Wire.begin(PORTA_SIDE_SDA_PIN, PORTA_SIDE_SCL_PIN);
    Wire.beginTransmission(0x50);
    if (Wire.endTransmission() == 0) {
        Units.add(nfcUnit, Wire);
        if (Units.begin()) {
            Serial.println("[OK] ST25R3916 Unit NFC connected on Side Grove Port (G2/G1).");
            return true;
        }
    }

    return false;
}

// ============================================================================
// 複数環境Wi-Fi接続（スマホ保存Wi-Fi ＋ config.h の複数リストから自動選択）
// ============================================================================
bool connectToMultiWifi(unsigned long timeoutMs = 15000) {
    WiFi.mode(WIFI_STA);

    if (savedSsid.length() > 0) {
        wifiMulti.addAP(savedSsid.c_str(), savedPass.c_str());
    }

    for (int i = 0; i < DEFAULT_WIFI_COUNT; i++) {
        if (strlen(DEFAULT_WIFI_LIST[i].ssid) > 0 && String(DEFAULT_WIFI_LIST[i].ssid) != "YOUR_DEV_WIFI") {
            wifiMulti.addAP(DEFAULT_WIFI_LIST[i].ssid, DEFAULT_WIFI_LIST[i].password);
        }
    }

    Serial.print("[WIFI] Scanning & connecting to available network ");
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (wifiMulti.run() == WL_CONNECTED) {
            Serial.println("\n[WIFI] Connected!");
            Serial.println("  - Connected SSID : " + WiFi.SSID());
            Serial.println("  - IP Address     : " + WiFi.localIP().toString());
            return true;
        }
        if (digitalRead(BTN_PIN) == LOW) {
            Serial.println("\n[BUTTON] Setup mode requested during boot.");
            return false;
        }
        blinkLedStartup(0, 0, 45, 1, 200);
        Serial.print(".");
    }

    Serial.println("\n[WIFI] Connection timed out.");
    return false;
}

// ============================================================================
// サーバーへの起動通知・拠点名登録 (POST /api/reader/ping)
// ============================================================================
void sendPingToServer() {
    if (WiFi.status() != WL_CONNECTED) return;

    String url = serverBaseUrl + "/api/reader/ping";
    HTTPClient http;

    WiFiClientSecure secureClient;
    WiFiClient plainClient;

    if (url.startsWith("https://")) {
        secureClient.setInsecure();
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Device-Id", deviceId);

    String payload = "{\"device_id\":\"" + escapeJson(deviceId) + "\"";
    if (terminalName.length() > 0) {
        payload += ",\"name\":\"" + escapeJson(terminalName) + "\"";
        if (nameNeedSync) {
            payload += ",\"update_name\":true";
        }
    }
    payload += "}";

    int httpCode = http.POST(payload);

    if (httpCode >= 200 && httpCode < 300) {
        String res = http.getString();
        Serial.printf("[PING OK] (%d): %s\n", httpCode, res.c_str());

        if (nameNeedSync) {
            prefs.begin("studyspace", false);
            prefs.putBool("name_sync", false);
            prefs.end();
            nameNeedSync = false;
        }
        showTimedLed(0, 40, 0, 400);
    } else {
        Serial.printf("[PING ERR] HTTP %d\n", httpCode);
        showTimedLed(40, 15, 0, 600);
    }

    http.end();
}

// ============================================================================
// カードタッチ打刻送信 (POST /api/reader/touch)
// 結果に応じて非ブロッキング音声通知 (buzzer.play) と非ブロッキングLED表示を発火
// ============================================================================
void sendTouchToServer(const String& idm) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Reconnecting via WiFiMulti...");
        if (wifiMulti.run() != WL_CONNECTED) {
            buzzer.play(SoundPattern::WARNING_ERROR);
            showTimedLed(60, 0, 0, 1000);
            return;
        }
    }

    // 通信開始：黄色点灯
    setLedColor(35, 25, 0);

    String url = serverBaseUrl + "/api/reader/touch";
    HTTPClient http;

    WiFiClientSecure secureClient;
    WiFiClient plainClient;

    if (url.startsWith("https://")) {
        secureClient.setInsecure();
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }

    http.setTimeout(6000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Device-Id", deviceId);

    String payload = "{\"idm\":\"" + idm + "\",\"device_id\":\"" + escapeJson(deviceId) + "\"";
    if (terminalName.length() > 0) {
        payload += ",\"terminal_name\":\"" + escapeJson(terminalName) + "\"";
    }
    payload += "}";

    Serial.printf("[TOUCH] POST %s -> %s\n", url.c_str(), payload.c_str());

    int httpCode = http.POST(payload);
    String response = http.getString();
    http.end();

    Serial.printf("[TOUCH RES] HTTP %d: %s\n", httpCode, response.c_str());

    if (httpCode == 200) {
        if (response.indexOf("\"type\":\"exit\"") >= 0) {
            // 退室成功：下降する2音 ＋ 水色（シアン）非ブロッキング点灯
            buzzer.play(SoundPattern::EXIT_SUCCESS);
            showTimedLed(0, 45, 55, 1200);
        } else {
            // 入室成功：上昇する2音 ＋ 緑色非ブロッキング点灯
            buzzer.play(SoundPattern::ENTRY_SUCCESS);
            showTimedLed(0, 60, 0, 1200);
        }
    } else {
        // 未登録カード(404) / 停止中端末(403) / API通信失敗：低い警告音を2回 ＋ 赤色非ブロッキング点灯
        buzzer.play(SoundPattern::WARNING_ERROR);
        showTimedLed(60, 0, 0, 1000);
    }
}

// ============================================================================
// NFCカード読み取り処理（音声通知とは分離された純粋なタグ検出関数）
// ============================================================================
String pollNfcCardIdm() {
    char buf[3];
    String hex = "";

    // 1. FeliCa (NFC-F: 学生証・交通系ICカード等) の検出
    if (nfcF.detect(picc) && picc.uid_len > 0) {
        for (size_t i = 0; i < picc.uid_len; i++) {
            snprintf(buf, sizeof(buf), "%02X", picc.uid[i]);
            hex += buf;
        }
        return hex;
    }

    // 2. NFC-A (MIFARE / NTAG等) のフォールバック検出
    if (nfcA.detect(picc) && picc.uid_len > 0) {
        for (size_t i = 0; i < picc.uid_len; i++) {
            snprintf(buf, sizeof(buf), "%02X", picc.uid[i]);
            hex += buf;
        }
        return hex;
    }

    return "";
}

// ============================================================================
// 初期化処理 (setup)
// ============================================================================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);

    pinMode(BTN_PIN, INPUT_PULLUP);
    setLedColor(20, 0, 20); // 起動開始（紫）

    WiFi.mode(WIFI_STA);
    deviceId = buildDeviceId();
    loadSettingsFromNvs();

    // 1. PORT B: Unit Buzzer (U132) の初期化（buzzerEnabled == false の場合はPWM出力を完全無効化）
    buzzer.begin(BUZZER_PIN, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_RES_BIT, buzzerEnabled);

    Serial.println("\n==========================================");
    Serial.println(" StudySpace Standalone Reader (ATOMS3 Lite)");
    Serial.println(" Device ID: " + deviceId);
    Serial.println("==========================================");

    // 起動時に本体ボタン(G41)が押されていたら即座にスマホ設定モードへ移行
    if (digitalRead(BTN_PIN) == LOW) {
        startSetupPortal();
        return;
    }

    // 2. PORT A: ST25R3916 (Unit NFC) の初期化
    if (!initNfcUnit()) {
        Serial.println("[ERROR] ST25R3916 Unit NFC (U216) が見つかりません。PORT A のGroveケーブルを確認してください。");
        while (true) {
            if (digitalRead(BTN_PIN) == LOW) {
                startSetupPortal();
                return;
            }
            setLedColor(50, 0, 0);
            delay(300);
            setLedColor(50, 20, 0);
            delay(300);
        }
    }

    // 3. Wi-Fi 接続（繋がらなければ自動でスマホWeb設定モードを起動）
    if (!connectToMultiWifi(15000)) {
        Serial.println("[WIFI] 利用可能なWi-Fiが見つからないため、スマホWeb設定モードを起動します。");
        startSetupPortal();
        return;
    }

    setIdleLed();

    // 4. サーバーへ起動Ping＆拠点名を送信
    sendPingToServer();
}

// ============================================================================
// メインループ (loop) - 非ブロッキング駆動
// ============================================================================
void loop() {
    // 音声通知ステートマシンの非ブロッキング更新（最優先で毎回実行）
    buzzer.update();
    updateLedState();

    // PCのワンクリックインストーラー(setup.exe)からのUSBシリアル初期設定コマンドを監視
    checkSerialConfigCommand();

    // スマホWeb設定モード中の場合はWebサーバー処理のみ実行
    if (isSetupPortalMode) {
        dnsServer.processNextRequest();
        webServer.handleClient();
        delay(2);
        return;
    }

    M5.update();
    Units.update();

    // 本体ボタン(G41)の短押し（Ping送信） / 2秒長押し（スマホ設定モード起動）判定
    if (digitalRead(BTN_PIN) == LOW) {
        unsigned long pressStart = millis();
        while (digitalRead(BTN_PIN) == LOW) {
            buzzer.update();
            if (millis() - pressStart >= 2000) {
                blinkLedStartup(20, 35, 60, 3, 120);
                startSetupPortal();
                return;
            }
            delay(10);
        }
        if (millis() - pressStart > 50) {
            Serial.println("[BUTTON] Short press -> Sending manual ping...");
            buzzer.play(SoundPattern::CARD_READ);
            sendPingToServer();
        }
    }

    // NFCカードのポーリング検出
    String detectedIdm = pollNfcCardIdm();

    if (detectedIdm.length() > 0) {
        unsigned long now = millis();
        if (detectedIdm != lastIdm || (now - lastTouchTime) > TOUCH_COOLDOWN_MS) {
            lastIdm = detectedIdm;
            lastTouchTime = now;

            // 1. ブザー有効時のみカード読み取り音（短い単音）を再生
            if (buzzer.isEnabled()) {
                buzzer.play(SoundPattern::CARD_READ);
                unsigned long waitTone = millis();
                while (millis() - waitTone < 65) {
                    buzzer.update();
                    delay(2);
                }
            }

            Serial.println("[CARD DETECTED] IDm/UID: " + detectedIdm);
            sendTouchToServer(detectedIdm);
        }
    }

    delay(15);
}
