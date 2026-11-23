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
#include <M5UnitUnifiedNFC.h>

#include "config.h"
#include "buzzer_notifier.h"

// NFC・ハードウェア制御インスタンス（音声通知とは分離）
m5::unit::UnitUnified Units;
m5::unit::UnitNFC nfcUnit;
m5::nfc::NFCLayerF nfcF{nfcUnit}; // FeliCa (NFC-F: Suica/PASMO/学生証等)
m5::nfc::NFCLayerA nfcA{nfcUnit}; // ISO14443A (NFC-A: MIFARE/NTAG等)

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

// スマホ・PCリアルタイムモニター用ステート
String uiLastIdm = "";
String uiLastResult = "待機中（ICカードをかざすとここにリアルタイム表示されます）";
unsigned long uiTouchSeq = 0;
String nfcPortStatus = "未初期化";

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
// デバイスID生成（eFuse MACアドレスベース - Wi-Fi初期化前でも確実に固有MACを取得）
// ============================================================================
String buildDeviceId() {
    if (strlen(CUSTOM_DEVICE_ID) > 0) {
        return String(CUSTOM_DEVICE_ID);
    }
    uint64_t efuseMac = ESP.getEfuseMac();
    uint8_t mac[6];
    mac[0] = (efuseMac >> 0) & 0xFF;
    mac[1] = (efuseMac >> 8) & 0xFF;
    mac[2] = (efuseMac >> 16) & 0xFF;
    mac[3] = (efuseMac >> 24) & 0xFF;
    mac[4] = (efuseMac >> 32) & 0xFF;
    mac[5] = (efuseMac >> 40) & 0xFF;
    char buf[24];
    snprintf(buf, sizeof(buf), "ATOMS3_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
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
// PCからのハードウェア一括診断コマンド (STUDYSPACE_DIAG / STUDYSPACE_TEST)
// 1. ブザー (PORT B/C: GPIO 8, 7, 6, 5) の鳴動テスト
// 2. 拡張ベース PORT A & ST25R3916 (Unit NFC) のI2Cレジスタ・RF電波状態診断
// 3. 10秒間のリアルタイムNFCカード読み取りテスト (FeliCa NFC-F & ISO14443A NFC-A)
// ============================================================================
String pollNfcCardIdm();

void runHardwareDiagnostics() {
    Serial.println("\n==========================================");
    Serial.println(" [DIAG] ハードウェア接続＆動作テスト開始");
    Serial.println("==========================================");

    // 1. ブザー鳴動テスト (PORT B: GPIO 8 / GPIO 7, PORT C: GPIO 6 / GPIO 5)
    Serial.println("[DIAG 1/3] Unit Buzzer 鳴動テスト開始...");
    const uint8_t testPins[] = { 8, 7, 6, 5 };
    for (int idx = 0; idx < 4; idx++) {
        uint8_t p = testPins[idx];
        int beeps = idx + 1;
        Serial.printf("  -> Testing GPIO %d (%d回 ピッ と鳴ります)...\n", p, beeps);
        for (int b = 0; b < beeps; b++) {
            buzzer.testPinTone(p, 4000, 110);
            delay(90);
        }
        delay(350);
    }
    // 通常のデュアルピン(G8+G7)に戻して入室音をテスト再生
    buzzer.begin(BUZZER_PIN, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_RES_BIT, true);
    Serial.println("  -> Playing ENTRY_OK sound on PORT B (GPIO 8 + GPIO 7)...");
    buzzer.play(SoundPattern::ENTRY_OK);
    unsigned long bStart = millis();
    while (millis() - bStart < 400) {
        buzzer.update();
        delay(5);
    }

    // 2. ST25R3916 (Unit NFC) I2C レジスタ診断
    Serial.println("[DIAG 2/3] ST25R3916 (Unit NFC) I2C レジスタ診断...");
    uint8_t icId = 0, opCtrl = 0, modeDef = 0;
    Wire.beginTransmission(0x50);
    uint8_t i2cErr = Wire.endTransmission();
    Serial.printf("  -> I2C 0x50 ping result: %d (0 = 正常応答)\n", i2cErr);

    // ST25R3916 Space A レジスタ直接読み取り (0x40 | reg)
    auto readSt25Reg = [](uint8_t reg) -> uint8_t {
        Wire.beginTransmission(0x50);
        Wire.write(0x40 | (reg & 0x3F));
        if (Wire.endTransmission(false) != 0) return 0xFF;
        if (Wire.requestFrom((uint8_t)0x50, (uint8_t)1) != 1) return 0xFF;
        return Wire.read();
    };
    icId    = readSt25Reg(0x3F); // REG_IC_IDENTITY
    opCtrl  = readSt25Reg(0x02); // REG_OPERATION_CONTROL
    modeDef = readSt25Reg(0x03); // REG_MODE_DEFINITION
    Serial.printf("  -> REG_IC_IDENTITY (0x3F)      : 0x%02X (type=%d, rev=%d) %s\n",
                  icId, (icId >> 3) & 0x1F, icId & 0x07,
                  (((icId >> 3) & 0x1F) == 5) ? "[OK: ST25R3916確認]" : "[WARN: チップID不一致]");
    Serial.printf("  -> REG_OPERATION_CONTROL (0x02): 0x%02X (EN=%d, RX_EN=%d, TX_EN=%d)\n",
                  opCtrl, (opCtrl >> 7) & 1, (opCtrl >> 6) & 1, (opCtrl >> 3) & 1);
    Serial.printf("  -> REG_MODE_DEFINITION (0x03)  : 0x%02X\n", modeDef);

    // 3. 8秒間のカードかざしライブテスト
    Serial.println("[DIAG 3/3] 8秒間カード読み取りテスト中... 今すぐリーダーにICカードをかざしてください！");
    unsigned long scanStart = millis();
    bool cardFound = false;
    while (millis() - scanStart < 8000) {
        Units.update();
        String idm = pollNfcCardIdm();
        if (idm.length() > 0) {
            Serial.println("  ✅ [DIAG CARD DETECTED!] IDm/UID = " + idm);
            buzzer.play(SoundPattern::ENTRY_OK);
            unsigned long tWait = millis();
            while (millis() - tWait < 350) { buzzer.update(); delay(5); }
            cardFound = true;
            break;
        }
        delay(20);
    }
    if (!cardFound) {
        Serial.println("  ℹ️ 8秒間以内にカードが検出されませんでした。");
    }
    Serial.println("==========================================");
    Serial.println(" [DIAG] ハードウェア診断完了");
    Serial.println("==========================================\n");
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
        } else if (line == "STUDYSPACE_DIAG" || line == "STUDYSPACE_TEST") {
            runHardwareDiagnostics();
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
// スマホ・PC向け Web設定＆リアルタイムテスト画面（http://192.168.4.1）
// ============================================================================
void handleApiStatus() {
    String wifiState = (WiFi.status() == WL_CONNECTED)
        ? ("接続中 (" + WiFi.SSID() + " / " + WiFi.localIP().toString() + ")")
        : "設定モード待機中 (192.168.4.1)";
    String json = "{";
    json += "\"device_id\":\"" + escapeJson(deviceId) + "\",";
    json += "\"terminal_name\":\"" + escapeJson(terminalName) + "\",";
    json += "\"nfc_status\":\"" + escapeJson(nfcPortStatus) + "\",";
    json += "\"wifi_status\":\"" + escapeJson(wifiState) + "\",";
    json += "\"last_idm\":\"" + escapeJson(uiLastIdm) + "\",";
    json += "\"last_result\":\"" + escapeJson(uiLastResult) + "\",";
    json += "\"touch_seq\":" + String(uiTouchSeq);
    json += "}";
    webServer.send(200, "application/json; charset=utf-8", json);
}

void handleApiTestBuzzer() {
    buzzer.begin(BUZZER_PIN, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_RES_BIT, true);
    buzzer.play(SoundPattern::ENTRY_OK);
    showTimedLed(0, 60, 0, 800);
    uiLastResult = "🔊 ブザーテスト音（ピロリ♪）を再生しました";
    uiTouchSeq++;
    webServer.send(200, "application/json; charset=utf-8", "{\"status\":\"ok\"}");
}

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
<title>StudySpace リーダー設定＆テスト</title>
<style>
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 16px; }
  .card { max-width: 460px; margin: 0 auto 16px; background: #1e293b; border-radius: 14px; padding: 20px; box-shadow: 0 10px 25px rgba(0,0,0,0.4); border: 1px solid rgba(56,189,248,0.25); }
  h1 { font-size: 1.2rem; margin: 0 0 6px; color: #38bdf8; }
  h2 { font-size: 1.05rem; margin: 0 0 10px; color: #34d399; }
  .sub { font-size: 0.8rem; color: #94a3b8; margin-bottom: 14px; line-height: 1.5; }
  label { display: block; font-size: 0.84rem; font-weight: 600; color: #cbd5e1; margin: 14px 0 6px; }
  input, select { width: 100%; box-sizing: border-box; padding: 11px 12px; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: #fff; font-size: 0.95rem; }
  .hint { font-size: 0.75rem; color: #64748b; margin-top: 4px; }
  button { width: 100%; margin-top: 18px; padding: 13px; border: none; border-radius: 10px; background: linear-gradient(135deg, #10b981, #059669); color: #fff; font-size: 0.98rem; font-weight: 700; cursor: pointer; }
  .btn-test { margin-top: 10px; padding: 10px; background: linear-gradient(135deg, #0284c7, #2563eb); font-size: 0.9rem; }
  .badge { display: inline-block; background: rgba(56,189,248,0.15); color: #38bdf8; padding: 4px 10px; border-radius: 6px; font-family: monospace; font-size: 0.8rem; margin-bottom: 10px; }
  .mon-row { display: flex; justify-content: space-between; font-size: 0.82rem; padding: 6px 0; border-bottom: 1px solid rgba(255,255,255,0.06); }
  .mon-box { margin-top: 10px; padding: 12px; border-radius: 10px; background: #0f172a; border: 1px solid rgba(52,211,153,0.4); }
  .idm-val { font-family: monospace; font-size: 1.05rem; font-weight: 700; color: #38bdf8; margin: 4px 0; }
</style>
</head>
<body>
<div class="card" style="border-color: rgba(16,185,129,0.4);">
  <h2>📡 リアルタイム動作モニター ＆ テスト</h2>
  <div class="mon-row"><span>NFCリーダー (PORT A):</span><strong id="st_nfc" style="color:#34d399;">)rawliteral" + escapeHtml(nfcPortStatus) + R"rawliteral(</strong></div>
  <div class="mon-row"><span>ネットワーク状態:</span><strong id="st_wifi" style="color:#cbd5e1;">確認中...</strong></div>
  <div class="mon-box" id="touch_box">
    <div style="font-size:0.75rem;color:#94a3b8;">直近に検知したICカード (リアルタイム更新):</div>
    <div class="idm-val" id="st_idm">--- (カードをタッチしてください) ---</div>
    <div style="font-size:0.82rem;color:#34d399;" id="st_res">)rawliteral" + escapeHtml(uiLastResult) + R"rawliteral(</div>
  </div>
  <button type="button" class="btn-test" onclick="testBuzzer()">🔊 スマホからブザーテスト音を鳴らす</button>
</div>

<div class="card">
  <div class="badge">ID: )rawliteral" + escapeHtml(deviceId) + R"rawliteral(</div>
  <h1>StudySpace カードリーダー設定</h1>
  <p class="sub">設置する拠点名（校舎・教室名）と2.4GHz帯Wi-Fiを設定します。</p>

  <form method="POST" action="/save">
    <label>① 拠点・設置場所名（所属名）</label>
    <input type="text" name="term_name" value=")rawliteral" + escapeHtml(terminalName) + R"rawliteral(" placeholder="例: 那覇校 1F自習室 / StudySpace長田校" required>

    <label>② Wi-Fi SSID（検出された2.4GHz電波から選択 または 手入力）</label>
    <select onchange="if(this.value) document.getElementById('ssid_input').value=this.value;">
      <option value="">▼ 検出された2.4GHz Wi-Fiから選ぶ...</option>
      )rawliteral" + ssidOptions + R"rawliteral(
    </select>
    <input type="text" id="ssid_input" name="ssid" value=")rawliteral" + escapeHtml(savedSsid) + R"rawliteral(" placeholder="Wi-FiのSSIDを入力（2.4GHz帯）" style="margin-top:8px;">
    <div class="hint">※ATOMS3は2.4GHz帯（-g 等）のみ対応です。上のプルダウンに出ているSSIDを選ぶと確実です。</div>

    <label>③ Wi-Fi パスワード</label>
    <input type="password" name="pass" value=")rawliteral" + escapeHtml(savedPass) + R"rawliteral(" placeholder="Wi-Fiパスワードを入力">

    <label>④ 接続先サーバーURL</label>
    <input type="url" name="server_url" value=")rawliteral" + escapeHtml(serverBaseUrl) + R"rawliteral(" placeholder="https://entry.studyspace.okinawa" required>

    <label>⑤ 音声通知（Unit Buzzer の有無）</label>
    <select name="buzzer_on">
      <option value="1" )rawliteral" + String(buzzerEnabled ? "selected" : "") + R"rawliteral(>有効（Unit Buzzer で音を鳴らす）</option>
      <option value="0" )rawliteral" + String(!buzzerEnabled ? "selected" : "") + R"rawliteral(>無効（ブザー未装着・LEDのみで稼働）</option>
    </select>

    <button type="submit">設定を保存して再起動</button>
  </form>
</div>
<script>
let lastSeq = -1;
async function pollStatus() {
  try {
    const r = await fetch('/status', {cache:'no-store'});
    if (!r.ok) return;
    const d = await r.json();
    document.getElementById('st_nfc').textContent = d.nfc_status || 'OK';
    document.getElementById('st_wifi').textContent = d.wifi_status || '';
    if (d.last_idm) {
      document.getElementById('st_idm').textContent = 'IDm: ' + d.last_idm;
    }
    document.getElementById('st_res').textContent = d.last_result || '';
    if (lastSeq !== -1 && d.touch_seq !== lastSeq) {
      const box = document.getElementById('touch_box');
      box.style.background = 'rgba(16,185,129,0.25)';
      setTimeout(() => { box.style.background = '#0f172a'; }, 700);
    }
    lastSeq = d.touch_seq;
  } catch (e) {}
}
async function testBuzzer() {
  try { await fetch('/test-buzzer', {method:'POST'}); pollStatus(); } catch (e) {}
}
setInterval(pollStatus, 800);
pollStatus();
</script>
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
    webServer.on("/status", HTTP_GET, handleApiStatus);
    webServer.on("/test-buzzer", HTTP_POST, handleApiTestBuzzer);
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
    auto cfg = nfcUnit.config();
    cfg.mode = m5::nfc::NFC::F;
    cfg.vdd_voltage_5V = true; // Grove PORT A は 5V 給電
    nfcUnit.config(cfg);

    // 候補ピンペアを順番にスキャン:
    // 1. Atomic Port ABC Base PORT A 標準 (SDA=G38, SCL=G39)
    // 2. Atomic Port ABC Base PORT A 反転 (SDA=G39, SCL=G38)
    // 3. ATOMS3 Lite 本体側面 Grove (SDA=G2, SCL=G1)
    const int sdaCandidates[] = { PORTA_BASE_SDA_PIN, PORTA_BASE_SCL_PIN, PORTA_SIDE_SDA_PIN };
    const int sclCandidates[] = { PORTA_BASE_SCL_PIN, PORTA_BASE_SDA_PIN, PORTA_SIDE_SCL_PIN };
    const char* labels[] = {
        "Atomic Port ABC Base PORT A (SDA=G38, SCL=G39)",
        "Atomic Port ABC Base PORT A Swapped (SDA=G39, SCL=G38)",
        "ATOMS3 Lite Side Grove Port (SDA=G2, SCL=G1)"
    };

    for (int i = 0; i < 3; i++) {
        Serial.printf("[NFC] Trying %s...\n", labels[i]);
        Wire.end();
        Wire.begin(sdaCandidates[i], sclCandidates[i], 400000U);
        Wire.beginTransmission(0x50);
        if (Wire.endTransmission() == 0) {
            if (Units.add(nfcUnit, Wire) && Units.begin()) {
                Serial.printf("[OK] ST25R3916 Unit NFC connected on %s.\n", labels[i]);
                nfcPortStatus = String("✅ 正常接続 (") + labels[i] + ")";
                return true;
            }
        }
    }

    nfcPortStatus = "❌ 未検出 (PORT A配線を確認してください)";
    return false;
}

// ============================================================================
// Wi-Fi接続（保存済みSSIDを最優先で直接接続し、未設定時のみWiFiMultiを使用）
// ============================================================================
bool connectToMultiWifi(unsigned long timeoutMs = 15000) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    // AtomS3 Lite (StampS3) 内蔵3Dアンテナの反射による接続失敗を防ぐ安定化設定
    WiFi.setTxPower(WIFI_POWER_15dBm);

    // 1. 保存されたSSIDがある場合は WiFiMulti を使わず直接 WiFi.begin() で確実に接続待機
    if (savedSsid.length() > 0) {
        Serial.printf("[WIFI] Connecting directly to saved SSID: %s ", savedSsid.c_str());
        WiFi.begin(savedSsid.c_str(), savedPass.c_str());

        unsigned long start = millis();
        while (millis() - start < timeoutMs) {
            checkSerialConfigCommand();
            if (WiFi.status() == WL_CONNECTED) {
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
        Serial.printf("\n[WIFI] Direct connection to '%s' timed out (status=%d).\n", savedSsid.c_str(), (int)WiFi.status());
    }

    // 2. 事前登録リスト（config.h）がある場合のフォールバック
    bool hasDefaultAp = false;
    for (int i = 0; i < DEFAULT_WIFI_COUNT; i++) {
        if (strlen(DEFAULT_WIFI_LIST[i].ssid) > 0 && String(DEFAULT_WIFI_LIST[i].ssid) != "YOUR_DEV_WIFI") {
            wifiMulti.addAP(DEFAULT_WIFI_LIST[i].ssid, DEFAULT_WIFI_LIST[i].password);
            hasDefaultAp = true;
        }
    }

    if (!hasDefaultAp || savedSsid.length() > 0) {
        return false;
    }

    Serial.print("[WIFI] Scanning & connecting to default Wi-Fi list ");
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        checkSerialConfigCommand();
        if (wifiMulti.run(8000) == WL_CONNECTED) {
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
            buzzer.play(SoundPattern::EXIT_OK);
            showTimedLed(0, 45, 55, 1200);
            uiLastResult = "✅ 退室打刻成功 (サーバー記録完了)";
        } else {
            // 入室成功：上昇する2音 ＋ 緑色非ブロッキング点灯
            buzzer.play(SoundPattern::ENTRY_OK);
            showTimedLed(0, 60, 0, 1200);
            uiLastResult = "✅ 入室打刻成功 (サーバー記録完了)";
        }
    } else if (httpCode == 404) {
        buzzer.play(SoundPattern::WARNING_ERROR);
        showTimedLed(60, 0, 0, 1000);
        uiLastResult = "⚠️ 未登録のカードです (管理画面の新規会員登録で自動入力されます)";
    } else {
        // 停止中端末(403) / API通信失敗：低い警告音を2回 ＋ 赤色非ブロッキング点灯
        buzzer.play(SoundPattern::WARNING_ERROR);
        showTimedLed(60, 0, 0, 1000);
        uiLastResult = "⚠️ サーバー通信エラー (HTTP " + String(httpCode) + ")";
    }
    uiTouchSeq++;
}

// ============================================================================
// NFCカード読み取り処理（FeliCa RFキャリア常時ON ＋ 1往復高速SENSF_REQポーリング）
// ============================================================================
String pollNfcCardIdm() {
    m5::nfc::f::PICC piccF{};

    // 1. ワイルドカード (0xFFFF: 全FeliCaカード共通) で1往復ポーリング (SENSF_REQ)
    if (nfcF.polling(piccF, 0xFFFF, m5::nfc::f::RequestCode::None, m5::nfc::f::TimeSlot::Slot4)) {
        std::string idmStr = piccF.idmAsString();
        if (!idmStr.empty()) {
            return String(idmStr.c_str());
        }
    }

    // 2. 交通系IC (0x0003: Suica/PASMO等) / 共通領域 (0xFE00: Edy/学生証等) でもフォールバックポーリング
    if (nfcF.polling(piccF, 0x0003, m5::nfc::f::RequestCode::None, m5::nfc::f::TimeSlot::Slot4)) {
        std::string idmStr = piccF.idmAsString();
        if (!idmStr.empty()) {
            return String(idmStr.c_str());
        }
    }

    if (nfcF.polling(piccF, 0xFE00, m5::nfc::f::RequestCode::None, m5::nfc::f::TimeSlot::Slot4)) {
        std::string idmStr = piccF.idmAsString();
        if (!idmStr.empty()) {
            return String(idmStr.c_str());
        }
    }

    return "";
}

// ============================================================================
// 初期化処理 (setup)
// ============================================================================
void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.external_imu = false;
    cfg.internal_rtc = false;
    cfg.external_rtc = false;
    M5.begin(cfg);
    M5.In_I2C.release();
    M5.Ex_I2C.release();
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

    // 起動直後の2秒間、PCインストーラー(setup.exe)からのUSBシリアル初期設定コマンドを優先待機
    unsigned long serialWaitStart = millis();
    while (millis() - serialWaitStart < 2000) {
        checkSerialConfigCommand();
        delay(10);
    }

    // 起動時に本体ボタン(G41)が押されていたら即座にスマホ設定モードへ移行
    if (digitalRead(BTN_PIN) == LOW) {
        startSetupPortal();
        return;
    }

    // 2. PORT A: ST25R3916 (Unit NFC) の初期化
    if (!initNfcUnit()) {
        Serial.println("[ERROR] ST25R3916 Unit NFC (U216) が見つかりません。PORT A のGroveケーブルを確認してください。");
        while (true) {
            checkSerialConfigCommand();
            if (digitalRead(BTN_PIN) == LOW) {
                startSetupPortal();
                return;
            }
            setLedColor(50, 0, 0);
            for (int i = 0; i < 30; i++) { checkSerialConfigCommand(); delay(10); }
            setLedColor(50, 20, 0);
            for (int i = 0; i < 30; i++) { checkSerialConfigCommand(); delay(10); }
        }
    }

    // 3. Wi-Fi 接続（繋がらなければ自動でスマホWeb設定モードを起動）
    if (!connectToMultiWifi(15000)) {
        Serial.println("[WIFI] 利用可能なWi-Fiが見つからないため、スマホWeb設定モードを起動します。");
        startSetupPortal();
        return;
    }

    setIdleLed();

    // 通常Wi-Fi接続中も同一LAN内のスマホ・PCから http://<IPアドレス> でリアルタイムモニターを開けるようWebサーバーを起動
    webServer.on("/", HTTP_GET, handlePortalRoot);
    webServer.on("/status", HTTP_GET, handleApiStatus);
    webServer.on("/test-buzzer", HTTP_POST, handleApiTestBuzzer);
    webServer.on("/save", HTTP_POST, handlePortalSave);
    webServer.begin();

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

    // スマホWeb設定モード中・通常Wi-Fi稼働中ともにWebモニターリクエストを処理
    if (isSetupPortalMode) {
        dnsServer.processNextRequest();
    }
    webServer.handleClient();

    Units.update();

    // 本体ボタン(G41)の短押し（ブザー鳴動＆Ping送信） / 2秒長押し（スマホ設定モード起動）判定
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
            Serial.println("[BUTTON] Short press -> Testing buzzer & sending manual ping...");
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
            uiLastIdm = detectedIdm;
            uiTouchSeq++;
            if (!isSetupPortalMode && WiFi.status() == WL_CONNECTED) {
                uiLastResult = "⏳ サーバーへ打刻通信中...";
                sendTouchToServer(detectedIdm);
            } else {
                // オフライン・設定モード中もテスト用に成功音と緑LEDで反応させる
                uiLastResult = "✅ カード読取テスト成功 (オフライン/設定モード中)";
                buzzer.play(SoundPattern::ENTRY_OK);
                showTimedLed(0, 60, 0, 1000);
            }
        }
    }

    delay(15);
}
