#pragma once
#include "debounce.h"
#include "events.h"

// 조이스틱 읽기 결과를 다듬어 InputLogic 에 넘길 값으로 만든다. 하드웨어를 모른다.
//  - 읽기가 잠깐 실패하면 직전 값으로 버틴다(한 번의 I2C 오류로 말하기가 끊기지 않게).
//  - 연속 실패가 한도를 넘으면 "조이스틱 없음"으로 판정한다.
//  - 누름은 두 번 연속 같은 값이 읽혀야 인정한다(한 번 튄 값 무시).
//  - 방향 보정: 조이스틱을 "화면 위쪽"으로 밀었을 때 y 가 +, 오른쪽으로 밀었을 때 x 가 + 가
//    되도록 축을 바꾸거나 뒤집는다.
class JoyFilter {
 public:
  // swapXY: x 와 y 를 맞바꾼다. invertY·invertX: (바꾼 뒤의) y·x 부호를 뒤집는다.
  explicit JoyFilter(bool swapXY = false, bool invertY = false, bool invertX = false)
      : swapXY_(swapXY), invertY_(invertY), invertX_(invertX) {}

  // 이 횟수까지의 연속 읽기 실패는 직전 값으로 버틴다 (20ms 주기면 40ms)
  static const int MAX_MISSES = 2;

  // 읽기 한 번의 결과(기기에서 읽은 그대로)를 넣는다. readOk 가 false 면 나머지 값은 쓰지 않는다.
  void feed(bool readOk, int8_t rawX, int8_t rawY, bool btn);

  // 현재 값으로 RawInput 의 조이스틱 부분(joyOk, x, y, joyBtn)을 채운다.
  void fill(RawInput& out) const;

  // 연속 읽기 실패가 한도를 넘은 상태. 처음(아직 한 번도 못 읽음)과 reset() 직후는 "아직 모름"이라
  // lost 가 아니다 — 꽂자마자 한 번 실패했다고 5초(JOY_RETRY_MS) 동안 조이스틱을 잃지 않게(D31).
  bool lost() const { return misses_ > MAX_MISSES; }

  // 읽은 값을 잊고 실패 횟수도 0 으로(방향 설정은 그대로 둔다).
  void reset();

 private:
  const bool swapXY_;
  const bool invertY_;
  const bool invertX_;
  void forget();  // 값만 잊는다(실패 횟수는 그대로 → lost 유지)
  bool   valid_     = false;
  int    misses_    = 0;
  int8_t x_         = 0;
  int8_t y_         = 0;
  Debounced btn_;
};
