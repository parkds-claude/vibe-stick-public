#pragma once
#include <stdint.h>

#include "logic/clock.h"

// D68 USB 선 너머에 컴퓨터가 아직 있는가: 컴퓨터가 보내는 박자(SOF)의 번호가 바뀌는지로 안다.
// 하드웨어를 모른다(컴퓨터에서 시험 가능).
//   까닭: 배터리로 켜져 있는 이 기기는 선을 뽑아도 USB 쪽이 "뽑힘"도 "잠듦"도 알리지 않았다 — 계속 "연결됨"으로
//         남아, 버튼의 키를 뽑힌 USB 로 보내려다 실패했고 블루투스로 길을 바꾸지 않았다(기기 속 기록:
//         선을 뽑은 6분 동안 연결 상태의 변화가 한 줄도 없고, Talk 11번이 모두 "보내지 못함").
//   방법: 컴퓨터는 연결돼 있는 동안 1ms 마다 박자를 보내고, USB 칩은 마지막으로 받은 박자의 번호를 들고 있다.
//         그 번호가 goneAfterMs 동안 그대로면 컴퓨터가 없다고 본다. 번호가 다시 바뀌면 곧바로 있다고 본다.
//   비용: 매 틱 레지스터 하나를 읽는다. 컴퓨터가 잠들 때(박자가 멎는다)도 "없음"이 된다 — 그때는 어차피 쓸 수 없다.
//   탈출구: usb_dev.cpp 의 link() 에서 pulse 를 보지 않으면 앞선 판이다.
namespace touchui {

class HostPulse {
 public:
  explicit HostPulse(uint32_t goneAfterMs) : goneAfterMs_(goneAfterMs) {}

  // 한 틱에 한 번. frame: USB 칩이 들고 있는 "마지막으로 받은 박자의 번호"
  void feed(uint16_t frame, uint32_t nowMs) {
    if (seen_ && frame == last_) return;
    last_ = frame;
    changedAtMs_ = nowMs;
    seen_ = true;
  }

  // 박자가 최근에 바뀌었는가(= 컴퓨터가 선 너머에 있다)
  bool alive(uint32_t nowMs) const { return seen_ && !elapsedMs(nowMs, changedAtMs_, goneAfterMs_); }

 private:
  const uint32_t goneAfterMs_;
  uint16_t       last_ = 0;
  uint32_t       changedAtMs_ = 0;
  bool           seen_ = false;
};

}  // namespace touchui
