#include "web.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPUpdateServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../qs/notify.h"
#include "context.h"
#include "web_page.h"

namespace app {

namespace {

WebServer server(80);
DNSServer dns;
HTTPUpdateServer updater;
String updaterPass;

String js(const String &s) { return String("\"") + qs::jsonEscape(s.c_str()).c_str() + "\""; }
String num(double v, int digits = 1) { return String(v, digits); }

// 設定を変える API は、設定用のパスワードを確かめる（Basic 認証。ブラウザーの入力欄は出さない）
bool authed() {
    if (ctx.setupMode) return true;  // 初期設定のときは、本体の Wi-Fi（パスワードつき）につながっている人だけ
    if (ctx.settings.adminPass.length() >= 8 && server.authenticate("admin", ctx.settings.adminPass.c_str())) return true;
    server.send(401, "application/json", "{\"error\":\"auth\"}");
    return false;
}

void sendPage() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(kWebPage), kWebPageLen);
}

void handleStatus() {
    qs::EngineStatus st;
    float hist[qs::kHistoryLen];
    xSemaphoreTake(ctx.engineLock, portMAX_DELAY);
    st = ctx.engine.status();
    ctx.engine.history(hist);
    xSemaphoreGive(ctx.engineLock);
    const qs::WallTime w = wallNow();
    String o;
    o.reserve(1600);
    o += "{\"setup\":"; o += ctx.setupMode ? "true" : "false";
    o += ",\"name\":" + js(deviceName());
    o += ",\"host\":" + js(deviceName() + ".local");
    o += ",\"version\":" + js(kVersion);
    o += ",\"place\":" + js(ctx.settings.place);
    o += ",\"time\":" + js(qs::formatTime(w, true).c_str());
    o += ",\"uptime\":" + String(millis() / 1000);
    o += ",\"ic\":" + num(ctx.settings.cautionIntensity) + ",\"iw\":" + num(ctx.settings.warningIntensity);
    o += ",\"wifi\":{\"ssid\":" + js(WiFi.SSID()) + ",\"rssi\":" + String(WiFi.RSSI()) + ",\"ip\":" + js(WiFi.localIP().toString()) + "}";
    o += ",\"pending\":" + String(ctx.notifier.pending());
    o += ",\"fsUsed\":" + String(ctx.storage.usedBytes()) + ",\"fsTotal\":" + String(ctx.storage.totalBytes());
    o += ",\"engine\":{\"calibrated\":"; o += st.calibrated ? "true" : "false";
    o += ",\"sensorOk\":"; o += (st.sensorOk && ctx.sensorAlive) ? "true" : "false";
    o += ",\"inEvent\":"; o += st.inEvent ? "true" : "false";
    o += ",\"level\":" + String(static_cast<int>(st.level));
    o += ",\"iNow\":" + num(st.intensityNow < 0 ? 0 : st.intensityNow, 2);
    o += ",\"iEvent\":" + num(st.intensityEvent < 0 ? 0 : st.intensityEvent, 2);
    o += ",\"pga\":" + num(st.pgaEvent) + ",\"staLta\":" + num(st.staLta, 2) + ",\"noise\":" + num(st.noiseGal, 3) + ",\"noiseI\":" + num(st.noiseIntensity, 2) + "}";
    o += ",\"hist\":[";
    for (int i = 0; i < qs::kHistoryLen; ++i) { if (i) o += ","; o += String(static_cast<int>(hist[i] * 10 + 0.5f)); }
    o += "]}";
    server.send(200, "application/json", o);
}

void handleScan() {
    const int n = WiFi.scanNetworks();
    String o = "[";
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i).isEmpty()) continue;
        if (k++) o += ",";
        o += "{\"ssid\":" + js(WiFi.SSID(i)) + ",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    WiFi.scanDelete();
    server.send(200, "application/json", o + "]");
}

void handleSetup() {
    if (!ctx.setupMode) { server.send(403, "application/json", "{}"); return; }
    Settings &s = ctx.settings;
    const String ssid = server.arg("ssid"), admin = server.arg("admin");
    if (ssid.isEmpty() || admin.length() < 8) { server.send(400, "application/json", "{\"error\":\"input\"}"); return; }
    s.wifiSsid = ssid;
    s.wifiPass = server.arg("wpass");
    s.adminPass = admin;
    if (server.hasArg("place") && server.arg("place").length()) s.place = server.arg("place").substring(0, 40);
    saveSettings(s);
    server.send(200, "application/json", "{\"ok\":true}");
    ctx.requestReboot = true;
}

void handleGetSettings() {
    if (!authed()) return;
    const Settings &s = ctx.settings;
    // パスワード・トークンは返さない（設定されているかどうかだけ）
    String o = "{";
    o += "\"place\":" + js(s.place) + ",\"oled\":" + String(s.oledHeight) + ",\"beep\":" + (s.beepOnDetect ? "true" : "false");
    o += ",\"volume\":" + String(s.volume) + ",\"ic\":" + js(String(s.cautionIntensity, 1)) + ",\"iw\":" + js(String(s.warningIntensity, 1));
    o += ",\"nmin\":" + js(String(s.notifyMinLevel)) + ",\"nend\":" + (s.sendEnd ? "true" : "false");
    o += ",\"lon\":" + String(s.lineOn ? "true" : "false") + ",\"luser\":" + js(s.lineUser) + ",\"ltokSet\":" + (s.lineToken.length() ? "true" : "false");
    o += ",\"ton\":" + String(s.tgOn ? "true" : "false") + ",\"tchat\":" + js(s.tgChat) + ",\"ttokSet\":" + (s.tgToken.length() ? "true" : "false");
    o += ",\"don\":" + String(s.dcOn ? "true" : "false") + ",\"dhookSet\":" + (s.dcWebhook.length() ? "true" : "false");
    o += ",\"mon\":" + String(s.mailOn ? "true" : "false") + ",\"mhost\":" + js(s.smtpHost) + ",\"mport\":" + js(String(s.smtpPort));
    o += ",\"muser\":" + js(s.smtpUser) + ",\"mto\":" + js(s.mailTo) + ",\"mpassSet\":" + (s.smtpPass.length() ? "true" : "false");
    o += ",\"ssid\":" + js(s.wifiSsid) + "}";
    server.send(200, "application/json", o);
}

void handlePostSettings() {
    if (!authed()) return;
    Settings &s = ctx.settings;
    bool reboot = false;
    auto str = [&](const char *k, String &dst, int maxLen) { if (server.hasArg(k)) dst = server.arg(k).substring(0, maxLen); };
    auto secret = [&](const char *k, String &dst) { if (server.hasArg(k) && server.arg(k).length()) dst = server.arg(k); };
    auto flag = [&](const char *k, bool &dst) { if (server.hasArg(k)) dst = server.arg(k) == "1"; };
    str("place", s.place, 40);
    if (s.place.isEmpty()) s.place = "自宅";
    if (server.hasArg("oled")) { const uint8_t h = server.arg("oled").toInt() == 32 ? 32 : 64; if (h != s.oledHeight) { s.oledHeight = h; reboot = true; } }
    flag("beep", s.beepOnDetect);
    if (server.hasArg("volume")) s.volume = constrain(server.arg("volume").toInt(), 0, 100);
    if (server.hasArg("ic")) s.cautionIntensity = constrain(server.arg("ic").toFloat(), 1.0f, 4.5f);
    if (server.hasArg("iw")) s.warningIntensity = constrain(server.arg("iw").toFloat(), s.cautionIntensity + 0.5f, 6.5f);
    if (server.hasArg("nmin")) s.notifyMinLevel = server.arg("nmin").toInt() == 3 ? 3 : 2;
    flag("nend", s.sendEnd);
    flag("lon", s.lineOn); secret("ltok", s.lineToken); str("luser", s.lineUser, 64);
    flag("ton", s.tgOn); secret("ttok", s.tgToken); str("tchat", s.tgChat, 32);
    flag("don", s.dcOn); secret("dhook", s.dcWebhook);
    flag("mon", s.mailOn); str("mhost", s.smtpHost, 64); str("muser", s.smtpUser, 96); secret("mpass", s.smtpPass); str("mto", s.mailTo, 200);
    if (server.hasArg("mport")) s.smtpPort = server.arg("mport").toInt() == 587 ? 587 : 465;
    if (server.hasArg("ssid") && server.arg("ssid").length() && server.arg("ssid") != s.wifiSsid) { s.wifiSsid = server.arg("ssid"); reboot = true; }
    if (server.hasArg("wpass") && server.arg("wpass").length()) { s.wifiPass = server.arg("wpass"); reboot = true; }
    if (server.hasArg("admin") && server.arg("admin").length()) {
        if (server.arg("admin").length() < 8) { server.send(400, "application/json", "{\"error\":\"admin\"}"); return; }
        s.adminPass = server.arg("admin");
        updaterPass = s.adminPass;
    }
    saveSettings(s);
    xSemaphoreTake(ctx.engineLock, portMAX_DELAY);
    ctx.engine.setLevels(s.cautionIntensity, s.warningIntensity);
    xSemaphoreGive(ctx.engineLock);
    ctx.alarm.setVolume(s.volume);
    server.send(200, "application/json", reboot ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":true}");
    if (reboot) ctx.requestReboot = true;
}

void handleLogs() {
    SendLog l[20];
    const int n = ctx.notifier.logs(l, 20);
    String o = "[";
    for (int i = 0; i < n; ++i) {
        if (i) o += ",";
        o += "{\"t\":" + String(l[i].epoch) + ",\"ch\":" + js(l[i].channel) + ",\"title\":" + js(l[i].title) + ",\"code\":" + String(l[i].code) +
             ",\"ok\":" + (l[i].ok ? "true" : "false") + "}";
    }
    server.send(200, "application/json", o + "]");
}

void handleEventBin() {
    const uint32_t id = server.arg("id").toInt();
    File f = LittleFS.open(ctx.storage.binPath(id));
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    server.streamFile(f, "application/octet-stream");
    f.close();
}

void post(const char *path, void (*fn)()) { server.on(path, HTTP_POST, fn); }

}  // namespace

void webBegin() {
    if (ctx.setupMode) {
        dns.start(53, "*", WiFi.softAPIP());
    } else {
        MDNS.begin(deviceName().c_str());
        MDNS.addService("http", "tcp", 80);
    }
    server.on("/", HTTP_GET, sendPage);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/setup", HTTP_POST, handleSetup);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/events", HTTP_GET, [] { server.send(200, "application/json", ctx.storage.listJson()); });
    server.on("/api/event", HTTP_GET, handleEventBin);
    server.on("/api/logs", HTTP_GET, handleLogs);
    post("/api/test/notify", [] { if (!authed()) return; ctx.requestTestNotify = true; server.send(200, "application/json", "{\"ok\":true}"); });
    post("/api/test/alarm", [] { if (!authed()) return; ctx.requestTestAlarm = true; server.send(200, "application/json", "{\"ok\":true}"); });
    post("/api/calibrate", [] { if (!authed()) return; ctx.requestCalibrate = true; server.send(200, "application/json", "{\"ok\":true}"); });
    post("/api/reboot", [] { if (!authed()) return; server.send(200, "application/json", "{\"ok\":true}"); ctx.requestReboot = true; });
    post("/api/events/clear", [] { if (!authed()) return; ctx.storage.clear(); server.send(200, "application/json", "{\"ok\":true}"); });
    post("/api/factory-reset", [] { if (!authed()) return; factoryReset(); server.send(200, "application/json", "{\"ok\":true}"); ctx.requestReboot = true; });
    // スマホが Wi-Fi につないだときの確認ページ（キャプティブポータル）にも、設定画面を出す
    server.onNotFound([] {
        if (ctx.setupMode) {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "not found");
        }
    });
    if (!ctx.setupMode) {
        updaterPass = ctx.settings.adminPass;
        updater.setup(&server, "/update", "admin", updaterPass.c_str());
    }
    server.begin();
}

void webLoop() {
    if (ctx.setupMode) dns.processNextRequest();
    server.handleClient();
}

}  // namespace app
