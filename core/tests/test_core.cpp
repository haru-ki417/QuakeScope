// 揺れの判定エンジンのテスト（外部のライブラリーを使わない小さなテストの仕組み）
//   c++ -std=c++17 -O2 -I../src test_core.cpp ../src/*.cpp -o test_core && ./test_core
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "engine.h"
#include "intensity.h"
#include "notify.h"
#include "recorder.h"
#include "screen.h"
#include "synth.h"

using namespace qs;

static int g_fail = 0, g_checks = 0;
static const char *g_test = "";
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) { ++g_fail; std::printf("  FAIL [%s] %s:%d  %s\n", g_test, __FILE__, __LINE__, #cond); } \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

struct TestCase { const char *name; std::function<void()> fn; };
static std::vector<TestCase> &tests() { static std::vector<TestCase> t; return t; }
struct Reg { Reg(const char *n, std::function<void()> f) { tests().push_back({n, f}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

// ------------------------------------------------------------ 共通

struct Run {
    std::vector<EngineEvent> events;
    double maxLevel = 0;
};

static Run runEngine(const SynthSignal &s, const EngineConfig &cfg = EngineConfig()) {
    Engine e(cfg);
    Run r;
    EngineEvent ev;
    for (size_t i = 0; i < s.x.size(); ++i) {
        e.push(s.x[i], s.y[i], s.z[i]);
        while (e.poll(ev)) {
            r.events.push_back(ev);
            if (static_cast<int>(ev.level) > r.maxLevel) r.maxLevel = static_cast<int>(ev.level);
        }
    }
    return r;
}

static const EngineEvent *find(const Run &r, EventType t, Level l = Level::Quiet) {
    for (auto &e : r.events)
        if (e.type == t && (l == Level::Quiet || e.level == l)) return &e;
    return nullptr;
}

static ExactResult exactOf(const SynthSignal &s) {
    return computeJmaIntensity(s.x.data(), s.y.data(), s.z.data(), s.x.size(), s.fs);
}

static SynthSignal quake(SynthKind k, double target, uint32_t seed, double seconds = 120) {
    SynthParams p;
    p.kind = k; p.targetIntensity = target; p.seed = seed; p.seconds = seconds; p.onset = 15;
    return synthesize(p);
}

// ------------------------------------------------------------ 震度

TEST(shindo_class_boundaries) {
    CHECK(shindoFromIntensity(0.4) == Shindo::S0);
    CHECK(shindoFromIntensity(0.5) == Shindo::S1);
    CHECK(shindoFromIntensity(2.4) == Shindo::S2);
    CHECK(shindoFromIntensity(3.5) == Shindo::S4);
    CHECK(shindoFromIntensity(4.5) == Shindo::S5Lower);
    CHECK(shindoFromIntensity(4.9) == Shindo::S5Lower);
    CHECK(shindoFromIntensity(5.0) == Shindo::S5Upper);
    CHECK(shindoFromIntensity(5.5) == Shindo::S6Lower);
    CHECK(shindoFromIntensity(6.0) == Shindo::S6Upper);
    CHECK(shindoFromIntensity(6.5) == Shindo::S7);
    CHECK(std::string(shindoLabel(Shindo::S5Upper)) == "5強");
}

TEST(jma_rounding) {
    // 小数第 3 位を四捨五入し、小数第 2 位を切り捨てる
    NEAR(roundIntensity(4.495), 4.5, 1e-9);   // 4.50 → 4.5
    NEAR(roundIntensity(4.494), 4.4, 1e-9);   // 4.49 → 4.4
    NEAR(roundIntensity(3.0), 3.0, 1e-9);
    NEAR(roundIntensity(2.999), 3.0, 1e-9);   // 3.00 → 3.0
}

TEST(jma_filter_gain_formula) {
    // 1 Hz: 周期の効果 1、高域 x=0.1、低域 (1/0.5)^3 = 8
    const double x2 = 0.01;
    const double hc = 1 / std::sqrt(1 + 0.694 * x2 + 0.241 * x2 * x2 + 0.0557 * std::pow(x2, 3) + 0.009664 * std::pow(x2, 4) +
                                    0.00134 * std::pow(x2, 5) + 0.000155 * std::pow(x2, 6));
    NEAR(jmaFilterGain(1.0), hc * std::sqrt(1 - std::exp(-8.0)), 1e-12);
    CHECK(jmaFilterGain(0) == 0);
    CHECK(jmaFilterGain(0.1) < 0.3);    // 低い周波数は大きく減らす（1 Hz では約 1）
    CHECK(jmaFilterGain(30) < 0.05);    // 高い周波数も減らす
}

TEST(exact_intensity_of_sines_matches_theory) {
    // 長い正弦波では、0.3 秒の超過加速度 ≒ 振幅 × フィルターの利得
    for (double f : {0.5, 1.0, 2.0, 4.0, 8.0}) {
        const size_t n = 6000;
        std::vector<float> x(n), z0(n, 0.f);
        for (size_t i = 0; i < n; ++i) x[i] = static_cast<float>(20 * std::sin(2 * kPi * f * i / 100.0));
        const ExactResult r = computeJmaIntensity(x.data(), z0.data(), z0.data(), n, 100);
        NEAR(r.raw, 2 * std::log10(20 * jmaFilterGain(f)) + 0.94, 0.02);
        NEAR(r.pga, 20, 0.5);
    }
}

TEST(exact_intensity_ignores_gravity_offset) {
    const SynthSignal s = quake(SynthKind::NearQuake, 3.0, 4);
    std::vector<float> z2(s.z);
    for (auto &v : z2) v += 500;  // 一定の成分は計測震度に影響しない
    const ExactResult a = computeJmaIntensity(s.x.data(), s.y.data(), s.z.data(), s.x.size(), 100);
    const ExactResult b = computeJmaIntensity(s.x.data(), s.y.data(), z2.data(), s.x.size(), 100);
    NEAR(a.raw, b.raw, 1e-6);
}

TEST(realtime_filter_tracks_exact_intensity) {
    // 本体で使うリアルタイムの近似（IIR）と、計測震度（FFT）の差
    double maxErr = 0, sumErr = 0;
    int n = 0;
    for (SynthKind k : {SynthKind::NearQuake, SynthKind::FarQuake})
        for (double t = 1.0; t <= 6.5; t += 0.5)
            for (uint32_t seed = 1; seed <= 4; ++seed) {
                const SynthSignal s = quake(k, t, seed);
                const double ex = exactOf(s).raw;
                const double rt = realtimeIntensityOf(s.x.data(), s.y.data(), s.z.data(), s.x.size());
                maxErr = std::fmax(maxErr, std::fabs(rt - ex));
                sumErr += std::fabs(rt - ex);
                ++n;
            }
    std::printf("    リアルタイム震度と計測震度の差: 平均 %.3f、最大 %.3f（%d 件）\n", sumErr / n, maxErr, n);
    CHECK(maxErr <= 0.15);
    CHECK(sumErr / n <= 0.06);
}

// ------------------------------------------------------------ 合成した揺れ

TEST(synth_is_deterministic_and_hits_target) {
    const SynthSignal a = quake(SynthKind::NearQuake, 4.2, 9), b = quake(SynthKind::NearQuake, 4.2, 9);
    CHECK(a.x == b.x && a.z == b.z);
    NEAR(exactOf(a).raw, 4.2, 0.02);
    const SynthSignal c = quake(SynthKind::NearQuake, 4.2, 10);
    CHECK(a.x != c.x);
    CHECK(a.sArrival > a.pArrival && a.pArrival == 15);
}

// ------------------------------------------------------------ 判定エンジン

TEST(calibration_reports_gravity_and_tilt) {
    SynthParams p; p.kind = SynthKind::Quiet; p.seconds = 10;
    const Run r = runEngine(synthesize(p));
    const EngineEvent *c = find(r, EventType::Calibrated);
    CHECK(c != nullptr);
    if (c) { NEAR(c->value, 980.665, 1.0); CHECK(c->tiltDeg < 1.0); NEAR(c->time, 3.0, 0.02); }
}

TEST(quiet_room_makes_no_events) {
    SynthParams p; p.kind = SynthKind::Quiet; p.seconds = 300; p.noiseGal = 0.4;
    const Run r = runEngine(synthesize(p));
    CHECK(find(r, EventType::ShakeStart) == nullptr);
    CHECK(r.events.size() == 1);  // 校正だけ
}

TEST(nuisance_vibrations_do_not_alarm) {
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        for (SynthKind k : {SynthKind::DoorSlam, SynthKind::Footsteps}) {
            SynthParams p; p.kind = k; p.seconds = 40; p.seed = seed;
            const Run r = runEngine(synthesize(p));
            CHECK(find(r, EventType::ShakeStart) == nullptr);  // 一瞬の衝撃は揺れとみなさない
        }
        SynthParams p; p.kind = SynthKind::Truck; p.seconds = 60; p.seed = seed;
        const Run r = runEngine(synthesize(p));
        CHECK(r.maxLevel <= static_cast<int>(Level::Detect));  // 振動は検知しても、注意・警報にはならない
    }
}

TEST(noisy_sensor_adapts_its_floor) {
    // MPU6050 の雑音（データシートの 400 µg/√Hz ≒ 100 Hz で 1 方向 3 gal）でも、
    // 雑音だけでは揺れとみなさず、震度 3 以上の揺れはとらえて、終わりも分かる
    for (uint32_t seed = 1; seed <= 3; ++seed) {
        SynthParams q; q.kind = SynthKind::Quiet; q.seconds = 300; q.noiseGal = 3.0; q.seed = seed;
        const Run quiet = runEngine(synthesize(q));
        CHECK(find(quiet, EventType::ShakeStart) == nullptr);
        for (SynthKind k : {SynthKind::DoorSlam, SynthKind::Footsteps, SynthKind::Truck}) {
            SynthParams p; p.kind = k; p.seconds = 60; p.seed = seed; p.noiseGal = 3.0;
            const Run r = runEngine(synthesize(p));
            CHECK(r.maxLevel <= static_cast<int>(Level::Detect));
        }
        for (double t : {3.0, 4.5}) {
            SynthParams p; p.kind = SynthKind::NearQuake; p.targetIntensity = t; p.seed = seed; p.seconds = 150; p.onset = 15; p.noiseGal = 3.0;
            const SynthSignal s = synthesize(p);
            const Run r = runEngine(s);
            const EngineEvent *end = find(r, EventType::ShakeEnd);
            CHECK(end != nullptr);
            if (end) NEAR(end->intensity, exactOf(s).intensity, 0.25);
            CHECK(r.maxLevel >= static_cast<int>(Level::Caution));
        }
    }
}

TEST(alert_levels_follow_intensity) {
    for (SynthKind k : {SynthKind::NearQuake, SynthKind::FarQuake})
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            for (double t : {1.5, 2.2}) {
                const Run r = runEngine(quake(k, t, seed));
                CHECK(find(r, EventType::ShakeStart) != nullptr);
                CHECK(r.maxLevel == static_cast<int>(Level::Detect));  // 震度 3 未満は注意にしない
            }
            for (double t : {2.8, 4.0}) {
                const Run r = runEngine(quake(k, t, seed));
                CHECK(r.maxLevel == static_cast<int>(Level::Caution));
            }
            for (double t : {4.8, 6.2}) {
                const Run r = runEngine(quake(k, t, seed));
                CHECK(r.maxLevel == static_cast<int>(Level::Warning));
            }
        }
}

TEST(near_quake_detected_on_p_wave) {
    // 震度 3 以上の近い地震では、S 波（強い揺れ）が来る前に揺れ始めをとらえる
    for (uint32_t seed = 1; seed <= 5; ++seed) {
        const SynthSignal s = quake(SynthKind::NearQuake, 3.5, seed);
        const Run r = runEngine(s);
        const EngineEvent *st = find(r, EventType::ShakeStart);
        CHECK(st != nullptr);
        if (st) {
            CHECK(st->time < s.sArrival);
            CHECK(st->time >= s.pArrival);
            CHECK(st->time - s.pArrival < 1.5);
        }
    }
}

TEST(event_summary_matches_exact_intensity) {
    for (uint32_t seed = 1; seed <= 4; ++seed)
        for (double t : {2.0, 3.6, 5.2}) {
            const SynthSignal s = quake(SynthKind::NearQuake, t, seed, 150);
            const Run r = runEngine(s);
            const EngineEvent *end = find(r, EventType::ShakeEnd);
            CHECK(end != nullptr);
            if (!end) continue;
            NEAR(end->intensity, exactOf(s).intensity, 0.2);
            CHECK(end->duration > 5 && end->duration < 120);
            CHECK(end->pga > 0);
            // 1 回の揺れで、ShakeStart と ShakeEnd は 1 つずつ
            int starts = 0, ends = 0;
            for (auto &e : r.events) { starts += e.type == EventType::ShakeStart; ends += e.type == EventType::ShakeEnd; }
            CHECK(starts == 1 && ends == 1);
        }
}

TEST(level_up_events_are_in_order) {
    const Run r = runEngine(quake(SynthKind::NearQuake, 6.0, 2));
    std::vector<int> levels;
    for (auto &e : r.events) if (e.type == EventType::LevelUp) levels.push_back(static_cast<int>(e.level));
    CHECK(levels.size() == 2);
    if (levels.size() == 2) CHECK(levels[0] == 2 && levels[1] == 3);
}

TEST(works_when_mounted_on_a_wall) {
    // 壁に付けて、重力が x 軸にかかっていても同じように判定する
    SynthSignal s = quake(SynthKind::NearQuake, 4.0, 5);
    for (size_t i = 0; i < s.x.size(); ++i) { float t = s.x[i]; s.x[i] = s.z[i]; s.z[i] = t; }
    const Run r = runEngine(s);
    const EngineEvent *c = find(r, EventType::Calibrated);
    CHECK(c && c->tiltDeg > 85);
    CHECK(r.maxLevel == static_cast<int>(Level::Caution));
}

TEST(stuck_sensor_is_reported) {
    SynthParams p; p.kind = SynthKind::Quiet; p.seconds = 20;
    SynthSignal s = synthesize(p);
    for (size_t i = 1000; i < 1500; ++i) { s.x[i] = 1; s.y[i] = 2; s.z[i] = 980; }
    const Run r = runEngine(s);
    CHECK(find(r, EventType::SensorFault) != nullptr);
    CHECK(find(r, EventType::SensorOk) != nullptr);
}

TEST(wrong_gravity_is_reported) {
    SynthParams p; p.kind = SynthKind::Quiet; p.seconds = 10; p.gravityGal = 490;  // 感度の設定違いなど
    const Run r = runEngine(synthesize(p));
    const EngineEvent *f = find(r, EventType::SensorFault);
    CHECK(f && f->value == 2);
}

TEST(history_and_screen) {
    const SynthSignal s = quake(SynthKind::NearQuake, 4.0, 1, 40);
    Engine e;
    for (size_t i = 0; i < 2300; ++i) e.push(s.x[i], s.y[i], s.z[i]);  // 23 秒（S 波の最中）
    ScreenModel m;
    ScreenInput in; in.engine = &e; in.hour = 9; in.minute = 5; in.second = 7; in.wifi = true;
    buildScreen(in, m);
    CHECK(std::string(m.clock) == "09:05:07");
    CHECK(std::string(m.status) == "CAUTION");
    CHECK(m.alert);
    CHECK(std::strlen(m.shindo) >= 1);
    CHECK(m.graph[kHistoryLen - 1] > m.graph[0]);  // 今の方が揺れている
    CHECK(graphLevel(0.05f) == 0 && graphLevel(300.f) == 255);
    CHECK(std::string(shindoAscii(Shindo::S6Upper)) == "6+");
}

// ------------------------------------------------------------ 記録

TEST(recorder_keeps_pre_trigger_samples) {
    WaveRecorder<1000> rec;
    for (int i = 0; i < 700; ++i) rec.push(i * 0.1, 0, 0);
    rec.begin(100);   // 100 サンプル前から
    for (int i = 700; i < 760; ++i) rec.push(i * 0.1, 0, 0);
    const int n = rec.end();
    CHECK(n == 160);
    CHECK(rec.at(0)[0] == 600);    // 60.0 gal → 0.1 gal 単位で 600
    CHECK(rec.at(159)[0] == 759);
    // 容量を超えたら、始まりを残して止める
    WaveRecorder<200> small;
    for (int i = 0; i < 50; ++i) small.push(1, 0, 0);
    small.begin(20);
    for (int i = 0; i < 500; ++i) small.push(2, 0, 0);
    CHECK(small.end() == 200);
    CHECK(small.at(0)[0] == 10 && small.at(199)[0] == 20);
}

// ------------------------------------------------------------ 通知

TEST(notify_policy_sends_once_per_level) {
    NotifyPolicy p;
    EngineEvent st; st.type = EventType::ShakeStart;
    EngineEvent c; c.type = EventType::LevelUp; c.level = Level::Caution;
    EngineEvent w; w.type = EventType::LevelUp; w.level = Level::Warning;
    EngineEvent d; d.type = EventType::LevelUp; d.level = Level::Detect;
    EngineEvent end; end.type = EventType::ShakeEnd;
    CHECK(!p.shouldSend(st, 0));
    CHECK(!p.shouldSend(d, 1));    // 揺れ検知だけでは送らない
    CHECK(p.shouldSend(c, 2));
    CHECK(!p.shouldSend(c, 3));    // 同じレベルはもう送らない
    CHECK(p.shouldSend(w, 4));
    CHECK(p.shouldSend(end, 30));  // 途中で送ったので、まとめも送る
    // 小さな揺れ（注意まで行かない）は、まとめも送らない
    CHECK(!p.shouldSend(st, 100));
    CHECK(!p.shouldSend(d, 101));
    CHECK(!p.shouldSend(end, 120));
}

TEST(notify_policy_rate_limit_but_never_blocks_warning) {
    NotifyPolicy p;
    NotifyPolicyConfig cfg; cfg.maxPerHour = 3; cfg.sendEnd = false;
    p.setConfig(cfg);
    EngineEvent st; st.type = EventType::ShakeStart;
    EngineEvent c; c.type = EventType::LevelUp; c.level = Level::Caution;
    EngineEvent w; w.type = EventType::LevelUp; w.level = Level::Warning;
    int sent = 0;
    for (int i = 0; i < 10; ++i) { p.shouldSend(st, i * 60.0); sent += p.shouldSend(c, i * 60.0 + 1); }
    CHECK(sent == 3);
    p.shouldSend(st, 700);
    CHECK(p.shouldSend(w, 701));   // 強い揺れは上限に関係なく送る
    p.shouldSend(st, 4000);
    CHECK(p.shouldSend(c, 4001));  // 1 時間たてば、また送れる
}

TEST(notify_policy_min_level_warning_only) {
    NotifyPolicy p;
    NotifyPolicyConfig cfg; cfg.minLevel = Level::Warning;
    p.setConfig(cfg);
    EngineEvent st; st.type = EventType::ShakeStart;
    EngineEvent c; c.type = EventType::LevelUp; c.level = Level::Caution;
    EngineEvent end; end.type = EventType::ShakeEnd;
    p.shouldSend(st, 0);
    CHECK(!p.shouldSend(c, 1));
    CHECK(!p.shouldSend(end, 9));
}

TEST(messages_are_japanese_and_honest) {
    MessageContext ctx;
    ctx.place = "リビング";
    ctx.when = {2026, 10, 4, 23, 41, 5};
    ctx.start = {2026, 10, 4, 23, 40, 58};
    EngineEvent w; w.type = EventType::LevelUp; w.level = Level::Warning; w.intensity = 4.7; w.shindo = Shindo::S5Lower;
    NotifyMessage m;
    CHECK(makeMessage(w, ctx, m));
    CHECK(m.title.find("リビング") != std::string::npos);
    CHECK(m.title.find("震度5弱相当") != std::string::npos);
    CHECK(m.body.find("身の安全") != std::string::npos);
    CHECK(m.body.find("2026/10/4 23:41:05") != std::string::npos);
    CHECK(m.body.find("気象庁の発表する震度ではありません") != std::string::npos);
    EngineEvent e; e.type = EventType::ShakeEnd; e.intensity = 4.7; e.shindo = Shindo::S5Lower; e.pga = 182.4; e.duration = 41.2;
    CHECK(makeMessage(e, ctx, m));
    CHECK(m.body.find("約41秒") != std::string::npos);
    CHECK(m.body.find("182 gal") != std::string::npos);
    CHECK(m.body.find("23:40:58") != std::string::npos);
    EngineEvent s; s.type = EventType::ShakeStart;
    CHECK(!makeMessage(s, ctx, m));
    const NotifyMessage t = makeTestMessage(ctx);
    CHECK(t.title.find("テスト") != std::string::npos);
}

TEST(payloads_are_valid_json) {
    NotifyMessage m; m.body = "1行目\n\"引用\" と \\ と\ttab";
    CHECK(jsonEscape(m.body) == "1行目\\n\\\"引用\\\" と \\\\ と\\ttab");
    CHECK(linePushJson("U123", m) == "{\"to\":\"U123\",\"messages\":[{\"type\":\"text\",\"text\":\"" + jsonEscape(m.body) + "\"}]}");
    CHECK(telegramJson("-100", m).find("\"chat_id\":\"-100\"") != std::string::npos);
    CHECK(discordJson(m).rfind("{\"content\":\"", 0) == 0);
    CHECK(jsonEscape(std::string("a\x01")) == "a\\u0001");
}

TEST(base64_and_mime) {
    CHECK(base64("") == "");
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("Man") == "TWFu");
    CHECK(base64("テスト") == "44OG44K544OI");
    CHECK(mimeHeaderWord("テスト") == "=?UTF-8?B?44OG44K544OI?=");
}

int main() {
    for (auto &t : tests()) {
        g_test = t.name;
        const int before = g_fail;
        t.fn();
        std::printf("%s %s\n", g_fail == before ? "ok  " : "NG  ", t.name);
    }
    std::printf("\n%zu テスト・%d 項目、失敗 %d\n", tests().size(), g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
