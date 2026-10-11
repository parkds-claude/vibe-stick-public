#pragma once
#include <stdint.h>

#include "logic/events.h"
#include "logic/link_state.h"

// 화면. 무엇을 그릴지는 logic/touch_state, 어떻게 그릴지는 logic/touch_draw 가 정하고
// 여기서는 그것을 LCD 에 옮기고 밝기를 다룬다.
namespace ui {

void begin();
// 컴퓨터와의 연결 상태(Ready = 키를 보낼 수 있다). wireless = 그 상태가 블루투스의 것인가(D59)
void setLink(LinkState link, bool wireless);
void pairReset(bool ok);           // 블루투스의 짝을 지우려 했다(ok = 지워졌다)
void setPressed(uint8_t mask);     // 지금 눌려 있는 자리들(touchui::bit(Zone))
void setMicLive(bool live);        // 컴퓨터가 이 기기의 마이크를 열었는가
void setVoice(bool on);            // 지금 목소리가 들어오는가(기기 마이크가 듣는 방의 소리)
void setHeard(bool on);            // 방의 소리에서 말소리가 들리는가(logic/speech_ear.h — 미루어 듣는 중을 이어 간다)
bool guessing();                   // 지금 듣는 중이 "미룸"인가(D65)
int  flipListening();              // 보는 곳을 톡 쳤다: 키 없이 듣는 중 표시만 뒤집는다(D71). TouchState::ViewTap 의 번호(0 듣지 않음 · 1 뒤집음 · 2 알림만 거둠)
bool truthKnown();                 // 말하기를 누르면 컴퓨터가 이 기기의 마이크를 여는 것이 확인됐는가(D74 — 기록용)
bool bright();                     // 화면이 밝게 켜져 있는가(어두워졌거나 꺼져 있지 않다 — 이번 틱의 ui::tick 전 상태)
int  takeGuessEnd();               // 미룸이 내려간 까닭(TouchState::GuessEnd 의 번호, 없으면 0). 한 번 읽으면 지워진다
bool dark();                       // 화면이 꺼져 있는가(이때의 누름은 화면만 깨운다 — D69)
bool listening();                  // 화면이 "듣는 중"을 보이고 있는가(컴퓨터가 마이크를 열었거나, 말하기를 누른 것으로 미룸 — D65)
void setBattery(int pct, bool charging);  // 배터리 남은 양(0~100, 모르면 음수)과 충전 여부 — 상태 줄(D54)
void sent(Ev e);                   // 이 사건의 키를 보냈다
void notSent();                    // 연결이 없어 보내지 못했다
// 컴퓨터가 펌웨어를 바꾸겠다고 했다: 화면에 `Update firmware?` 를 띄우고 사람의 확인을 기다린다(D47).
bool askUpdate();    // 물음을 띄웠으면 true(이미 묻는 중이거나 방금 끝난 직후면 무시하고 false)
void cancelUpdate();
bool updateAsking(uint32_t nowMs);  // 묻는 중인가(이 동안 확인·취소는 키로 나가지 않는다)
bool updateArmed(uint32_t nowMs);   // 확인을 "예"로 받아도 되는가(뜬 지 1.5초 뒤부터)
// 화면 그리기를 잠시 멈춘다/다시 시작한다. 다른 것이 화면 전체를 덮어 그리는 동안(개발판의 시안 그림)
// 멈춰 두고, 다시 시작하면 바탕부터 전부 새로 그린다. 무엇을 보여야 하는지는 멈춘 동안에도 따라간다.
void hold(bool on);
// 매 틱 부른다. 달라진 자리가 있으면 한 틱에 한 자리씩 다시 그린다.
// touching: 화면에 손가락이 닿아 있는가. moving: 기기가 움직이고 있는가. near: 손이 가까이 있는가
// (셋 다 어두워진 화면을 깨운다 — D60·D61).
void tick(uint32_t nowMs, bool touching, bool moving, bool near);
void setAmbient(uint16_t als);      // 주변 밝기(센서의 날 값) → 화면 밝기 단계(D60)
void setClock(int minutesOfDay);    // 시계 칩의 시각(자정부터의 분, 모르면 음수) — 쉴 때 캐릭터 옆에 보인다(D62)
void note(bool ask);                // 컴퓨터의 Claude 가 알려 왔다: 끝났다(false) / 답을 기다린다(true) — D63

}  // namespace ui
