#include "dispatch.h"

// 키를 한 번 눌렀다 뗀다: 누름 → holdMs → 뗌 → afterMs. 못 보냈으면 false(아무것도 하지 않음).
static bool tap(KeyPort& keys, uint8_t mods, uint8_t key, uint32_t holdMs, uint32_t afterMs) {
  if (!keys.send(mods, key)) return false;
  keys.hold(holdMs);
  keys.releaseAll();
  keys.hold(afterMs);
  return true;
}

void dispatchEvent(Ev e, const HostProfile& host, const KeyTiming& timing, KeyPort& keys,
                   PeerControl& peers, ActionScreen& screen) {
  if (e == Ev::PairReset) {
    // 실제로 지워졌을 때만 성공으로 보여준다.
    screen.pairReset(peers.forgetPeers());
    return;
  }

  // D10: 말하기 앞에 초점 키를 먼저 보내 Claude 입력창으로 초점을 옮긴다(설정된 경우).
  // 말하기에만 붙인다 — Enter 앞에 붙이면 승인창에서 사용자가 고른 선택지를 덮어쓴다.
  const bool talkStart = (e == Ev::TalkDown || e == Ev::TalkTap);
  if (host.focusKey != 0 && talkStart &&
      !tap(keys, 0, host.focusKey, timing.tapHoldMs, host.focusSettleMs)) {
    screen.notSent();
    return;
  }

  const KeyAction a = mapEvent(e, host.os);
  if (a.type == KeyAction::None) return;
  if (a.type == KeyAction::Release) {
    keys.releaseAll();
    screen.sent(e);  // 못 보냈더라도 알린다: 끊기면 컴퓨터가 스스로 뗀 것으로 처리한다
    return;
  }

  const bool sent = (a.type == KeyAction::Tap)
                        ? tap(keys, a.mods, a.key, timing.tapHoldMs, timing.gapMs)
                        : keys.send(a.mods, a.key);
  if (!sent) {
    // 연결이 없으면 버리고 알려준다. 쌓아 두지 않는다(검토 초점 5).
    screen.notSent();
    return;
  }
  screen.sent(e);
}

void onLinkChange(bool ready, InputLogic& logic, KeyPort& keys) {
  if (ready) {
    keys.releaseAll();
  } else {
    logic.reset();
  }
}
