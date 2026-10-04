#include "alarm.h"

namespace app {

namespace {

struct Step { uint16_t hz; uint16_t ms; };

// 音のパターン（周波数 0 は無音）。最後まで行ったら、鳴らす時間が残っていればくり返す
const Step kBoot[] = {{1500, 60}, {0, 40}, {2000, 80}};
const Step kClick[] = {{2500, 25}};
const Step kDetect[] = {{1200, 80}, {0, 80}, {1200, 80}};
const Step kCaution[] = {{2000, 200}, {0, 300}};
const Step kWarning[] = {{2400, 120}, {1800, 120}};

struct Pattern { const Step *steps; int n; uint32_t totalMs; int rank; };

Pattern pattern(Sound s) {
    switch (s) {
        case Sound::Boot: return {kBoot, 3, 180, 1};
        case Sound::Click: return {kClick, 1, 25, 1};
        case Sound::Detect: return {kDetect, 3, 240, 2};
        case Sound::Caution: return {kCaution, 2, 30000, 3};
        case Sound::Test: return {kCaution, 2, 3000, 3};
        case Sound::Warning: return {kWarning, 2, 60000, 4};
        default: return {nullptr, 0, 0, 0};
    }
}

}  // namespace

void Alarm::begin(int buzzerPin, int r, int g, int b) {
    buz_ = buzzerPin; r_ = r; g_ = g; b_ = b;
    ledcAttach(buz_, 2000, 10);
    ledcWrite(buz_, 0);
    ledcAttach(r_, 5000, 8);
    ledcAttach(g_, 5000, 8);
    ledcAttach(b_, 5000, 8);
    rgb(0, 0, 0);
}

void Alarm::tone(uint16_t hz) {
    if (hz == lastHz_) return;
    lastHz_ = hz;
    if (hz == 0 || volume_ == 0) { ledcWrite(buz_, 0); return; }
    ledcChangeFrequency(buz_, hz, 10);
    ledcWrite(buz_, 512u * volume_ / 100u);   // 50% が最大の音量
}

void Alarm::rgb(uint8_t r, uint8_t g, uint8_t b) {
    ledcWrite(r_, r);
    ledcWrite(g_, g);
    ledcWrite(b_, b);
}

void Alarm::play(Sound s) {
    if (pattern(s).rank < pattern(sound_).rank && playing()) return;  // 強い音を優先
    sound_ = s;
    started_ = stepAt_ = millis();
    until_ = started_ + pattern(s).totalMs;
    step_ = 0;
    lastHz_ = 0xFFFF;
    tone(pattern(s).steps ? pattern(s).steps[0].hz : 0);
}

void Alarm::stop() {
    sound_ = Sound::None;
    tone(0);
}

void Alarm::update() {
    const uint32_t now = millis();
    if (sound_ != Sound::None) {
        const Pattern p = pattern(sound_);
        if (static_cast<int32_t>(now - until_) >= 0) {
            stop();
        } else if (now - stepAt_ >= p.steps[step_].ms) {
            stepAt_ = now;
            step_ = (step_ + 1) % p.n;
            tone(p.steps[step_].hz);
        }
    }
    // LED
    const bool blinkSlow = (now / 500) % 2 == 0, blinkFast = (now / 150) % 2 == 0;
    switch (led_) {
        case LedMode::Off: rgb(0, 0, 0); break;
        case LedMode::Normal: {
            // ゆっくり明るさを変える（動いていることが分かるように）
            const int ph = (now / 8) % 512;
            const uint8_t v = 4 + (ph < 256 ? ph : 511 - ph) / 16;
            rgb(0, v, 0);
            break;
        }
        case LedMode::Setup: rgb(0, 0, blinkSlow ? 60 : 0); break;
        case LedMode::Busy: rgb(0, 30, 60); break;
        case LedMode::Detect: rgb(120, 90, 0); break;
        case LedMode::Caution: rgb(blinkSlow ? 200 : 40, blinkSlow ? 70 : 10, 0); break;
        case LedMode::Warning: rgb(blinkFast ? 255 : 0, 0, 0); break;
        case LedMode::Fault: rgb(blinkSlow ? 120 : 0, 0, blinkSlow ? 120 : 0); break;
    }
}

}  // namespace app
