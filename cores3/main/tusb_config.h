// TinyUSB 설정: 이 기기는 USB 로 "마이크 + 키보드 + 로그 포트" 세 가지가 한 몸인 장치가 된다.
// (TinyUSB 라이브러리가 이 파일을 읽는다 — main/CMakeLists.txt 에서 포함 경로를 넣어 준다.)
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//------------- 공통 -------------//
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

// ESP32-S3 의 USB 는 Full Speed(12Mbps) 장치 하나다.
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_FREERTOS
#endif
#define CFG_TUSB_OS_INC_PATH freertos/

// 1 로 올리면 TinyUSB 가 스스로 진단 문구를 찍는다(끝점을 못 잡는 등의 조용한 실패를 볼 때).
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUD_ENABLED 1

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

//------------- 쓰는 기능 -------------//
#define CFG_TUD_AUDIO  1  // 마이크
#define CFG_TUD_HID    1  // 키보드
#define CFG_TUD_CDC    1  // 로그·명령 포트
#define CFG_TUD_MSC    0
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

//------------- 마이크(USB Audio Class 2) -------------//
// D39 16kHz · 16비트 · 한 채널.
//   이유: 받아쓰기(음성 인식)는 16kHz 로 충분하고, 같은 칩(ESP32-S3)에서 이 형식으로 맥에 붙여
//         녹음까지 확인한 공개 사례가 있다(voxstick). USB 로 흐르는 양이 가장 적다(1ms 에 32바이트).
//   비용: 음악 녹음용으로는 음질이 낮다(이 기기의 용도가 아니다).
//   탈출구: 아래 값을 48000 으로 바꾸면 끝점 크기·보관함·마이크 받는 속도가 함께 따라간다.
#define VIBE_MIC_SAMPLE_RATE 16000

#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN               TUD_AUDIO_MIC_ONE_CH_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT               1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ            64
#define CFG_TUD_AUDIO_ENABLE_EP_IN                  1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX  2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX          1
// 1ms 에 보내는 가장 큰 묶음: (16 + 1)표본 x 2바이트 = 34바이트
#define CFG_TUD_AUDIO_EP_SZ_IN \
  TUD_AUDIO_EP_SIZE(VIBE_MIC_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, \
                    CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX CFG_TUD_AUDIO_EP_SZ_IN
// 보내기 전 소리를 담아 두는 보관함: 묶음 48개분. TinyUSB 가 이것을 절반(25ms)쯤 찬 상태로
// 지키면서 보내는 양을 조절한다. 마이크는 8ms 씩 뭉쳐서 오므로(mic.cpp) 절반 근처에서 오르내린다.
// 크기는 모의 실험(tools/sim_flow.py)으로 정했다: 시계 차이 ±1000ppm, 도착이 4ms 까지 늦어지는
// 흔들림에서도 바닥(빈 묶음)과 천장(버림)에 15ms 넘게 여유가 남는다. 36개분이면 바닥 여유가 3ms 뿐이었다.
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (48 * CFG_TUD_AUDIO_EP_SZ_IN)

//------------- 키보드 -------------//
#define CFG_TUD_HID_EP_BUFSIZE 16  // 키보드 보고는 8바이트

//------------- 로그·명령 포트 -------------//
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 1024
#define CFG_TUD_CDC_EP_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif  // _TUSB_CONFIG_H_
