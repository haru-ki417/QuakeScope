// このファイルは core/src から写したものです。編集は core/src で行い、tools/sync_core.py を実行してください。
#include "screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace qs {

uint8_t graphLevel(float gal) {
    if (gal <= 0.1f) return 0;
    const double v = (std::log10(gal) + 1.0) / (std::log10(300.0) + 1.0);  // 0.1 gal → 0、300 gal → 1
    const double c = v < 0 ? 0 : v > 1 ? 1 : v;
    return static_cast<uint8_t>(c * 255.0 + 0.5);
}

const char *shindoAscii(Shindo s) {
    static const char *const k[] = {"0", "1", "2", "3", "4", "5-", "5+", "6-", "6+", "7"};
    return k[static_cast<int>(s)];
}

void buildScreen(const ScreenInput &in, ScreenModel &out) {
    out = ScreenModel();
    if (in.hour >= 0) std::snprintf(out.clock, sizeof out.clock, "%02d:%02d:%02d", in.hour % 24 & 31, in.minute % 60 & 63, in.second % 60 & 63);
    else std::snprintf(out.clock, sizeof out.clock, "--:--:--");
    out.wifi = in.wifi;
    out.muted = in.muted;
    if (!in.engine) return;
    const EngineStatus &st = in.engine->status();
    const char *status = "NORMAL";
    if (in.setupMode) status = "SETUP";
    else if (!st.calibrated) status = "CALIB";
    else if (!st.sensorOk) status = "FAULT";
    else if (st.level == Level::Warning) status = "WARNING";
    else if (st.level == Level::Caution) status = "CAUTION";
    else if (st.level == Level::Detect) status = "SHAKE";
    std::snprintf(out.status, sizeof out.status, "%s", status);
    out.alert = st.level == Level::Caution || st.level == Level::Warning;

    const double now = st.intensityNow < 0 ? 0 : st.intensityNow;
    std::snprintf(out.nowText, sizeof out.nowText, "NOW %.1f", roundIntensity(now));
    if (st.inEvent) {
        const double mx = roundIntensity(st.intensityEvent < 0 ? 0 : st.intensityEvent);
        std::snprintf(out.maxText, sizeof out.maxText, "MAX %.1f", mx);
        std::snprintf(out.shindo, sizeof out.shindo, "%s", shindoAscii(shindoFromIntensity(mx)));
    }
    float h[kHistoryLen];
    in.engine->history(h);
    for (int i = 0; i < kHistoryLen; ++i) out.graph[i] = graphLevel(h[i]);
}

}  // namespace qs
