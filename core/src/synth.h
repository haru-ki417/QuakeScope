// テストとブラウザー版の「見本の揺れ」に使う、合成した揺れ。
// 本物の地震の記録ではなく、P 波・S 波・尾部（コーダ）の形を数式で作ったもの。
// 同じ種（seed）からは、どの端末でも同じ揺れになる。
#pragma once

#include <cstdint>
#include <vector>

namespace qs {

enum class SynthKind : uint8_t {
    Quiet,        // 何もない（センサーの雑音だけ）
    NearQuake,    // 近くの地震（P 波と S 波の間が短い・揺れが急に強くなる）
    FarQuake,     // 遠くの地震（P 波が長く、ゆっくり大きく揺れる）
    DoorSlam,     // ドアを閉めた衝撃（一瞬だけ大きい）
    Footsteps,    // 足音（小さな衝撃のくり返し）
    Truck,        // 近くを通るトラック（高めの周波数の振動が十数秒）
};

struct SynthParams {
    SynthKind kind = SynthKind::NearQuake;
    double seconds = 60;          // 全体の長さ
    double onset = 8;             // 揺れが始まる時刻（秒）
    double targetIntensity = 4.0; // 地震のとき: 目標の計測震度（調整される）
    double noiseGal = 0.25;       // センサーの雑音（gal、標準偏差）
    double gravityGal = 980.665;  // z 軸にかかる重力
    uint32_t seed = 1;
};

struct SynthSignal {
    std::vector<float> x, y, z;   // gal（z には重力を含む）
    double fs = 100;
    double pArrival = -1, sArrival = -1;  // 秒（地震以外は -1）
};

SynthSignal synthesize(const SynthParams &p);

const char *synthKindName(SynthKind k);

}  // namespace qs
