#include "engine.h"

#include <cmath>

namespace qs {

const char *levelName(Level l) {
    switch (l) {
        case Level::Quiet: return "平常";
        case Level::Detect: return "揺れ検知";
        case Level::Caution: return "注意";
        case Level::Warning: return "警報";
    }
    return "";
}

namespace {

// 最小ヒープ（大きい方から K 個を持つ）
void heapPush(float *h, int &n, int cap, float v) {
    auto down = [&](int i) {
        for (;;) {
            int l = 2 * i + 1, r = l + 1, m = i;
            if (l < n && h[l] < h[m]) m = l;
            if (r < n && h[r] < h[m]) m = r;
            if (m == i) return;
            float t = h[i]; h[i] = h[m]; h[m] = t; i = m;
        }
    };
    if (n < cap) {
        int i = n++;
        h[i] = v;
        while (i > 0) {
            int p = (i - 1) / 2;
            if (h[p] <= h[i]) break;
            float t = h[i]; h[i] = h[p]; h[p] = t; i = p;
        }
    } else if (v > h[0]) {
        h[0] = v;
        down(0);
    }
}

}  // namespace

Engine::Engine(const EngineConfig &cfg) { setConfig(cfg); }

void Engine::setConfig(const EngineConfig &cfg) {
    cfg_ = cfg;
    double hp[5], lp[5];
    butterHighpass(1.0, cfg_.sampleRate, hp);
    butterLowpass(10.0, cfg_.sampleRate, lp);
    const double c[2][5] = {{hp[0], hp[1], hp[2], hp[3], hp[4]}, {lp[0], lp[1], lp[2], lp[3], lp[4]}};
    bx_.set(c); by_.set(c); bz_.set(c);
    reset();
}

void Engine::reset() {
    st_ = EngineStatus();
    ri_.reset();
    bx_.reset(); by_.reset(); bz_.reset();
    calN_ = 0;
    for (int i = 0; i < 3; ++i) { calSum_[i] = base_[i] = dyn_[i] = last_[i] = 0; }
    sta_ = lta_ = 0;
    peak_ = 0;
    triggered_ = false;
    ltaReadyAt_ = 0;
    bg_ = -9;
    for (auto &a : active_) a = 0;
    activeHead_ = activeCount_ = 0;
    topN_ = 0;
    quietFor_ = 0;
    sinceIntensity_ = 0;
    sameFor_ = 0;
    for (auto &h : hist_) h = 0;
    histHead_ = 0; histMax_ = 0; histCount_ = 0;
    qHead_ = qLen_ = 0;
}

void Engine::emit(const EngineEvent &e) {
    if (qLen_ == 16) { qHead_ = (qHead_ + 1) % 16; --qLen_; }  // あふれたら古いものを捨てる
    queue_[(qHead_ + qLen_) % 16] = e;
    ++qLen_;
}

bool Engine::poll(EngineEvent &ev) {
    if (qLen_ == 0) return false;
    ev = queue_[qHead_];
    qHead_ = (qHead_ + 1) % 16;
    --qLen_;
    return true;
}

void Engine::history(float out[kHistoryLen]) const {
    for (int i = 0; i < kHistoryLen; ++i) out[i] = hist_[(histHead_ + i) % kHistoryLen];
}

void Engine::push(double x, double y, double z) {
    const double dt = 1.0 / cfg_.sampleRate;
    st_.time += dt;

    // ---- センサーの故障（値がまったく変わらない）
    if (x == last_[0] && y == last_[1] && z == last_[2]) sameFor_ += dt; else sameFor_ = 0;
    last_[0] = x; last_[1] = y; last_[2] = z;
    if (st_.sensorOk && sameFor_ >= cfg_.stuckSec) {
        st_.sensorOk = false;
        EngineEvent e; e.type = EventType::SensorFault; e.time = st_.time; e.value = 1;
        emit(e);
    } else if (!st_.sensorOk && sameFor_ == 0) {
        st_.sensorOk = true;
        EngineEvent e; e.type = EventType::SensorOk; e.time = st_.time;
        emit(e);
    }

    // ---- 校正（起動直後、置かれた向きを測る）
    if (!st_.calibrated) {
        calSum_[0] += x; calSum_[1] += y; calSum_[2] += z;
        if (++calN_ >= static_cast<long>(cfg_.calibrateSec * cfg_.sampleRate)) {
            for (int i = 0; i < 3; ++i) base_[i] = calSum_[i] / calN_;
            st_.calibrated = true;
            ltaReadyAt_ = st_.time + cfg_.ltaSec;
            const double g = std::sqrt(base_[0] * base_[0] + base_[1] * base_[1] + base_[2] * base_[2]);
            EngineEvent e; e.type = EventType::Calibrated; e.time = st_.time; e.value = g;
            e.tiltDeg = g > 0 ? std::acos(std::fabs(base_[2]) / g) * 180.0 / kPi : 0;
            emit(e);
            if (g < 780 || g > 1180) {  // 重力（約 980 gal）から大きく外れる
                st_.sensorOk = false;
                EngineEvent f; f.type = EventType::SensorFault; f.time = st_.time; f.value = 2;
                emit(f);
            }
        }
        return;
    }

    // ---- 重力を引く。平常時だけ、ゆっくり基準を追いかける（温度などによるずれ）
    if (!st_.inEvent && !triggered_) {
        const double a = dt / cfg_.baselineTau;
        base_[0] += a * (x - base_[0]);
        base_[1] += a * (y - base_[1]);
        base_[2] += a * (z - base_[2]);
    }
    dyn_[0] = x - base_[0]; dyn_[1] = y - base_[1]; dyn_[2] = z - base_[2];

    // ---- 震度のフィルター
    ri_.push(dyn_[0], dyn_[1], dyn_[2]);
    const float mag = static_cast<float>(ri_.filteredMagnitude());

    // ---- 揺れ始めをとらえる（STA/LTA）
    const double bx = bx_.process(dyn_[0]), by = by_.process(dyn_[1]), bz = bz_.process(dyn_[2]);
    const double e2 = bx * bx + by * by + bz * bz;
    const double as = dt / cfg_.staSec, al = dt / cfg_.ltaSec;
    sta_ += as * (e2 - sta_);
    if (lta_ == 0) lta_ = e2;
    if (!triggered_) lta_ += al * (e2 - lta_);  // 揺れている間は長い平均を止める
    const double ltaFloor = 0.05 * 0.05;        // 0.05 gal（雑音がほとんどないとき、比が暴れないように）
    const double ratio = sta_ / (lta_ > ltaFloor ? lta_ : ltaFloor);
    st_.staLta = ratio;
    st_.noiseGal = std::sqrt(lta_);
    const bool ltaReady = st_.time >= ltaReadyAt_;
    if (!triggered_ && ltaReady && ratio >= cfg_.triggerRatio && std::sqrt(sta_) >= cfg_.minStaGal) triggered_ = true;
    else if (triggered_ && ratio < cfg_.detriggerRatio) triggered_ = false;

    // 直近 1 秒のうち、「揺れている」サンプルの数（一瞬の衝撃を除くため）。
    // 衝撃はすぐに小さくなるので、直近の最大値の 1/4 を超えている時間が短い。地震の揺れは同じ強さが続く
    const double amp = std::sqrt(e2);
    peak_ = std::fmax(amp, peak_ * std::exp(-dt / 0.5));
    // 雑音の多いセンサーでは、雑音の大きさ（LTA）の 2 倍を超えたときだけ数える（雑音だけでは 1% 未満）
    const double thr = std::fmax(std::fmax(cfg_.minStaGal, 0.25 * peak_), 2.0 * std::sqrt(lta_));
    const uint8_t act = amp > thr ? 1 : 0;
    activeCount_ += act - active_[activeHead_];
    active_[activeHead_] = act;
    activeHead_ = (activeHead_ + 1) % 100;
    const bool sustained = activeCount_ >= static_cast<int>(cfg_.confirmSec * cfg_.sampleRate);

    // ---- 表示用の履歴（0.1 秒ごとの最大値）
    if (mag > histMax_) histMax_ = mag;
    if (++histCount_ >= 10) {
        hist_[histHead_] = histMax_;
        histHead_ = (histHead_ + 1) % kHistoryLen;
        histMax_ = 0; histCount_ = 0;
    }

    // ---- 揺れの中: この揺れの最大
    if (st_.inEvent) {
        heapPush(top_, topN_, kExceedSamples, mag);
        const double pga = std::sqrt(dyn_[0] * dyn_[0] + dyn_[1] * dyn_[1] + dyn_[2] * dyn_[2]);
        if (pga > st_.pgaEvent) st_.pgaEvent = pga;
    }

    // ---- 震度は 0.1 秒ごとに更新
    if (++sinceIntensity_ >= 10) {
        sinceIntensity_ = 0;
        st_.intensityNow = ri_.intensityShort();
        if (st_.inEvent) {
            const float a = topN_ > 0 ? top_[0] : 0;  // ヒープの最小 = 大きい方から 30 番目
            st_.intensityEvent = intensityFromAccel(a);
        }
        if (!st_.inEvent) {
            // 平常時の震度（雑音の床）。下がるのは速く、上がるのはゆっくり（ゆっくり強くなる揺れに引っぱられないように）。
            // 測り始めの 10 秒は早めに合わせる（ただし上がるほうは 10 秒。起動したときに揺れていても、それを雑音と思い込まない）
            if (st_.calibrated) {
                if (bg_ < -8) bg_ = st_.intensityNow;
                const bool up = st_.intensityNow > bg_;
                const bool filling = ltaReadyAt_ - st_.time > cfg_.ltaSec - 5.0;  // 震度の窓（5 秒）がまだ埋まっていない
                const double tau = !ltaReady ? (up ? 10.0 : 0.5) : (up ? 60.0 : 5.0);
                if (filling) bg_ = st_.intensityNow;
                else bg_ += (st_.intensityNow - bg_) * (0.1 / tau);
                st_.noiseIntensity = bg_;
            }
            // 揺れ始め: 「STA/LTA が反応した」かつ「震度 1 相当以上（雑音の床より 0.6 以上大きい）」かつ
            // 「同じ強さの揺れが 0.3 秒以上続いた」。STA/LTA が反応しなくても（ゆっくり強くなる揺れ）、
            // はっきり揺れ続けていれば始める
            const double det = std::fmax(cfg_.detectIntensity, bg_ + 0.6);
            const bool byTrigger = triggered_ && sustained && st_.intensityNow >= det;
            const bool byLevel = sustained && st_.intensityNow >= det + 0.5;
            if (st_.sensorOk && ltaReady && (byTrigger || byLevel)) startEvent();
        } else {
            updateLevel();
            const bool calm = st_.intensityNow < std::fmax(cfg_.detectIntensity - 0.2, bg_ + 0.3) && !triggered_;
            quietFor_ = calm ? quietFor_ + 0.1 : 0;
            if (quietFor_ >= cfg_.endQuietSec || st_.time - st_.eventStart >= cfg_.maxEventSec) endEvent();
        }
    }
}

void Engine::startEvent() {
    st_.inEvent = true;
    st_.eventStart = st_.time;
    st_.level = Level::Detect;
    st_.pgaEvent = 0;
    quietFor_ = 0;
    // 揺れ始めの直前（5 秒の窓）の値から始める
    topN_ = 0;
    const double a = ri_.a03Short();
    for (int i = 0; i < kExceedSamples; ++i) heapPush(top_, topN_, kExceedSamples, static_cast<float>(a));
    st_.intensityEvent = intensityFromAccel(a);
    EngineEvent e;
    e.type = EventType::ShakeStart;
    e.level = Level::Detect;
    e.time = e.startTime = st_.time;
    e.intensity = roundIntensity(st_.intensityEvent);
    e.shindo = shindoFromIntensity(e.intensity);
    emit(e);
    updateLevel();
}

void Engine::updateLevel() {
    Level target = Level::Detect;
    if (st_.intensityEvent >= cfg_.warningIntensity) target = Level::Warning;
    else if (st_.intensityEvent >= cfg_.cautionIntensity) target = Level::Caution;
    // 警戒レベルは揺れの間、下げない（いちばん強かったレベルを保つ）
    while (static_cast<int>(target) > static_cast<int>(st_.level)) {
        st_.level = static_cast<Level>(static_cast<int>(st_.level) + 1);
        EngineEvent e;
        e.type = EventType::LevelUp;
        e.level = st_.level;
        e.time = st_.time;
        e.startTime = st_.eventStart;
        e.intensity = roundIntensity(st_.intensityEvent);
        e.shindo = shindoFromIntensity(e.intensity);
        e.pga = st_.pgaEvent;
        emit(e);
    }
}

void Engine::endEvent() {
    EngineEvent e;
    e.type = EventType::ShakeEnd;
    e.level = st_.level;
    e.time = st_.time;
    e.startTime = st_.eventStart;
    e.duration = st_.time - st_.eventStart - quietFor_;
    if (e.duration < 0) e.duration = 0;
    e.intensity = roundIntensity(st_.intensityEvent);
    e.shindo = shindoFromIntensity(e.intensity);
    e.pga = st_.pgaEvent;
    emit(e);
    st_.inEvent = false;
    st_.level = Level::Quiet;
    st_.intensityEvent = -5;
    st_.pgaEvent = 0;
    topN_ = 0;
    quietFor_ = 0;
}

}  // namespace qs
