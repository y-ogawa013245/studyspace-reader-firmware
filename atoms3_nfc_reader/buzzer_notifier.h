#pragma once

/**
 * StudySpace 音声通知モジュール (BuzzerNotifier)
 *
 * 対象ハードウェア:
 *  - M5Stack Unit Buzzer (SKU: U132 / パッシブブザー・推奨駆動周波数 4000Hz)
 *  - 接続先: M5Stack Atomic Port ABC Base (A130) の PORT B (GPIO 8)
 *
 * 設計方針・安全対策:
 *  1. 【PWM駆動の徹底・直流HIGH固定の防止】
 *     Unit Buzzer (U132) はパッシブブザー＋トランジスタ駆動回路のため、直流HIGH固定を禁止し、
 *     LEDC PWM による矩形波（デューティ比50%）のみで駆動します。鳴動終了時は即座にデューティ比 0 (LOW) に戻します。
 *  2. 【Arduino-ESP32 v2.x / v3.x 両対応 LEDC API】
 *     ESP_ARDUINO_VERSION マクロでバージョンを自動判定し、v3.x のピン指定型APIと v2.x のチャンネル指定型APIの両方に対応します。
 *  3. 【非ブロッキング制御】
 *     delay() を一切使用せず、millis() ベースのステートマシンにより NFC 読み取りループを妨げずに複数音パターンを再生します。
 */

#include <Arduino.h>
#include "config.h"

// 音声通知パターン種別（※ stdlib.h の EXIT_SUCCESS マクロとの衝突を避けるため _OK 表記）
enum class SoundPattern {
    NONE = 0,
    CARD_READ,      // カード読み取り：短い単音
    ENTRY_OK,       // 入室成功：上昇する2音
    EXIT_OK,        // 退室成功：下降する2音
    WARNING_ERROR   // 未登録カード・API通信失敗：低い警告音を2回
};

// 1音分のステップ定義（freqHz == 0 の場合は休符/無音区間）
struct ToneStep {
    uint16_t freqHz;
    uint16_t durationMs;
};

// Unit Buzzer (U132) の推奨共振周波数 4000Hz 前後を基準にした各通知音シーケンス定義
// 1. カード読み取り：短い単音 (4000Hz, 60ms)
static constexpr ToneStep BUZZER_SEQ_CARD_READ[] = {
    { 4000, 60 }
};

// 2. 入室成功：上昇する2音 (3200Hz -> 4000Hz)
static constexpr ToneStep BUZZER_SEQ_ENTRY_SUCCESS[] = {
    { 3200, 90 },
    {    0, 30 },
    { 4000, 150 }
};

// 3. 退室成功：下降する2音 (4000Hz -> 3000Hz)
static constexpr ToneStep BUZZER_SEQ_EXIT_SUCCESS[] = {
    { 4000, 90 },
    {    0, 30 },
    { 3000, 150 }
};

// 4. 未登録カード・API通信失敗：低い警告音を2回 (1000Hz x 2)
static constexpr ToneStep BUZZER_SEQ_WARNING_ERROR[] = {
    { 1000, 140 },
    {    0, 80  },
    { 1000, 180 }
};

class BuzzerNotifier {
public:
    void begin(uint8_t pin = BUZZER_PIN, uint8_t channel = BUZZER_LEDC_CHANNEL, uint8_t resolutionBits = BUZZER_LEDC_RES_BIT, bool enabled = DEFAULT_BUZZER_ENABLED) {
        _pin = pin;
        _channel = channel;
        _resolutionBits = resolutionBits;
        _enabled = enabled;

        // 初期状態で直流HIGHにならないよう確実にLOW出力で初期化
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, LOW);

        if (_enabled) {
            attachPwm();
        }

        _isPlaying = false;
        _currentSequence = nullptr;
        _sequenceLength = 0;
        _stepIndex = 0;
    }

    /**
     * ブザー有無・消音モードの切り替え（false時はPWM出力を完全に行わない）
     */
    void setEnabled(bool enabled) {
        if (!enabled && _enabled) {
            stop();
        }
        _enabled = enabled;
        if (_enabled && !_pwmAttached) {
            attachPwm();
        }
    }

    bool isEnabled() const {
        return _enabled;
    }

    /**
     * 指定した音声通知パターンの非ブロッキング再生を開始する
     * ※ブザー無効（未装着・消音モード）時は何もしない
     */
    void play(SoundPattern pattern) {
        if (!_enabled) {
            return;
        }
        switch (pattern) {
            case SoundPattern::CARD_READ:
                startSequence(BUZZER_SEQ_CARD_READ, sizeof(BUZZER_SEQ_CARD_READ) / sizeof(ToneStep));
                break;
            case SoundPattern::ENTRY_OK:
                startSequence(BUZZER_SEQ_ENTRY_SUCCESS, sizeof(BUZZER_SEQ_ENTRY_SUCCESS) / sizeof(ToneStep));
                break;
            case SoundPattern::EXIT_OK:
                startSequence(BUZZER_SEQ_EXIT_SUCCESS, sizeof(BUZZER_SEQ_EXIT_SUCCESS) / sizeof(ToneStep));
                break;
            case SoundPattern::WARNING_ERROR:
                startSequence(BUZZER_SEQ_WARNING_ERROR, sizeof(BUZZER_SEQ_WARNING_ERROR) / sizeof(ToneStep));
                break;
            default:
                stop();
                break;
        }
    }

    /**
     * メインループ (loop) から毎回呼び出す非ブロッキング更新処理
     */
    void update() {
        if (!_isPlaying || _currentSequence == nullptr) {
            return;
        }

        unsigned long now = millis();
        if (now - _stepStartTime >= _currentSequence[_stepIndex].durationMs) {
            _stepIndex++;
            if (_stepIndex >= _sequenceLength) {
                stop();
                return;
            }
            _stepStartTime = now;
            applyTone(_currentSequence[_stepIndex].freqHz);
        }
    }

    /**
     * 即座に鳴動を停止し、出力をLOW(デューティ0)に落とす（直流HIGH固定の完全防止）
     */
    void stop() {
        applyTone(0);
        _isPlaying = false;
        _currentSequence = nullptr;
        _sequenceLength = 0;
        _stepIndex = 0;
    }

    bool isPlaying() const {
        return _isPlaying;
    }

private:
    uint8_t _pin = BUZZER_PIN;
    uint8_t _channel = BUZZER_LEDC_CHANNEL;
    uint8_t _resolutionBits = BUZZER_LEDC_RES_BIT;
    bool _enabled = DEFAULT_BUZZER_ENABLED;
    bool _pwmAttached = false;

    bool _isPlaying = false;
    const ToneStep* _currentSequence = nullptr;
    size_t _sequenceLength = 0;
    size_t _stepIndex = 0;
    unsigned long _stepStartTime = 0;

    void attachPwm() {
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
        ledcAttach(_pin, 4000, _resolutionBits);
        ledcWrite(_pin, 0);
#else
        ledcSetup(_channel, 4000, _resolutionBits);
        ledcAttachPin(_pin, _channel);
        ledcWrite(_channel, 0);
#endif
        _pwmAttached = true;
    }

    void startSequence(const ToneStep* seq, size_t len) {
        if (seq == nullptr || len == 0) {
            stop();
            return;
        }
        _currentSequence = seq;
        _sequenceLength = len;
        _stepIndex = 0;
        _stepStartTime = millis();
        _isPlaying = true;
        applyTone(_currentSequence[0].freqHz);
    }

    void applyTone(uint16_t freqHz) {
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
        if (freqHz == 0) {
            ledcWriteTone(_pin, 0);
            ledcWrite(_pin, 0);
        } else {
            // ledcWriteTone は内部で自動的にデューティ比 50% の矩形波を出力する
            ledcWriteTone(_pin, freqHz);
        }
#else
        if (freqHz == 0) {
            ledcWriteTone(_channel, 0);
            ledcWrite(_channel, 0);
        } else {
            ledcWriteTone(_channel, freqHz);
        }
#endif
    }
};

