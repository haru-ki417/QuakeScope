// 揺れの波形の記録。揺れ始めの少し前から、重力を引いた 3 軸の加速度を 0.1 gal 単位の整数で残す。
// 容量は固定（マイコンでも動的に確保しない）。
#pragma once

#include <cstdint>

namespace qs {

template <int kCapacity>
class WaveRecorder {
public:
    // 平常時も常に入れておく（揺れ始めの前の部分を残すため）
    void push(double x, double y, double z) {
        if (recording_ && len_ >= kCapacity) return;  // いっぱいになったら、始まりの部分を残して止める
        int16_t *d = buf_[head_];
        d[0] = clamp(x); d[1] = clamp(y); d[2] = clamp(z);
        head_ = (head_ + 1) % kCapacity;
        if (filled_ < kCapacity) ++filled_;
        if (recording_) ++len_;
    }
    // 揺れ始め: pre サンプル前から記録する
    void begin(int pre) {
        if (pre > filled_) pre = filled_;
        if (pre > kCapacity / 2) pre = kCapacity / 2;
        start_ = (head_ - pre + kCapacity) % kCapacity;
        len_ = pre;
        recording_ = true;
    }
    // 揺れの終わり。記録の長さ（サンプル数）を返す
    int end() { recording_ = false; return len_; }
    bool recording() const { return recording_; }
    int length() const { return len_; }
    // 記録の i 番目（0.1 gal 単位）
    const int16_t *at(int i) const { return buf_[(start_ + i) % kCapacity]; }
    static constexpr int capacity() { return kCapacity; }

private:
    static int16_t clamp(double v) {
        double s = v * 10.0;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        return static_cast<int16_t>(s >= 0 ? s + 0.5 : s - 0.5);
    }
    int16_t buf_[kCapacity][3] = {};
    int head_ = 0, filled_ = 0, start_ = 0, len_ = 0;
    bool recording_ = false;
};

}  // namespace qs
