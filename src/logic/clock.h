#pragma once
#include <stdint.h>

// 시간 비교 도우미. millis() 가 한 바퀴 돌아도(약 49.7일) 맞게 비교한다.

#ifdef CLOCK_AUDIT
// 시험용: elapsedMs 에 "now 보다 뒤인 since" 가 들어온 횟수. 그런 비교는 "아주 오래 지남"으로
// 계산되어 조용히 틀린 결과를 낸다(예: 다른 태스크나 같은 틱의 뒷부분에서 기록한 시각).
inline unsigned long& clockInversions() {
  static unsigned long n = 0;
  return n;
}
#endif

// since 로부터 ms 이상 지났는가.
// since 는 now 보다 앞선 시각이어야 한다. now 가 "틱이 시작될 때 읽어 둔 값"이고 since 가 그 뒤에
// 기록된 값일 수 있으면 이 함수 대신 reachedMs(now, since + ms) 를 쓴다.
inline bool elapsedMs(uint32_t now, uint32_t since, uint32_t ms) {
#ifdef CLOCK_AUDIT
  if ((int32_t)(now - since) < 0) ++clockInversions();
#endif
  return (uint32_t)(now - since) >= ms;
}

// 마감 시각 deadline 에 이르렀는가 (deadline 은 now 로부터 약 24일 이내여야 한다)
inline bool reachedMs(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

// 일정 주기마다 한 번씩 true 를 돌려주는 타이머
struct Every {
  uint32_t last = 0;
  bool due(uint32_t now, uint32_t ms) {
    if (!elapsedMs(now, last, ms)) return false;
    last = now;
    return true;
  }
};
