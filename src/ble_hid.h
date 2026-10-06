#pragma once
#include <stdint.h>

#include "logic/link_state.h"

// 블루투스 키보드. 컴퓨터는 이 기기를 평범한 키보드로 본다(D5).
namespace blehid {

using Link = LinkState;  // Ready = 암호화가 끝나고 정착 시간이 지나 키를 보낼 수 있음

void begin(const char* deviceName);
Link link();

// loop 에서 매 틱 부른다: 보내지 못한 "뗌"을 다시 보내고, 암호화가 끝나지 않는 연결을 끊는다.
void poll(uint32_t nowMs);

// 지금 눌린 키 상태를 보낸다. Ready 가 아니면 보내지 않고 false 를 돌려준다.
// 보내지 못한 키를 쌓아 두지 않는다(검토 초점 5).
bool send(uint8_t mods, uint8_t key);

// 모든 키를 뗀 상태를 보낸다. 연결은 살아 있는데 실패하면 poll() 이 다시 보낸다.
bool releaseAll();

void setBattery(uint8_t pct);

// 결합 정보를 모두 지운다. 실제로 지워졌으면 true — 이때는 기기를 재시작해
// 깨끗한 상태에서 새 컴퓨터를 기다리게 한다(부르는 쪽 책임).
// 실패하면 false 를 돌려주고 원래 상태(광고)를 되살린다.
bool forgetPeers();

}  // namespace blehid
