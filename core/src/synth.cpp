#include "synth.h"

#include <algorithm>
#include <cmath>

#include "biquad.h"
#include "intensity.h"

namespace qs {

namespace {

// 再現できる乱数（xorshift）
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 2463534242u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double uniform() { return (next() + 0.5) / 4294967296.0; }
    double gauss() {
        const double u1 = uniform(), u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2 * kPi * u2);
    }
};

// 帯域を絞った雑音（lo〜hi Hz）
std::vector<double> bandNoise(Rng &rng, size_t n, double fs, double lo, double hi) {
    double cl[5], ch[5];
    butterHighpass(lo, fs, ch);
    butterLowpass(hi, fs, cl);
    Biquad h1, h2, l1, l2;
    h1.set(ch); h2.set(ch); l1.set(cl); l2.set(cl);
    std::vector<double> out(n);
    // 立ち上がりの過渡を捨てるため、先に 2 秒ぶん回す
    const size_t warm = static_cast<size_t>(2 * fs);
    for (size_t i = 0; i < n + warm; ++i) {
        double v = l2.process(l1.process(h2.process(h1.process(rng.gauss()))));
        if (i >= warm) out[i - warm] = v;
    }
    double ss = 0;
    for (double v : out) ss += v * v;
    const double rms = std::sqrt(ss / std::max<size_t>(n, 1));
    if (rms > 0) for (double &v : out) v /= rms;
    return out;
}

double smoothstep(double t) { t = std::min(1.0, std::max(0.0, t)); return t * t * (3 - 2 * t); }

}  // namespace

const char *synthKindName(SynthKind k) {
    switch (k) {
        case SynthKind::Quiet: return "静かな部屋";
        case SynthKind::NearQuake: return "近くの地震";
        case SynthKind::FarQuake: return "遠くの地震";
        case SynthKind::DoorSlam: return "ドアを閉めた衝撃";
        case SynthKind::Footsteps: return "足音";
        case SynthKind::Truck: return "トラックの振動";
    }
    return "";
}

SynthSignal synthesize(const SynthParams &p) {
    SynthSignal out;
    out.fs = 100;
    const double fs = out.fs;
    const size_t n = static_cast<size_t>(p.seconds * fs);
    Rng rng(p.seed * 2654435761u + static_cast<uint32_t>(p.kind) * 97u + 1u);
    std::vector<double> x(n, 0), y(n, 0), z(n, 0);

    const bool quake = p.kind == SynthKind::NearQuake || p.kind == SynthKind::FarQuake;
    if (quake) {
        const bool nearq = p.kind == SynthKind::NearQuake;
        const double tsp = nearq ? 2.5 + rng.uniform() : 9 + 4 * rng.uniform();   // S-P 時間
        const double coda = nearq ? 7 : 16;                                        // 尾部の減衰（秒）
        const double tp = p.onset, ts = p.onset + tsp;
        out.pArrival = tp;
        out.sArrival = ts;
        // P 波: 高めの周波数・上下動が大きい
        auto pz = bandNoise(rng, n, fs, nearq ? 2.0 : 1.0, nearq ? 12.0 : 6.0);
        auto px = bandNoise(rng, n, fs, nearq ? 2.0 : 1.0, nearq ? 12.0 : 6.0);
        auto py = bandNoise(rng, n, fs, nearq ? 2.0 : 1.0, nearq ? 12.0 : 6.0);
        // S 波: 低めの周波数・水平動が大きい
        auto sx = bandNoise(rng, n, fs, nearq ? 0.7 : 0.3, nearq ? 6.0 : 3.0);
        auto sy = bandNoise(rng, n, fs, nearq ? 0.7 : 0.3, nearq ? 6.0 : 3.0);
        auto sz = bandNoise(rng, n, fs, nearq ? 0.7 : 0.3, nearq ? 6.0 : 3.0);
        const double pAmp = nearq ? 0.28 : 0.18;  // S 波に対する P 波の大きさ
        for (size_t i = 0; i < n; ++i) {
            const double t = i / fs;
            double ep = 0, es = 0;
            if (t >= tp) {
                ep = smoothstep((t - tp) / 0.6);
                if (t >= ts) ep *= std::exp(-(t - ts) / 2.0);   // S 波が来たら P 波は埋もれる
            }
            if (t >= ts) {
                es = smoothstep((t - ts) / (nearq ? 1.0 : 3.0));
                const double peakEnd = ts + (nearq ? 3.0 : 8.0);
                if (t > peakEnd) es *= std::exp(-(t - peakEnd) / coda);
            }
            x[i] = pAmp * ep * 0.5 * px[i] + es * sx[i];
            y[i] = pAmp * ep * 0.5 * py[i] + es * 0.9 * sy[i];
            z[i] = pAmp * ep * 1.0 * pz[i] + es * 0.45 * sz[i];
        }
        // 目標の計測震度になるように、全体の大きさを合わせる
        std::vector<float> fx(x.begin(), x.end()), fy(y.begin(), y.end()), fz(z.begin(), z.end());
        const ExactResult r = computeJmaIntensity(fx.data(), fy.data(), fz.data(), n, fs);
        const double k = std::pow(10.0, (p.targetIntensity + 0.005 - r.raw) / 2.0);
        for (size_t i = 0; i < n; ++i) { x[i] *= k; y[i] *= k; z[i] *= k; }
    } else if (p.kind == SynthKind::DoorSlam) {
        auto hf = bandNoise(rng, n, fs, 15, 40);
        for (size_t i = 0; i < n; ++i) {
            const double t = i / fs - p.onset;
            if (t < 0) continue;
            const double e = 60 * std::exp(-t / 0.06) + 20 * (t > 0.35 ? std::exp(-(t - 0.35) / 0.05) : 0);
            x[i] = e * hf[i] * 0.6; y[i] = e * hf[(i + 37) % n] * 0.4; z[i] = e * hf[(i + 71) % n];
        }
    } else if (p.kind == SynthKind::Footsteps) {
        auto hf = bandNoise(rng, n, fs, 10, 30);
        for (size_t i = 0; i < n; ++i) {
            const double t = i / fs - p.onset;
            if (t < 0 || t > 8) continue;
            const double ph = std::fmod(t, 0.6);
            const double e = 12 * std::exp(-ph / 0.05);
            z[i] = e * hf[i]; x[i] = 0.3 * e * hf[(i + 11) % n]; y[i] = 0.3 * e * hf[(i + 23) % n];
        }
    } else if (p.kind == SynthKind::Truck) {
        auto hi = bandNoise(rng, n, fs, 12, 20);
        auto lo = bandNoise(rng, n, fs, 2, 4);
        for (size_t i = 0; i < n; ++i) {
            const double t = i / fs - p.onset;
            if (t < 0 || t > 16) continue;
            const double e = std::sin(kPi * t / 16);
            z[i] = e * (2.5 * hi[i] + 0.3 * lo[i]);
            x[i] = e * (1.2 * hi[(i + 5) % n] + 0.25 * lo[(i + 9) % n]);
            y[i] = e * (1.0 * hi[(i + 13) % n] + 0.2 * lo[(i + 17) % n]);
        }
    }

    out.x.resize(n); out.y.resize(n); out.z.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out.x[i] = static_cast<float>(x[i] + p.noiseGal * rng.gauss());
        out.y[i] = static_cast<float>(y[i] + p.noiseGal * rng.gauss());
        out.z[i] = static_cast<float>(z[i] + p.gravityGal + p.noiseGal * rng.gauss());
    }
    return out;
}

}  // namespace qs
