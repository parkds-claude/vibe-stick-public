#pragma once
// 진단용 흔적(빌드 옵션 -DDEBUG_LINK=1 일 때만 동작, 평소 빌드에서는 아무 코드도 남지 않는다).
//
// loop 가 지금 어느 단계에 있는지 번호로 적어 둔다. 따로 도는 감시 태스크(main.cpp)가
// loop 가 1초 넘게 돌지 않으면 그 번호를 시리얼로 알린다 → "어디서 멈췄는지"를 알 수 있다.
#include <stdint.h>

#if defined(DEBUG_LINK) && DEBUG_LINK
#include <Arduino.h>
extern volatile uint16_t dbgStage;  // loop 의 현재 단계
extern volatile uint32_t dbgBeat;   // loop 가 한 바퀴 돌 때마다 1 증가
#define DBG_STAGE(n) (dbgStage = (n))
#define DBG_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define DBG_STAGE(n) ((void)0)
#define DBG_LOG(...) ((void)0)
#endif
