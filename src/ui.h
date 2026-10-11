#pragma once
#include <stdint.h>

#include "logic/events.h"
#include "logic/link_state.h"

// 화면. 무엇을 그릴지는 logic/screen_state, 어떻게 그릴지는 logic/screen_draw 가 정하고
// 여기서는 그것을 LCD 에 옮기고 밝기를 다룬다(설계서).
namespace ui {

void begin();
void setLink(LinkState l);
void setBattery(int pct);    // 0~100, 모르면 -1
// 기기가 방금 한 일(무엇을 어떤 그림으로 보일지는 logic/screen_state 가 정한다)
void sent(Ev e);             // 이 사건의 키를 보냈다
void notSent();              // 연결이 없어 보내지 못했다
void pairReset(bool ok);     // 짝 초기화 결과
void setNoStick(bool on);    // 조이스틱 없음 안내(맨 아래 띠)
// true 인 동안에는 화면을 어둡게 하지 않는다(USB 전원 등).
void setKeepAwake(bool keep);
// 매 틱 부른다. 그림이 달라졌으면 다시 그린다. anyInput: 이번 틱에 조작이 있었는가.
// 조작이 30초 없으면 화면을 어둡게 한다.
void tick(uint32_t nowMs, bool anyInput);

}  // namespace ui
