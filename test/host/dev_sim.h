#pragma once
// 기기의 loop 를 가상 시계로 흉내 낸다 — src/main.cpp 와 같은 순서로:
//   틱이 시작될 때 시각(now)을 한 번 읽고 → 조이스틱 읽기 → 입력 판독 → 키 전송(시간이 흐른다)
//   → 화면 상태 갱신(그 순간의 시각으로 기록) → 화면 그리기(시간이 흐른다)
//
// 단위 시험은 "같은 시각"으로 부르기 때문에, 틱의 시작 시각과 나중에 읽은 시각이 섞이는
// 결함(에 세 번 나옴)을 잡지 못한다. 이 도우미는 그 순서를 그대로 재현한다.
#include <functional>
#include <utility>
#include <vector>

#include "config.h"
#include "logic/clock.h"
#include "logic/dispatch.h"
#include "logic/input_logic.h"
#include "logic/joy_filter.h"
#include "logic/screen_state.h"

// 그 시각의 물리 상태(손이 하고 있는 것)
struct Phys {
  int  x = 0, y = 0;
  bool joyBtn = false, a = false, b = false;
};
using PhysFn = std::function<Phys(uint32_t)>;

struct DevSim {
  uint32_t t = 0;        // 가상 millis()
  uint32_t bandMs = 9;   // 화면의 띠 하나를 그리는 데 걸리는 시간(기기는 틱마다 한 띠씩 그린다)
  int      bandsLeft = 0;
  bool     linkUp = true;

  InputLogic  logic;
  JoyFilter   joy{JOY_SWAP_XY, JOY_INVERT_Y, JOY_INVERT_X};
  ScreenState screenState;
  Every       joyPoll;
  uint32_t    lastTick = 0;

  std::vector<std::pair<uint32_t, Ev>>    events;  // (사건이 나온 시각, 사건)
  std::vector<std::pair<uint32_t, Scene>> draws;   // (그리기 시작 시각, 장면)

  struct Port : KeyPort {
    DevSim* d = nullptr;
    bool send(uint8_t, uint8_t) override { return d->linkUp; }
    bool releaseAll() override { return d->linkUp; }
    void hold(uint32_t ms) override { d->t += ms; }  // 키를 누르고 있는 동안 시간이 흐른다
  } port;
  struct Peers : PeerControl {
    bool forgetPeers() override { return true; }
  } peers;
  struct Scr : ActionScreen {  // ui.cpp 와 같이 "지금 시각"으로 기록한다
    DevSim* d = nullptr;
    void sent(Ev e) override { d->screenState.onSent(e, d->t); }
    void notSent() override { d->screenState.onNotSent(d->t); }
    void pairReset(bool ok) override { d->screenState.onPairReset(ok, d->t); }
  } scr;

  DevSim() {
    port.d = this;
    scr.d = this;
    logic.setTalkMode(TALK_MODE);
    screenState.setLink(LinkState::Ready);
    screenState.setBattery(72);
  }

  // until 시각까지 loop 를 돌린다.
  void run(const PhysFn& phys, uint32_t until) {
    while (t < until) {
      const uint32_t now = t;  // 틱의 시작 시각 — 이 틱 내내 이 값을 쓴다
      const uint32_t since = now - lastTick;
      if (since < TICK_MS) {
        t += TICK_MS - since;
        continue;
      }
      lastTick = now;

      const Phys p = phys(t);
      if (joyPoll.due(now, JOY_POLL_MS)) joy.feed(true, (int8_t)p.x, (int8_t)p.y, p.joyBtn);
      RawInput in{};
      in.btnA = p.a;
      in.btnB = p.b;
      joy.fill(in);

      Ev out[InputLogic::MAX_EVENTS];
      const int n = logic.update(in, now, out);
      for (int i = 0; i < n; i++) {
        events.push_back({t, out[i]});
        dispatchEvent(out[i], HOST, KEY_TIMING, port, peers, scr);
      }

      screen::Frame f;
      if (screenState.poll(now, f)) {  // 그림이 달라졌다: 맨 위 띠부터 다시
        draws.push_back({t, f.scene});
        bandsLeft = screen::BANDS;
      }
      if (bandsLeft > 0) {
        t += bandMs;
        --bandsLeft;
      }
      t += 1;  // 나머지 일
    }
  }

  int count(Ev e) const {
    int n = 0;
    for (const auto& ev : events) n += (ev.second == e);
    return n;
  }
  // 처음 나온 시각(없으면 -1)
  long firstAt(Ev e) const {
    for (const auto& ev : events) {
      if (ev.second == e) return (long)ev.first;
    }
    return -1;
  }
};

inline bool isDirection(Ev e) {
  return e == Ev::Up || e == Ev::Down || e == Ev::WindowNext || e == Ev::WindowPrev;
}
