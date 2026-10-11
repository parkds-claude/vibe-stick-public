#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 블랙박스: 재시작해도 지워지지 않는 자리에 "어디까지 갔는지"를 적어 둔다.
//
// 이 기기는 USB 가 떠야 로그를 볼 수 있다. USB 가 뜨지 않는 문제는 로그로 볼 수 없다 — 그래서 켜지는
// 단계, 컴퓨터가 물어 온 횟수, 내려받기 모드로 물러난 까닭을 여기에 적어 두고, 내려받기 모드에서
// 굽기 도구로 읽어 낸다(tools/diag.py). 전원을 껐다 켜면 지워진다.
// 값을 더하면 tools/diag.py 의 이름표도 함께 고칠 것(차례가 곧 약속이다).
typedef struct {
  uint32_t magic;            // DIAG_MAGIC 이면 아래 값이 쓸 만하다
  uint32_t boots;            // 전원을 넣은 뒤 켜진 횟수
  uint32_t stage;            // 이번에 어디까지 갔나(diag_stage_t)
  uint32_t uptime_ms;        // 이번에 얼마나 돌았나
  uint32_t reset_reason;     // 이번에 켜진 까닭(esp_reset_reason)
  uint32_t download_reason;  // 내려받기 모드로 물러난 까닭(diag_download_t). 0 = 물러난 적 없음
  uint32_t board;            // M5Unified 가 알아본 기종 번호
  uint32_t flags;            // 비트: DIAG_F_*
  uint32_t desc_device;      // 컴퓨터가 "장치 설명자"를 물어 온 횟수
  uint32_t desc_config;      // "구성 설명자"
  uint32_t desc_string;      // "문자열"
  uint32_t bus_resets;       // USB 버스 초기화 횟수
  uint32_t setups;           // 컴퓨터가 보낸 제어 요청 수
  uint32_t mic_opens;        // 컴퓨터가 마이크를 연 횟수
  uint32_t mic_level;        // 마지막 소리 크기
  uint32_t heap_min;         // 남은 메모리의 최저값
} vibe_diag_t;

#define DIAG_MAGIC 0x56444941u  // "VDIA"

typedef enum {
  DIAG_STAGE_START = 1,   // app_main 에 들어옴
  DIAG_STAGE_BOARD,       // M5.begin 끝(화면·전원·터치)
  DIAG_STAGE_SCREEN,      // 화면 준비
  DIAG_STAGE_USB_PHY,     // USB 통로 켬
  DIAG_STAGE_USB_STACK,   // TinyUSB 켬
  DIAG_STAGE_USB_TASK,    // USB 태스크 만듦
  DIAG_STAGE_MIC,         // 마이크 켬
  DIAG_STAGE_LOOP,        // loop 가 돌기 시작
} diag_stage_t;

typedef enum {
  DIAG_DL_COMMAND = 1,    // vibe:boot 명령(개발판) 
  DIAG_DL_CONFIRMED,      // 화면에서 확인을 누름(평소 판)
  DIAG_DL_NEVER_MOUNTED,  // 개발판 안전장치: 컴퓨터가 잡지 않음
  DIAG_DL_NO_COMMAND,     // 개발판 안전장치: 명령이 없음
  DIAG_DL_USB_INIT,       // USB 를 켜지 못함
  DIAG_DL_CRASH_LOOP,     // 연달아 죽음
} diag_download_t;

#define DIAG_F_MOUNTED_EVER 0x01  // 컴퓨터가 장치를 잡은 적이 있다
#define DIAG_F_MOUNTED_NOW  0x02
#define DIAG_F_SUSPENDED    0x04
#define DIAG_F_LOG_OPENED   0x08  // 컴퓨터가 로그 포트를 연 적이 있다
#define DIAG_F_MIC_OK       0x10  // 마이크를 켜는 데 성공
#define DIAG_F_DEV_BUILD    0x20

extern vibe_diag_t g_vibe_diag;

// app_main 맨 처음에 부른다: 처음이면 자리를 만들고, 이번 켜짐의 값들을 비운다.
void diag_boot(void);

#ifdef __cplusplus
}
#endif
