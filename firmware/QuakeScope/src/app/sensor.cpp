#include "sensor.h"

namespace app {

bool Mpu6050::write(uint8_t reg, uint8_t v) {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->write(v);
    return wire_->endTransmission() == 0;
}

bool Mpu6050::readBytes(uint8_t reg, uint8_t *buf, int n) {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) return false;
    if (wire_->requestFrom(static_cast<int>(addr_), n) != n) return false;
    for (int i = 0; i < n; ++i) buf[i] = wire_->read();
    return true;
}

bool Mpu6050::begin(TwoWire &wire, uint8_t addr) {
    wire_ = &wire;
    addr_ = addr;
    ok_ = false;
    if (!readBytes(0x75, &who_, 1)) return false;           // WHO_AM_I
    // MPU6050 は 0x68。互換品（MPU6500 など）は別の値を返すが、同じレジスターで動くものは受け入れる
    if (who_ != 0x68 && who_ != 0x70 && who_ != 0x72 && who_ != 0x98) return false;
    write(0x6B, 0x80);                                      // リセット
    delay(100);
    write(0x6B, 0x01);                                      // 時計をジャイロの PLL に
    write(0x6C, 0x07);                                      // ジャイロは使わない（止める）
    write(0x1A, 0x03);                                      // 低域通過 44 Hz（内部 1 kHz）
    write(0x19, 9);                                         // 1 kHz / (1 + 9) = 100 Hz
    write(0x1C, 0x00);                                      // ±2 g
    write(0x23, 0x08);                                      // FIFO に加速度を入れる
    write(0x6A, 0x04);                                      // FIFO を空に
    delay(5);
    write(0x6A, 0x40);                                      // FIFO を有効に
    ok_ = true;
    return true;
}

int Mpu6050::available() {
    uint8_t st = 0, c[2];
    if (!readBytes(0x3A, &st, 1)) { ok_ = false; return -1; }   // INT_STATUS
    if (st & 0x10) {                                            // FIFO があふれた
        ++overflows_;
        write(0x6A, 0x04);
        write(0x6A, 0x40);
        return 0;
    }
    if (!readBytes(0x72, c, 2)) { ok_ = false; return -1; }
    ok_ = true;
    return ((c[0] << 8) | c[1]) / 6;
}

}  // namespace app
