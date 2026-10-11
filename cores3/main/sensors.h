#pragma once
#include <stdint.h>

// 화면 말고는 놀고 있던 부품들: 근접·밝기 센서(LTR-553), 움직임 센서(BMI270), 시계 칩(BM8563).
// 모두 기기 안의 I2C 줄에 있다 → 터치와 같은 태스크(loop)에서만 부른다. (계획: 설계 문서(공개판에는 없음))
namespace sensors {

void begin();

// 근접·밝기 센서가 있는가(없거나 답하지 않으면 그 기능은 쉰다)
bool proxOk();
// 근접(0~2047, 가까울수록 크다)과 밝기(센서의 날 값, 밝을수록 크다)를 읽는다. 0.1초에 한 번이면 된다.
bool readProx(uint16_t& ps, uint16_t& als);

// 움직임 센서가 있는가
bool imuOk();
// 가속도를 읽는다(1000 = 1g, 기기에 붙은 축). 새 값이 없으면 false.
bool readAccel(int16_t& ax, int16_t& ay, int16_t& az);

// 시계 칩의 시각. 칩이 없거나 한 번도 맞춘 적이 없으면(전원이 끊겼던 표시) false.
bool readClock(int& hour, int& minute, int& second);
// 시계를 맞춘다(컴퓨터가 알려 준 시각).
void setClock(int year, int month, int day, int hour, int minute, int second);

}  // namespace sensors
