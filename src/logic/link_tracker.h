#pragma once
#include <stdint.h>

#include "clock.h"
#include "link_state.h"

// 블루투스 쪽에서 오는 세 가지 사건(연결됨 / 암호화·결합 확인됨 / 끊김)으로 연결 상태를 판정한다.
// 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
// 사건의 순서를 가정하지 않는다. 이미 짝을 맺은 컴퓨터가 다시 붙을 때는 "암호화 확인"이
// "연결됨"보다 먼저 온다(맥에서 실측: 0.12초 먼저). 처음 구현은 "연결됨"에서
// 암호화 표시를 무조건 지워서, 다시 연결될 때마다 "연결 중"에 머물렀다.
//
// 사건은 블루투스 태스크에서, state()·connectTimedOut() 은 loop 태스크에서 불린다.
class LinkTracker {
 public:
  // settleMs: 암호화 뒤 컴퓨터가 키보드를 쓸 준비를 마칠 때까지 두는 여유
  // connectTimeoutMs: 연결은 됐는데 이 시간 안에 암호화가 안 끝나면 끊을 때가 된 것
  LinkTracker(uint32_t settleMs, uint32_t connectTimeoutMs)
      : settleMs_(settleMs), connectTimeoutMs_(connectTimeoutMs) {}

  // ---- 블루투스 태스크에서 ----

  // 연결됐다. secured = 그 연결이 이미 암호화·결합돼 있는가(연결 정보에서 읽은 실제 상태).
  void onConnected(uint32_t nowMs, bool secured) {
    connectedAtMs_ = nowMs;
    if (!secured) {
      secured_ = false;  // 앞선 연결에서 남은 표시가 있더라도 믿지 않는다
    } else {
      markSecured(nowMs);
    }
    connected_ = true;
  }

  // 암호화·결합이 확인됐다. "연결됨"보다 먼저 와도 된다.
  // 이미 확인된 연결에서 또 와도(키 갱신 등) 시각을 다시 쓰지 않는다 — 다시 쓰면 그동안
  // "연결 중"으로 보여 말하기가 끊긴다.
  void onSecured(uint32_t nowMs) { markSecured(nowMs); }

  void onDisconnected() {
    connected_ = false;
    secured_ = false;
  }

  // 결합 정보를 지웠다 → 지금 연결도 더는 믿을 수 없다.
  void dropSecurity() { secured_ = false; }

  bool connected() const { return connected_; }
  bool secured() const { return secured_; }

  // ---- loop 태스크에서 ----

  // hasBond: 짝을 맺은 컴퓨터가 있는가(끊겨 있을 때 "찾는 중"과 "짝 맺기 대기"를 가른다)
  LinkState state(uint32_t nowMs, bool hasBond) {
    if (!connected_) {
      settled_ = false;
      return hasBond ? LinkState::Searching : LinkState::Pairing;
    }
    if (!secured_) {
      settled_ = false;
      return LinkState::Connecting;
    }
    // 한 번 정착하면 다시 재지 않는다(시각 비교는 약 49.7일마다 한 바퀴 돌기 때문).
    // (nowMs 가 securedAtMs_ 보다 조금 앞선 값이어도 "아직"으로 보게 reachedMs 를 쓴다.)
    if (!settled_ && reachedMs(nowMs, securedAtMs_ + settleMs_)) settled_ = true;
    return settled_ ? LinkState::Ready : LinkState::Connecting;
  }

  // 연결은 됐는데 암호화가 끝나지 않은 채 시간 제한을 넘겼으면 true.
  // true 를 한 번 주면 시간을 다시 잰다(끊는 중에 거듭 걸리지 않게).
  // nowMs 가 연결 시각보다 조금 앞선 값이어도(다른 태스크가 방금 기록) "아직"으로 본다.
  bool connectTimedOut(uint32_t nowMs) {
    if (!connected_ || secured_) return false;
    if (!reachedMs(nowMs, connectedAtMs_ + connectTimeoutMs_)) return false;
    connectedAtMs_ = nowMs;
    return true;
  }

 private:
  void markSecured(uint32_t nowMs) {
    if (secured_) return;
    securedAtMs_ = nowMs;
    secured_ = true;
  }

  const uint32_t settleMs_;
  const uint32_t connectTimeoutMs_;

  // 두 태스크가 주고받는 값
  volatile bool     connected_     = false;
  volatile bool     secured_       = false;
  volatile uint32_t connectedAtMs_ = 0;
  volatile uint32_t securedAtMs_   = 0;

  // loop 태스크에서만 쓰는 값
  bool settled_ = false;
};
