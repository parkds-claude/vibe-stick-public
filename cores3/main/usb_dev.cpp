#include "usb_dev.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "audio_ring.h"
#include "config.h"
#include "device/dcd.h"
#include "diag.h"
#include "download_mode.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "calendar.h"
#include "log_budget.h"
#include "hist_ring.h"
#include "host_pulse.h"
#include "line_reader.h"
#include "logic/clock.h"
#include "mic.h"
#include "soc/usb_dwc_struct.h"
#include "tusb.h"
#include "usb_desc.h"
#include "vibe_serial.h"
#if DEV_BUILD
#include "dev_rescue.h"
#endif

// src/ble_hid.cpp 의 진단(-DDEBUG_LINK=1)이 찾는 것들. 단계 번호는 쓰지 않지만 이름은 있어야 한다.
volatile uint16_t dbgStage = 0;
volatile uint32_t dbgBeat = 0;
VibeSerial        Serial;

namespace {

// ---- 기기 속 기록(D67, logic/hist_ring.h) ----
// 모든 기록 줄을 여기에도 넣는다. 선이 뽑혀 있거나 아무도 로그 포트를 열지 않은 동안의 일을 나중에
// `vibe:hist` 로 읽는다. 여러 태스크(loop · 블루투스)가 넣으므로 잠그고 넣는다(짧다: 글자 몇십 개 복사).
// 크기 12KB(6KB 는 말하기를 열 번쯤 쓰면 다 차서, 선이 뽑혀 있던 동안의 연결 줄이 밀려났다 — 통합 점검).
touchui::HistRing<12288> hist;
portMUX_TYPE             histMux = portMUX_INITIALIZER_UNLOCKED;
char                     histOut[12288];  // 내보낼 때 옮겨 담는 자리(loop 전용)
// `vibe:hist` 의 답을 내보내는 중인가(loop 전용). 한 번에 다 쓰려고 기다리면 loop 가 선다 → 틱마다 들어가는 만큼만.
enum class HistSend : uint8_t { Idle, Head, Body, Tail };
HistSend histSend = HistSend::Idle;
size_t   histTotal = 0, histAt = 0;
uint32_t histStalledSinceMs = 0;  // 마지막으로 한 글자라도 나간 시각

// 버퍼보다 긴 줄은 잘린다. 그때 끝의 줄바꿈까지 잘리면 다음 줄이 이 줄 끝에 붙는다 → 마지막 글자를 줄바꿈으로 둔다.
// buf: vsnprintf 가 쓴 버퍼(크기 size), n: vsnprintf 가 돌려준 값. 반환: 실제로 든 글자 수.
int clampLine(char* buf, size_t size, int n) {
  if (n < (int)size) return n;
  buf[size - 2] = '\n';
  return (int)size - 1;
}

// 명령의 답(usbdev::cmdf)이 기록에 남는 예산: 10초에 여덟 줄.
touchui::LogBudget cmdBudget(8, 10000);

// 답만 하고 기기 속 기록에는 넣지 않는다(로그 포트가 열려 있을 때만 나간다).
//   `vibe:ping` · `vibe:status` · 모르는 명령의 되돌림처럼 "컴퓨터가 묻는 대로 나오는 줄"을 기록에 넣으면, 같은
//   컴퓨터의 프로그램이 그것을 퍼부어 기록을 통째로 밀어낼 수 있다(보안 점검: ping 100번이면 6KB).
void replyf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void replyf(const char* fmt, ...) {
  char    buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0 || !tud_cdc_connected()) return;
  n = clampLine(buf, sizeof(buf), n);
  tud_cdc_write(buf, (uint32_t)n);
  tud_cdc_write_flush();
}

// 줄 머리에 켜진 뒤의 시각(ms)을 붙여 넣는다.
void histAdd(const char* s, int n) {
  if (n <= 0) return;
  char head[16];
  const int h = snprintf(head, sizeof(head), "%lu ", (unsigned long)(esp_timer_get_time() / 1000));
  portENTER_CRITICAL(&histMux);
  hist.add(head, (size_t)h);
  hist.add(s, (size_t)n);
  portEXIT_CRITICAL(&histMux);
}

// ESP-IDF 의 기록 줄(ESP_LOGE·ESP_LOGW — 블루투스 부품의 "보내기 실패 rc=…" 같은 것) 가운데 오류와 경고만
// 기기 속 기록에 넣는다(D67). 원래 나가던 곳(UART)으로도 그대로 보낸다. 어느 태스크에서든 불린다.
vprintf_like_t logPrev = nullptr;
int logHook(const char* fmt, va_list ap) {
  char    buf[160] = {0};
  va_list copy;
  va_copy(copy, ap);
  int n = vsnprintf(buf, sizeof(buf), fmt, copy);
  va_end(copy);
  n = clampLine(buf, sizeof(buf), n);
  // 줄은 "E (1234) TAG: …" 꼴이다(색을 켜 두면 앞에 ESC[…m 이 붙는다).
  const char* p = buf;
  if (n > 0 && *p == '\033') {
    while (*p && *p != 'm') ++p;
    if (*p == 'm') ++p;
  }
  if ((p[0] == 'E' || p[0] == 'W') && p[1] == ' ' && p[2] == '(') histAdd(p, n - (int)(p - buf));
  return logPrev ? logPrev(fmt, ap) : n;
}

usb_phy_handle_t phy = nullptr;
TaskHandle_t     usbTaskHandle = nullptr;

// ---- 연결 상태 (USB 태스크가 바꾸고 loop 가 읽는다) ----
std::atomic<bool> mounted{false};     // 컴퓨터가 장치를 잡았다
std::atomic<bool> suspended{false};   // 컴퓨터가 잠들었다
std::atomic<bool> streamOpen{false};  // 컴퓨터가 마이크 인터페이스를 "열림"으로 바꿔 두었다
std::atomic<bool> prefill{false};     // 마이크가 막 열렸다: 보관함을 절반까지 조용한 소리로 채울 것
std::atomic<uint32_t> micOpens{0};   // 진단용: 마이크가 열린 횟수

// ---- 선 너머에 컴퓨터가 있는가(D68, logic/host_pulse.h) ----
// 배터리로 켜져 있는 기기는 선을 뽑아도 USB 쪽이 "뽑힘"·"잠듦"을 알리지 않았다 → 박자(SOF)의 번호로 안다.
touchui::HostPulse pulse(150);         // 박자가 150ms 멎으면 없다고 본다(loop 전용)
std::atomic<bool>  hostAlive{false};   // loop 가 적고 마이크 태스크도 읽는다

// ---- 키보드 ----
bool releasePending = false;  // "뗌"을 아직 보내지 못했다(loop 전용)

// ---- 마이크 ----
AudioRing ring;  // 마이크 태스크 → USB 인터럽트 (logic/audio_ring.h, D38)
// 컴퓨터가 마이크를 열었을 때 먼저 넣어 두는 조용한 소리: 보관함의 절반.
// 이것이 없으면 처음 몇 ms 동안 보낼 것이 없어 빈 묶음이 나가고, 보관함이 바닥 근처에서 맴돈다.
const size_t PREFILL_BYTES = (CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ / 2) & ~(size_t)1;

// ---- 로그 포트 ----
// 받고 있는 명령 한 줄(47자까지). 넘친 줄과 0 바이트가 든 줄은 통째로 버린다(logic/line_reader.h).
LineReader<48> lineReader;
bool           bootRequested = false;
int            timeSet[6] = {0};     // 컴퓨터가 알려 온 시각(년 월 일 시 분 초)
bool           timePending = false;
int            notePending = -1;     // 컴퓨터의 Claude 가 알려 온 것(0 끝남, 1 답을 기다림)
bool           noteSeen = false;
uint32_t       lastNoteMs = 0;
const uint32_t NOTE_MIN_GAP_MS = 1000;  // 알림은 1초에 한 번만 받는다(화면과 소리를 쉴 새 없이 흔들지 못하게)
#if DEV_BUILD
// D42 안전장치의 판정(logic/dev_rescue.h). 평소 판에는 이 묶음이 통째로 없다.
DevRescue devRescue(DEV_MOUNT_WAIT_MS, DEV_ALLOW_WAIT_MS, DEV_RESCUE_MS);
// 컴퓨터가 장치를 잡은 횟수(USB 태스크가 올리고 loop 가 읽는다). loop 가 막힌 사이에 떼였다가 다시
// 잡힌 것은 tud_mounted() 로는 보이지 않는다 — 횟수가 달라진 것으로 안다.
std::atomic<uint32_t> mountCount{0};
Ev  injected[8];
int injectedHead = 0, injectedTail = 0;
int  imageCmd = usbdev::IMAGE_NONE;  // 시안 그림 명령(dev_show.h)
bool rawMode = false;                // 그림 바이트를 받는 중: 로그 포트를 줄로 읽지 않는다
#endif

inline uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// 컴퓨터가 지금 이 마이크를 듣고 있는가: 열어 두었고, 장치가 잡혀 있고, 잠들지 않았다.
// (잠들었다 깨어나면 "열림"은 그대로이므로 다시 듣는 것으로 본다.)
// "잡혀 있다·잠들었다"는 TinyUSB 에게 직접 묻는다: 선이 뽑히지 않은 채 버스만 초기화될 때는
// 뽑힘 콜백이 오지 않아, 우리가 적어 둔 값(mounted·suspended)은 잠깐 옛것일 수 있다.
inline bool micLiveNow() {
  return streamOpen.load(std::memory_order_acquire) && tud_mounted() && !tud_suspended() &&
         hostAlive.load(std::memory_order_relaxed);  // 선이 뽑혔으면 듣고 있지 않다(D68)
}

// USB 를 처리하는 태스크. 컴퓨터의 요청에 답하고 아래 콜백들을 부른다.
void usbTask(void*) {
  for (;;) tud_task();
}

#if DEV_BUILD
bool parseEv(const char* name, Ev& out) {
  static const struct {
    const char* name;
    Ev          ev;
  } TABLE[] = {{"talk", Ev::TalkTap},   {"ok", Ev::Confirm},        {"nope", Ev::Cancel},
               {"up", Ev::Up},          {"down", Ev::Down},         {"next", Ev::WindowNext},
               {"back", Ev::WindowPrev}};
  for (const auto& t : TABLE) {
    if (strcmp(name, t.name) == 0) {
      out = t.ev;
      return true;
    }
  }
  return false;
}
#endif

// 로그 포트로 들어온 명령 한 줄. 모든 명령은 "vibe:" 로 시작한다.
// D44 내려받기 모드는 글자로 된 명령(vibe:boot)으로만 들어간다.
//   이유: 흔한 방식(포트의 DTR·RTS 신호 순서를 보고 들어가기)은 시리얼 프로그램이 포트를 열기만
//         해도 우연히 맞아떨어질 수 있다. 그러면 기기가 멀쩡히 쓰던 중에 꺼진 것처럼 보인다.
//   비용: esptool 이 혼자서는 이 기기를 내려받기 모드로 넣지 못한다 → tools/flash.sh 가 명령을 먼저 보낸다.
//   탈출구: tud_cdc_line_state_cb 에 신호 순서 감지를 더한다.
void handleLine(const char* s, uint32_t now) {
  if (strncmp(s, "vibe:", 5) != 0) return;
#if DEV_BUILD
  // 안전장치에 "명령이 왔다"고 알린다. 이번 틱이 시작된 시각을 넘긴다: 여기서 시계를 새로 읽으면
  // poll() 이 든 시각보다 뒤가 된다(그래도 뒤집히지 않게 짜 두었지만 — logic/dev_rescue.h 의 (1)).
  devRescue.onCommand(now);
#else
  (void)now;
#endif
  const char* cmd = s + 5;
  if (strcmp(cmd, "boot") == 0) {
    bootRequested = true;
  } else if (strncmp(cmd, "time ", 5) == 0) {
    // 시계를 맞춘다(D62): vibe:time YYYYMMDDHHMMSS. 숫자 열넷이 아니거나 값이 엉뚱하면 받지 않는다.
    const char* d = cmd + 5;
    bool ok = strlen(d) == 14;
    for (int i = 0; ok && i < 14; i++) ok = d[i] >= '0' && d[i] <= '9';
    if (ok) {
      auto num = [d](int at, int len) {
        int v = 0;
        for (int i = 0; i < len; i++) v = v * 10 + (d[at + i] - '0');
        return v;
      };
      const int t[6] = {num(0, 4), num(4, 2), num(6, 2), num(8, 2), num(10, 2), num(12, 2)};
      ok = touchui::validDateTime(t[0], t[1], t[2], t[3], t[4], t[5]);  // 달마다의 날 수까지(logic/calendar.h)
      if (ok) {
        memcpy(timeSet, t, sizeof(timeSet));
        timePending = true;
      }
    }
    usbdev::cmdf(ok ? "vibe:time ok\n" : "vibe:time ?\n");
  } else if (strcmp(cmd, "note done") == 0 || strcmp(cmd, "note ask") == 0) {
    // 컴퓨터의 Claude 가 알린다(D63): 화면에 잠깐 보이고 소리를 낸다.
    // (마감 비교 reachedMs 가 아니라 흐른 시간으로 본다: 마지막 알림 뒤 24.8일이 지나면 마감 비교는 거꾸로 읽혀,
    //  그 뒤 24.9일 동안 모든 알림이 busy 로 버려졌다 — 통합 점검. now 는 lastNoteMs 보다 뒤다.)
    if (!noteSeen || elapsedMs(now, lastNoteMs, NOTE_MIN_GAP_MS)) {
      notePending = (cmd[5] == 'a') ? 1 : 0;
      noteSeen = true;
      lastNoteMs = now;
      usbdev::cmdf("vibe:note ok\n");
    } else {
      usbdev::cmdf("vibe:note busy\n");
    }
  } else if (strcmp(cmd, "hist") == 0) {
    // 기기 속 기록(D67)을 통째로 내준다. 이 답 자체는 기록에 넣지 않는다.
    // 여기서는 옮겨 담기만 하고, 내보내는 것은 poll 이 틱마다 조금씩 한다(pumpHist) — loop 를 세우지 않는다.
    // 이미 내보내는 중이면 새 요청은 버린다(요청을 퍼부어 기기를 붙잡아 둘 수 없게).
    if (histSend == HistSend::Idle) {
      portENTER_CRITICAL(&histMux);
      histTotal = hist.copy(histOut, sizeof(histOut));
      portEXIT_CRITICAL(&histMux);
      histAt = 0;
      histSend = HistSend::Head;
      histStalledSinceMs = now;
    }
  } else if (strcmp(cmd, "ping") == 0) {
    replyf("vibe:pong %lu\n", (unsigned long)nowMs());
  } else if (strcmp(cmd, "status") == 0) {
    // vol 은 소리에 실제로 곱하는 값(전체 + 채널 1), volm·volc 는 컴퓨터가 "전체"와 "채널 1"에 써 둔 값이다
    // (어느 쪽에 쓰는 컴퓨터인지 여기서 가른다 — usb_desc.c). 값이 모두 가장 길 때 159자다: logf 의
    // 한 줄은 199자까지이므로, 항목을 더할 때는 다시 세고 넘으면 줄을 나눈다.
    // level(소리 크기)은 평소 판에서는 컴퓨터가 마이크를 연 동안에만 내준다(그 밖에는 0): 열지 않았는데도 이 값을
    // 초당 백 번 물으면 방의 말소리가 있고 없음·리듬이 새고, 그때는 맥의 마이크 표시도 기기의 빨강도 뜨지 않는다
    // (보안 점검). 기록에는 넣지 않는다(replyf).
    replyf("vibe:status dev=%d mounted=%d suspended=%d mic=%d opens=%lu level=%u vol=%ddB "
                 "volm=%ddB volc=%ddB mute=%d overruns=%lu heap=%lu reset=%d\n",
                 DEV_BUILD, (int)tud_mounted(), (int)tud_suspended(), (int)micLiveNow(),
                 (unsigned long)micOpens.load(), (unsigned)((DEV_BUILD || micLiveNow()) ? mic::level() : 0),
                 usbdesc_mic_volume_db(),
                 usbdesc_mic_master_volume_db(), usbdesc_mic_channel_volume_db(),
                 (int)usbdesc_mic_muted(), (unsigned long)ring.overruns(),
                 (unsigned long)esp_get_free_heap_size(), (int)esp_reset_reason());
#if DEV_BUILD
  } else if (strcmp(cmd, "img off") == 0) {
    imageCmd = usbdev::IMAGE_OFF;
  } else if (strcmp(cmd, "img") == 0 || (strncmp(cmd, "img ", 4) == 0 && cmd[4] >= '0' && cmd[4] <= '3' && cmd[5] == '\0')) {
    // 시안 그림(dev_show.h): 이 줄 뒤로 오는 것은 명령이 아니라 그림 바이트다 → 곧바로 날 바이트 통로로 바꾼다.
    imageCmd = cmd[3] ? cmd[4] - '0' : 1;
    rawMode = true;
  } else if (strcmp(cmd, "ramp 1") == 0 || strcmp(cmd, "ramp 0") == 0) {
    mic::setTestRamp(cmd[5] == '1');
    usbdev::logf("vibe:ramp %c\n", cmd[5]);
  } else if (strncmp(cmd, "ev ", 3) == 0) {
    Ev e;
    const int next = (injectedHead + 1) % 8;
    if (parseEv(cmd + 3, e) && next != injectedTail) {
      injected[injectedHead] = e;
      injectedHead = next;
      usbdev::logf("vibe:ev ok %s\n", cmd + 3);
    } else {
      usbdev::logf("vibe:ev ? %s\n", cmd + 3);
    }
#endif
  } else {
    replyf("vibe:? %s\n", cmd);
  }
}

}  // namespace

// ---- TinyUSB 가 부르는 것들(USB 태스크 또는 USB 인터럽트에서) ----
extern "C" {

// USB 에서 일어나는 일마다 불린다(인터럽트 안일 수 있다). 블랙박스에 횟수만 센다.
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
  (void)rhport;
  (void)in_isr;
  if (eventid == DCD_EVENT_BUS_RESET) ++g_vibe_diag.bus_resets;
  if (eventid == DCD_EVENT_SETUP_RECEIVED) ++g_vibe_diag.setups;
}

void tud_mount_cb(void) {
  streamOpen.store(false);  // 새로 잡혔다: 마이크는 닫힌 상태에서 시작한다
  suspended.store(false);
  mounted.store(true);
#if DEV_BUILD
  mountCount.fetch_add(1);  // 안전장치가 "다시 잡혔다"를 놓치지 않게(logic/dev_rescue.h 의 (4))
#endif
}

void tud_umount_cb(void) {
  mounted.store(false);
  streamOpen.store(false);
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  suspended.store(true);  // 컴퓨터가 잠들면 듣고 있지 않다(micLiveNow)
}

void tud_resume_cb(void) {
  // 잠들기 전에 마이크가 열려 있었다면 그대로 이어진다 → 보관함을 다시 채우고 시작한다.
  if (streamOpen.load()) prefill.store(true);
  suspended.store(false);
}

// 컴퓨터가 마이크를 열거나(alt 1) 닫으려고(alt 0) 인터페이스를 바꿨다.
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
  (void)rhport;
  const uint8_t itf = TU_U16_LOW(p_request->wIndex);
  const uint8_t alt = TU_U16_LOW(p_request->wValue);
  if (itf == VIBE_ITF_AUDIO_STREAMING && alt != 0) {
    prefill.store(true);  // 첫 묶음을 보낸 직후(아래 tud_audio_tx_done_isr)에 채운다
    micOpens.fetch_add(1);
    streamOpen.store(true);
  }
  return true;
}

// 열려 있던 마이크가 닫혔다(TinyUSB 가 보관함을 비운 뒤에 부른다).
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const* p_request) {
  (void)rhport;
  if (TU_U16_LOW(p_request->wIndex) == VIBE_ITF_AUDIO_STREAMING) streamOpen.store(false);
  return true;
}

// 소리 묶음 하나를 보낼 준비가 끝날 때마다(1ms 에 한 번) USB 인터럽트에서 부른다.
// 마이크가 넘겨 둔 표본을 TinyUSB 의 보관함으로 옮긴다 — 보관함에는 여기서만 쓴다(D38).
bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t n_bytes_sent, uint8_t func_id, uint8_t ep_in,
                           uint8_t cur_alt_setting) {
  (void)rhport;
  (void)n_bytes_sent;
  (void)func_id;
  (void)ep_in;
  (void)cur_alt_setting;
  static int16_t buf[64];
  if (prefill.exchange(false)) {
    ring.discard();              // 열리기 전에 넘어와 있던 소리는 버린다
    tud_audio_clear_ep_in_ff();  // 보관함에 남은 것도(잠들기 전의 소리) — 읽는 곳도 이 인터럽트라 안전하다
    memset(buf, 0, sizeof(buf));
    for (size_t left = PREFILL_BYTES; left > 0;) {
      const size_t n = left < sizeof(buf) ? left : sizeof(buf);
      tud_audio_write(buf, (uint16_t)n);
      left -= n;
    }
  }
  // 마이크는 8ms 치(128표본)를 한꺼번에 넘기므로 그때는 여러 번 돌아 다 옮긴다.
  for (;;) {
    const size_t n = ring.pop(buf, 64);
    if (n == 0) break;
    tud_audio_write(buf, (uint16_t)(n * sizeof(int16_t)));
  }
  return true;
}

}  // extern "C"

namespace usbdev {

void begin() {
  logPrev = esp_log_set_vprintf(logHook);  // ESP-IDF 의 오류·경고 줄을 기기 속 기록에도(D67)
  // 일련번호: 기기 고유의 MAC 주소. 컴퓨터가 이 번호로 "이 마이크"를 기억한다.
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  char serial[13];
  snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  usbdesc_set_serial(serial);

  // 칩 안의 USB 를 "장치" 노릇으로 켠다. 이 순간부터 USB-Serial-JTAG(자동 굽기 통로)는 닫힌다.
  usb_phy_config_t phyConf = {};
  phyConf.controller = USB_PHY_CTRL_OTG;
  phyConf.otg_mode = USB_OTG_MODE_DEVICE;
  phyConf.target = USB_PHY_TARGET_INT;
  // USB 를 켜지 못하면 이 기기는 컴퓨터에 보이지 않는다 → 손 없이 다시 구울 수 있게 내려받기 모드로
  // 물러난다. (켜지 못한 채 USB 태스크를 만들면 그 태스크가 쉬지 않고 돌아 loop 를 굶긴다.)
  const bool phyOk = usb_new_phy(&phyConf, &phy) == ESP_OK;
  if (phyOk) g_vibe_diag.stage = DIAG_STAGE_USB_PHY;
  if (!phyOk || !tusb_init()) {
    g_vibe_diag.download_reason = DIAG_DL_USB_INIT;
    vTaskDelay(pdMS_TO_TICKS(50));
    download_mode_restart(phy);
  }
  g_vibe_diag.stage = DIAG_STAGE_USB_STACK;
  // 화면·터치를 다루는 loop(우선순위 1)보다 높게: 컴퓨터의 요청에 늦지 않게 답해야 한다.
  // D53 USB 태스크는 USB 인터럽트와 같은 코어에 묶는다(인터럽트는 tusb_init 을 부른 코어 = 지금 이 코어에 걸린다).
  //   이유: 마이크를 닫을 때 TinyUSB 는 이 태스크에서 소리 보관함을 비우는데, 태스크가 다른 코어에서 돌면
  //         같은 순간 인터럽트(tud_audio_tx_done_isr)가 그 보관함을 읽고 쓰고 있을 수 있다(코덱스 검토
  //         ). 같은 코어면 인터럽트가 태스크를 잠깐 밀어낼 뿐 둘이 동시에 돌지 않는다 —
  //         TinyUSB 가 전제하는 모양이다.
  //   비용: USB 처리와 loop 가 한 코어를 나눠 쓴다(USB 태스크는 할 일이 있을 때만 깬다).
  //   탈출구: 마지막 인자를 tskNO_AFFINITY 로.
  if (xTaskCreatePinnedToCore(usbTask, "usb", 4096, nullptr, 5, &usbTaskHandle, xPortGetCoreID()) != pdPASS) {
    // 태스크를 만들지 못하면 USB 를 처리할 것이 없어 컴퓨터에 보이지 않는다 → USB 를 켜지 못한 때처럼 물러난다.
    usbTaskHandle = nullptr;
    enterDownloadMode("usb task", DIAG_DL_USB_INIT);
  }
  g_vibe_diag.stage = DIAG_STAGE_USB_TASK;
#if DEV_BUILD
  devRescue.begin(nowMs());  // 안전장치의 마감은 USB 를 켠 지금부터 센다
#endif
}

void enterDownloadMode(const char* why, int reason) {
  g_vibe_diag.download_reason = (uint32_t)reason;
  logf("[boot] download mode: %s\n", why);
  if (tud_inited()) {
    tud_cdc_write_flush();
    vTaskDelay(pdMS_TO_TICKS(150));  // 위 문구가 나갈 시간
    // 컴퓨터에서 장치를 떼고, USB 의 인터럽트와 처리 태스크를 세운 뒤에 USB 를 끈다
    // (도는 채로 USB 의 시계를 끄면, 꺼진 장치의 레지스터를 건드리게 된다).
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    dcd_int_disable(0);
    if (usbTaskHandle) vTaskSuspend(usbTaskHandle);
  }
  download_mode_restart(phy);
  for (;;) {
  }
}

Link link() {
  if (tud_suspended()) return LinkState::Searching;
  // 잡혀 있다고 적혀 있어도, 컴퓨터의 박자가 멎었으면 선이 뽑힌 것이다(D68) → 블루투스로 길을 넘긴다.
  if (tud_mounted()) return hostAlive.load(std::memory_order_relaxed) ? LinkState::Ready : LinkState::Searching;
  return tud_connected() ? LinkState::Connecting : LinkState::Searching;
}

// `vibe:hist` 의 답을 들어가는 만큼만 쓴다. 머리말·꼬리말은 통째로 들어갈 자리가 있을 때만 쓴다(잘리지 않게).
// 컴퓨터가 2초 동안 한 글자도 받아 가지 않으면 그만둔다(꼬리말이 없으면 도구는 잘린 것으로 안다).
void pumpHist(uint32_t now) {
  if (histSend == HistSend::Idle) return;
  if (!tud_cdc_connected() || reachedMs(now, histStalledSinceMs + 2000)) {
    histSend = HistSend::Idle;
    return;
  }
  char line[40];
  if (histSend == HistSend::Head) {
    const int n = snprintf(line, sizeof(line), "vibe:hist begin %u\n", (unsigned)histTotal);
    if (tud_cdc_write_available() < (uint32_t)n) return;
    tud_cdc_write(line, (uint32_t)n);
    histSend = HistSend::Body;
    histStalledSinceMs = now;
  }
  if (histSend == HistSend::Body) {
    if (histAt < histTotal) {
      const uint32_t w = tud_cdc_write(histOut + histAt, (uint32_t)(histTotal - histAt));
      histAt += w;
      if (w > 0) histStalledSinceMs = now;
    }
    if (histAt >= histTotal) histSend = HistSend::Tail;
  }
  if (histSend == HistSend::Tail) {
    const int n = snprintf(line, sizeof(line), "\nvibe:hist end %u\n", (unsigned)histAt);
    if (tud_cdc_write_available() >= (uint32_t)n) {
      tud_cdc_write(line, (uint32_t)n);
      histSend = HistSend::Idle;
    }
  }
  tud_cdc_write_flush();
}

void poll(uint32_t now) {
  pumpHist(now);
  // 컴퓨터의 박자를 본다(D68): USB 칩이 들고 있는 "마지막으로 받은 박자의 번호"가 바뀌는가.
  pulse.feed((uint16_t)USB_DWC.dsts_reg.soffn, now);
  const bool alive = pulse.alive(now);
  if (alive != hostAlive.load(std::memory_order_relaxed)) {
    hostAlive.store(alive, std::memory_order_relaxed);
    // (컴퓨터가 잠든 때에도 박자는 멎는다 — 그때는 "잠듦"이 먼저 적힌다.)
    if (tud_mounted()) {
      logf("[usb] host %s t=%lu\n", alive ? "back" : tud_suspended() ? "quiet (asleep)" : "gone (cable pulled?)",
           (unsigned long)now);
    }
  }

  // 보내지 못한 "뗌"을 다시 보낸다(눌린 채 남으면 컴퓨터가 그 키를 계속 친다).
  if (releasePending) releaseAll();

  // 로그 포트의 명령. 한 틱에 LOG_READ_BUDGET 바이트까지만 읽고, 남은 것은 다음 틱에 잇는다
  // (컴퓨터가 쉬지 않고 보내도 loop 가 여기에 갇히지 않게 — logic/line_reader.h).
  struct LogPort {
    int read() { return (int)tud_cdc_read_char(); }  // 온 것이 없으면 -1
  } port;
#if DEV_BUILD
  // (개발판) 시안 그림을 받는 중이면 줄로 읽지 않는다 — 그 바이트는 main 이 readRaw 로 가져간다.
  if (!rawMode)
#endif
  pumpLines(port, lineReader, LOG_READ_BUDGET, [now](const char* s) { handleLine(s, now); });
#if DEV_BUILD
  // D42 개발판의 안전장치. 언제 물러나는지와 그 사연은 logic/dev_rescue.h 에 있다(이 자리에서 탈이
  // 여러 번 나서 판정을 컴퓨터에서 시험할 수 있게 빼 두었다). 이번 틱에 온 명령은 위에서 이미 알렸다.
  switch (devRescue.poll(now, tud_connected(), tud_mounted(), mountCount.load())) {
    case DevRescueVerdict::Stay:
      break;
    case DevRescueVerdict::NoHost:
      enterDownloadMode("dev: no host", DIAG_DL_NEVER_MOUNTED);
    case DevRescueVerdict::NeverMounted:
      enterDownloadMode("dev: never mounted", DIAG_DL_NEVER_MOUNTED);
    case DevRescueVerdict::NoCommand:
      enterDownloadMode("dev: no command", DIAG_DL_NO_COMMAND);
  }
#endif

  // 블랙박스: 지금의 모습을 적어 둔다(값 몇 개를 옮겨 적을 뿐이라 매 틱 해도 가볍다).
  g_vibe_diag.uptime_ms = now;
  g_vibe_diag.desc_device = usbdesc_requests[0];
  g_vibe_diag.desc_config = usbdesc_requests[1];
  g_vibe_diag.desc_string = usbdesc_requests[2];
  g_vibe_diag.mic_opens = micOpens.load();
  g_vibe_diag.mic_level = mic::level();
  g_vibe_diag.heap_min = esp_get_minimum_free_heap_size();
  uint32_t f = g_vibe_diag.flags & (DIAG_F_MOUNTED_EVER | DIAG_F_LOG_OPENED | DIAG_F_MIC_OK | DIAG_F_DEV_BUILD);
  if (tud_mounted()) f |= DIAG_F_MOUNTED_EVER | DIAG_F_MOUNTED_NOW;
  if (tud_suspended()) f |= DIAG_F_SUSPENDED;
  if (tud_cdc_connected()) f |= DIAG_F_LOG_OPENED;
  if (DEV_BUILD) f |= DIAG_F_DEV_BUILD;
  g_vibe_diag.flags = f;
}

bool takeTime(int out[6]) {
  if (!timePending) return false;
  timePending = false;
  memcpy(out, timeSet, sizeof(timeSet));
  return true;
}

int takeNote() {
  const int was = notePending;
  notePending = -1;
  return was;
}

bool takeBootRequest() {
  const bool was = bootRequested;
  bootRequested = false;
  return was;
}

bool send(uint8_t mods, uint8_t key) {
  if (link() != LinkState::Ready) return false;
  // 바로 앞의 보고(뗌)가 아직 컴퓨터로 나가는 중일 수 있다(컴퓨터는 4ms 마다 가져간다).
  // 그것을 "연결 없음"으로 치면 키를 버리고 "보내지 못함"(`Not connected`)을 띄우게 된다 → 잠깐 기다린다.
  for (int i = 0; i < 20 && !tud_hid_ready(); i++) vTaskDelay(pdMS_TO_TICKS(1));
  // 앞선 "뗌"을 아직 보내지 못했다면 그것부터 보낸다. 컴퓨터가 그 키를 눌린 것으로 알고 있는 채
  // 같은 키의 누름을 또 보내면, 컴퓨터에는 새 누름으로 보이지 않는다(두 번이 한 번이 된다).
  if (releasePending) {
    if (!releaseAll()) return false;
    for (int i = 0; i < 20 && !tud_hid_ready(); i++) vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (!tud_hid_ready()) return false;
  const uint8_t keys[6] = {key, 0, 0, 0, 0, 0};
  return tud_hid_keyboard_report(0, mods, keys);
}

bool releaseAll() {
  if (!tud_mounted()) {
    releasePending = false;  // 컴퓨터가 없으면 눌린 키도 없다
    return false;
  }
  if (tud_hid_ready() && tud_hid_keyboard_report(0, 0, nullptr)) {
    releasePending = false;
    return true;
  }
  releasePending = true;
  return false;
}

bool micOpen() { return micLiveNow(); }

void micPush(const int16_t* samples, size_t n) {
  // 컴퓨터가 듣는 동안에만 넘긴다. 그 밖에는 여기서 버린다 — 기기 밖으로 나가지 않는다.
  if (micLiveNow()) ring.push(samples, n);
}

bool logOpen() { return tud_cdc_connected(); }

void cmdf(const char* fmt, ...) {
  char    buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  n = clampLine(buf, sizeof(buf), n);
  if (cmdBudget.allow(nowMs())) histAdd(buf, n);  // 기록에는 예산 안에서만
  if (!tud_cdc_connected()) return;
  tud_cdc_write(buf, (uint32_t)n);
  tud_cdc_write_flush();
}

void logf(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  n = clampLine(buf, sizeof(buf), n);
  histAdd(buf, n);                   // 기기 속 기록에는 늘 남긴다(D67)
  if (!tud_cdc_connected()) return;  // 컴퓨터가 포트를 열지 않았으면 내보내지 않는다(USB 쪽에 쌓아 두지 않는다)
  tud_cdc_write(buf, (uint32_t)n);
  tud_cdc_write_flush();
}

void note(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  n = clampLine(buf, sizeof(buf), n);
  histAdd(buf, n);
}

int takeImageCmd() {
#if DEV_BUILD
  const int was = imageCmd;
  imageCmd = IMAGE_NONE;
  return was;
#else
  return IMAGE_NONE;
#endif
}

void setRawMode(bool on) {
#if DEV_BUILD
  rawMode = on;
#else
  (void)on;
#endif
}

size_t readRaw(uint8_t* buf, size_t n) {
#if DEV_BUILD
  return rawMode ? (size_t)tud_cdc_read(buf, (uint32_t)n) : 0;
#else
  (void)buf;
  (void)n;
  return 0;
#endif
}

bool takeInjected(Ev& out) {
#if DEV_BUILD
  if (injectedTail == injectedHead) return false;
  out = injected[injectedTail];
  injectedTail = (injectedTail + 1) % 8;
  return true;
#else
  (void)out;
  return false;
#endif
}

}  // namespace usbdev

// src/ble_hid.cpp 의 진단 줄(블루투스 태스크에서 불린다): 기기 속 기록에만 넣는다.
void VibeSerial::printf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  n = clampLine(buf, sizeof(buf), n);
  histAdd(buf, n);
}
