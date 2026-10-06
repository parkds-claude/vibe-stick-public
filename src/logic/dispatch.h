#pragma once
#include "events.h"
#include "input_logic.h"
#include "keymap.h"

// 키를 실제로 내보내는 쪽. 기기에서는 블루투스(blehid)가, 시험에서는 가짜가 구현한다.
class KeyPort {
 public:
  virtual ~KeyPort() {}
  // 지금 눌린 키 상태를 보낸다. 보냈으면 true, 연결이 없으면 false(쌓아 두지 않는다).
  virtual bool send(uint8_t mods, uint8_t key) = 0;
  virtual bool releaseAll() = 0;
  // 정해진 시간만큼 기다린다(키를 누르고 있는 시간, 보고 사이 간격)
  virtual void hold(uint32_t ms) = 0;
};

// 결합(짝) 정보를 다루는 쪽. 키 전송과는 다른 일이라 따로 둔다
// (결합 개념이 없는 전송 방식 — 예: USB — 은 구현하지 않아도 되게).
class PeerControl {
 public:
  virtual ~PeerControl() {}
  // 결합 정보를 모두 지운다. 실제로 지워졌으면 true.
  virtual bool forgetPeers() = 0;
};

// 키 보고의 시간 간격
struct KeyTiming {
  uint32_t tapHoldMs;  // 한 번 누르는 키를 누르고 있는 시간
  uint32_t gapMs;      // 뗀 뒤 다음 보고까지 띄우는 시간
};

// 화면에는 "기기가 실제로 한 일"만 알린다. 그것을 어떤 그림으로 얼마 동안 보일지는
// 화면 쪽(logic/screen_state)이 정한다.
class ActionScreen {
 public:
  virtual ~ActionScreen() {}
  // 이 사건의 키를 보냈다. 말하기의 뗌(TalkUp)도 알린다.
  virtual void sent(Ev e) = 0;
  // 보내려 했으나 연결이 없어 버렸다.
  virtual void notSent() = 0;
  // 짝 초기화를 했다(ok = 실제로 지워졌는가).
  virtual void pairReset(bool ok) = 0;
};

// 사건 하나를 처리한다: 키로 바꿔 보내고, 실제로 보낸 것만 화면에 알린다.
// 보내지 못하면 버리고 "연결 없음" 장면을 보여준다(검토 초점 5).
void dispatchEvent(Ev e, const HostProfile& host, const KeyTiming& timing, KeyPort& keys,
                   PeerControl& peers, ActionScreen& screen);

// 연결 상태가 바뀌었을 때 부른다. 눌린 채 남는 키가 없게 한다(검토 초점 2).
void onLinkChange(bool ready, InputLogic& logic, KeyPort& keys);
