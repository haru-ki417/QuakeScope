// このファイルは core/src から写したものです。編集は core/src で行い、tools/sync_core.py を実行してください。
// 揺れの判定エンジン。センサーの値（gal、重力を含む 3 軸）を 1 サンプルずつ入れると、
// 「揺れ始め」「警戒レベルの上昇」「揺れの終わり」などの出来事を返す。
//
//  1. 起動直後の数秒で、置かれた向き（重力）を測る（校正）
//  2. 重力を引いた加速度から
//     - リアルタイム震度（気象庁の計測震度のフィルターを近似）
//     - STA/LTA（短い時間と長い時間の揺れの比）で、揺れの始まり（P 波）をとらえる
//  3. ドアの衝撃や足音のような「一瞬の揺れ」は、0.3 秒以上続かないので揺れとみなさない
//  4. 揺れの強さ（震度相当）で警戒レベルを上げ、収まったら記録をまとめる
//
// メモリーを動的に確保しない（マイコンでも同じコードを使う）。
#pragma once

#include <cstdint>

#include "biquad.h"
#include "intensity.h"

namespace qs {

enum class Level : uint8_t { Quiet = 0, Detect = 1, Caution = 2, Warning = 3 };
const char *levelName(Level l);  // "平常" "揺れ検知" "注意" "警報"

struct EngineConfig {
    double sampleRate = 100;
    double calibrateSec = 3;        // 起動時に向きを測る時間
    double detectIntensity = 0.5;   // 揺れとみなす震度（0.5 = 震度 1 相当の下限）
    double cautionIntensity = 2.5;  // 注意（2.5 = 震度 3 相当）
    double warningIntensity = 4.5;  // 警報（4.5 = 震度 5 弱相当）
    double staSec = 0.5;
    double ltaSec = 10;
    double triggerRatio = 3.0;
    double detriggerRatio = 1.5;
    double minStaGal = 0.8;         // STA の下限（センサーの雑音だけで反応しないように）
    double confirmSec = 0.3;        // 揺れが続いた時間（直近 1 秒のうち）
    double endQuietSec = 8;         // 収まってから、終わりとみなすまで
    double maxEventSec = 300;       // 1 回の揺れの最長
    double baselineTau = 30;        // 重力の基準を追いかける時定数（平常時だけ）
    double stuckSec = 3;            // 値がまったく変わらなければセンサーの故障
};

enum class EventType : uint8_t {
    Calibrated,   // 校正が終わった（重力の大きさ・傾き）
    ShakeStart,   // 揺れ始め
    LevelUp,      // 警戒レベルが上がった
    ShakeEnd,     // 揺れが収まった（まとめ）
    SensorFault,  // センサーの値が止まった・重力がおかしい
    SensorOk,     // センサーが戻った
};

struct EngineEvent {
    EventType type = EventType::Calibrated;
    Level level = Level::Quiet;
    double time = 0;          // 起動からの秒数
    double intensity = 0;     // その時点の震度（この揺れの最大、計測震度の近似）
    Shindo shindo = Shindo::S0;
    double pga = 0;           // 最大加速度（gal）
    double duration = 0;      // 揺れていた時間（秒、ShakeEnd のとき）
    double startTime = 0;     // 揺れ始めの時刻（秒）
    double value = 0;         // Calibrated: 重力の大きさ（gal）／ SensorFault: 理由（1 値が止まった・2 重力がおかしい・3 値が届かない）
    double tiltDeg = 0;       // Calibrated: 傾き（度）
};

struct EngineStatus {
    bool calibrated = false;
    bool sensorOk = true;
    bool inEvent = false;
    Level level = Level::Quiet;
    double time = 0;
    double intensityNow = -5;    // 直近 5 秒の震度
    double intensityEvent = -5;  // この揺れの最大（揺れていないときは -5）
    double pgaEvent = 0;
    double staLta = 0;
    double noiseGal = 0;         // 平常時の揺れの大きさ（STA/LTA の LTA の平方根）
    double noiseIntensity = -5;  // 平常時の震度（センサーの雑音や周りの機械の振動）。これより十分大きい揺れだけを揺れとみなす
    double eventStart = 0;
};

// 表示用の履歴（0.1 秒ごとの最大値、直近 12.8 秒）
constexpr int kHistoryLen = 128;

class Engine {
public:
    explicit Engine(const EngineConfig &cfg = EngineConfig());
    void reset();
    void setConfig(const EngineConfig &cfg);
    // 警戒レベルの震度だけを変える（校正をやり直さない）
    void setLevels(double caution, double warning) { cfg_.cautionIntensity = caution; cfg_.warningIntensity = warning; }
    const EngineConfig &config() const { return cfg_; }

    // 加速度（gal、重力を含む）を 1 サンプル
    void push(double x, double y, double z);

    // 出来事を 1 つ取り出す（なければ false）
    bool poll(EngineEvent &ev);

    const EngineStatus &status() const { return st_; }
    // 表示用: フィルター後の揺れの大きさ（gal）の履歴。古い順に kHistoryLen 個
    void history(float out[kHistoryLen]) const;
    // 重力の基準（校正値、gal）
    void baseline(double out[3]) const { out[0] = base_[0]; out[1] = base_[1]; out[2] = base_[2]; }
    // 重力を引いた直近の値（gal）
    void lastDynamic(double out[3]) const { out[0] = dyn_[0]; out[1] = dyn_[1]; out[2] = dyn_[2]; }

private:
    void emit(const EngineEvent &e);
    void startEvent();
    void endEvent();
    void updateLevel();

    EngineConfig cfg_;
    EngineStatus st_;
    RealtimeIntensity ri_;
    SosCascade<2> bx_, by_, bz_;   // 揺れ始めをとらえる帯域（1〜10 Hz）

    // 校正
    long calN_ = 0;
    double calSum_[3] = {0, 0, 0};
    double base_[3] = {0, 0, 0};
    double dyn_[3] = {0, 0, 0};

    // STA/LTA
    double sta_ = 0, lta_ = 0, peak_ = 0;
    bool triggered_ = false;
    double ltaReadyAt_ = 0;
    double bg_ = -9;               // 平常時の震度（-9 = まだ分からない）
    uint8_t active_[100] = {};     // 直近 1 秒の「揺れている」サンプル（0/1）
    int activeHead_ = 0, activeCount_ = 0;

    // この揺れの 0.3 秒超過加速度（大きい方から 30 個を最小ヒープで持つ）
    float top_[kExceedSamples] = {};
    int topN_ = 0;
    double quietFor_ = 0;
    int sinceIntensity_ = 0;

    // センサーの故障
    double last_[3] = {0, 0, 0};
    double sameFor_ = 0;

    // 表示用の履歴
    float hist_[kHistoryLen] = {};
    int histHead_ = 0;
    float histMax_ = 0;
    int histCount_ = 0;

    // 出来事の待ち行列
    EngineEvent queue_[16];
    int qHead_ = 0, qLen_ = 0;
};

}  // namespace qs
