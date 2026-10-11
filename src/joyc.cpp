#include "joyc.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

// Mini JoyC HAT (STM32F030): 주소 0x54, SDA=G0, SCL=G26
//   위치: reg 0x20(X), 0x21(Y) 각 1바이트 int8
//   버튼: reg 0x30 1바이트, 0 = 눌림
//   LED: reg 0x40 에 R,G,B 3바이트
//   (reg 0x50 은 보정값 저장 — 영구 변경이라 쓰지 않는다)
//   읽기: write(reg)+STOP 뒤 requestFrom (repeated start 미지원)
const uint8_t ADDR     = 0x54;
const uint8_t REG_X    = 0x20;
const uint8_t REG_Y    = 0x21;
const uint8_t REG_BTN  = 0x30;
const uint8_t REG_LED  = 0x40;
const uint8_t REG_FW   = 0xFE;
const int     PIN_SDA  = 0;
const int     PIN_SCL  = 26;

// 마지막으로 써 넣은 LED 색(0x00RRGGBB). begin() 이 끄면서 0 이 된다.
uint32_t ledColor = 0;

bool readReg(uint8_t reg, uint8_t& out) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)ADDR, 1) != 1) return false;
  out = Wire.read();
  return true;
}

// I2C 버스 풀기: 슬레이브가 SDA 를 물고 있으면 SCL 을 수동으로 흔들어 풀어준다.
// SDA(G0)는 ESP32 부팅 설정 핀이라, 고착되면 부팅까지 꼬인다.
void busClear() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  delay(2);
  if (digitalRead(PIN_SDA) == HIGH) return;
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SCL, HIGH);
  for (int i = 0; i < 16 && digitalRead(PIN_SDA) == LOW; i++) {
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(10);
  }
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);  // STOP: SCL high 에서 SDA low→high
  digitalWrite(PIN_SDA, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_SDA, HIGH);
  delayMicroseconds(10);
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  delay(2);
}

}  // namespace

namespace joyc {

bool begin() {
  Wire.end();
  busClear();
  if (!Wire.begin(PIN_SDA, PIN_SCL, 100000)) return false;
  // D31: I2C 한 번의 시간 제한을 10ms 로(기본 50ms). 케이블 접촉 불량 등으로 버스가 막히면
  //   read() 는 레지스터 3개를 따로 읽으므로 기본값으로는 loop 가 최대 150ms 멈춘다(버튼·키 지연).
  //   이유: 공개 전 독립 검토 지적. 정상 통신은 1ms 안에 끝나 10ms 로 충분하다.
  //   비용: 아주 느린 버스에서는 읽기 실패가 늘 수 있다(JoyFilter 가 2회까지 버틴다).
  //   탈출구: 이 줄을 지우면 기본 50ms. 기기에서 아직 실측하지 않음 — 굽고 NO STICK 이 자주 뜨면 20 으로.
  Wire.setTimeOut(10);
  delay(20);
  // 빈 write 의 ACK 는 SDA 고착 시 가짜로 성공하므로,
  // 펌웨어 버전 레지스터가 실제로 읽혀야 "있음"으로 본다.
  uint8_t fw = 0;
  if (!readReg(REG_FW, fw)) return false;
  ledColor = 0xFFFFFFFF;  // 전원을 켠 직후의 LED 상태를 모른다 → 아래에서 반드시 써 넣게
  setLed(0, 0, 0);        // 꺼 둔다(배터리)
  return true;
}

bool read(int8_t& x, int8_t& y, bool& btn) {
  uint8_t vx = 0, vy = 0, vb = 1;
  if (!readReg(REG_X, vx)) return false;
  if (!readReg(REG_Y, vy)) return false;
  if (!readReg(REG_BTN, vb)) return false;
  x = (int8_t)vx;
  y = (int8_t)vy;
  btn = (vb == 0);
  return true;
}

bool setLed(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (color == ledColor) return true;
  Wire.beginTransmission(ADDR);
  Wire.write(REG_LED);
  Wire.write(r);
  Wire.write(g);
  Wire.write(b);
  if (Wire.endTransmission() != 0) return false;
  ledColor = color;
  return true;
}

}  // namespace joyc
