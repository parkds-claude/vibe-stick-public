// USB 설명자와 마이크 제어 요청의 답.
// 마이크 부분은 TinyUSB 의 공식 예제(examples/device/audio_test_freertos, MIT)를 따랐다:
// 설명자는 TinyUSB 가 제공하는 "마이크 한 채널" 표준 묶음을 그대로 쓰고, 요청 처리의 뼈대도 같다.
#include "usb_desc.h"

#include <string.h>

#include "tusb.h"

// 끝점 번호. ESP32-S3 는 "컴퓨터로 보내는(IN)" 끝점을 0번 포함 5개까지만 쓸 수 있고,
// 번호는 1~4 여야 한다(번호가 곧 보내는 통로 번호라서). 그래서 IN 넷을 1~4 에 딱 맞춰 쓴다.
#define EPNUM_AUDIO_IN  0x81
#define EPNUM_HID_IN    0x82
#define EPNUM_CDC_NOTIF 0x83
#define EPNUM_CDC_IN    0x84
#define EPNUM_CDC_OUT   0x04

// 문자열 번호
enum { STR_LANG = 0, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_MIC, STR_LOG, STR_COUNT };

// 마이크 설명자 안의 부품 번호(TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR 가 정해 둔 값)
#define ENTITY_INPUT_TERMINAL 1
#define ENTITY_FEATURE_UNIT   2
#define ENTITY_CLOCK          4

// 입력 음량의 범위(데시벨)
#define VOLUME_MIN_DB (-24)
#define VOLUME_MAX_DB 24

//--------------------------------------------------------------------+
// 장치 설명자
//--------------------------------------------------------------------+
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // 여러 기능이 한 몸인 장치(기능 묶음 표시 IAD 를 쓴다는 뜻)
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = VIBE_USB_VID,
    .idProduct = VIBE_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 0x01,
};

volatile uint32_t usbdesc_requests[3];

uint8_t const* tud_descriptor_device_cb(void) {
  ++usbdesc_requests[0];
  return (uint8_t const*)&desc_device;
}

//--------------------------------------------------------------------+
// 키보드 보고 설명자: 표준 키보드(수정키 1바이트 + 예약 1바이트 + 키 6개)
//--------------------------------------------------------------------+
static const uint8_t desc_hid_report[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

// 컴퓨터가 키보드 상태를 묻거나(GET) 불빛(Caps Lock 등)을 알려 줄 때(SET). 쓰지 않는다.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

//--------------------------------------------------------------------+
// 구성 설명자: 마이크(인터페이스 2개) + 키보드(1개) + 로그 포트(2개)
//--------------------------------------------------------------------+
#define CONFIG_TOTAL_LEN \
  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_MIC_ONE_CH_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // 번호, 인터페이스 수, 문자열, 전체 길이, 속성(USB 전원), 전류(mA)
    TUD_CONFIG_DESCRIPTOR(1, VIBE_ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),

    // 마이크: 첫 인터페이스, 이름, 표본 바이트 수, 비트 수, 끝점, 묶음 크기
    TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(VIBE_ITF_AUDIO_CONTROL, STR_MIC,
                                    CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
                                    CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * 8, EPNUM_AUDIO_IN,
                                    CFG_TUD_AUDIO_EP_SZ_IN),

    // 키보드: 인터페이스, 문자열, 부트 규약, 보고 설명자 길이, 끝점, 크기, 묻는 간격(ms)
    TUD_HID_DESCRIPTOR(VIBE_ITF_HID, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report),
                       EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 4),

    // 로그 포트: 인터페이스, 문자열, 알림 끝점·크기, 받는 끝점, 보내는 끝점, 크기
    TUD_CDC_DESCRIPTOR(VIBE_ITF_CDC, STR_LOG, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  ++usbdesc_requests[1];
  return desc_configuration;
}

//--------------------------------------------------------------------+
// 문자열
//--------------------------------------------------------------------+
static char serial_str[17] = "0000";

void usbdesc_set_serial(const char* serial) {
  strncpy(serial_str, serial, sizeof(serial_str) - 1);
  serial_str[sizeof(serial_str) - 1] = '\0';
}

static const char* string_for(uint8_t index) {
  switch (index) {
    case STR_MANUFACTURER: return "vibe-stick";
    case STR_PRODUCT:      return "vibe-stick";      // 키보드 목록에 보이는 이름
    case STR_SERIAL:       return serial_str;
    case STR_MIC:          return "vibe-stick mic";  // 소리 설정의 입력 목록에 보이는 이름
    case STR_LOG:          return "vibe-stick log";
    default:               return NULL;
  }
}

static uint16_t desc_str[32 + 1];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  ++usbdesc_requests[2];
  size_t count;
  if (index == STR_LANG) {
    desc_str[1] = 0x0409;  // 영어(미국)
    count = 1;
  } else {
    const char* str = string_for(index);
    if (str == NULL) return NULL;
    count = strlen(str);
    if (count > 32) count = 32;
    for (size_t i = 0; i < count; i++) desc_str[1 + i] = (uint16_t)str[i];
  }
  // 첫 칸: 길이(바이트)와 종류
  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
  return desc_str;
}

//--------------------------------------------------------------------+
// 마이크 제어: 컴퓨터가 묻는 것(주파수·음량·음소거)에 답한다
//--------------------------------------------------------------------+
// [0] 은 전체, [1] 은 채널 1. 컴퓨터가 어느 쪽을 쓰든 받는다.
static bool    mic_mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static int16_t mic_volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];  // 1/256 데시벨 단위

// D46 소리에 곱하는 음량은 "전체 + 채널 1"(데시벨의 합)이고, 합이 범위를 넘으면 끝값으로 자른다.
//   이유: USB 오디오 규격에서 전체와 채널은 차례로 걸리는 두 조절이다(데시벨로는 더한다). 그리고 이
//         기기는 둘 다 "읽고 쓸 수 있다"고 알린다 — 알린 대로 움직여야 한다. 예전에는 "전체를 쓴 적이
//         있으면 전체만" 썼는데, 그러면 컴퓨터가 전체에 0 을 쓴 뒤 채널 1 을 -24 로 낮춰도 요청은
//         성공하고 되물으면 -24 라고 답하면서 소리는 그대로였다(코덱스 검토).
//   비용: 같은 조절값을 전체와 채널 양쪽에 쓰는(규격 밖의) 컴퓨터가 있다면 두 배가 걸린다. 맥이 어느
//         쪽에 쓰는지는 아직 기기에서 보지 못했다 → vibe:status 의 volm=(전체)·volc=(채널 1)로 확인한다.
//   탈출구: 아래 합을 한쪽만 쓰게 고친다(예: 맥이 양쪽에 쓴다면 mic_volume[0] 만).
int usbdesc_mic_volume_db(void) {
  int v = (int)mic_volume[0] + (int)mic_volume[1];
  if (v < VOLUME_MIN_DB * 256) v = VOLUME_MIN_DB * 256;
  if (v > VOLUME_MAX_DB * 256) v = VOLUME_MAX_DB * 256;
  return v / 256;
}

int usbdesc_mic_master_volume_db(void) { return mic_volume[0] / 256; }
int usbdesc_mic_channel_volume_db(void) { return mic_volume[1] / 256; }

bool usbdesc_mic_muted(void) { return mic_mute[0] || mic_mute[1]; }

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request,
                                 uint8_t* pBuff) {
  (void)rhport;
  const uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  const uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  const uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // 지금 값을 정하는 요청만 받는다.
  TU_VERIFY(p_request->bRequest == AUDIO_CS_REQ_CUR);

  if (entityID == ENTITY_FEATURE_UNIT) {
    // 채널 번호는 컴퓨터가 준 값이다 → 표의 크기를 넘지 않는지 반드시 확인한다.
    TU_VERIFY(channelNum <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX);
    switch (ctrlSel) {
      case AUDIO_FU_CTRL_MUTE:
        TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_1_t));
        mic_mute[channelNum] = ((audio_control_cur_1_t*)pBuff)->bCur != 0;
        return true;
      case AUDIO_FU_CTRL_VOLUME: {
        TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_2_t));
        int v = (int16_t)((audio_control_cur_2_t*)pBuff)->bCur;
        if (v < VOLUME_MIN_DB * 256) v = VOLUME_MIN_DB * 256;
        if (v > VOLUME_MAX_DB * 256) v = VOLUME_MAX_DB * 256;
        mic_volume[channelNum] = (int16_t)v;
        return true;
      }
      default:
        return false;
    }
  }
  // 시계: 주파수는 16kHz 하나뿐이다. 같은 값을 "정하는" 요청은 받아 주고 다른 값은 거절한다.
  // 한계: TinyUSB 는 이 함수를 부르기 전에 요청받은 값을 자기 기록(1ms 에 보낼 표본 수의 바탕)에
  // 이미 적어 두고, 우리가 거절해도 되돌리지 않는다. 다른 값을 보내는 컴퓨터가 있다면 그 뒤로
  // 보내는 양이 틀어진다(다시 꽂으면 풀린다). 우리가 "고를 수 있는 값은 16kHz 뿐"이라고 알리므로
  // 규격대로인 컴퓨터는 다른 값을 보내지 않는다 — 막을 수 있는 것은 여기까지다.
  if (entityID == ENTITY_CLOCK && ctrlSel == AUDIO_CS_CTRL_SAM_FREQ) {
    TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_4_t));
    return (uint32_t)((audio_control_cur_4_t*)pBuff)->bCur == VIBE_MIC_SAMPLE_RATE;
  }
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
  const uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  const uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  const uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // 입력 단자(마이크): 연결 상태 — 늘 한 채널이 연결돼 있다.
  if (entityID == ENTITY_INPUT_TERMINAL) {
    if (ctrlSel == AUDIO_TE_CTRL_CONNECTOR) {
      audio_desc_channel_cluster_t ret;
      ret.bNrChannels = 1;
      ret.bmChannelConfig = (audio_channel_config_t)0;
      ret.iChannelNames = 0;
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*)&ret, sizeof(ret));
    }
    return false;
  }

  // 음량·음소거
  if (entityID == ENTITY_FEATURE_UNIT) {
    TU_VERIFY(channelNum <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX);
    switch (ctrlSel) {
      case AUDIO_FU_CTRL_MUTE: {
        audio_control_cur_1_t cur = {.bCur = mic_mute[channelNum] ? 1 : 0};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
      }
      case AUDIO_FU_CTRL_VOLUME:
        if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
          audio_control_cur_2_t cur = {.bCur = mic_volume[channelNum]};
          return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
        }
        if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
          audio_control_range_2_n_t(1) range;
          range.wNumSubRanges = 1;
          range.subrange[0].bMin = VOLUME_MIN_DB * 256;
          range.subrange[0].bMax = VOLUME_MAX_DB * 256;
          range.subrange[0].bRes = 256;  // 1dB 씩
          return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*)&range,
                                                            sizeof(range));
        }
        return false;
      default:
        return false;
    }
  }

  // 시계: 주파수는 16kHz 하나뿐이고 언제나 유효하다.
  // (TinyUSB 는 여기서 내준 "지금 주파수"를 보고 1ms 에 보낼 표본 수를 정한다.)
  if (entityID == ENTITY_CLOCK) {
    switch (ctrlSel) {
      case AUDIO_CS_CTRL_SAM_FREQ:
        if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
          audio_control_cur_4_t cur = {.bCur = VIBE_MIC_SAMPLE_RATE};
          return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
        }
        if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
          audio_control_range_4_n_t(1) range;
          range.wNumSubRanges = 1;
          range.subrange[0].bMin = VIBE_MIC_SAMPLE_RATE;
          range.subrange[0].bMax = VIBE_MIC_SAMPLE_RATE;
          range.subrange[0].bRes = 0;
          return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*)&range,
                                                            sizeof(range));
        }
        return false;
      case AUDIO_CS_CTRL_CLK_VALID: {
        audio_control_cur_1_t cur = {.bCur = 1};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
      }
      default:
        return false;
    }
  }

  return false;
}
