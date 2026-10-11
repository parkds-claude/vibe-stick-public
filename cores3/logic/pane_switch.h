#pragma once
#include <stdint.h>

#include "logic/clock.h"
#include "logic/events.h"

// D58 창 넘기기 버튼(Back·Next)의 두 가지 뜻: 짧게 누르면 같은 창 안의 옆 패널로, 길게 누르면 다른 창으로.
//   이유: VS Code 창 하나에 Claude 패널을 여럿 나란히 두면 "다른 창으로"(⌘`)는 갈 곳이 없어 아무 일도
//         일어나지 않는다(사용자). 패널 사이를 옮기는 일이 더 잦으므로 짧게 누름에 둔다.
//   비용: 패널 넘기기는 손을 뗄 때 나간다 — 길게 누를지를 기다려야 하기 때문이다. 그리고 컴퓨터의 VS Code 에
//         단축키 두 줄이 있어야 한다(F17·F18 → 이전·다음 편집기 묶음. docs/사용법-cores3.md).
//   탈출구: HOLD_MS 를 0 으로 하면 예전처럼 누르는 순간 "다른 창으로"만 보낸다.
// 하드웨어를 모른다(컴퓨터에서 시험).
namespace touchui {

enum class PaneAction : uint8_t { None, PanePrev, PaneNext, WindowPrev, WindowNext };

class PaneSwitch {
 public:
  static const uint32_t HOLD_MS = 600;  // 이만큼 누르고 있으면 "다른 창으로"

  // 입력 판정이 낸 사건을 받는다. 창 넘기기 사건(버튼이 눌린 순간에 나온다)이면 맡아 두고 true —
  // 무엇을 보낼지는 poll 이 정한다. 다른 사건이면 false(부르는 쪽이 평소대로 처리한다).
  bool onEvent(Ev e, uint32_t nowMs) {
    if (e != Ev::WindowNext && e != Ev::WindowPrev) return false;
    pending_ = true;
    next_ = (e == Ev::WindowNext);
    sinceMs_ = nowMs;
    return true;
  }

  // 매 틱 부른다. stillPressed: 그 버튼이 아직 눌려 있는가(waitingNext() 가 가리키는 쪽).
  // 돌려주는 값 = 지금 보낼 것. 한 번의 누름에 한 번만 나온다.
  PaneAction poll(uint32_t nowMs, bool stillPressed) {
    if (!pending_) return PaneAction::None;
    if (HOLD_MS == 0 || (stillPressed && reachedMs(nowMs, sinceMs_ + HOLD_MS))) {
      pending_ = false;  // 길게: 누르고 있는 채로 한 번만
      return next_ ? PaneAction::WindowNext : PaneAction::WindowPrev;
    }
    if (!stillPressed) {
      pending_ = false;  // 짧게: 뗀 순간
      return next_ ? PaneAction::PaneNext : PaneAction::PanePrev;
    }
    return PaneAction::None;
  }

  bool waiting() const { return pending_; }
  bool waitingNext() const { return next_; }
  // 기다리던 누름을 버린다(연결이 끊기거나, 펌웨어 물음이 떠서 누름이 무효가 될 때).
  void reset() { pending_ = false; }

 private:
  bool     pending_ = false;
  bool     next_ = false;
  uint32_t sinceMs_ = 0;
};

}  // namespace touchui
