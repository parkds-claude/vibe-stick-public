#pragma once
#include "events.h"

enum class HostOs : uint8_t { Mac, Windows };

// 컴퓨터 쪽 사정: 운영체제와, (선택) 말하기 앞에 먼저 보낼 "초점 키".
struct HostProfile {
  HostOs   os;
  // 0 이면 쓰지 않는다. 0 이 아니면 말하기(⌘D) 직전에 이 키를 먼저 보낸다.
  // VS Code 에 "Claude 입력창으로 초점 이동" 단축키로 등록해 둔 키여야 한다(D10).
  uint8_t  focusKey;
  // 초점 키를 보낸 뒤 초점이 실제로 옮겨질 때까지 기다리는 시간
  uint32_t focusSettleMs;
};

struct KeyAction {
  enum Type : uint8_t { None, Press, Release, Tap };
  Type    type;
  uint8_t mods;  // HID 수정키 비트
  uint8_t key;   // HID 키 코드
};

namespace hid {
const uint8_t MOD_LCTRL = 0x01;
const uint8_t MOD_LSHIFT = 0x02;
const uint8_t MOD_LGUI  = 0x08;  // 맥의 ⌘
const uint8_t KEY_D     = 0x07;
const uint8_t KEY_ENTER = 0x28;
const uint8_t KEY_ESC   = 0x29;
const uint8_t KEY_GRAVE = 0x35;  // ` (한글 자판의 ₩ 자리)
const uint8_t KEY_DOWN  = 0x51;
const uint8_t KEY_UP    = 0x52;
const uint8_t KEY_F19   = 0x6E;  // 입력기(한/영) 상태와 무관하고 기본 기능이 없는 키 — 초점 키 후보
}  // namespace hid

// 사건을 키 동작으로 바꾼다. 맥/윈도우 차이는 이 함수 안에만 있다.
// (화면에 무엇을 보일지는 scene.h 의 sceneFor 가 정한다.)
KeyAction mapEvent(Ev e, HostOs os);
