#pragma once
#include <stdint.h>

#include "clock.h"
#include "events.h"
#include "scene.h"
#include "screen_draw.h"

// 지금 화면에 무엇을 보여야 하는가. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
// - 평소에는 연결 상태에 맞는 대기 장면을 보인다.
// - 말하기를 켰다고 보는 동안에는 TALK 를 유지한다(사용자 요청: 말하는 중에 HI! 가 뜨면 어색하다).
//   다시 눌러 끄거나, 앞 버튼으로 보내거나(컴퓨터가 녹음을 끈다), 연결이 끊기거나,
//   TALK_MAX_MS(컴퓨터가 녹음을 끄는 최대 시간)가 지나면 내린다.
//   한계: 컴퓨터는 15초 조용하면 녹음을 스스로 끄는데 기기는 그것을 알 수 없다. 그 뒤 조이스틱을
//   누르면 화면과 실제가 뒤집힐 수 있고, 앞 버튼으로 보내거나 시간이 지나면 다시 맞는다.
// - 그 밖의 동작은 ACTION_SHOW_MS 동안 보였다가 돌아간다(D24).
// - 화면 전체를 다시 그리는 일은 비싸므로, 그림이 실제로 달라질 때만 poll() 이 true 를 준다.
class ScreenState {
 public:
  static const uint32_t ACTION_SHOW_MS = 1500;
  static const uint32_t TALK_MAX_MS    = 120000;

  void setLink(LinkState l) {
    link_ = l;
    if (l != LinkState::Ready) talking_ = false;  // 끊기면 말하기도 끝난 것이다
    // 연결됐는데 "NO LINK" 가 남아 있으면 어색하다 → 바로 내린다.
    if (l == LinkState::Ready && actionOn_ && action_ == Scene::NoLink) actionOn_ = false;
  }
  void setNoStick(bool on) { noStick_ = on; }

  // 배터리(0~100, 모르면 -1). 배터리 그림이 달라질 때만 값을 바꾼다.
  void setBattery(int pct) {
    if (batteryLook(pct) != batteryLook(batteryPct_)) batteryPct_ = pct;
  }

  // 이 사건의 키를 보냈다.
  void onSent(Ev e, uint32_t nowMs) {
    switch (e) {
      case Ev::TalkTap:  // 켜고 끄기 방식: 누를 때마다 뒤집힌다
        setTalking(!talking_, nowMs);
        return;
      case Ev::TalkDown:  // 누르는 동안 방식
        setTalking(true, nowMs);
        return;
      case Ev::TalkUp:
        setTalking(false, nowMs);
        return;
      case Ev::Confirm:  // Enter: 녹음 중이었다면 컴퓨터가 녹음을 끄고 글을 보낸다
        talking_ = false;
        break;
      default:
        break;
    }
    Scene s;
    if (sceneFor(e, s)) showAction(s, nowMs);
  }

  // 보내려 했으나 연결이 없었다.
  void onNotSent(uint32_t nowMs) { showAction(Scene::NoLink, nowMs); }

  // 짝 초기화를 했다.
  void onPairReset(bool ok, uint32_t nowMs) { showAction(ok ? Scene::Bye : Scene::Oops, nowMs); }

  // 동작 장면을 잠깐 띄운다.
  void showAction(Scene s, uint32_t nowMs) {
    action_ = s;
    actionAtMs_ = nowMs;
    actionOn_ = true;
  }

  // 지금 보여야 할 화면이 마지막으로 내준 것과 다르면 out 에 담고 true 를 돌려준다.
  bool poll(uint32_t nowMs, screen::Frame& out) {
    // nowMs 는 그 틱이 시작될 때의 시각이라, 키를 다 보낸 뒤에 기록한 actionAtMs_ 보다
    // 조금 앞설 수 있다. reachedMs 는 그런 경우를 "아직"으로 본다(elapsedMs 를 쓰면 곧바로 끝난다).
    if (actionOn_ && reachedMs(nowMs, actionAtMs_ + ACTION_SHOW_MS)) actionOn_ = false;
    if (talking_ && reachedMs(nowMs, talkAtMs_ + TALK_MAX_MS)) talking_ = false;
    screen::Frame f{};
    f.scene = actionOn_ ? action_ : (talking_ ? Scene::Talk : idleSceneFor(link_));
    f.link = link_;
    f.batteryPct = batteryPct_;
    f.noStick = noStick_;
    if (drawn_ && f == last_) return false;
    last_ = f;
    drawn_ = true;
    out = f;
    return true;
  }

 private:
  // 배터리 그림을 가르는 값: 채움 칸 수(0~16)와 "부족(빨강)" 여부. 모르면 -1.
  static int batteryLook(int pct) {
    if (pct < 0) return -1;
    return screen::batteryFill(pct) * 2 + (screen::batteryLow(pct) ? 1 : 0);
  }

  // 말하기를 켜거나 끈다. 떠 있던 다른 동작 장면은 접어 바로 반영되게 한다.
  void setTalking(bool on, uint32_t nowMs) {
    talking_ = on;
    talkAtMs_ = nowMs;
    actionOn_ = false;
  }

  LinkState link_       = LinkState::Searching;
  bool      talking_    = false;  // 말하기가 켜져 있다고 본다
  uint32_t  talkAtMs_   = 0;
  int       batteryPct_ = -1;
  bool      noStick_    = false;
  Scene     action_     = Scene::Ready;
  uint32_t  actionAtMs_ = 0;
  bool      actionOn_   = false;
  screen::Frame last_{};
  bool      drawn_      = false;
};
