#include "input_logic.h"

#include "clock.h"

using namespace inputcfg;

static inline int iabs(int v) { return v < 0 ? -v : v; }

bool InputLogic::stillPushed(Dir d, int x, int y) {
  switch (d) {
    case Dir::Up:   return y >= DIR_OFF;
    case Dir::Down: return y <= -DIR_OFF;
    case Dir::Left:  return x <= -DIR_OFF;
    case Dir::Right: return x >= DIR_OFF;
  }
  return false;
}

bool InputLogic::pickDir(int x, int y, int threshold, Dir& out) {
  const int ax = iabs(x), ay = iabs(y);
  const int big = ax > ay ? ax : ay, small = ax > ay ? ay : ax;
  if (big < threshold) return false;
  if (big * DIR_MINOR < small * DIR_MAJOR) return false;  // 대각선: 분명하지 않다
  out = (ay > ax) ? ((y > 0) ? Dir::Up : Dir::Down) : ((x > 0) ? Dir::Right : Dir::Left);
  return true;
}

Ev InputLogic::eventFor(Dir d) {
  switch (d) {
    case Dir::Up:   return Ev::Up;
    case Dir::Down: return Ev::Down;
    case Dir::Left:  return Ev::WindowPrev;
    case Dir::Right: return Ev::WindowNext;
  }
  return Ev::None;
}

bool InputLogic::Press::update(bool raw, uint32_t nowMs) {
  const bool pressed = db.update(raw);
  if (pressed && !down) {
    down = true;
    spoiled = false;
    sinceMs = nowMs;
  }
  return pressed;
}

bool InputLogic::Press::released(bool pressedNow) {
  if (pressedNow || !down) return false;
  down = false;
  return !spoiled;
}

// 말하기를 끝낸다: 뗀 직후 한동안은 방향을 받지 않는다.
void InputLogic::endTalk(uint32_t nowMs) {
  talkHeld_ = false;
  talkUpPending_ = false;
  guard_ = true;
  guardUntilMs_ = nowMs + CLICK_GUARD_MS;
}

// D19: 누를 때마다 켜고 끄기. 누르는 순간 한 번 톡 치고, 뗄 때는 아무것도 보내지 않는다.
void InputLogic::talkToggle(const RawInput& in, uint32_t nowMs, Ev out[], int& n) {
  if (!in.joyOk) {
    // 읽지 못했다: 눌렸는지 알 수 없다. 걸린 채 남지 않게 보호는 풀되, "누르고 있었음"은 기억해 둔다
    // — 읽기가 돌아왔을 때 계속 누르고 있었다면 새 누름으로 치지 않는다(치면 녹음이 뒤집힌다).
    if (talkHeld_) endTalk(nowMs);
    return;
  }
  const bool click = in.joyBtn;
  if (click && !clickWas_) {
    // 앞선 누름 바로 뒤에 또 눌린 것(접점 튐, 손 떨림)은 톡으로 치지 않는다.
    if (!tapSent_ || elapsedMs(nowMs, lastTapMs_, TALK_TAP_GAP_MS)) {
      tapSent_ = true;
      lastTapMs_ = nowMs;
      push(out, n, Ev::TalkTap);
    }
  }
  if (click) {
    // 누르고 있는 동안 내내 방향·버튼을 받지 않는다. 매 틱 다시 세워, 연결이 끊겼다 이어져
    // reset() 이 불린 뒤에도 보호가 풀리지 않게 한다.
    talkHeld_ = true;
  } else if (talkHeld_) {
    endTalk(nowMs);  // 뗀 직후에도 한동안 방향을 받지 않는다
  }
  clickWas_ = click;
}

// 누르는 동안만 말하기. 톡과 꾹의 구분은 D18.
void InputLogic::talkHold(bool click, uint32_t nowMs, Ev out[], int& n) {
  // 늦춰 둔 뗌(D18): 때가 되면 낸다. 그 사이에 다시 눌렸으면 뗀 적이 없는 것으로 잇는다
  // (거기서 뗌을 내면 ⌘D 가 눌려 있던 시간이 톡과 꾹의 경계 근처가 되어 버린다).
  if (talkUpPending_) {
    if (click) {
      talkUpPending_ = false;
    } else if (reachedMs(nowMs, talkDownMs_ + TALK_HOLD_MIN_MS)) {
      endTalk(nowMs);
      push(out, n, Ev::TalkUp);
    }
  }
  if (click && !talkHeld_) {
    talkHeld_ = true;
    talkDownMs_ = nowMs + talkLeadMs_;
    push(out, n, Ev::TalkDown);
  } else if (!click && talkHeld_ && !talkUpPending_) {
    // 컴퓨터에서 ⌘D 가 눌려 있던 시간(아직 눌리기 전에 뗐으면 0)
    const uint32_t held = reachedMs(nowMs, talkDownMs_) ? nowMs - talkDownMs_ : 0;
    if (held >= TALK_TAP_MAX_MS && held < TALK_HOLD_MIN_MS) {
      talkUpPending_ = true;  // 어중간한 길이: 뗌을 늦춰 "누르는 동안"으로 확정시킨다
    } else {
      endTalk(nowMs);
      push(out, n, Ev::TalkUp);
    }
  }
}

int InputLogic::update(const RawInput& in, uint32_t nowMs, Ev out[MAX_EVENTS]) {
  int n = 0;
  auto emit = [&](Ev e) { push(out, n, e); };

  // 1) 조이스틱 누름 → 말하기. (누름의 떨림은 JoyFilter 가 걸러서 넘겨준다.)
  //    읽기 실패는 "뗌"으로 취급해 ⌘D 가 눌린 채 남지 않게 한다.
  const bool click = in.joyOk && in.joyBtn;
  if (talkMode_ == TalkMode::Toggle) {
    talkToggle(in, nowMs, out, n);
  } else {
    talkHold(click, nowMs, out, n);
  }
  if (guard_ && reachedMs(nowMs, guardUntilMs_)) guard_ = false;

  // 2) 방향. 말하는 동안과 뗀 직후에는 받지 않고, 중앙을 거친 뒤에만 받는다.
  //    임계값을 넘어도 DIR_DWELL_MS 동안 머물러야 인정한다 — 누르려고 힘을 줄 때
  //    스틱이 먼저 기울어지는 것과, 잠깐 튄 값을 거르기 위해서다.
  //    loop 가 막혀 있던 시간은 머문 시간으로 치지 않는다(그동안 누름을 알 기회가 없었다).
  const uint32_t gap = hasPrev_ ? (uint32_t)(nowMs - prevMs_) : 0;
  if (stage_ == Stage::Pending && gap > STALL_MS) dirAtMs_ += gap;
  prevMs_ = nowMs;
  hasPrev_ = true;

  const int  x = in.joyOk ? in.x : 0;
  const int  y = in.joyOk ? in.y : 0;
  const int  ax = iabs(x), ay = iabs(y);
  const bool center = (ax < DIR_OFF && ay < DIR_OFF);

  if (talkHeld_ || guard_) {
    stage_ = Stage::Disarmed;
  } else {
    switch (stage_) {
      case Stage::Disarmed:
        if (center) stage_ = Stage::Armed;
        break;
      case Stage::Armed:
        if (pickDir(x, y, DIR_ON, which_)) {
          dirAtMs_ = nowMs;
          stage_ = Stage::Pending;
        }
        break;
      case Stage::Pending:
        if (center) {
          stage_ = Stage::Armed;
        } else if (elapsedMs(nowMs, dirAtMs_, DIR_DWELL_MS)) {
          // 머무는 시간이 끝난 지금의 모습으로 다시 판정한다: 처음 고른 방향이 여전히 뚜렷해야 한다.
          Dir cur = which_;
          if (pickDir(x, y, DIR_OFF, cur) && cur == which_) {
            stage_ = Stage::Held;
            dirAtMs_ = nowMs + REPEAT_MS;
            emit(eventFor(which_));
          } else {
            stage_ = Stage::Armed;  // 그 사이 다른 쪽으로 옮겨 갔다 → 처음부터 다시 잰다
          }
        }
        break;
      case Stage::Held:
        if (center) {
          stage_ = Stage::Armed;
        } else if (repeats(which_) && reachedMs(nowMs, dirAtMs_) && stillPushed(which_, x, y)) {
          emit(eventFor(which_));
          dirAtMs_ = nowMs + REPEAT_MS;
        }
        break;
    }
  }

  // 3) 앞·옆 버튼. 접점 떨림을 거르고, 뗄 때 판정한다(D12).
  const bool wasChord = aBtn_.down && bBtn_.down;  // 직전 틱에 둘 다 눌려 있었는가
  const bool a = aBtn_.update(in.btnA, nowMs);
  const bool b = bBtn_.update(in.btnB, nowMs);

  // 누름이 살아 있는 동안 말하기나 동시 누름과 겹치면 그 누름은 무효다.
  //  - 말하는 중: 그 키가 나가면 ⌘D 유지가 풀려 녹음이 끊긴다.
  //  - 동시 누름: 짝 초기화 동작이지 Enter·Esc 가 아니다.
  if (talkHeld_ || (a && b)) {
    aBtn_.spoilIfDown();
    bBtn_.spoilIfDown();
  }

  if (a && b) {
    if (!wasChord) {  // 동시 누름이 지금 시작됐다
      chordFired_ = false;
      chordMs_ = nowMs;
    }
    if (!chordFired_ && elapsedMs(nowMs, chordMs_, CHORD_MS)) {
      chordFired_ = true;
      emit(Ev::PairReset);
    }
  }

  if (aBtn_.released(a)) {
    // 조이스틱이 없을 때는 앞 버튼을 길게 눌러 말하기를 켜고 끈다.
    const bool longPress = elapsedMs(nowMs, aBtn_.sinceMs, LONG_MS);
    emit((!in.joyOk && longPress) ? Ev::TalkTap : Ev::Confirm);
  }
  if (bBtn_.released(b)) {
    // D17: 취소는 CANCEL_COOLDOWN_MS 에 한 번만.
    const bool cooling = cancelSent_ && !elapsedMs(nowMs, lastCancelMs_, CANCEL_COOLDOWN_MS);
    if (!cooling) {
      cancelSent_ = true;
      lastCancelMs_ = nowMs;
      emit(Ev::Cancel);
    }
  }
  return n;
}

bool InputLogic::reset() {
  const bool was = talkHeld_;
  talkHeld_ = false;
  talkUpPending_ = false;
  guard_ = false;
  stage_ = Stage::Disarmed;
  // 눌려 있던 버튼은 뗄 때 키를 내지 않는다.
  aBtn_.spoilIfDown();
  bBtn_.spoilIfDown();
  // 동시 누름 상태(chordFired_)는 건드리지 않는다:
  // 짝 초기화로 연결이 끊겨 reset() 이 불려도, 계속 쥐고 있다고 다시 발사되면 안 된다.
  return was;
}
