#pragma once
#include <stdint.h>

// 기기에서 일어난 "사건". input_logic 이 만들고 keymap 이 키로 바꾼다.
enum class Ev : uint8_t {
  None = 0,
  TalkDown,    // ("누르는 동안" 방식) 조이스틱 누름 시작
  TalkUp,      // ("누르는 동안" 방식) 조이스틱 뗌
  TalkTap,     // 말하기 켜고 끄기(⌘D 한 번 톡): 조이스틱 누름(D19), 조이스틱이 없으면 앞 버튼 길게
  Confirm,     // 앞 큰 버튼
  Cancel,      // 옆 버튼
  Up,          // 조이스틱 위
  Down,        // 조이스틱 아래
  WindowNext,  // 조이스틱 오른쪽: 다음 창으로
  WindowPrev,  // 조이스틱 왼쪽: 이전 창으로
  PairReset,   // 앞 버튼 + 옆 버튼 3초 (키를 보내지 않음)
};

// 한 틱의 입력. 버튼은 그 틱에 읽은 값, 조이스틱은 JoyFilter 가 다듬어 들고 있는 값이다.
struct RawInput {
  bool   joyOk;   // 조이스틱 읽기 성공
  int8_t x;       // -128..127, 중앙≈0, 오른쪽이 +
  int8_t y;       // -128..127, 중앙≈0, 위쪽이 +
  bool   joyBtn;  // 조이스틱 눌림
  bool   btnA;    // 앞 큰 버튼 눌림
  bool   btnB;    // 옆 버튼 눌림
};
