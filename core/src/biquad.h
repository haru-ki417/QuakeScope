// 2 次の IIR フィルター（直接型 II 転置）と、その直列つなぎ。
// 係数の精度が必要な低い周波数のフィルターがあるので、計算は double で行う。
#pragma once

#include <cmath>

namespace qs {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSqrtHalf = 0.70710678118654752440;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void set(const double c[5]) { b0 = c[0]; b1 = c[1]; b2 = c[2]; a1 = c[3]; a2 = c[4]; }
    void reset() { z1 = z2 = 0; }
    double process(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

template <int N>
struct SosCascade {
    Biquad s[N];
    void set(const double (*c)[5]) { for (int i = 0; i < N; ++i) s[i].set(c[i]); }
    void reset() { for (int i = 0; i < N; ++i) s[i].reset(); }
    double process(double x) {
        for (int i = 0; i < N; ++i) x = s[i].process(x);
        return x;
    }
};

// 2 次のバターワース（双一次変換・周波数のゆがみを補正）
inline void butterLowpass(double fc, double fs, double out[5]) {
    const double k = std::tan(kPi * fc / fs), q = kSqrtHalf;
    const double n = 1.0 / (1.0 + k / q + k * k);
    out[0] = k * k * n; out[1] = 2 * out[0]; out[2] = out[0];
    out[3] = 2 * (k * k - 1) * n; out[4] = (1 - k / q + k * k) * n;
}
inline void butterHighpass(double fc, double fs, double out[5]) {
    const double k = std::tan(kPi * fc / fs), q = kSqrtHalf;
    const double n = 1.0 / (1.0 + k / q + k * k);
    out[0] = n; out[1] = -2 * n; out[2] = n;
    out[3] = 2 * (k * k - 1) * n; out[4] = (1 - k / q + k * k) * n;
}

}  // namespace qs
