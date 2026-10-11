#pragma once
#include <stdbool.h>
#include <stdint.h>

// USB 설명자(컴퓨터에게 "나는 이런 장치다"라고 알리는 표)와, 컴퓨터가 마이크에 묻는 것들의 답.
#ifdef __cplusplus
extern "C" {
#endif

// D40 USB 식별값은 시험대용: 제조사 번호 0x303A(Espressif), 제품 번호 0x4015.
//   0x4015 = Espressif 가 TinyUSB 개발용으로 쓰는 관례(0x4000 + 쓰는 기능의 비트: 로그 1 + 키보드 4 + 소리 16).
//   이유: 맥·윈도우에서 동작이 알려진 형태(D14 와 같은 생각).
//   비용: 판매용으로는 쓸 수 없다(정식 번호를 받아야 한다).
//   탈출구: 아래 두 값. 바꾸면 컴퓨터는 새 장치로 본다(소리 입력을 다시 골라야 한다).
#define VIBE_USB_VID 0x303A
#define VIBE_USB_PID 0x4015

// 인터페이스 번호
enum {
  VIBE_ITF_AUDIO_CONTROL = 0,
  VIBE_ITF_AUDIO_STREAMING,  // 컴퓨터가 마이크를 열고 닫는 곳
  VIBE_ITF_HID,
  VIBE_ITF_CDC,
  VIBE_ITF_CDC_DATA,
  VIBE_ITF_TOTAL
};

// 일련번호 문자열을 정한다(기기마다 달라야 컴퓨터가 "이 마이크를 입력으로 쓴다"는 선택을 기억한다).
// USB 를 켜기 전에 부른다.
void usbdesc_set_serial(const char* serial);

// 컴퓨터가 설명자를 물어 온 횟수: [0] 장치, [1] 구성, [2] 문자열. USB 가 뜨지 않을 때
// "컴퓨터가 말을 걸기는 했는가"를 가리는 데 쓴다(diag.h).
extern volatile uint32_t usbdesc_requests[3];

// 컴퓨터가 정한 입력 음량(데시벨, -24 ~ +24)과 음소거. 소리 설정의 "입력 음량"이 여기로 온다.
// 소리에 곱하는 값은 컴퓨터가 "전체"에 쓴 값 + "채널 1"에 쓴 값이다(범위를 넘으면 끝값, D46).
int  usbdesc_mic_volume_db(void);
bool usbdesc_mic_muted(void);
// 위 합의 두 조각을 따로(진단용): 컴퓨터가 어느 쪽에 쓰는지 vibe:status 의 volm=·volc= 로 본다.
int usbdesc_mic_master_volume_db(void);
int usbdesc_mic_channel_volume_db(void);

#ifdef __cplusplus
}
#endif
