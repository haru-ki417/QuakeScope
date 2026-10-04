// ブラウザー版（WebAssembly）から、本体と同じ判定エンジンを呼ぶための窓口。
// 本体のファームウェアには入れない（tools/sync_core.py で除外）。
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#include "intensity.h"
#include "notify.h"
#include "screen.h"
#include "synth.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

using namespace qs;

namespace {
Engine g_engine;
EngineEvent g_ev;
float g_hist[kHistoryLen];
SynthSignal g_synth;
std::string g_str;
ExactResult g_exact;
}  // namespace

EXPORT void *qs_alloc(int bytes) { return std::malloc(static_cast<size_t>(bytes)); }
EXPORT void qs_free(void *p) { std::free(p); }

EXPORT void qs_init(double caution, double warning, double calibrateSec) {
    EngineConfig c;
    c.cautionIntensity = caution;
    c.warningIntensity = warning;
    c.calibrateSec = calibrateSec;
    g_engine.setConfig(c);
}
EXPORT void qs_set_levels(double caution, double warning) { g_engine.setLevels(caution, warning); }
EXPORT void qs_push(float x, float y, float z) { g_engine.push(x, y, z); }

// 出来事を 1 つ取り出す（なければ 0）。中身は qs_ev で読む
EXPORT int qs_poll() { return g_engine.poll(g_ev) ? 1 : 0; }
EXPORT double qs_ev(int f) {
    switch (f) {
        case 0: return static_cast<double>(g_ev.type);
        case 1: return static_cast<double>(g_ev.level);
        case 2: return g_ev.time;
        case 3: return g_ev.intensity;
        case 4: return static_cast<double>(g_ev.shindo);
        case 5: return g_ev.pga;
        case 6: return g_ev.duration;
        case 7: return g_ev.startTime;
        case 8: return g_ev.value;
        case 9: return g_ev.tiltDeg;
    }
    return 0;
}
EXPORT double qs_st(int f) {
    const EngineStatus &s = g_engine.status();
    switch (f) {
        case 0: return s.calibrated;
        case 1: return s.sensorOk;
        case 2: return s.inEvent;
        case 3: return static_cast<double>(s.level);
        case 4: return s.time;
        case 5: return s.intensityNow;
        case 6: return s.intensityEvent;
        case 7: return s.pgaEvent;
        case 8: return s.staLta;
        case 9: return s.noiseGal;
        case 10: return s.eventStart;
        case 11: return s.noiseIntensity;
    }
    return 0;
}
EXPORT double qs_dyn(int axis) { double d[3]; g_engine.lastDynamic(d); return d[axis < 0 ? 0 : axis > 2 ? 2 : axis]; }
EXPORT float *qs_history() { g_engine.history(g_hist); return g_hist; }

// 本体の画面（OLED）の内容を、"時刻\t状態\t震度\tいま\t最大\t反転\tグラフ（16 進）" で返す
EXPORT const char *qs_screen(int hour, int minute, int second, int wifi) {
    ScreenInput in;
    in.engine = &g_engine;
    in.hour = hour; in.minute = minute; in.second = second;
    in.wifi = wifi != 0;
    ScreenModel m;
    buildScreen(in, m);
    g_str = std::string(m.clock) + "\t" + m.status + "\t" + m.shindo + "\t" + m.nowText + "\t" + m.maxText + "\t" + (m.alert ? "1" : "0") + "\t";
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < kHistoryLen; ++i) { g_str += hx[m.graph[i] >> 4]; g_str += hx[m.graph[i] & 15]; }
    return g_str.c_str();
}

// 直前に取り出した出来事の通知の文面。"件名\x1f本文"（通知しない出来事なら空）
EXPORT const char *qs_message(const char *place, int y, int mo, int d, int h, int mi, int s, double startAgo) {
    MessageContext c;
    c.place = place;
    c.when = {y, mo, d, h, mi, s};
    // 揺れ始めの時刻（startAgo 秒前）
    int tot = h * 3600 + mi * 60 + s - static_cast<int>(std::lround(startAgo));
    WallTime st = c.when;
    if (tot >= 0) { st.hour = tot / 3600; st.minute = tot / 60 % 60; st.second = tot % 60; }
    c.start = st;
    NotifyMessage m;
    g_str = makeMessage(g_ev, c, m) ? m.title + "\x1f" + m.body : "";
    return g_str.c_str();
}
EXPORT const char *qs_line_json(const char *userId, const char *title, const char *body) {
    NotifyMessage m; m.title = title; m.body = body;
    g_str = linePushJson(userId, m);
    return g_str.c_str();
}

// 合成した揺れ
EXPORT int qs_synth(int kind, double target, int seed, double seconds, double onset) {
    SynthParams p;
    p.kind = static_cast<SynthKind>(kind);
    p.targetIntensity = target;
    p.seed = static_cast<uint32_t>(seed);
    p.seconds = seconds;
    p.onset = onset;
    g_synth = synthesize(p);
    return static_cast<int>(g_synth.x.size());
}
EXPORT float *qs_synth_axis(int a) { return a == 0 ? g_synth.x.data() : a == 1 ? g_synth.y.data() : g_synth.z.data(); }
EXPORT double qs_synth_p() { return g_synth.pArrival; }
EXPORT double qs_synth_s() { return g_synth.sArrival; }

// 計測震度（記録全体から）。続けて qs_exact_field で a03・最大加速度を読める
EXPORT double qs_exact(const float *x, const float *y, const float *z, int n, double fs) {
    g_exact = computeJmaIntensity(x, y, z, static_cast<size_t>(n), fs);
    return g_exact.raw;
}
EXPORT double qs_exact_field(int f) { return f == 0 ? g_exact.intensity : f == 1 ? g_exact.a03 : f == 2 ? g_exact.pga : static_cast<double>(g_exact.shindo); }
EXPORT double qs_realtime_of(const float *x, const float *y, const float *z, int n) { return realtimeIntensityOf(x, y, z, static_cast<size_t>(n)); }

// フィルターの特性（しくみのグラフ用）: 気象庁の式と、本体で使う近似（IIR）
EXPORT double qs_gain_jma(double f) { return jmaFilterGain(f); }
EXPORT double qs_gain_iir(double f) {
    double g = 1;
    const double w = 2 * kPi * f / kJmaSampleRate;
    for (int i = 0; i < kJmaSections; ++i) {
        const double *c = kJmaSos[i];
        // H(e^jw) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
        const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
        const double nr = c[0] + c[1] * cr + c[2] * c2r, ni = c[1] * ci + c[2] * c2i;
        const double dr = 1 + c[3] * cr + c[4] * c2r, di = c[3] * ci + c[4] * c2i;
        g *= std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return g;
}
EXPORT const char *qs_shindo_label(int s) { return shindoLabel(static_cast<Shindo>(s)); }
