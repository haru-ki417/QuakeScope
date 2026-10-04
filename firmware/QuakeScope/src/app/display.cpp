#include "display.h"

namespace app {

bool Display::begin(TwoWire &wire, uint8_t height) {
    h_ = height == 32 ? 32 : 64;
    oled_ = new Adafruit_SSD1306(128, h_, &wire, -1);
    ok_ = oled_->begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);  // Wire.begin() は済ませてある
    if (ok_) {
        oled_->setTextWrap(false);
        oled_->clearDisplay();
        oled_->display();
    }
    return ok_;
}

void Display::showBoot(const char *version) {
    if (!ok_) return;
    oled_->clearDisplay();
    oled_->setTextColor(SSD1306_WHITE);
    oled_->setTextSize(2);
    oled_->setCursor(4, h_ == 64 ? 16 : 2);
    oled_->print("QuakeScope");
    oled_->setTextSize(1);
    oled_->setCursor(4, h_ == 64 ? 40 : 22);
    oled_->print("v");
    oled_->print(version);
    oled_->print("  CALIBRATING");
    oled_->display();
}

void Display::showSetup(const String &apName, const String &apPass) {
    if (!ok_) return;
    oled_->clearDisplay();
    oled_->setTextColor(SSD1306_WHITE);
    oled_->setTextSize(1);
    if (h_ == 64) {
        oled_->setCursor(0, 0);  oled_->print("SETUP  connect to:");
        oled_->setCursor(0, 14); oled_->print(apName);
        oled_->setCursor(0, 26); oled_->print("PASS "); oled_->print(apPass);
        oled_->setCursor(0, 42); oled_->print("then open");
        oled_->setCursor(0, 54); oled_->print("http://192.168.4.1");
    } else {
        oled_->setCursor(0, 0);  oled_->print(apName);
        oled_->setCursor(0, 11); oled_->print("PASS "); oled_->print(apPass);
        oled_->setCursor(0, 22); oled_->print("http://192.168.4.1");
    }
    oled_->display();
}

void Display::showInfo(const String &l1, const String &l2, const String &l3) {
    if (!ok_) return;
    oled_->clearDisplay();
    oled_->setTextColor(SSD1306_WHITE);
    oled_->setTextSize(1);
    oled_->setCursor(0, 0);
    oled_->print(l1);
    oled_->setCursor(0, h_ == 64 ? 20 : 11);
    oled_->print(l2);
    oled_->setCursor(0, h_ == 64 ? 40 : 22);
    oled_->print(l3);
    oled_->display();
}

void Display::graph(const uint8_t *g, int y, int h) {
    // 右が新しい。高さは対数目盛（0.1〜300 gal）
    for (int x = 0; x < 128; ++x) {
        const int v = (g[x] * (h - 1) + 127) / 255;
        if (v > 0) oled_->drawFastVLine(x, y + h - v, v, SSD1306_WHITE);
    }
}

void Display::show(const qs::ScreenModel &m) {
    if (!ok_) return;
    oled_->clearDisplay();
    oled_->setTextColor(SSD1306_WHITE);
    oled_->setTextSize(1);
    const bool shaking = m.shindo[0] != 0;
    if (h_ == 64) {
        oled_->setCursor(0, 0);
        oled_->print(m.clock);
        oled_->setCursor(98, 0);
        oled_->print(m.muted ? "M " : "  ");
        oled_->print(m.wifi ? "W" : "-");
        // 大きく: 揺れていれば震度、そうでなければ状態
        if (m.alert) oled_->fillRect(0, 10, 128, 20, SSD1306_WHITE);
        oled_->setTextColor(m.alert ? SSD1306_BLACK : SSD1306_WHITE);
        oled_->setTextSize(2);
        oled_->setCursor(2, 13);
        if (shaking) { oled_->print("SHINDO "); oled_->print(m.shindo); }
        else oled_->print(m.status);
        oled_->setTextColor(SSD1306_WHITE);
        oled_->setTextSize(1);
        oled_->setCursor(0, 33);
        oled_->print(m.nowText);
        if (shaking) { oled_->setCursor(74, 33); oled_->print(m.maxText); }
        graph(m.graph, 43, 21);
    } else {
        oled_->setCursor(0, 0);
        oled_->print(m.clock);
        oled_->setCursor(56, 0);
        oled_->print(m.status);
        if (m.alert) oled_->fillRect(0, 9, 128, 16, SSD1306_WHITE);
        oled_->setTextColor(m.alert ? SSD1306_BLACK : SSD1306_WHITE);
        oled_->setTextSize(2);
        oled_->setCursor(2, 9);
        if (shaking) { oled_->print("SHINDO "); oled_->print(m.shindo); }
        else oled_->print(m.nowText);
        oled_->setTextColor(SSD1306_WHITE);
        oled_->setTextSize(1);
        graph(m.graph, 26, 6);
    }
    oled_->display();
}

}  // namespace app
