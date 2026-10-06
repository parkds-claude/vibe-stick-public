#pragma once
#include <stdint.h>

// Mini JoyC HAT 읽기. 같은 기기로 먼저 검증해 둔 방식(이전 시제품)을 옮겨 왔다.
namespace joyc {

// 조이스틱을 찾으면 true. 없으면 false (나중에 다시 불러도 된다).
bool begin();

// x, y: -128..127 (중앙≈0). btn: 눌림이면 true. 읽기 실패 시 false.
bool read(int8_t& x, int8_t& y, bool& btn);

// 조이스틱에 달린 LED 색. (0,0,0)이면 끔. 지금 색과 같으면 통신하지 않는다
// (매 틱 불러도 된다). 쓰기 실패 시 false.
bool setLed(uint8_t r, uint8_t g, uint8_t b);

}  // namespace joyc
