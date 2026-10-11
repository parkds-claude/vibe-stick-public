#pragma once
#include <stdint.h>

#include "touch_layout.h"
#include "logic/clock.h"
#include "logic/events.h"
#include "logic/input_logic.h"

// 화면에 닿은 손가락 → 눌린 자리 → 기존 입력 판정(InputLogic)이 받는 값. 하드웨어를 모른다.
//
// D34 터치를 "가상의 버튼·조이스틱"으로 바꿔 StickC 의 입력 판정을 그대로 쓴다.
//   얼굴 = 조이스틱 누름(말하기), 확인 = 앞 버튼, 취소 = 옆 버튼, ▲▼◀▶ = 조이스틱을 끝까지 밀기.
//   이유: 이미 실물에서 다듬은 규칙(취소는 1초에 한 번 D17, 확인·취소는 뗄 때 D12, ▲▼ 는 누르고
//         있으면 반복, 말하기 떨림 무시)을 다시 만들지 않고 그대로 얻는다. 시험 283건도 그대로 유효하다.
//   비용: 조이스틱용 규칙 하나(방향은 0.1초 머물러야 인정)가 터치에는 필요 없는데 따라온다
//         → 화살표를 짧게 톡 쳐도 ARROW_HOLD_MS 동안 눌린 것으로 이어 주어 놓치지 않게 한다.
//   탈출구: 이 파일만 터치 전용 판정으로 바꾸면 된다(나머지는 사건 Ev 만 주고받는다).
namespace touchui {

// 화면에 닿아 있는 손가락 하나
struct TouchPoint {
  uint8_t id;  // 닿아 있는 동안 바뀌지 않는 번호(터치 칩이 준다)
  int16_t x, y;
};

class TouchMapper {
 public:
  static const int MAX_TOUCH = 2;  // 터치 칩(FT6336)이 한 번에 알려 주는 손가락 수
  // 이만큼 연속으로 보여야 눌림으로 친다(한 번 튄 값을 거른다).
  static const int PRESS_TICKS = 2;
  // 이만큼 연속으로 안 보여야 뗀 것으로 친다(터치 칩이 한두 번 놓친 것을 잇는다).
  // 이 덕에 아무리 짧게 쳐도 눌림이 3틱은 이어져, InputLogic 의 버튼 떨림 거르기(2틱)를 통과한다.
  static const int RELEASE_TICKS = 3;
  // 화살표는 눌린 순간부터 적어도 이만큼 눌린 것으로 본다(위 D34 의 비용 참고).
  // 이 시간은 InputLogic 과 같은 방식으로 센다: loop 가 막혀 있던 시간(틱 간격이 STALL_MS 를
  // 넘은 때 — 키를 보내는 42ms, 얼굴을 다시 그리는 시간)은 세지 않는다. InputLogic 도 그 시간을
  // "머문 시간"으로 치지 않으므로, 같은 잣대로 재야 막힘이 길어도 톡을 놓치지 않는다.
  static const uint32_t ARROW_HOLD_MS = 160;
  // D71 보는 곳(VIEW_TAP_RECT)의 "톡"으로 치는 가장 긴 시간(처음 닿은 때부터 마지막으로 보인 때까지).
  // 그보다 오래 닿아 있던 것은 쥐느라 닿은 손이다. 벽시계로 잰다: loop 가 막힌 시간만큼 톡이 길게 재어질 수
  // 있어 넉넉히 잡았다(키를 보내는 42ms · 화면을 그리는 시간).
  static const uint32_t VIEW_TAP_MAX_MS = 700;

  // 한 틱에 한 번 부른다. pts 는 지금 닿아 있는 손가락들(n 개).
  void update(const TouchPoint* pts, int n, uint32_t nowMs) {
    // 지난 판독부터 흐른 시간. 막혀 있었다면(간격이 길면) 이어 주는 시간에서 빼지 않는다.
    const uint32_t gap = hasPrev_ ? (uint32_t)(nowMs - prevMs_) : 0;
    const uint32_t counted = (gap > inputcfg::STALL_MS) ? 0 : gap;
    prevMs_ = nowMs;
    hasPrev_ = true;
    for (int z = 0; z < ZONE_COUNT; z++) {
      if (!(arrowHeld_ & (1u << z))) continue;
      if (arrowLeftMs_[z] > counted) {
        arrowLeftMs_[z] -= counted;
      } else {
        arrowHeld_ &= (uint8_t)~(1u << z);  // 이어 주던 시간이 다 됐다
      }
    }

    bool seen[MAX_TOUCH] = {false, false};
    for (int i = 0; i < n && i < MAX_TOUCH; i++) {
      int s = find(pts[i].id);
      if (s < 0) s = alloc(pts[i], nowMs);
      if (s < 0) continue;  // 자리가 없다(있을 수 없다: 손가락 수 = 칸 수)
      seen[s] = true;
      slots_[s].missed = 0;
      slots_[s].lastMs = nowMs;
      if (slots_[s].seen < PRESS_TICKS) ++slots_[s].seen;
      if (slots_[s].seen >= PRESS_TICKS && !slots_[s].down) {
        slots_[s].down = true;
        if (slots_[s].hasZone && isArrow(slots_[s].zone)) {
          arrowLeftMs_[(int)slots_[s].zone] = ARROW_HOLD_MS;
          arrowHeld_ |= bit(slots_[s].zone);
        }
      }
    }
    for (int s = 0; s < MAX_TOUCH; s++) {
      if (slots_[s].used && !seen[s] && ++slots_[s].missed >= RELEASE_TICKS) {
        // D71 보는 곳에서 시작한 손가락을 뗐다: 혼자였고, 눌림으로 인정됐고, 짧았으면 "톡"이다.
        const Slot& f = slots_[s];
        if (f.inView && f.down && f.solo && (uint32_t)(f.lastMs - f.firstMs) <= VIEW_TAP_MAX_MS) viewTap_ = true;
        slots_[s] = Slot{};
      }
    }
  }

  // D71 보는 곳을 톡 쳤는가(뗀 뒤에 한 번). 한 번 읽으면 지워진다.
  //   처음 닿은 곳에 묶인다(버튼과 같다): 보는 곳에서 시작한 손가락만이고, 다른 손가락이 함께 닿아 있었다면
  //   (쥔 손 + 누르는 손가락) 톡이 아니다.
  bool takeViewTap() {
    const bool t = viewTap_;
    viewTap_ = false;
    return t;
  }

  // 지금 눌려 있는 자리들(bit(Zone)). 손가락은 처음 닿은 자리에 묶인다: 미끄러져 옆 버튼으로
  // 넘어가도 처음 자리가 눌린 것이고, 틈에서 시작한 손가락은 어디로 가도 누름이 아니다.
  uint8_t pressed() const {
    uint8_t m = arrowHeld_;
    for (const Slot& s : slots_) {
      if (s.used && s.down && s.hasZone) m |= bit(s.zone);
    }
    return m;
  }

  // 화면에 손가락이 닿아 있는가(자리가 아닌 곳 포함) — 어두워진 화면을 깨울 때 쓴다.
  bool touching() const { return slots_[0].used || slots_[1].used; }

  // InputLogic 이 받는 값으로 바꾼다.
  void fill(RawInput& in) const {
    const uint8_t m = pressed();
    in.joyOk = true;
    in.joyBtn = (m & bit(Zone::Talk)) != 0;
    in.btnA = (m & bit(Zone::Ok)) != 0;
    in.btnB = (m & bit(Zone::Nope)) != 0;
    in.x = (int8_t)(((m & bit(Zone::Next)) ? 127 : 0) - ((m & bit(Zone::Back)) ? 127 : 0));
    in.y = (int8_t)(((m & bit(Zone::Up)) ? 127 : 0) - ((m & bit(Zone::Down)) ? 127 : 0));
  }

  void reset() {
    slots_[0] = slots_[1] = Slot{};
    arrowHeld_ = 0;
    hasPrev_ = false;
    viewTap_ = false;
  }

 private:
  struct Slot {
    bool    used = false;
    uint8_t id = 0;
    bool    hasZone = false;
    Zone    zone = Zone::Talk;
    uint8_t seen = 0;    // 연속으로 보인 횟수(PRESS_TICKS 까지만 센다)
    uint8_t missed = 0;  // 연속으로 안 보인 횟수
    bool    down = false;
    bool    inView = false;   // 보는 곳(VIEW_TAP_RECT)에서 시작했다(D71)
    bool    solo = true;      // 닿아 있는 동안 다른 손가락이 없었다
    uint32_t firstMs = 0;     // 처음 보인 시각
    uint32_t lastMs = 0;      // 마지막으로 보인 시각
  };

  static bool isArrow(Zone z) {
    return z == Zone::Up || z == Zone::Down || z == Zone::Back || z == Zone::Next;
  }
  int find(uint8_t id) const {
    for (int s = 0; s < MAX_TOUCH; s++) {
      if (slots_[s].used && slots_[s].id == id) return s;
    }
    return -1;
  }
  int alloc(const TouchPoint& p, uint32_t nowMs) {
    for (int s = 0; s < MAX_TOUCH; s++) {
      if (slots_[s].used) continue;
      slots_[s] = Slot{};
      slots_[s].used = true;
      slots_[s].id = p.id;
      slots_[s].hasZone = zoneAt(p.x, p.y, slots_[s].zone);
      slots_[s].inView = !slots_[s].hasZone && viewTapAt(p.x, p.y);
      slots_[s].firstMs = slots_[s].lastMs = nowMs;
      // 다른 손가락이 이미 닿아 있다: 둘 다 "혼자"가 아니다(D71 — 톡으로 치지 않는다).
      for (int o = 0; o < MAX_TOUCH; o++) {
        if (o != s && slots_[o].used) slots_[o].solo = slots_[s].solo = false;
      }
      return s;
    }
    return -1;
  }

  Slot     slots_[MAX_TOUCH];
  uint8_t  arrowHeld_ = 0;                // 짧은 톡을 이어 주는 중인 화살표들
  uint32_t arrowLeftMs_[ZONE_COUNT] = {};  // 앞으로 더 이어 줄 시간(막힌 시간은 빼지 않는다)
  uint32_t prevMs_ = 0;                    // 직전 update() 의 시각
  bool     hasPrev_ = false;
  bool     viewTap_ = false;               // 보는 곳을 톡 쳤다(아직 읽어 가지 않은 것 — D71)
};

// 화살표를 이어 주는 시간은 InputLogic 이 방향을 인정하기까지의 시간보다 길어야 한다
// (머묾 0.1초 + 판정이 한두 틱 늦는 것). 막힌 시간은 양쪽 모두 세지 않으므로 여유에 넣지 않는다.
static_assert(TouchMapper::ARROW_HOLD_MS >= inputcfg::DIR_DWELL_MS + 2 * inputcfg::STALL_MS,
              "ARROW_HOLD_MS 가 방향 인정 시간보다 짧다");

}  // namespace touchui
