#include "sensors.h"

#include <M5Unified.h>

namespace {

// ---- 근접·밝기 센서 LTR-553ALS (라이브러리에 없어 직접 읽는다) ----
const uint8_t LTR_ADDR = 0x23;
const uint32_t I2C_HZ = 400000;
const uint8_t REG_ALS_CONTR = 0x80, REG_PS_CONTR = 0x81, REG_PS_N_PULSES = 0x83, REG_PS_MEAS_RATE = 0x84;
const uint8_t REG_PART_ID = 0x86, REG_ALS_DATA = 0x88 /* CH1 낮은·높은, CH0 낮은·높은 */, REG_PS_DATA = 0x8D;
const uint8_t LTR_PART_ID = 0x92;

bool ltrOn = false;

}  // namespace

namespace sensors {

void begin() {
  // 부품 번호를 물어 그 센서가 맞는지 본다(다른 기종이거나 고장이면 조용히 쉰다).
  if (M5.In_I2C.readRegister8(LTR_ADDR, REG_PART_ID, I2C_HZ) == LTR_PART_ID) {
    M5.In_I2C.writeRegister8(LTR_ADDR, REG_PS_N_PULSES, 0x04, I2C_HZ);   // 빛을 네 번 쏜다(한 번보다 멀리 본다)
    M5.In_I2C.writeRegister8(LTR_ADDR, REG_PS_MEAS_RATE, 0x00, I2C_HZ);  // 근접은 0.05초마다 잰다
    M5.In_I2C.writeRegister8(LTR_ADDR, REG_PS_CONTR, 0x03, I2C_HZ);      // 근접 켜기
    M5.In_I2C.writeRegister8(LTR_ADDR, REG_ALS_CONTR, 0x01, I2C_HZ);     // 밝기 켜기(배율 1)
    ltrOn = true;
  }
}

bool proxOk() { return ltrOn; }

bool readProx(uint16_t& ps, uint16_t& als) {
  if (!ltrOn) return false;
  uint8_t a[4] = {0}, p[2] = {0};
  // 밝기는 CH1 → CH0 차례로 네 바이트를 다 읽어야 다음 값으로 넘어간다.
  if (!M5.In_I2C.readRegister(LTR_ADDR, REG_ALS_DATA, a, sizeof(a), I2C_HZ)) return false;
  if (!M5.In_I2C.readRegister(LTR_ADDR, REG_PS_DATA, p, sizeof(p), I2C_HZ)) return false;
  als = (uint16_t)(a[2] | (a[3] << 8));          // CH0: 눈에 보이는 빛 + 적외선
  ps = (uint16_t)(p[0] | ((p[1] & 0x07) << 8));  // 11비트
  return true;
}

bool imuOk() { return M5.Imu.isEnabled(); }

bool readAccel(int16_t& ax, int16_t& ay, int16_t& az) {
  if (!M5.Imu.isEnabled()) return false;
  if (!M5.Imu.update()) return false;
  float x = 0, y = 0, z = 0;
  if (!M5.Imu.getAccel(&x, &y, &z)) return false;
  ax = (int16_t)(x * 1000);
  ay = (int16_t)(y * 1000);
  az = (int16_t)(z * 1000);
  return true;
}

bool readClock(int& hour, int& minute, int& second) {
  if (!M5.Rtc.isEnabled() || M5.Rtc.getVoltLow()) return false;
  m5::rtc_datetime_t dt;
  if (!M5.Rtc.getDateTime(&dt)) return false;
  if (dt.date.year < 2026) return false;  // 한 번도 맞춘 적이 없다
  hour = dt.time.hours;
  minute = dt.time.minutes;
  second = dt.time.seconds;
  return true;
}

void setClock(int year, int month, int day, int hour, int minute, int second) {
  if (!M5.Rtc.isEnabled()) return;
  m5::rtc_datetime_t dt;
  dt.date.year = (int16_t)year;
  dt.date.month = (int8_t)month;
  dt.date.date = (int8_t)day;
  dt.date.weekDay = -1;  // 요일은 쓰지 않는다
  dt.time.hours = (int8_t)hour;
  dt.time.minutes = (int8_t)minute;
  dt.time.seconds = (int8_t)second;
  M5.Rtc.setDateTime(&dt);
}

}  // namespace sensors
