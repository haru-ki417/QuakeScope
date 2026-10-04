// 設定（本体の中の不揮発メモリー「NVS」に保存する）と、ピンの割り当て。
#pragma once

#include <Arduino.h>

namespace app {

// ESP32-DevKitC（ESP32-WROOM-32E）のピン。起動時に使われるピン（0, 2, 5, 12, 15）は避けている
namespace pins {
constexpr int kSda = 21;      // I²C（加速度センサーと OLED で共有）
constexpr int kScl = 22;
constexpr int kBuzzer = 25;   // NPN トランジスター経由でブザーを鳴らす
constexpr int kLedR = 26;     // RGB LED（カソードコモン）
constexpr int kLedG = 27;
constexpr int kLedB = 14;
constexpr int kButton = 33;   // 押すと GND（内部プルアップ）
}  // namespace pins

struct Settings {
    // Wi-Fi
    String wifiSsid, wifiPass;
    // 本体
    String place = "自宅";          // 通知に入れる場所の名前
    String adminPass;              // 設定を変えるときのパスワード（8 文字以上）
    String apPass;                 // 初期設定のときの Wi-Fi のパスワード（最初の起動で作る）
    uint8_t oledHeight = 64;       // 64 または 32
    bool beepOnDetect = true;      // 揺れ始め（震度 1 相当〜）で短く鳴らす
    uint8_t volume = 100;          // 0〜100
    float cautionIntensity = 2.5;  // 注意（震度 3 相当）
    float warningIntensity = 4.5;  // 警報（震度 5 弱相当）
    // 通知
    uint8_t notifyMinLevel = 2;    // 2: 注意から、3: 警報だけ
    bool sendEnd = true;           // 収まったときにまとめを送る
    bool lineOn = false;   String lineToken, lineUser;
    bool tgOn = false;     String tgToken, tgChat;
    bool dcOn = false;     String dcWebhook;
    bool mailOn = false;   String smtpHost, smtpUser, smtpPass, mailTo; uint16_t smtpPort = 465;

    bool configured() const { return wifiSsid.length() > 0 && adminPass.length() >= 8; }
};

void loadSettings(Settings &s);
void saveSettings(const Settings &s);
void factoryReset();
// 機器ごとの名前（"QuakeScope-1A2B"）
String deviceName();

}  // namespace app
