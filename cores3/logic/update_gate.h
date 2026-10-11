#pragma once
#include <stdint.h>

#include "logic/clock.h"
#include "logic/events.h"

// 펌웨어 물음(화면의 `Update firmware?`)을 묻는 동안(D47) 손가락을 어떻게 받을지 정한다. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
// main 의 한 틱은 이 순서로 쓴다:
//   if (gate.begin(지금, 묻는 중인가, 확인을 받아도 되는가, 화면에 손가락이 있는가)) logic.reset();
//   ... 터치 → 사건 ...
//   사건마다 gate.judge(e) 로 갈래를 정한다.
//
// D51 물음의 경계에 걸친 손가락은 뜻을 알 수 없으므로 받지 않는다.
//   이유: 확인·취소는 뗄 때 나가고(D12), 누름은 닿은 지 몇 틱 뒤에야 인식된다(떨림 거르기).
//         그래서 "그 순간 눌려 있던 버튼을 무효로" 하는 것만으로는 구멍이 남았다(코덱스 검토):
//         - 물음이 뜨기 전부터 누르던 확인을 1.5초 넘겨 떼면 "예"가 됐다.
//         - 물음이 사라지는 순간에 닿은(아직 인식되는 중인) 확인이 Enter 로 컴퓨터에 나갔다.
//   규칙: (가) 확인을 받기 시작하는 순간 화면에 손가락이 있었다면, 손가락을 모두 뗄 때까지 "예"는 없다.
//              취소는 그대로 받는다("아니오"는 언제나 통한다).
//         (나) 물음이 끝나면 QUIET_MS 동안, 그리고 그때 닿아 있던 손가락을 모두 뗄 때까지
//              어떤 누름도 키로 나가지 않는다(화면이 바뀌는 중에 닿은 손가락은 물음에 답하려던 것이다).
//   비용: 물음이 끝난 뒤 0.7초는 기기가 누름을 받지 않는다. 뜨자마자 꾹 누르고 기다린 사람은 한 번
//         뗐다가 다시 눌러야 한다.
//   탈출구: QUIET_MS 를 0 으로, begin() 의 stale_ 를 늘 false 로 두면 예전 동작이다.
//
// D75 "예"는 확인을 HOLD_MS 넘게 꾹 눌렀다 떼야 한다. 평소처럼 톡 친 확인은 "예"가 아니다.
//   이유(보안 점검): 확인(✓)은 Enter 로 하루에도 수십 번 톡 치는 자리다. 같은 컴퓨터의 프로그램이 알림 소리(vibe:note)로
//         사람을 불러 놓고 곧바로 물음(vibe:boot)을 띄우면, 습관대로 친 ✓ 가 "펌웨어를 바꿔도 좋다"가 된다. 화면의
//         글은 영어이고, 내려받기 모드로 들어간 기기에는 그 프로그램이 무엇이든 구울 수 있다(무엇이든 치는 키보드).
//         꾹 누르기는 습관으로 나오지 않는 손짓이라, 물음을 읽지 못해도 "뭔가 다르다"에서 멈추게 된다.
//   규칙: 확인 자리만(다른 자리가 함께 눌리지 않은 채) HOLD_MS 넘게 눌렀다 뗀 누름만 "예"가 될 수 있다. 그 위에
//         앞의 (가)가 그대로 걸린다 — 확인을 받기 시작한 뒤에, 손가락을 모두 뗐다가 새로 누른 것이어야 한다.
//   비용: 펌웨어를 올릴 때 ✓ 를 2초쯤 누르고 있어야 한다. 주머니·가방에서 확인 자리에만 2초 닿았다 떨어지는
//         우연은 여전히 "예"가 된다(물음은 컴퓨터가 로그 포트로 요청해야 뜨고, 선이 뽑히면 거둔다 — main.cpp).
//   탈출구: HOLD_MS 를 0 으로 두면 톡으로 받던 앞선 판이다.
namespace touchui {

enum class UpdateVerdict : uint8_t {
  Pass,     // 평소처럼 키로 내보낸다
  Swallow,  // 키로 내보내지 않는다
  Yes,      // 사람이 "예"라고 답했다 → 내려받기 모드로
  No,       // 사람이 "아니오"라고 답했다 → 물음을 거둔다
};

class UpdateGate {
 public:
  // 손가락이 이만큼(틱) 연속으로 보이지 않아야 "모두 뗐다"로 친다. 손가락을 뗀 뒤 그 누름의 사건
  // (확인·취소)이 나오기까지 터치 3틱 + 버튼 2틱이 걸리므로, 그보다 길어야 한다.
  static const int      LIFT_TICKS = 6;
  static const uint32_t QUIET_MS   = 700;  // 물음이 끝난 뒤 누름을 받지 않는 시간
  static const uint32_t HOLD_MS    = 1500; // "예"가 되려면 확인을 이만큼 넘게 누르고 있어야 한다(D75)

  // 매 틱, 터치를 읽은 뒤(사건을 judge 하기 전)에 부른다: 확인 자리의 누름이 얼마나 이어졌는지 잰다(D75).
  //   okDown: 확인 자리가 눌려 있는가   othersDown: 다른 자리도 눌려 있는가
  void press(uint32_t nowMs, bool okDown, bool othersDown) {
    if (!okDown) {
      okHeld_ = false;  // 뗐다. okLong_ 은 이어 올 확인 사건(judge)이 읽도록 둔다 — 새로 누르면 지워진다
      return;
    }
    if (!okHeld_) {  // 새로 눌렀다
      okHeld_ = true;
      okSinceMs_ = nowMs;
      okSpoiled_ = false;
    }
    if (othersDown) okSpoiled_ = true;  // 다른 자리가 함께 눌린 적이 있다: 이 누름은 "예"가 될 수 없다
    okLastMs_ = nowMs;
    okLong_ = !okSpoiled_ && elapsedMs(nowMs, okSinceMs_, HOLD_MS);
  }

  // 받지 않은 확인의 까닭을 가리는 데 쓴다(기기 속 기록 — "✓ 를 눌렀는데 안 된다"가 무엇 때문인가).
  bool     armed() const { return armed_; }          // 확인을 받는 때인가(물음이 뜨고 1.5초 뒤)
  bool     stale() const { return stale_; }          // 그때 닿아 있던 손가락이 아직 남아 있는가
  bool     okLong() const { return okLong_; }        // 방금의 확인 누름이 HOLD_MS 를 넘겼는가
  bool     okSpoiled() const { return okSpoiled_; }  // 그 누름 도중에 다른 자리가 함께 눌렸는가
  // 그 누름이 이어진 시간(누르는 중이면 지금까지, 뗐으면 뗄 때까지)
  uint32_t okHeldMs(uint32_t nowMs) const { return (uint32_t)((okHeld_ ? nowMs : okLastMs_) - okSinceMs_); }

  // 한 틱의 맨 앞(터치를 읽기 전)에 부른다. touching: 직전 틱까지 화면에 손가락이 닿아 있었는가
  // (자리가 아닌 곳, 아직 누름으로 인식되기 전인 것 포함).
  // true 를 돌려주면 지금 눌려 있는 버튼을 무효로 해야 한다(InputLogic::reset()).
  bool begin(uint32_t nowMs, bool asking, bool armed, bool touching) {
    const bool ended = asking_ && !asking;
    const bool armedNow = armed && !armed_;
    asking_ = asking;
    armed_ = armed;
    if (touching) {
      liftedTicks_ = 0;
    } else if (liftedTicks_ < LIFT_TICKS) {
      ++liftedTicks_;
    }
    const bool lifted = liftedTicks_ >= LIFT_TICKS;

    // (가)
    if (armedNow) {
      stale_ = !lifted;
    } else if (lifted || !asking) {
      stale_ = false;
    }
    // (나)
    if (asking) {
      drain_ = false;  // 새 물음이 떴다: 묻는 중의 규칙이 맡는다
    } else if (ended) {
      drain_ = true;
      quietUntilMs_ = nowMs + QUIET_MS;
    } else if (drain_ && lifted && reachedMs(nowMs, quietUntilMs_)) {
      drain_ = false;
    }
    return drain_;
  }

  // 이번 틱에 나온 사건 하나의 갈래. begin() 때의 상태로 정한다: 같은 틱 안에서 "아니오"로 물음이
  // 거둬져도 그 틱의 나머지 사건은 키로 나가지 않는다.
  UpdateVerdict judge(Ev e) const {
    if (asking_) {
      if (e == Ev::Confirm) return (armed_ && !stale_ && okLong_) ? UpdateVerdict::Yes : UpdateVerdict::Swallow;
      if (e == Ev::Cancel) return UpdateVerdict::No;
      return UpdateVerdict::Swallow;
    }
    return drain_ ? UpdateVerdict::Swallow : UpdateVerdict::Pass;
  }

  // 지금 누름을 키로 내보내면 안 되는 때인가(묻는 중이거나, 물음이 끝난 뒤의 뜸).
  // 사건을 거치지 않고 따로 키를 내는 곳(Back·Next 의 짧게/길게 판정 — pane_switch.h)이 이것을 보고 멈춘다.
  bool blocking() const { return asking_ || drain_; }

 private:
  bool     asking_ = false;
  bool     armed_ = false;
  bool     stale_ = false;  // (가) 확인을 받기 시작할 때 화면에 있던 손가락이 아직 남아 있다
  bool     drain_ = false;  // (나) 물음이 끝난 뒤의 뜸
  uint32_t quietUntilMs_ = 0;
  int      liftedTicks_ = LIFT_TICKS;  // 손가락이 연속으로 보이지 않은 틱 수
  bool     okHeld_ = false;     // 확인 자리가 눌려 있다(D75)
  bool     okLong_ = false;     // 그 누름(또는 방금 뗀 누름)이 HOLD_MS 넘게 이어졌다
  bool     okSpoiled_ = false;  // 그 누름 도중에 다른 자리가 함께 눌린 적이 있다
  uint32_t okSinceMs_ = 0;      // 확인을 누르기 시작한 시각
  uint32_t okLastMs_ = 0;       // 확인이 눌려 있는 것을 마지막으로 본 시각(기록용)
};

}  // namespace touchui
