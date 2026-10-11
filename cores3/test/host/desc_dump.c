// 기기가 컴퓨터에 내놓는 USB 설명자를 컴퓨터에서 그대로 뽑아 찍는다(검사는 check_desc.py 가 한다).
// main/usb_desc.c 를 TinyUSB 헤더와 함께 호스트용으로 빌드한다 — 기기에 올리지 않고도
// "맥이 아예 못 알아보는" 종류의 잘못(길이 계산, 인터페이스·끝점 번호)을 잡으려는 것이다.
// 설명자 말고도, 컴퓨터가 마이크의 음량을 정하고 묻는 요청을 흉내 내어 그 결과를 찍는다(D46).
#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "usb_desc.h"

// usb_desc.c 가 부르는 TinyUSB 함수(호스트 빌드에는 라이브러리가 없으므로 흉내로 채운다).
// 기기에서는 컴퓨터가 물은 것의 답을 이 함수로 보낸다 → 무엇을 보내려 했는지 적어 둔다.
static uint8_t  answer[16];
static uint16_t answer_len;

bool tud_audio_buffer_and_schedule_control_xfer(uint8_t rhport, tusb_control_request_t const* p_request,
                                                void* data, uint16_t len) {
  (void)rhport;
  (void)p_request;
  answer_len = len < sizeof(answer) ? len : (uint16_t)sizeof(answer);
  memcpy(answer, data, answer_len);
  return true;
}

static void dump(const char* name, const uint8_t* p, unsigned len) {
  printf("%s", name);
  for (unsigned i = 0; i < len; i++) printf(" %02x", p[i]);
  printf("\n");
}

// ---- 음량 요청 흉내 ----
// 음량을 다루는 부품(Feature Unit)의 번호. usb_desc.c 의 ENTITY_FEATURE_UNIT 과 같다
// (check_desc.py 가 설명자에서 "부품 번호 2 = 음량"을 따로 확인한다).
#define FEATURE_UNIT 2

// 음량 요청의 뼈대. channel: 0 = 전체, 1 = 채널 1.
static tusb_control_request_t volume_request(uint8_t type, uint8_t channel) {
  tusb_control_request_t req;
  memset(&req, 0, sizeof(req));
  req.bmRequestType = type;
  req.bRequest = AUDIO_CS_REQ_CUR;
  req.wValue = (uint16_t)((AUDIO_FU_CTRL_VOLUME << 8) | channel);
  req.wIndex = (uint16_t)((FEATURE_UNIT << 8) | VIBE_ITF_AUDIO_CONTROL);
  req.wLength = sizeof(audio_control_cur_2_t);
  return req;
}

// 컴퓨터가 "음량을 db 로" 라고 보낸다(SET CUR). 기기가 받아들였으면 true.
static bool set_volume(uint8_t channel, int db) {
  const tusb_control_request_t req = volume_request(0x21, channel);  // 컴퓨터 → 기기
  audio_control_cur_2_t cur = {.bCur = (int16_t)(db * 256)};         // 1/256 데시벨 단위
  return tud_audio_set_req_entity_cb(0, &req, (uint8_t*)&cur);
}

// 컴퓨터가 "지금 음량은?" 하고 묻는다(GET CUR). 기기가 답한 값(데시벨). 답하지 않았으면 -999.
static int get_volume(uint8_t channel) {
  const tusb_control_request_t req = volume_request(0xA1, channel);  // 기기 → 컴퓨터
  answer_len = 0;
  if (!tud_audio_get_req_entity_cb(0, &req) || answer_len != sizeof(audio_control_cur_2_t)) return -999;
  return (int16_t)(answer[0] | (answer[1] << 8)) / 256;
}

// 한 줄: volume <이름> <요청을 받아들였는가> <소리에 곱하는 값> <전체> <채널 1> <GET 전체> <GET 채널 1>
static void volume_line(const char* name, bool accepted) {
  printf("volume %s %d %d %d %d %d %d\n", name, (int)accepted, usbdesc_mic_volume_db(),
         usbdesc_mic_master_volume_db(), usbdesc_mic_channel_volume_db(), get_volume(0), get_volume(1));
}

// 전체와 채널 1 을 차례로 정하고 한 줄 찍는다.
static void volume_both(const char* name, int master_db, int channel_db) {
  const bool a = set_volume(0, master_db);
  const bool b = set_volume(1, channel_db);
  volume_line(name, a && b);
}

static void dump_volume(void) {
  volume_line("initial", true);                        // 컴퓨터가 아무것도 정하지 않았다
  volume_line("channel_only", set_volume(1, -10));     // 채널 1 만(전체는 한 번도 쓰지 않음)
  volume_both("master_only", -6, 0);
  volume_both("both", -6, -10);                        // 둘 다 → 더한 값
  volume_both("review", 0, -24);                       // 코덱스 검토의 재현: 전체 0 을 쓴 뒤 채널 -24
  volume_both("sum_below", -20, -20);                  // 합이 범위 아래 → 아래 끝값
  volume_both("sum_above", 20, 20);                    // 합이 범위 위 → 위 끝값
  volume_both("cancel", 24, -24);                      // 서로 지운다
  volume_both("set_below", -100, 0);                   // 범위 밖의 값을 쓰면 끝값으로 적힌다
  volume_line("bad_channel", set_volume(2, -12));      // 없는 채널: 거절하고 값은 그대로
  volume_both("reset", 0, 0);
}

int main(void) {
  usbdesc_set_serial("A1B2C3D4E5F6");
  const uint8_t* dev = tud_descriptor_device_cb();
  dump("device", dev, dev[0]);
  const uint8_t* cfg = tud_descriptor_configuration_cb(0);
  dump("config", cfg, (unsigned)(cfg[2] | (cfg[3] << 8)));
  const uint8_t* hid = tud_hid_descriptor_report_cb(0);
  // 키보드 보고 설명자의 길이는 구성 설명자의 HID 부분에 적혀 있다 → 넉넉히 찍고 검사에서 자른다.
  dump("hidreport", hid, 96);
  for (uint8_t i = 0; i < 8; i++) {
    const uint16_t* s = tud_descriptor_string_cb(i, 0x0409);
    if (!s) continue;
    const unsigned n = ((s[0] & 0xFF) - 2) / 2;
    printf("string %u ", i);
    if (i == 0) {
      printf("lang=%04x\n", s[1]);
    } else {
      for (unsigned k = 0; k < n; k++) putchar((char)s[1 + k]);
      printf("\n");
    }
  }
  printf("ep_audio_size %u\n", (unsigned)CFG_TUD_AUDIO_EP_SZ_IN);
  printf("audio_fifo %u\n", (unsigned)CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ);
  printf("sample_rate %u\n", (unsigned)VIBE_MIC_SAMPLE_RATE);
  dump_volume();
  return 0;
}
