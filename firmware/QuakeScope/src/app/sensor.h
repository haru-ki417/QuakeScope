// 加速度センサー MPU6050（GY-521 などのモジュール）。
// センサーの中の FIFO に 100 Hz で値をためさせ、まとめて読む（ほかの処理で読むのが遅れても取りこぼさない）。
#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace app {

class Mpu6050 {
public:
    bool begin(TwoWire &wire, uint8_t addr = 0x68);
    // たまっている値を読み、1 サンプルごとに fn(x, y, z)（gal）を呼ぶ。読んだ数を返す
    template <typename F>
    int read(F fn) {
        int n = available();
        if (n < 0) return -1;
        int done = 0;
        while (n > 0) {
            const int k = n > kChunk ? kChunk : n;
            uint8_t buf[kChunk * 6];
            if (!readBytes(0x74, buf, k * 6)) return done;
            for (int i = 0; i < k; ++i) {
                const int16_t ax = (int16_t)(buf[i * 6] << 8 | buf[i * 6 + 1]);
                const int16_t ay = (int16_t)(buf[i * 6 + 2] << 8 | buf[i * 6 + 3]);
                const int16_t az = (int16_t)(buf[i * 6 + 4] << 8 | buf[i * 6 + 5]);
                fn(ax * kGalPerLsb, ay * kGalPerLsb, az * kGalPerLsb);
            }
            n -= k;
            done += k;
        }
        return done;
    }
    uint8_t whoAmI() const { return who_; }
    uint32_t overflows() const { return overflows_; }
    bool ok() const { return ok_; }

private:
    static constexpr int kChunk = 20;                         // 1 回の I²C で読む数（120 バイト）
    static constexpr double kGalPerLsb = 980.665 / 16384.0;   // ±2 g のとき
    int available();
    bool write(uint8_t reg, uint8_t v);
    bool readBytes(uint8_t reg, uint8_t *buf, int n);
    TwoWire *wire_ = nullptr;
    uint8_t addr_ = 0x68, who_ = 0;
    bool ok_ = false;
    uint32_t overflows_ = 0;
};

}  // namespace app
