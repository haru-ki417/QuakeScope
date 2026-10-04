#include "intensity.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace qs {

Shindo shindoFromIntensity(double i) {
    if (i < 0.5) return Shindo::S0;
    if (i < 1.5) return Shindo::S1;
    if (i < 2.5) return Shindo::S2;
    if (i < 3.5) return Shindo::S3;
    if (i < 4.5) return Shindo::S4;
    if (i < 5.0) return Shindo::S5Lower;
    if (i < 5.5) return Shindo::S5Upper;
    if (i < 6.0) return Shindo::S6Lower;
    if (i < 6.5) return Shindo::S6Upper;
    return Shindo::S7;
}

const char *shindoLabel(Shindo s) {
    static const char *const kLabels[] = {"0", "1", "2", "3", "4", "5弱", "5強", "6弱", "6強", "7"};
    return kLabels[static_cast<int>(s)];
}

double intensityFromAccel(double a03) {
    if (a03 <= 1e-6) return -5.0;  // 揺れがない
    return 2.0 * std::log10(a03) + 0.94;
}

double roundIntensity(double i) {
    // 小数第 3 位を四捨五入（→ 小数第 2 位まで）してから、小数第 2 位を切り捨てる
    const double r2 = std::round(i * 100.0) / 100.0;
    return std::floor(r2 * 10.0 + 1e-9) / 10.0;
}

double jmaFilterGain(double f) {
    if (f <= 0) return 0;
    const double x = f / 10.0, x2 = x * x;
    const double period = 1.0 / std::sqrt(f);
    const double hc = 1.0 / std::sqrt(1 + 0.694 * x2 + 0.241 * x2 * x2 + 0.0557 * x2 * x2 * x2 +
                                      0.009664 * std::pow(x2, 4) + 0.00134 * std::pow(x2, 5) + 0.000155 * std::pow(x2, 6));
    const double lc = std::sqrt(std::max(0.0, 1 - std::exp(-std::pow(f / 0.5, 3))));
    return period * hc * lc;
}

// ------------------------------------------------------------ 計測震度（FFT）

namespace {

using cd = std::complex<double>;

void fft(std::vector<cd> &a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / static_cast<double>(len) * (inverse ? 1 : -1);
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto &v : a) v /= static_cast<double>(n);
}

// 1 成分にフィルターをかけ、結果の 2 乗を acc に足す
void filterComponent(const float *s, size_t n, double fs, size_t nfft, std::vector<double> &acc) {
    double mean = 0;
    for (size_t i = 0; i < n; ++i) mean += s[i];
    mean /= static_cast<double>(n);
    std::vector<cd> a(nfft, cd(0));
    for (size_t i = 0; i < n; ++i) a[i] = cd(s[i] - mean, 0);
    fft(a, false);
    for (size_t k = 0; k <= nfft / 2; ++k) {
        const double g = jmaFilterGain(fs * static_cast<double>(k) / static_cast<double>(nfft));
        a[k] *= g;
        if (k != 0 && k != nfft / 2) a[nfft - k] *= g;
    }
    fft(a, true);
    for (size_t i = 0; i < n; ++i) acc[i] += a[i].real() * a[i].real();
}

}  // namespace

ExactResult computeJmaIntensity(const float *x, const float *y, const float *z, size_t n, double fs) {
    ExactResult r;
    if (n < 4) return r;
    // 端の影響（循環）を避けるため、2 倍以上の長さの 2 のべき乗にする
    size_t nfft = 1;
    while (nfft < n * 2) nfft <<= 1;
    std::vector<double> acc(n, 0.0);
    filterComponent(x, n, fs, nfft, acc);
    filterComponent(y, n, fs, nfft, acc);
    filterComponent(z, n, fs, nfft, acc);
    for (auto &v : acc) v = std::sqrt(v);

    const size_t k = static_cast<size_t>(std::lround(0.3 * fs));
    const size_t idx = std::min(n, std::max<size_t>(k, 1)) - 1;
    std::nth_element(acc.begin(), acc.begin() + static_cast<long>(idx), acc.end(), std::greater<double>());
    r.a03 = acc[idx];
    r.raw = intensityFromAccel(r.a03);
    r.intensity = roundIntensity(r.raw);
    r.shindo = shindoFromIntensity(r.intensity);

    // 最大加速度（平均を除いた 3 成分の合成）
    double mx = 0, my = 0, mz = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; mz += z[i]; }
    mx /= n; my /= n; mz /= n;
    for (size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mx, dy = y[i] - my, dz = z[i] - mz;
        r.pga = std::max(r.pga, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return r;
}

// ------------------------------------------------------------ リアルタイム震度

template <int kWindow>
float ExceedWindow<kWindow>::a03() const {
    if (count_ == 0) return 0;
    const int k = std::min(kExceedSamples, count_) - 1;
    std::copy(buf_, buf_ + count_, work_);
    std::nth_element(work_, work_ + k, work_ + count_, std::greater<float>());
    return work_[k];
}

template class ExceedWindow<500>;

RealtimeIntensity::RealtimeIntensity() {
    fx_.set(kJmaSos);
    fy_.set(kJmaSos);
    fz_.set(kJmaSos);
}

void RealtimeIntensity::reset() {
    fx_.reset(); fy_.reset(); fz_.reset();
    short_.reset();
    lastMag_ = 0;
}

void RealtimeIntensity::push(double x, double y, double z) {
    const double fx = fx_.process(x), fy = fy_.process(y), fz = fz_.process(z);
    lastMag_ = std::sqrt(fx * fx + fy * fy + fz * fz);
    short_.push(static_cast<float>(lastMag_));
}

double RealtimeIntensity::intensityShort() const { return intensityFromAccel(short_.a03()); }

double realtimeIntensityOf(const float *x, const float *y, const float *z, size_t n) {
    // 重力などの一定の成分は、最初の 1 秒の平均を引いてから入れる
    double m[3] = {0, 0, 0};
    const size_t k = n < 100 ? n : 100;
    for (size_t i = 0; i < k; ++i) { m[0] += x[i]; m[1] += y[i]; m[2] += z[i]; }
    for (double &v : m) v /= (k ? k : 1);
    RealtimeIntensity ri;
    std::vector<float> mags(n);
    for (size_t i = 0; i < n; ++i) {
        ri.push(x[i] - m[0], y[i] - m[1], z[i] - m[2]);
        mags[i] = static_cast<float>(ri.filteredMagnitude());
    }
    if (n == 0) return -5;
    const size_t idx = std::min<size_t>(kExceedSamples, n) - 1;
    std::nth_element(mags.begin(), mags.begin() + static_cast<long>(idx), mags.end(), std::greater<float>());
    return intensityFromAccel(mags[idx]);
}

}  // namespace qs
