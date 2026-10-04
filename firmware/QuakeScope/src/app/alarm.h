// ブザーの音のパターンと、RGB LED の色。止めずに（ブロックせずに）動かす。
#pragma once

#include <Arduino.h>

namespace app {

enum class Sound : uint8_t { None, Boot, Click, Detect, Caution, Warning, Test };
enum class LedMode : uint8_t { Off, Normal, Setup, Detect, Caution, Warning, Fault, Busy };

class Alarm {
public:
    void begin(int buzzerPin, int r, int g, int b);
    void play(Sound s);          // 鳴らす（今より弱い音では上書きしない）
    void stop();                 // 止める（ボタン）
    bool playing() const { return sound_ != Sound::None; }
    Sound sound() const { return sound_; }
    void setVolume(uint8_t v) { volume_ = v > 100 ? 100 : v; }
    void setLed(LedMode m) { led_ = m; }
    void update();               // 10 ms ごとに呼ぶ

private:
    void tone(uint16_t hz);
    void rgb(uint8_t r, uint8_t g, uint8_t b);
    int buz_ = -1, r_ = -1, g_ = -1, b_ = -1;
    Sound sound_ = Sound::None;
    uint32_t started_ = 0, stepAt_ = 0, until_ = 0;
    int step_ = 0;
    uint8_t volume_ = 100;
    uint16_t lastHz_ = 0;
    LedMode led_ = LedMode::Off;
};

}  // namespace app
