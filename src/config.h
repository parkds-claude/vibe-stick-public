#pragma once
#include <stdint.h>

#include "logic/dispatch.h"
#include "logic/keymap.h"

// 블루투스에 보이는 이름 (15자 이하 — 길면 애플 기기 목록에 안 보인다)
static const char* const DEVICE_NAME = "vibe-stick";

// 컴퓨터 설정. 맥이면 ⌘D(말하기)·⌘`(창 이동), 윈도우면 Ctrl+D (윈도우에서 쓸 때 HostOs::Windows 로 바꿔
// 다시 굽는다. 윈도우에는 창 이동 키가 아직 없다).
//
// D10 초점 키(기본은 끔 = {HostOs::Mac, 0, 0}):
//   커서가 Claude 입력창이 아닌 곳(편집기 등)에 있으면 ⌘D 가 다른 기능으로 동작한다.
//   이를 막으려면 (1) VS Code 의 keybindings.json 에
//       { "key": "f19", "command": "claude-vscode.focus" }
//   를 한 번 등록하고 (2) 아래를 {HostOs::Mac, hid::KEY_F19, 300} 으로 바꿔 다시 굽는다.
//   그러면 말하기 직전에 F19 를 먼저 보내 입력창으로 초점을 옮긴다.
//   비용: 말하기 시작이 0.3초쯤 늦어진다. 컴퓨터 설정을 바꾸는 일이라 사용자 승인 뒤에 켠다.
static const HostProfile HOST = {HostOs::Mac, 0, 0};

// D19 말하기 방식: 조이스틱을 누를 때마다 켜고 끈다(한 번 누르면 켜지고, 다시 누르면 꺼진다).
//   이유: 누른 채 말하는 것이 불안하고 조이스틱 내구성이 걱정된다는 사용자 의견(2026-10-05 실물 시험).
//   비용: 말이 끝난 뒤 한 번 더 눌러야 한다(끄지 않아도 15초 조용하면 컴퓨터가 스스로 끈다).
//         기기는 녹음이 켜져 있는지 알 수 없다 — 컴퓨터 화면의 마이크 표시를 본다.
//   탈출구: TalkMode::Hold 로 바꿔 다시 구우면 "누르는 동안만 말하기"(D18 의 톡/꾹 구분 포함).
static const TalkMode TALK_MODE = TalkMode::Toggle;

// 조이스틱을 "조이스틱이 달린 쪽(화면 위쪽)"으로 밀었을 때 y 가 + 가 되어야 한다.
// 실물에서 반대로 나오면 JOY_INVERT_Y 를, 위로 밀 때 x 가 변하면 JOY_SWAP_XY 를 true 로 바꾼다.
static const bool JOY_INVERT_Y = false;
static const bool JOY_SWAP_XY  = false;
// 오른쪽으로 밀었을 때 "이전 창"으로 가면(왼쪽·오른쪽이 반대) true 로 바꾼다.
static const bool JOY_INVERT_X = false;

// 버튼을 읽고 사건을 처리하는 주기. 버튼 떨림 거르기가 "두 틱 연속"이라 이 값에 묶여 있다
// (10ms 면 20ms 짜리 거르기). 바꾸면 거르기 시간도 함께 바뀐다.
static const uint32_t TICK_MS = 10;

// D15: 조이스틱은 20ms 마다 읽는다(버튼보다 느리게).
//   이유: 조이스틱 SDA 가 G0(부팅 설정 핀)이라, 리셋 순간에 통신 중이면 기기가
//         다운로드 모드로 빠진다. 통신 횟수를 줄여 그 확률을 낮춘다(같은 기기로 만든 이전 시제품의 교훈).
//   비용: 조이스틱 반응이 최대 20ms 늦다.
//   탈출구: 이 값을 바꾼다. JoyFilter 의 판정은 "읽기 횟수" 기준이라 함께 따라간다.
//           단, 늘리면 누름 인식이 그만큼 늦어지므로 logic/input_logic.h 의 DIR_DWELL_MS 도
//           함께 늘려야 한다(안 늘리면 main.cpp 의 static_assert 에서 빌드가 멈춘다).
static const uint32_t JOY_POLL_MS = 20;

// 키 보고 시간: 한 번 누르는 키는 30ms 누르고, 다음 보고까지 12ms 띄운다.
// (다른 BLE 키보드 구현들이 맥·iOS 에서 누락 없이 쓰는 값의 범위)
static const KeyTiming KEY_TIMING = {30, 12};

static const uint32_t JOY_RETRY_MS = 5000;   // 조이스틱이 없을 때 다시 찾는 주기
static const uint32_t BATTERY_MS   = 30000;  // 배터리·전원 상태 갱신 주기

// 조이스틱을 누르고 있는 동안 조이스틱 LED 를 켠다(눌린 것이 인식됐다는 표시).
// 32 는 실물에서 켜졌는지 알아보기 어려웠다(2026-10-05) → 120.
static const bool    TALK_LED       = true;
static const uint8_t TALK_LED_LEVEL = 120;

// 실물을 보고 조정할 값이 있는 곳
//   이 파일          : 조이스틱 방향, 읽기 주기, 키 보고 시간, LED 밝기
//   logic/pixel_art.h : 화면의 마스코트 고르기(MASCOT_ID — 다섯 가운데 하나, D26)
//   logic/input_logic.h (inputcfg) : 조이스틱 임계값(DIR_ON/OFF), 머묾 시간, 톡/꾹 경계, 취소 간격
//   ui.cpp           : 화면 밝기(BRIGHT/DIM), 어두워지는 시간(DIM_AFTER_MS)
//   ble_hid.cpp      : 정착 시간(SETTLE_MS), 연결 시간 제한(CONNECT_TIMEOUT_MS)
// (조작 판정 값이 logic/ 에 있는 것은 그 코드가 기기와 무관하게 컴퓨터에서 시험되기 때문이다.)

// 진단 로그. 파일을 고치지 않고 켜려면:
//   PLATFORMIO_BUILD_FLAGS="-DDEBUG_JOY=1 -DDEBUG_LINK=1" pio run -e stickc -t upload
#ifndef DEBUG_JOY
#define DEBUG_JOY 0  // 1: 조이스틱 값을 0.2초마다 찍는다(방향 확인용)
#endif
#ifndef DEBUG_LINK
#define DEBUG_LINK 0  // 1: 남은 메모리를 5초마다 찍는다
#endif
