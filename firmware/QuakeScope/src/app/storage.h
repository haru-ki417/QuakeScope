// 揺れの記録を本体のフラッシュ（LittleFS）に保存する。まとめは新しい 50 件、波形は新しい 8 件を残す。
//   /ev/<番号>.json  まとめ
//   /ev/<番号>.bin   波形（重力を引いた 3 軸、0.1 gal 単位の int16、50 Hz、リトルエンディアン）
#pragma once

#include <Arduino.h>

#include "../qs/recorder.h"

namespace app {

constexpr int kRecordCapacity = 6500;   // 65 秒（揺れ始めの 5 秒前から）
constexpr int kKeepSummaries = 50;
constexpr int kKeepWaves = 8;
using Recorder = qs::WaveRecorder<kRecordCapacity>;

struct EventSummary {
    uint32_t epochStart = 0;   // 揺れ始め（UNIX 秒、時刻が分からなければ 0）
    float duration = 0;
    float intensity = 0;
    const char *shindo = "0";
    float pga = 0;
    uint8_t level = 0;
    bool notified = false;
};

class Storage {
public:
    bool begin();
    bool ok() const { return ok_; }
    // 記録を保存して、番号を返す（失敗したら 0）
    uint32_t save(const EventSummary &s, const Recorder &rec, int samples);
    // まとめの一覧（新しい順、JSON の配列）
    String listJson(int maxN = 50);
    String binPath(uint32_t id) const;
    void clear();
    size_t usedBytes() const;
    size_t totalBytes() const;

private:
    void prune(int keepSummaries, int keepWaves);
    bool ok_ = false;
};

}  // namespace app
