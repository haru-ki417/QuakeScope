#include "settings.h"

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>

namespace app {

namespace {
constexpr const char *kNs = "quakescope";
}

String deviceName() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char b[24];
    snprintf(b, sizeof b, "QuakeScope-%02X%02X", mac[4], mac[5]);
    return b;
}

void loadSettings(Settings &s) {
    Preferences p;
    p.begin(kNs, true);
    s.wifiSsid = p.getString("ssid", "");
    s.wifiPass = p.getString("wpass", "");
    s.place = p.getString("place", "自宅");
    s.adminPass = p.getString("admin", "");
    s.apPass = p.getString("appass", "");
    s.oledHeight = p.getUChar("oled", 64);
    s.beepOnDetect = p.getBool("beep", true);
    s.volume = p.getUChar("vol", 100);
    s.cautionIntensity = p.getFloat("ic", 2.5f);
    s.warningIntensity = p.getFloat("iw", 4.5f);
    s.notifyMinLevel = p.getUChar("nmin", 2);
    s.sendEnd = p.getBool("nend", true);
    s.lineOn = p.getBool("lon", false);   s.lineToken = p.getString("ltok", ""); s.lineUser = p.getString("luser", "");
    s.tgOn = p.getBool("ton", false);     s.tgToken = p.getString("ttok", "");   s.tgChat = p.getString("tchat", "");
    s.dcOn = p.getBool("don", false);     s.dcWebhook = p.getString("dhook", "");
    s.mailOn = p.getBool("mon", false);   s.smtpHost = p.getString("mhost", "");  s.smtpPort = p.getUShort("mport", 465);
    s.smtpUser = p.getString("muser", ""); s.smtpPass = p.getString("mpass", ""); s.mailTo = p.getString("mto", "");
    p.end();
    if (s.apPass.length() < 8) {
        // 初期設定用の Wi-Fi のパスワード（8 桁の数字）。最初の起動で作り、画面に表示する
        char b[12];
        snprintf(b, sizeof b, "%08lu", static_cast<unsigned long>(esp_random() % 100000000UL));
        s.apPass = b;
        Preferences w;
        w.begin(kNs, false);
        w.putString("appass", s.apPass);
        w.end();
    }
    if (s.oledHeight != 32) s.oledHeight = 64;
    if (s.notifyMinLevel < 2 || s.notifyMinLevel > 3) s.notifyMinLevel = 2;
}

void saveSettings(const Settings &s) {
    Preferences p;
    p.begin(kNs, false);
    p.putString("ssid", s.wifiSsid);
    p.putString("wpass", s.wifiPass);
    p.putString("place", s.place);
    p.putString("admin", s.adminPass);
    p.putString("appass", s.apPass);
    p.putUChar("oled", s.oledHeight);
    p.putBool("beep", s.beepOnDetect);
    p.putUChar("vol", s.volume);
    p.putFloat("ic", s.cautionIntensity);
    p.putFloat("iw", s.warningIntensity);
    p.putUChar("nmin", s.notifyMinLevel);
    p.putBool("nend", s.sendEnd);
    p.putBool("lon", s.lineOn);   p.putString("ltok", s.lineToken); p.putString("luser", s.lineUser);
    p.putBool("ton", s.tgOn);     p.putString("ttok", s.tgToken);   p.putString("tchat", s.tgChat);
    p.putBool("don", s.dcOn);     p.putString("dhook", s.dcWebhook);
    p.putBool("mon", s.mailOn);   p.putString("mhost", s.smtpHost); p.putUShort("mport", s.smtpPort);
    p.putString("muser", s.smtpUser); p.putString("mpass", s.smtpPass); p.putString("mto", s.mailTo);
    p.end();
}

void factoryReset() {
    Preferences p;
    p.begin(kNs, false);
    p.clear();
    p.end();
}

}  // namespace app
