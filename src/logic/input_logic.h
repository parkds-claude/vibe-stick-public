#pragma once
#include "debounce.h"
#include "events.h"

namespace inputcfg {
const int      DIR_ON         = 90;    // 이 이상 밀어야 방향으로 인정
const int      DIR_OFF        = 40;    // 이 아래로 돌아와야 중앙으로 인정
// 큰 축이 작은 축의 DIR_MAJOR/DIR_MINOR (=1.5)배 이상이어야 방향으로 본다. 그보다 비슷하면
// 대각선이라 어느 쪽인지 분명하지 않다 — 옆 방향은 곧바로 창이 넘어가므로 추측하지 않는다.
const int      DIR_MAJOR      = 3;
const int      DIR_MINOR      = 2;
// 조이스틱을 누르려고 힘을 주면 스틱이 먼저 기울어진다. 그 기울임을 방향으로 치지 않으려고
// 방향은 DIR_DWELL_MS 만큼 머문 뒤에 인정한다(그 사이에 누름이 오면 방향을 버린다).
//   DIR_DWELL_MS = 기울임이 누름보다 앞서는 것을 봐주는 시간(TILT_LEAD_MS)
//                + 누름이 인식되기까지의 지연(조이스틱 읽기 2회, 20ms 주기면 최대 40ms — JoyFilter)
//                + 여유 한 틱
//   기울임은 읽는 즉시 반영되고 누름만 늦게 인식되므로, 이 지연을 빼면 실제로 봐주는 시간이 줄어든다.
//   조이스틱 읽기 주기(config.h 의 JOY_POLL_MS)를 늘리면 이 값도 늘려야 한다(main.cpp 의 static_assert).
//   비용: 방향(위·아래·창 고르기) 반응이 0.1초 늦다. 실물에서 답답하면 TILT_LEAD_MS 와 함께 줄인다.
const uint32_t TILT_LEAD_MS   = 50;
const uint32_t DIR_DWELL_MS   = 100;
const uint32_t REPEAT_MS      = 400;   // 위·아래 반복 간격
const uint32_t CLICK_GUARD_MS = 300;   // 조이스틱을 뗀 뒤 방향을 무시하는 시간
const uint32_t LONG_MS        = 600;   // 조이스틱이 없을 때: 앞 버튼 길게 = 말하기
const uint32_t CHORD_MS       = 3000;  // 앞+옆 동시 누름 = 짝 초기화

// D18: 말하기 누름의 길이를 둘로 또렷이 가른다.
//   Claude Code 확장은 ⌘D 를 200ms 미만으로 누르면 "켜고 끄기", 그 이상이면 "누르는 동안"으로 본다.
//   경계 근처(180~220ms)의 누름은 어느 쪽이 될지 알 수 없고, 의도와 달리 켜고 끄기로 들어가면
//   녹음이 최대 120초 동안 켜진 채 남는다. 그래서:
//   - TAP_MAX 미만으로 뗀 것은 바로 뗀다(→ 켜고 끄기).
//   - TAP_MAX 이상 HOLD_MIN 미만에서 뗀 것은, 뗌을 누른 지 HOLD_MIN 이 될 때까지 늦춘다(→ 누르는 동안).
//     늦추는 사이에 다시 누르면 계속 누른 것으로 잇는다.
const uint32_t TALK_TAP_MAX_MS  = 130;
const uint32_t TALK_HOLD_MIN_MS = 260;

// 직전 판독과의 간격이 이보다 길면 loop 가 막혀 있었던 것으로 본다(키 전송 42ms, 화면 그리기 등).
// 막혀 있던 동안에는 조이스틱을 읽지 못했으므로 그 시간은 방향의 "머문 시간"으로 치지 않는다.
// 치면, 버튼을 누른 직후 조이스틱을 누를 때 기울임이 방향으로 먼저 나가 버린다(2차 검토 I1).
const uint32_t STALL_MS = 25;

// D19: "누를 때마다 켜고 끄기" 방식에서, 앞선 누름 뒤 이 시간 안에 또 눌린 것은 무시한다.
//   접점이 튀거나 손이 떨려 두 번 눌리면 녹음이 켜지자마자 꺼지기 때문이다.
const uint32_t TALK_TAP_GAP_MS = 250;

// D17: 취소(Esc)는 이 간격에 한 번만 낸다.
//   Claude Code 확장은 빈 입력창에서 Esc 두 번(800ms 안)을 "되감기 창 열기"로 받고,
//   이어진 Enter 가 Claude 가 고친 파일을 되돌린다.
const uint32_t CANCEL_COOLDOWN_MS = 1000;
}  // namespace inputcfg

// 조이스틱 누름으로 말하기를 다루는 방식 (D19)
enum class TalkMode : uint8_t {
  // 누를 때마다 켜고 끈다: 누르는 순간 ⌘D 를 한 번 톡 친다(얼마나 오래 누르든 같다).
  // 누른 채 버틸 필요가 없다. 사건은 TalkTap 하나뿐이다.
  Toggle,
  // 누르는 동안만 말한다: 누르면 ⌘D 를 누르고(TalkDown) 떼면 뗀다(TalkUp).
  // 짧게 톡 치면 컴퓨터가 "켜고 끄기"로 본다 — 그 구분은 D18 이 맡는다.
  Hold,
};

// 버튼·조이스틱의 원시 값을 사건으로 바꾼다. 하드웨어를 모른다(컴퓨터에서 시험 가능).
// 시간 판정은 모두 ms 기준이다. 단, 버튼 떨림 거르기만 "update() 두 번 연속"이라
// 호출 주기(TICK_MS)에 묶여 있다 — 10ms 주기면 20ms 짜리 거르기가 된다.
class InputLogic {
 public:
  // 한 틱에 나오는 사건은 많아야 2개다(예: 방향 + 확인). 넉넉히 잡아 둔다.
  static const int MAX_EVENTS = 4;

  // 한 틱의 입력을 받아 사건을 out 에 채우고 개수를 돌려준다.
  int update(const RawInput& in, uint32_t nowMs, Ev out[MAX_EVENTS]);

  // 연결이 끊기는 등 상태를 버려야 할 때 부른다. 말하는 중이었으면 true.
  // 이때 눌려 있던 버튼은 뗄 때 키를 내지 않는다.
  bool reset();

  // 조이스틱을 누르고 있는 중인가(Hold 방식에서는 뗌을 늦추는 동안도 포함)
  bool talking() const { return talkHeld_; }

  void setTalkMode(TalkMode m) { talkMode_ = m; }

  // 말하기 사건(TalkDown)을 낸 뒤 ⌘D 가 실제로 눌리기까지 걸리는 시간.
  // 초점 키(D10)를 먼저 보내는 경우 그만큼 늦어진다. 톡/꾹 구분(D18)은 컴퓨터가 보는
  // "⌘D 가 눌려 있던 시간"으로 해야 하므로 이 값을 뺀다. 초점 키를 쓰지 않으면 0.
  void setTalkLeadMs(uint32_t ms) { talkLeadMs_ = ms; }

 private:
  // 방향 입력의 단계
  enum class Stage : uint8_t {
    Disarmed,  // 중앙을 한 번 거쳐야 방향을 받는다(켠 직후, 말하기 중과 직후)
    Armed,     // 중앙을 확인함 — 방향을 받을 준비
    Pending,   // 임계값을 넘었고, 머문 시간을 재는 중
    Held,      // 방향 확정 — 중앙으로 돌아올 때까지 유지(위·아래는 반복)
  };
  enum class Dir : uint8_t { Up, Down, Left, Right };

  // 앞·옆 버튼 하나의 "누름 한 번". 누름이 살아 있는 동안 다른 일(말하기, 동시 누름,
  // 연결 끊김)과 겹치면 그 누름은 무효가 되어 뗄 때 키를 내지 않는다.
  struct Press {
    Debounced db;
    bool      down    = false;
    bool      spoiled = false;
    uint32_t  sinceMs = 0;

    // 원시 값을 받아 떨림을 거르고, 누름이 시작됐으면 기록한다. 반환: 지금 눌려 있는가
    bool update(bool raw, uint32_t nowMs);
    void spoilIfDown() { spoiled = spoiled || down; }
    // 방금 뗐고 키를 내야 하면 true
    bool released(bool pressedNow);
  };

  static bool stillPushed(Dir d, int x, int y);
  // (x, y) 가 가리키는 방향을 고른다. 큰 축이 threshold 에 못 미치거나 대각선이면 false.
  static bool pickDir(int x, int y, int threshold, Dir& out);
  // 밀고 있으면 반복하는 방향인가(위·아래만. 창 이동은 한 번 밀면 한 번)
  static bool repeats(Dir d) { return d == Dir::Up || d == Dir::Down; }
  static Ev   eventFor(Dir d);
  static void push(Ev out[], int& n, Ev e) {
    if (n < MAX_EVENTS) out[n++] = e;
  }
  void endTalk(uint32_t nowMs);
  void talkToggle(const RawInput& in, uint32_t nowMs, Ev out[], int& n);
  void talkHold(bool click, uint32_t nowMs, Ev out[], int& n);

  // 말하기
  TalkMode talkMode_      = TalkMode::Hold;
  uint32_t talkLeadMs_    = 0;
  bool     talkHeld_      = false;  // Hold: TalkDown 을 냈고 TalkUp 을 아직 내지 않음 / Toggle: 누르고 있음
  bool     clickWas_      = false;  // Toggle: 직전 틱에 눌려 있었는가(누르는 순간을 잡는다)
  bool     tapSent_       = false;  // Toggle: 톡을 보낸 적이 있음
  uint32_t lastTapMs_     = 0;      // Toggle: 마지막으로 톡을 보낸 시각
  uint32_t talkDownMs_    = 0;      // ⌘D 가 컴퓨터에서 눌리는 시각(= 사건 시각 + talkLeadMs_)
  bool     talkUpPending_ = false;  // 조이스틱은 뗐지만 TalkUp 을 늦추는 중(D18)
  bool     guard_         = false;  // 말하기를 뗀 직후: 방향을 받지 않는다
  uint32_t guardUntilMs_  = 0;

  // 방향
  Stage    stage_   = Stage::Disarmed;
  Dir      which_   = Dir::Up;  // Pending·Held 일 때의 방향
  uint32_t dirAtMs_ = 0;        // Pending: 임계값을 넘은 시각 / Held: 다음 반복 시각
  uint32_t prevMs_  = 0;        // 직전 update() 의 시각
  bool     hasPrev_ = false;

  // 버튼
  Press    aBtn_, bBtn_;
  bool     chordFired_   = false;  // 이번 동시 누름에서 짝 초기화를 이미 냈음
  uint32_t chordMs_      = 0;      // 동시 누름이 시작된 시각
  bool     cancelSent_   = false;  // 취소를 낸 적이 있음(D17)
  uint32_t lastCancelMs_ = 0;
};
