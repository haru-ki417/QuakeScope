// このファイルは core/src から写したものです。編集は core/src で行い、tools/sync_core.py を実行してください。
// 震度の計算。
//  - 計測震度（気象庁の方法）: 記録した揺れ全体から、周波数領域でフィルターをかけて求める（正確）
//  - リアルタイム震度: 同じフィルターを IIR で近似し、1 サンプルごとに更新する（本体の警報に使う）
// 加速度の単位は gal（cm/s²）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "biquad.h"
#include "jma_filter_coeffs.h"

namespace qs {

constexpr double kSampleRate = 100.0;   // 本体の標本化周波数（Hz）
constexpr int kExceedSamples = 30;      // 0.3 秒ぶんのサンプル数（100 Hz）

// 震度階級（気象庁の 10 段階）
enum class Shindo : uint8_t { S0, S1, S2, S3, S4, S5Lower, S5Upper, S6Lower, S6Upper, S7 };

// 計測震度 I（小数第 1 位まで）から震度階級へ
Shindo shindoFromIntensity(double intensity);
// 表示用（"0" "1" … "5弱" "5強" … "7"）
const char *shindoLabel(Shindo s);
// 0.3 秒の超過加速度 a（gal）から計測震度（丸める前）
double intensityFromAccel(double a03);
// 気象庁の丸め: 小数第 3 位を四捨五入し、小数第 2 位を切り捨てる
double roundIntensity(double intensity);

// ------------------------------------------------------------ 計測震度（記録全体から）

struct ExactResult {
    double intensity = 0;     // 計測震度（丸めたもの）
    double raw = 0;           // 丸める前
    double a03 = 0;           // 0.3 秒の超過加速度（gal、フィルター後のベクトル和）
    Shindo shindo = Shindo::S0;
    double pga = 0;           // 最大加速度（gal、フィルター前の 3 成分合成、平均を除いたもの）
};

// x, y, z: 同じ長さの加速度（gal）、fs: 標本化周波数
ExactResult computeJmaIntensity(const float *x, const float *y, const float *z, size_t n, double fs);

// 気象庁のフィルターの振幅特性（f: Hz）
double jmaFilterGain(double f);

// ------------------------------------------------------------ リアルタイム震度

// 過去 kWindow サンプルのフィルター後の値から、0.3 秒の超過加速度を求める。
// kWindow = 6000 なら 60 秒（地震の揺れのほとんどが収まる長さ）
template <int kWindow>
class ExceedWindow {
public:
    void reset() { count_ = 0; head_ = 0; }
    void push(float v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % kWindow;
        if (count_ < kWindow) ++count_;
    }
    // 大きい方から kExceedSamples 番目の値（サンプルが足りないときは最小値）
    float a03() const;
    int size() const { return count_; }

private:
    float buf_[kWindow] = {};
    mutable float work_[kWindow] = {};
    int count_ = 0;
    int head_ = 0;
};

class RealtimeIntensity {
public:
    RealtimeIntensity();
    void reset();
    // 重力を除いた加速度（gal）を 1 サンプルずつ入れる
    void push(double x, double y, double z);
    // フィルター後のベクトルの大きさ（直近のサンプル、gal）
    double filteredMagnitude() const { return lastMag_; }
    // 直近 5 秒の窓での震度（「今」の揺れの強さ）。重いので 0.1 秒に 1 回ほど呼ぶ
    double intensityShort() const;
    // 直近 5 秒の窓の 0.3 秒超過加速度（gal）
    double a03Short() const { return short_.a03(); }

private:
    SosCascade<kJmaSections> fx_, fy_, fz_;
    ExceedWindow<500> short_;
    double lastMag_ = 0;
};

// 記録全体に、リアルタイムのフィルター（本体と同じ）をかけて求めた震度（丸める前）。
// 計測震度（computeJmaIntensity）とどれだけ近いかを確かめるのに使う
double realtimeIntensityOf(const float *x, const float *y, const float *z, size_t n);

}  // namespace qs
