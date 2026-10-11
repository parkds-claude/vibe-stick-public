#pragma once
#include <stddef.h>
#include <stdint.h>

#include "logic/events.h"
#include "logic/link_state.h"

// USB 장치: 컴퓨터는 이 기기를 "키보드 + 마이크 + 로그 포트"로 본다(D32).
// StickC 판의 blehid 와 같은 모양이다 — 전송 방식만 바뀌고 loop 는 그대로다.
namespace usbdev {

// Ready      컴퓨터가 장치를 쓰기 시작했고 잠들지 않았다(키를 보낼 수 있다)
// Connecting 컴퓨터가 장치를 알아봤지만(설명자를 읽어 갔다) 아직 쓰기 시작하지 않았다 —
//            맥북이 "액세서리 연결을 허용하겠습니까?" 를 묻고 있을 때 이 상태에 머문다
// Searching  컴퓨터가 없다(뽑힘·잠듦)
using Link = LinkState;

void begin();
Link link();

// loop 에서 매 틱 부른다: 보내지 못한 "뗌"을 다시 보내고, 로그 포트의 명령을 처리한다.
void poll(uint32_t nowMs);

// 지금 눌린 키 상태를 보낸다. 보낼 수 없으면 false(쌓아 두지 않는다).
bool send(uint8_t mods, uint8_t key);
// 모든 키를 뗀 상태를 보낸다. 지금 못 보내면 poll() 이 다시 보낸다.
bool releaseAll();

// 컴퓨터가 이 기기의 마이크를 열어 듣고 있는가
bool micOpen();
// 마이크가 받은 표본을 넘긴다(마이크 태스크가 부른다). 컴퓨터가 듣고 있지 않으면 버린다.
void micPush(const int16_t* samples, size_t n);

// 컴퓨터가 로그 포트를 열어 두었는가
bool logOpen();
// 로그 포트로 한 줄 보낸다(컴퓨터가 포트를 열어 두었을 때만). loop 에서만 부른다.
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// 컴퓨터가 보낸 명령(vibe:time · vibe:note · vibe:boot)에 답한다: 로그 포트로는 늘 나가고, 기기 속 기록에는 10초에
// 여덟 줄까지만 남는다 — 같은 컴퓨터의 프로그램이 명령을 퍼부어 기록을 통째로 밀어내지 못하게(보안 점검).
void cmdf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// 기기 속 기록(D67)에만 적는다(USB 로 내보내지 않는다). 어느 태스크에서 불러도 된다.
void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// (개발판) 로그 포트로 들어온 시험용 사건을 하나 꺼낸다. 없으면 false.
bool takeInjected(Ev& out);

// (개발판) 시안 그림 띄우기(dev_show.h). 컴퓨터가 `vibe:img [방향]` 을 보냈으면 그 방향(0~3),
// `vibe:img off` 면 IMAGE_OFF, 아무것도 없었으면 IMAGE_NONE. 한 번 주고 지운다. 평소 판은 늘 IMAGE_NONE.
const int IMAGE_NONE = -1, IMAGE_OFF = -2;
int takeImageCmd();
// (개발판) 그림 바이트를 받는 동안에는 로그 포트를 명령 줄로 읽지 않는다.
void setRawMode(bool on);
// (개발판) 로그 포트에 온 바이트를 그대로 읽는다. 읽은 수(없으면 0).
size_t readRaw(uint8_t* buf, size_t n);

// 컴퓨터가 시각을 알려 왔는가(vibe:time YYYYMMDDHHMMSS — D62). out = {년, 월, 일, 시, 분, 초}. 한 번 주고 지운다.
bool takeTime(int out[6]);
// 컴퓨터의 Claude 가 알려 온 것(vibe:note done | ask — D63): 0 = 끝났다, 1 = 답을 기다린다, 없으면 -1. 한 번 주고 지운다.
// 화면과 소리만 바꾸는 명령이라 평소 판에도 둔다(키를 치게 하거나 펌웨어를 바꾸는 길이 아니다). 1초에 한 번만 받는다.
int takeNote();

// 컴퓨터가 "펌웨어를 바꾸겠다"(vibe:boot)고 했는가. 한 번 true 를 주고 지운다.
// 따를지는 부르는 쪽이 정한다(평소 판은 화면에서 확인을 받는다 — D47).
bool takeBootRequest();
// 내려받기 모드로 재시작한다. 돌아오지 않는다. loop 에서만 부른다.
// reason 은 블랙박스에 남길 까닭(diag.h 의 DIAG_DL_*).
[[noreturn]] void enterDownloadMode(const char* why, int reason);

}  // namespace usbdev
