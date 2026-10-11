#pragma once
#include <stdint.h>

#include "logic/clock.h"

// D69 누름을 받지 않는 뜸. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//   언제: 기기가 막 켜졌을 때 · 연결이 막 쓸 수 있게 됐거나 길(선 ↔ 블루투스)이 바뀌었을 때 · 꺼진 화면을
//         누름으로 깨웠을 때. 그런 때의 누름은 "지금 화면을 보고 한 것"이 아니다.
//   까닭: 펌웨어를 올린 직후, 사용자가 `Update firmware?` 에 답하려고 잇달아 누른 ✓ 가 재시작한
//         기기에서 Enter 로 세 번 맥에 나갔다(켜진 지 2.3 · 2.9 · 3.4초). Enter 는 Claude 의 허락 창을 승인할 수 있다.
//         연결되기 전에 닿은 손가락을 연결된 뒤에 떼어도 같은 일이 난다(확인은 뗄 때 나간다 — D12).
//   규칙: 뜸을 걸면(arm) holdMs 가 지나고, 그 뒤 화면에 아무것도 닿지 않은 채 calmMs 가 이어져야 풀린다.
//         닫혀 있는 동안 main 은 누름을 사건으로 만들지 않는다(InputLogic::reset, 사건 버림).
//   비용: 켜고 3초, 연결이 바뀌고 0.5초는 버튼이 듣지 않는다. 잇달아 두드리면 손을 뗄 때까지(길어야 STUCK_MS 더) 듣지 않는다.
//   탈출구: main.cpp 에서 arm 을 부르지 않으면 앞선 판이다.
namespace touchui {

class QuietGate {
 public:
  // 풀릴 때가 됐는데도 화면에 무언가 계속 닿아 있으면(쥔 손의 살이 가장자리에 닿음 · 터치 칩의 헛것) 이만큼 더
  // 기다린 뒤에는 그냥 연다. 그때 닿아 있던 누름은 닫힌 동안 무효가 되어 뗄 때 키가 나가지 않고, 새 누름만 받는다.
  // 이것이 없으면 그런 접촉이 있는 동안 버튼이 끝없이 듣지 않는다(독립 검토).
  static const uint32_t STUCK_MS = 4000;
  // 뜸을 건다. 이미 걸려 있으면 더 긴 쪽을 따른다.
  void arm(uint32_t nowMs, uint32_t holdMs, uint32_t calmMs) {
    const uint32_t until = nowMs + holdMs;
    if (!closed_ || !reachedMs(holdUntilMs_, until)) holdUntilMs_ = until;  // 더 늦은 마감
    if (!closed_ || calmMs > calmMs_) calmMs_ = calmMs;
    if (!closed_) calmSinceMs_ = nowMs;
    closed_ = true;
  }

  // 매 틱(터치를 읽기 전)에 부른다. touching: 직전 틱까지 화면에 손가락이 닿아 있었는가.
  // 반환: 닫혀 있는가(= 이번 틱의 누름을 버려야 한다)
  bool closed(uint32_t nowMs, bool touching) {
    if (!closed_) return false;
    if (touching) calmSinceMs_ = nowMs;  // 닿아 있는 동안에는 "조용한 시간"을 다시 센다
    if (reachedMs(nowMs, holdUntilMs_)) {
      const bool calm = !touching && elapsedMs(nowMs, calmSinceMs_, calmMs_);
      const bool stuck = reachedMs(nowMs, holdUntilMs_ + calmMs_ + STUCK_MS);  // 너무 오래 닫혀 있었다
      if (calm || stuck) closed_ = false;
    }
    return closed_;
  }

  bool isClosed() const { return closed_; }

 private:
  bool     closed_ = false;
  uint32_t holdUntilMs_ = 0;   // 적어도 이때까지는 닫혀 있다
  uint32_t calmMs_ = 0;        // 그 뒤로 이만큼 아무것도 닿지 않아야 풀린다
  uint32_t calmSinceMs_ = 0;   // 마지막으로 손가락이 닿아 있던 시각
};

// D17 을 잇는다: 취소(Esc)는 gapMs 에 한 번만 내보낸다 — 버튼의 취소와 흔들기의 취소(D61)를 함께 센다.
//   까닭: Claude Code 는 빈 입력창에서 Esc 두 번(0.8초 안)을 "되감기 창 열기"로 받고, 이어진 Enter 가 Claude 가
//         고친 파일을 되돌린다. 버튼의 취소는 입력 판정(InputLogic)이 1초에 한 번으로 막지만, 흔들기의 취소는
//         그 판정을 거치지 않고 끼어들어 ✕ 를 누른 직후의 흔들림이 두 번째 Esc 가 됐다(독립 검토).
class CancelGuard {
 public:
  explicit CancelGuard(uint32_t gapMs) : gapMs_(gapMs) {}
  // 지금 취소를 내보내도 되는가. 된다면 그 시각을 적는다.
  bool allow(uint32_t nowMs) {
    if (seen_ && !elapsedMs(nowMs, lastMs_, gapMs_)) return false;
    seen_ = true;
    lastMs_ = nowMs;
    return true;
  }

 private:
  const uint32_t gapMs_;
  uint32_t       lastMs_ = 0;
  bool           seen_ = false;
};

}  // namespace touchui
