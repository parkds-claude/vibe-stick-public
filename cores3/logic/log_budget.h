#pragma once
#include <stdint.h>

#include "logic/clock.h"

// 같은 종류의 기록 줄이 기기 속 기록(D67 — 몇 KB 의 고리)을 채우지 않게 하는 예산. 하드웨어를 모른다.
//   까닭: 기록의 쓸모는 "선이 뽑혀 있던 동안 무슨 일이 있었나"(연결·블루투스 줄)를 나중에 읽는 것인데, 위쪽을
//         계속 두드리거나(`[view]`), 연결 없이 ▲ 를 누르고 있으면(`[key] not sent` 가 0.4초마다) 그런 줄이 고리를
//         채워 정작 봐야 할 줄을 밀어낸다(통합 점검: 136번의 톡, 35초의 누름이면 6KB 가 찬다).
//   규칙: windowMs 동안 burst 줄까지만 적는다. 넘친 것은 세어 두었다가, 다음에 적을 때 함께 알린다(takeSkipped).
//   비용: 몰아서 일어난 일의 낱낱의 시각은 남지 않는다(몇 번이었는지만 남는다).
namespace touchui {

class LogBudget {
 public:
  LogBudget(uint8_t burst, uint32_t windowMs) : burst_(burst), windowMs_(windowMs) {}

  // 지금 한 줄을 적어도 되는가. 안 되면 "못 적은 수"에 더한다.
  bool allow(uint32_t nowMs) {
    if (!started_ || elapsedMs(nowMs, windowAtMs_, windowMs_)) {  // 새 창
      started_ = true;
      windowAtMs_ = nowMs;
      used_ = 0;
    }
    if (used_ < burst_) {
      ++used_;
      return true;
    }
    if (skipped_ < 65535) ++skipped_;
    return false;
  }

  // 그동안 적지 못한 줄의 수. 읽으면 지워진다.
  uint16_t takeSkipped() {
    const uint16_t n = skipped_;
    skipped_ = 0;
    return n;
  }

 private:
  const uint8_t  burst_;
  const uint32_t windowMs_;
  bool           started_ = false;
  uint32_t       windowAtMs_ = 0;  // 지금 창이 시작된 시각
  uint8_t        used_ = 0;
  uint16_t       skipped_ = 0;
};

}  // namespace touchui
