// imu_mpu6050.h - minimal MPU6050 driver over I2C (no external library needed).
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "detectors.h"   // ImuSample

class Mpu6050 {
 public:
  static const uint8_t ADDR = 0x68;   // AD0 pin to GND

  bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    if (!writeReg(0x6B, 0x01)) return false;   // wake up, clock = gyro X PLL
    delay(10);
    writeReg(0x1A, 0x03);                      // DLPF ~44 Hz
    writeReg(0x19, 0x09);                      // sample-rate divider: 1 kHz / (1+9) = 100 Hz
    writeReg(0x1B, 0x08);                      // gyro  +-500 deg/s  (65.5 LSB per deg/s)
    writeReg(0x1C, 0x18);                      // accel +-16 g       (2048 LSB per g)
    ok_ = true;
    return true;
  }

  bool read(ImuSample& s) {
    if (!ok_) return false;
    Wire.beginTransmission(ADDR);
    Wire.write(0x3B);                          // ACCEL_XOUT_H
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)14, (uint8_t) true) != 14) return false;
    int16_t v[7];
    for (int i = 0; i < 7; i++) {              // read hi and lo separately: evaluation order matters
      uint8_t hi = Wire.read();
      uint8_t lo = Wire.read();
      v[i] = (int16_t)((hi << 8) | lo);
    }
    s.ax = v[0] / 2048.0f;  s.ay = v[1] / 2048.0f;  s.az = v[2] / 2048.0f;
    /* v[3] = temperature, unused */
    s.gx = v[4] / 65.5f;    s.gy = v[5] / 65.5f;    s.gz = v[6] / 65.5f;
    return true;
  }

 private:
  bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
  }
  bool ok_ = false;
};
