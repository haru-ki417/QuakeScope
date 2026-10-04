// 本体の各部分が共有するもの。
#pragma once

#include <Arduino.h>

#include "../qs/engine.h"
#include "alarm.h"
#include "notifier.h"
#include "settings.h"
#include "storage.h"

namespace app {

constexpr const char *kVersion = "1.0.0";

struct Context {
    Settings settings;
    qs::Engine engine;
    SemaphoreHandle_t engineLock = nullptr;   // engine と recorder を触るとき
    Recorder *recorder = nullptr;
    Storage storage;
    Notifier notifier;
    Alarm alarm;
    bool setupMode = false;
    volatile bool requestCalibrate = false;
    volatile bool requestReboot = false;
    volatile bool requestTestAlarm = false;
    volatile bool requestTestNotify = false;
    volatile bool sensorAlive = true;         // センサーから値が届いているか
};

extern Context ctx;

// 時刻（NTP で合わせた日本時間）。合っていなければ year = 0
qs::WallTime wallNow();
qs::WallTime wallFromEpoch(time_t t);

}  // namespace app
