// QuakeScope — 揺れ検知アラーム（ESP32 + MPU6050）
//
// 置いた場所の揺れを 100 Hz で測り、気象庁の計測震度の方法に近い計算で「震度相当」を求める。
// 揺れ始め（P 波）で短い音、震度 3 相当で警報音とスマートフォンへの通知、震度 5 弱相当で強い警報音。
// 揺れは本体に記録し、家の Wi-Fi の中から Web 画面で見られる。
//
// ボード: ESP32 Dev Module（ESP32-DevKitC など）／ パーティション: 「Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)」
// ライブラリー: Adafruit SSD1306、Adafruit GFX Library（Arduino IDE のライブラリーマネージャーから）
// 配線と組み立て: docs/assembly.md
//
// ※ 本機の震度は推定値（参考値）です。気象庁の発表する震度ではありません。

#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "src/app/context.h"
#include "src/app/display.h"
#include "src/app/sensor.h"
#include "src/app/web.h"
#include "src/qs/notify.h"
#include "src/qs/screen.h"

using namespace app;

namespace app {
Context ctx;

qs::WallTime wallFromEpoch(time_t t) {
    qs::WallTime w;
    if (t < 1700000000) return w;   // 時刻が合っていない
    struct tm tmv;
    localtime_r(&t, &tmv);
    w.year = tmv.tm_year + 1900; w.month = tmv.tm_mon + 1; w.day = tmv.tm_mday;
    w.hour = tmv.tm_hour; w.minute = tmv.tm_min; w.second = tmv.tm_sec;
    return w;
}
qs::WallTime wallNow() { return wallFromEpoch(time(nullptr)); }
}  // namespace app

namespace {

Mpu6050 mpu;
Display display;
Recorder recorder;
QueueHandle_t eventQueue;
volatile bool recHold = false;     // 記録を保存している間は、記録に書き込まない
volatile int recLength = 0;
volatile uint32_t lastSampleMs = 0;
qs::NotifyPolicy policy;
uint32_t infoUntil = 0;
bool faultNotified = false;

// ------------------------------------------------------------ 計測（専用のタスク、20 ms ごと）

void sensorTask(void *) {
    TickType_t last = xTaskGetTickCount();
    uint32_t retryAt = 0;
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(20));
        if (!mpu.ok()) {
            if (millis() > retryAt) { retryAt = millis() + 3000; mpu.begin(Wire); }
            continue;
        }
        xSemaphoreTake(ctx.engineLock, portMAX_DELAY);
        if (ctx.requestCalibrate) { ctx.engine.reset(); ctx.requestCalibrate = false; }
        const int n = mpu.read([](double x, double y, double z) {
            ctx.engine.push(x, y, z);
            double d[3];
            ctx.engine.lastDynamic(d);
            if (!recHold) recorder.push(d[0], d[1], d[2]);
            qs::EngineEvent ev;
            while (ctx.engine.poll(ev)) {
                if (ev.type == qs::EventType::ShakeStart && !recHold) recorder.begin(500);   // 5 秒前から
                if (ev.type == qs::EventType::ShakeEnd && recorder.recording()) {
                    recLength = recorder.end();
                    recHold = true;
                }
                xQueueSend(eventQueue, &ev, 0);
            }
        });
        xSemaphoreGive(ctx.engineLock);
        if (n > 0) lastSampleMs = millis();
    }
}

// ------------------------------------------------------------ 出来事への対応（音・通知・記録）

void notify(const qs::EngineEvent &ev) {
    if (!policy.shouldSend(ev, millis() / 1000.0)) return;
    qs::MessageContext mc;
    mc.place = ctx.settings.place.c_str();
    mc.when = wallNow();
    const time_t now = time(nullptr);
    mc.start = wallFromEpoch(now - static_cast<time_t>(ev.time - ev.startTime + 0.5));
    qs::NotifyMessage m;
    if (qs::makeMessage(ev, mc, m)) ctx.notifier.enqueue(m, ctx.settings);
}

void handleEvent(const qs::EngineEvent &ev) {
    using qs::EventType;
    switch (ev.type) {
        case EventType::ShakeStart:
            if (ctx.settings.beepOnDetect) ctx.alarm.play(Sound::Detect);
            break;
        case EventType::LevelUp:
            if (ev.level == qs::Level::Warning) ctx.alarm.play(Sound::Warning);
            else if (ev.level == qs::Level::Caution) ctx.alarm.play(Sound::Caution);
            break;
        case EventType::ShakeEnd:
            if (recHold) {
                EventSummary s;
                const time_t now = time(nullptr);
                s.epochStart = now > 1700000000 ? static_cast<uint32_t>(now - (ev.time - ev.startTime)) : 0;
                s.duration = ev.duration;
                s.intensity = ev.intensity;
                s.shindo = qs::shindoLabel(ev.shindo);
                s.pga = ev.pga;
                s.level = static_cast<uint8_t>(ev.level);
                s.notified = static_cast<int>(ev.level) >= ctx.settings.notifyMinLevel;
                ctx.storage.save(s, recorder, recLength);
                recHold = false;
            }
            break;
        default:
            break;
    }
    notify(ev);
}

// ------------------------------------------------------------ ボタン

void handleButton() {
    static bool down = false;
    static uint32_t since = 0;
    static bool clicked3 = false, clicked10 = false;
    const bool pressed = digitalRead(pins::kButton) == LOW;
    const uint32_t now = millis();
    if (pressed && !down) { down = true; since = now; clicked3 = clicked10 = false; }
    if (pressed && down) {
        const uint32_t held = now - since;
        if (held > 3000 && !clicked3) { clicked3 = true; ctx.alarm.play(Sound::Click); }
        if (held > 10000 && !clicked10) {
            clicked10 = true;
            ctx.alarm.play(Sound::Click);
            display.showInfo("RESET ALL SETTINGS?", "release button now", "to reset");
            infoUntil = now + 4000;
        }
    }
    if (!pressed && down) {
        down = false;
        const uint32_t held = now - since;
        if (held < 50) return;   // チャタリング
        if (held >= 10000) {
            factoryReset();
            ESP.restart();
        } else if (held >= 3000) {
            ctx.requestTestAlarm = true;
        } else if (ctx.alarm.playing()) {
            ctx.alarm.stop();     // 警報音を止める
        } else {
            display.showInfo(deviceName(), WiFi.isConnected() ? WiFi.localIP().toString() : String("WiFi: not connected"),
                             "v" + String(kVersion));
            infoUntil = now + 5000;
        }
    }
}

// ------------------------------------------------------------ 画面と LED（0.1 秒ごと）

void updateUi() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (now - last < 100) return;
    last = now;
    qs::EngineStatus st;
    qs::ScreenModel m;
    const qs::WallTime w = wallNow();
    qs::ScreenInput in;
    in.engine = &ctx.engine;
    if (w.valid()) { in.hour = w.hour; in.minute = w.minute; in.second = w.second; }
    in.wifi = WiFi.isConnected();
    in.setupMode = ctx.setupMode;
    in.muted = ctx.settings.volume == 0;
    xSemaphoreTake(ctx.engineLock, portMAX_DELAY);
    st = ctx.engine.status();
    qs::buildScreen(in, m);
    xSemaphoreGive(ctx.engineLock);

    LedMode led = LedMode::Normal;
    if (ctx.setupMode) led = LedMode::Setup;
    else if (!ctx.sensorAlive || !st.sensorOk) led = LedMode::Fault;
    else if (!st.calibrated) led = LedMode::Busy;
    else if (st.level == qs::Level::Warning) led = LedMode::Warning;
    else if (st.level == qs::Level::Caution) led = LedMode::Caution;
    else if (st.level == qs::Level::Detect) led = LedMode::Detect;
    ctx.alarm.setLed(led);

    if (static_cast<int32_t>(now - infoUntil) < 0) return;   // 情報の表示中
    if (ctx.setupMode) {
        static uint32_t lastSetup = 0;
        if (now - lastSetup > 2000) { lastSetup = now; display.showSetup(deviceName(), ctx.settings.apPass); }
        return;
    }
    if (!ctx.sensorAlive) std::snprintf(m.status, sizeof m.status, "NO SENSOR");
    display.show(m);
}

void startWifi() {
    WiFi.persistent(false);
    WiFi.setHostname(deviceName().c_str());
    if (ctx.setupMode) {
        WiFi.mode(WIFI_AP_STA);   // 近くの Wi-Fi を探せるように
        WiFi.softAP(deviceName().c_str(), ctx.settings.apPass.c_str());
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(ctx.settings.wifiSsid.c_str(), ctx.settings.wifiPass.c_str());
        configTzTime("JST-9", "ntp.nict.jp", "time.google.com", "pool.ntp.org");
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(pins::kButton, INPUT_PULLUP);
    loadSettings(ctx.settings);
    ctx.setupMode = !ctx.settings.configured() || digitalRead(pins::kButton) == LOW;  // ボタンを押しながら起動しても設定に入る

    ctx.alarm.begin(pins::kBuzzer, pins::kLedR, pins::kLedG, pins::kLedB);
    ctx.alarm.setVolume(ctx.settings.volume);
    ctx.alarm.setLed(LedMode::Busy);

    Wire.begin(pins::kSda, pins::kScl, 400000);
    display.begin(Wire, ctx.settings.oledHeight);
    display.showBoot(kVersion);
    ctx.alarm.play(Sound::Boot);
    if (!mpu.begin(Wire)) Serial.println("MPU6050 が見つかりません（配線を確かめてください）");

    ctx.storage.begin();
    ctx.engineLock = xSemaphoreCreateMutex();
    qs::EngineConfig cfg;
    cfg.cautionIntensity = ctx.settings.cautionIntensity;
    cfg.warningIntensity = ctx.settings.warningIntensity;
    ctx.engine.setConfig(cfg);
    ctx.recorder = &recorder;

    qs::NotifyPolicyConfig pc;
    pc.minLevel = static_cast<qs::Level>(ctx.settings.notifyMinLevel);
    pc.sendEnd = ctx.settings.sendEnd;
    policy.setConfig(pc);

    eventQueue = xQueueCreate(32, sizeof(qs::EngineEvent));
    ctx.notifier.begin();
    startWifi();
    webBegin();
    // 計測は CPU の 1 番で、高い優先度で動かす（Wi-Fi は 0 番）
    xTaskCreatePinnedToCore(sensorTask, "sensor", 8192, nullptr, 10, nullptr, 1);
    lastSampleMs = millis();
}

void loop() {
    webLoop();

    qs::EngineEvent ev;
    while (xQueueReceive(eventQueue, &ev, 0) == pdTRUE) handleEvent(ev);

    // 設定の変更（通知の条件）を反映
    qs::NotifyPolicyConfig pc = policy.config();
    pc.minLevel = static_cast<qs::Level>(ctx.settings.notifyMinLevel);
    pc.sendEnd = ctx.settings.sendEnd;
    policy.setConfig(pc);

    // センサーから値が届いているか
    const bool alive = millis() - lastSampleMs < 3000;
    if (!alive && ctx.sensorAlive && millis() > 10000) {
        ctx.sensorAlive = false;
        if (!faultNotified) {
            faultNotified = true;
            qs::EngineEvent f;
            f.type = qs::EventType::SensorFault;
            f.value = 3;
            notify(f);
        }
    } else if (alive) {
        ctx.sensorAlive = true;
    }

    if (ctx.requestTestAlarm) { ctx.requestTestAlarm = false; ctx.alarm.play(Sound::Test); }
    if (ctx.requestTestNotify) {
        ctx.requestTestNotify = false;
        qs::MessageContext mc;
        mc.place = ctx.settings.place.c_str();
        mc.when = wallNow();
        ctx.notifier.enqueue(qs::makeTestMessage(mc), ctx.settings);
    }
    if (ctx.requestReboot) { delay(500); ESP.restart(); }

    handleButton();
    ctx.alarm.update();
    updateUi();
    delay(5);
}
