#pragma once
#include <stdint.h>

#include "logic/dispatch.h"
#include "logic/input_logic.h"
#include "logic/keymap.h"
#include "tusb_config.h"

// vibe-stick CoreS3 판의 조정 값. 실물을 보고 바꿀 것은 대부분 여기에 있다.

// 컴퓨터 설정. 맥이면 ⌘D(말하기)·⌘`(창 이동), 윈도우면 Ctrl+D (StickC 판의 config.h 와 같은 뜻).
// 초점 키(D10)는 쓰지 않는다 — 켜는 법과 조건은 src/config.h 의 설명을 따른다.
static const HostProfile HOST = {HostOs::Mac, 0, 0};

// D58 Back·Next 를 짧게 눌렀을 때 보내는 키: F17(이전)·F18(다음). 컴퓨터의 VS Code 에 이 키를
// "이전·다음 편집기 묶음으로 초점"에 묶어 둔다(docs/사용법-cores3.md). 다른 프로그램에서는 아무 일도 하지 않는
// 키라, 초점이 엉뚱한 곳에 있어도 해가 없다. (F19 는 초점 키 D10 의 후보라 비워 둔다 — src/logic/keymap.h)
static const uint8_t KEY_PANE_PREV = 0x6C;  // F17
static const uint8_t KEY_PANE_NEXT = 0x6D;  // F18

// D59 블루투스 이름. 조이스틱 판("vibe-stick")과 목록에서 구분되게 한다. 애플 기기 목록에 보이려면 15자 이하.
static const char* const BLE_NAME = "vibe-stick S3";

// D19 말하기 방식: 얼굴을 누를 때마다 켜고 끈다.
static const TalkMode TALK_MODE = TalkMode::Toggle;

// 터치를 읽고 사건을 처리하는 주기. 터치·버튼의 떨림 거르기가 "몇 틱 연속"이라 이 값에 묶여 있다.
static const uint32_t TICK_MS = 10;

// 키 보고 시간: 한 번 누르는 키는 30ms 누르고, 다음 보고까지 12ms 띄운다(StickC 판과 같다).
// USB 는 컴퓨터가 4ms 마다 키보드에 묻기 때문에 30ms 면 누름과 뗌이 따로 전해진다.
static const KeyTiming KEY_TIMING = {30, 12};

// 마이크. USB 로 내보내는 것과 같은 속도로 받는다(변환하지 않는다).
static const uint32_t MIC_SAMPLE_RATE = VIBE_MIC_SAMPLE_RATE;
// 마이크 칩이 준 값에 곱하는 수(M5Unified 의 magnification). CoreS3 의 기본값은 2.
// 실물에서 맥의 입력 레벨이 낮거나 찢어지면 여기를 바꾼다. 사용자는 맥의 "입력 음량"으로도 조절할 수 있다.
static const uint8_t MIC_MAGNIFICATION = 2;

// 목소리가 들어오는지 가르는 문턱(8ms 덩어리의 RMS, 0~32767). 말하는 동안 입을 여닫는 데만 쓴다.
// 조용한 방의 값보다 넉넉히 높게, 보통 목소리보다 낮게 잡는다(실측은 개발 기록(공개판에는 없음)).
// 실측(8ms 덩어리의 RMS): 조용한 사무실 25~100, 말소리의 절반이 103 이상·4분의 1이 397 이상.
// 처음 값(900/500)은 말소리의 90%를 놓쳐 입과 파형이 거의 움직이지 않았다 → 조용한 값의 세 배쯤으로 낮춤.
static const uint16_t VOICE_ON_LEVEL  = 300;
static const uint16_t VOICE_OFF_LEVEL = 150;
static const uint32_t VOICE_HOLD_MS   = 150;
// "말소리가 들린다"를 가리는 값(logic/speech_ear.h — 미루어 듣는 중 D65 를 이어 가는 데 쓴다).
// 방의 바닥 소리의 GUESS_VOICE_RATIO 배 이상이고 GUESS_VOICE_MIN 이상인 소리만 말소리로 친다.
// 그리고 그런 소리가 GUESS_VOICE_RUN 틱쯤 이어져야 한다(D70 — 툭 하고 한 번 튀는 소리는 말소리가 아니다).
// 맥북이 놓인 방: 평소 48~250, 몇 초에 한 번 349~702 까지 튄다. 처음 값 400 은 그 튀는 소리에
// 번번이 걸려 15초 조용함이 오지 않았다 → 600 으로 올리고, 이어져야 치게 했다. 가까이에서 말하면 수천~2만이다.
// 기기의 기록(vibe:hist)의 [ear] 줄(peak·loud·heard)로 맞춘다.
static const uint16_t GUESS_VOICE_MIN   = 600;
static const uint8_t  GUESS_VOICE_RATIO = 3;
static const uint8_t  GUESS_VOICE_RUN   = 4;
// 기록에만 쓰는 낮은 후보 문턱: [ear] 줄의 lo=(이 값을 넘은 틱 수) · lorun=(가장 길게 이어진 틱 수).
// GUESS_VOICE_MIN 을 내려도 되는지 볼 자료다 — 조용한 방에서 lorun 이 GUESS_VOICE_RUN 보다 짧고, 말할 때는 길어야 한다.
static const uint16_t EAR_PROBE_LEVEL   = 400;
// [ear] 줄을 적는 간격: 듣는 중의 처음 EAR_LOG_DENSE_MS 동안은 EAR_LOG_MS 마다, 그 뒤로는 EAR_LOG_SPARSE_MS 마다.
static const uint32_t EAR_LOG_MS        = 5000;
static const uint32_t EAR_LOG_DENSE_MS  = 60000;
static const uint32_t EAR_LOG_SPARSE_MS = 60000;

// 누름을 받지 않는 뜸(D69, logic/quiet_gate.h): 그런 때의 누름은 "지금 화면을 보고 한 것"이 아니다.
static const uint32_t QUIET_BOOT_MS      = 3000;  // 켜진 뒤 적어도 이만큼은 받지 않는다(굽고 난 직후의 잇단 ✓ 가 Enter 로 나갔다)
static const uint32_t QUIET_BOOT_CALM_MS = 1000;  // 그리고 손을 떼고 이만큼 조용해야 풀린다
static const uint32_t QUIET_LINK_CALM_MS = 500;   // 연결이 막 쓸 수 있게 됐거나 길(선 ↔ 블루투스)이 바뀐 뒤
static const uint32_t QUIET_WAKE_CALM_MS = 300;   // 꺼진 화면을 깨운 누름: 그 손가락을 떼고 이만큼 뒤부터 받는다

// 화면 방향(D55): 0 = 세로 240x320, USB 단자가 위. 왼손에 세로로 쥐면 선이 위로 빠진다
// (기기에 세로 그림을 띄워 방향이 맞음을 확인). 평소 방향 1 은 가로·USB 가 왼쪽이다.
static const uint8_t SCREEN_ROTATION = 0;

// 화면 밝기(0~255). 이 기종은 전원 칩이 백라이트 전압을 9단계로 바꾼다(1~255 → 가장 어두움~가장 밝음).
// D60 주변 밝기에 따라 세 벌을 쓴다: 어두운 방 · 보통 · 아주 밝은 곳(logic/screen_power.h 의 AutoBright).
static const uint8_t  SCREEN_BRIGHT_BY_LIGHT[3] = {90, 160, 255};
static const uint8_t  SCREEN_DIM_BY_LIGHT[3]    = {12, 24, 40};
static const uint32_t SCREEN_DIM_AFTER_MS = 60000;   // 손대지 않으면 어둡게
static const uint32_t SCREEN_OFF_AFTER_MS = 300000;  // 컴퓨터가 없으면(잠듦·뽑힘) 아예 끈다

// 배터리 남은 양·충전 여부를 읽는 주기(StickC 판의 BATTERY_MS 와 같다). 전원 칩에 I2C 로 묻는다.
static const uint32_t BATTERY_MS = 30000;

// 로그 포트에서 한 틱에 읽는 바이트 수의 한도(logic/line_reader.h 의 pumpLines).
//   이유: 한도가 없으면 컴퓨터가 쉬지 않고 보내는 동안 loop 가 읽기에서 빠져나오지 못해 터치·화면이
//         그만큼 밀린다(코덱스 검토).
//   비용: 로그 포트로 받는 양이 한 틱(10ms)에 이만큼으로 묶인다. 명령은 한 줄에 47자까지라 넉넉하고,
//         넘는 만큼은 USB 가 붙들고 있다가 다음 틱에 넘겨준다(버려지지 않는다).
//   탈출구: 이 값을 키운다.
static const int LOG_READ_BUDGET = 256;

// ---- 놀고 있던 부품들 (D60~D63, 설계 문서(공개판에는 없음)) ----
// 기기에서 잰 것(책상 위, 손 없음, 사무실): 근접 3~20 · 밝기 49~50 · 가속도 흔들림 6mg 이하(0.25초 간격으로 본 값).
// ⚠ 손을 댔을 때·쥐었을 때·흔들 때의 값은 아직 재지 못했다 → 근접·움직임·흔들기의 문턱은 어림값이다.
//    개발판(tools/flash.sh --dev)의 [sens] 기록으로 재고 고칠 것(개발 기록(공개판에는 없음)).
// D60 근접 센서(0~2047, 가까울수록 크다): 이 값 이상이면 손이 가까이 있다 / 이 값 이하면 멀어졌다
static const uint16_t PROX_NEAR = 120, PROX_FAR = 60;
// D60 밝기 센서의 날 값: 어두운 방으로 가는 문턱·돌아오는 문턱, 아주 밝은 곳으로 가는 문턱·돌아오는 문턱
static const uint16_t ALS_DARK_BELOW = 8, ALS_DARK_ABOVE = 20, ALS_SUNNY_ABOVE = 1000, ALS_SUNNY_BELOW = 600;
// D61 움직임: 두 번의 읽기(0.01초) 사이에 세 축의 변화가 합쳐 이만큼(1000 = 1g) 넘으면 움직인 것, 그 뒤 이만큼은 움직이는 중
static const int      MOTION_MOVE_MG = 60;
static const uint32_t MOTION_HOLD_MS = 3000;
// D61 흔들어서 취소(Esc). 잘못 알아보면 진행 중인 일이 멈추므로 끌 수 있게 둔다.
static const bool     SHAKE_CANCEL = true;
static const int      SHAKE_STRONG_MG = 1200;   // 중력을 뺀 가속이 이만큼 넘는 흔들림이
static const int      SHAKE_SWINGS = 4;         // 방향을 바꿔 가며 이만큼
static const uint32_t SHAKE_WINDOW_MS = 800;    // 이 시간 안에
static const uint32_t SHAKE_COOLDOWN_MS = 1500; // 알아본 뒤 쉬는 시간
// D62 시계: 손을 뗀 뒤 이만큼 지나면 캐릭터 옆에 시각을 보인다
static const uint32_t CLOCK_AFTER_MS = 4000;
// D63 소리(누름 소리, Claude 알림음). 컴퓨터가 듣는 중에는 내지 않는다.
static const bool     SOUND_ON = true;
static const uint8_t  SOUND_VOLUME = 90;        // 0~255

// ---- 개발판 ----
// 개발판은 `idf.py -DVIBE_DEV=1 build` 로 만든다(tools/flash.sh --dev). 평소 판과 다른 점:
//   - D42 안전장치: 컴퓨터와 대화(로그 포트의 명령)가 DEV_RESCUE_MS 동안 없으면 스스로 내려받기
//     모드로 돌아간다. USB 가 잘못돼 명령을 받을 수 없게 돼도 사람 손 없이 다시 구울 수 있게 한다.
//     컴퓨터가 기기를 알아봤지만 아직 쓰지 않는 동안(컴퓨터가 잠겨 있거나 "허용"을 기다릴 때)은
//     DEV_ALLOW_WAIT_MS 까지 기다린다 — 맥북이 잠겨 있어 15초 만에 물러난 일이 있었다.
//     언제 물러나는지의 판정은 logic/dev_rescue.h 에 있다(컴퓨터에서 시험한다).
//   - D43 시험용 사건 주입: 로그 포트로 `vibe:ev ok` 처럼 보내면 그 버튼을 누른 것처럼 동작한다.
//     평소 판에는 넣지 않는다 — 컴퓨터의 아무 프로그램이나 이 기기에게 키를 치게 할 수 있게 되기 때문이다.
//   - 시험 신호: `vibe:ramp 1` 을 보내면 마이크 소리 대신 1씩 올라가는 숫자를 내보낸다(mic.h).
#ifndef DEV_BUILD
#define DEV_BUILD 0
#endif
static const uint32_t DEV_MOUNT_WAIT_MS = 15000;   // 컴퓨터가 이만큼 말조차 걸어 오지 않으면 내려받기 모드로
static const uint32_t DEV_ALLOW_WAIT_MS = 1800000; // 말은 걸어 왔는데 쓰기 시작하지 않으면(잠김·허용 대기) 30분까지 기다린다
static const uint32_t DEV_RESCUE_MS     = 180000;  // (쓰기 시작한 뒤) 명령이 이만큼 없으면 내려받기 모드로

// 진단 로그: 1 이면 화면을 다시 그릴 때마다 걸린 시간을 찍는다.
#ifndef DEBUG_UI
#define DEBUG_UI 0
#endif
