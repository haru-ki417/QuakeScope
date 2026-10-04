// 小さな画面（SSD1306 の OLED、128×64 または 128×32）。
#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "../qs/screen.h"

namespace app {

class Display {
public:
    bool begin(TwoWire &wire, uint8_t height);
    bool ok() const { return ok_; }
    void showBoot(const char *version);
    void showSetup(const String &apName, const String &apPass);
    void showInfo(const String &line1, const String &line2, const String &line3);
    void show(const qs::ScreenModel &m);

private:
    void graph(const uint8_t *g, int y, int h);
    Adafruit_SSD1306 *oled_ = nullptr;
    uint8_t h_ = 64;
    bool ok_ = false;
};

}  // namespace app
