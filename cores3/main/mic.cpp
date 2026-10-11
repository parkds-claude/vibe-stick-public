#include "mic.h"

#include <M5Unified.h>
#include <atomic>
#include <cmath>

#include "audio_ring.h"
#include "config.h"
#include "esp_timer.h"
#include "logic/clock.h"
#include "usb_desc.h"
#include "usb_dev.h"

namespace {

// 마이크 태스크가 한 번에 채우는 덩어리(8ms = 16kHz 에서 128표본). 두 개를 번갈아 맡겨 끊김 없이 받는다.
// 8ms 인 까닭: 마이크 라이브러리(M5Unified)가 소리를 128표본씩 받아 넘긴다. 덩어리를 그 크기에 맞추면
// 8ms 마다 고르게 오고, 10ms(160표본)로 잡으면 8·8·8·16ms 로 들쭉날쭉해 USB 보관함이 더 출렁인다
// (tools/sim_flow.py).
const size_t CHUNK = MIC_SAMPLE_RATE / 125;
int16_t chunkA[CHUNK];
int16_t chunkB[CHUNK];

std::atomic<uint16_t> lastRms{0};
std::atomic<uint16_t> lastRawRms{0};  // 입력 음량을 곱하기 전의 크기
std::atomic<uint32_t> lastChunkMs{0};  // 마지막으로 덩어리를 받은 시각
const uint32_t STALL_REARM_MS = 500;   // 이만큼 소리가 오지 않으면 멎은 것으로 본다
bool paused = false;                   // 스피커가 통로를 쓰는 중(loop 전용): 되살리지 않는다
#if DEV_BUILD
std::atomic<bool> testRamp{false};
uint16_t rampNext = 0;  // 마이크 태스크 전용
#endif

// 마이크 태스크가 덩어리 하나를 다 채우면 부른다(ISR 아님). 짧게 끝내야 한다.
void onChunk(void*, void* data, size_t length) {
  int16_t* s = (int16_t*)data;
  lastChunkMs.store((uint32_t)(esp_timer_get_time() / 1000), std::memory_order_relaxed);

  // 방의 소리 그대로의 크기(컴퓨터의 입력 음량·음소거를 곱하기 전). "말소리가 들리는가"를 가리는 데 쓴다:
  // 컴퓨터가 이 기기의 입력을 줄여 두었거나 꺼 두었어도 방에서 말하고 있는지는 알아야 한다(D65).
  uint64_t rawSum = 0;
  for (size_t i = 0; i < length; i++) rawSum += (int64_t)s[i] * s[i];
  lastRawRms.store((uint16_t)sqrtf((float)(rawSum / (length ? length : 1))), std::memory_order_relaxed);

  // 컴퓨터의 "입력 음량"과 음소거를 반영한다(컴퓨터가 건드리지 않았으면 0dB = 그대로).
  applyGain(s, length, usbdesc_mic_muted() ? 0 : gainQ8FromDb(usbdesc_mic_volume_db()));

  uint64_t sum = 0;
  for (size_t i = 0; i < length; i++) sum += (int64_t)s[i] * s[i];
  lastRms.store((uint16_t)sqrtf((float)(sum / (length ? length : 1))), std::memory_order_relaxed);

#if DEV_BUILD
  // 시험 신호: 음량을 곱한 뒤에 넣는다(맥이 입력 음량을 바꿔 놓아도 숫자가 그대로 가게).
  if (testRamp.load(std::memory_order_relaxed)) {
    for (size_t i = 0; i < length; i++) {
      s[i] = (int16_t)rampNext;
      if (++rampNext >= 16000) rampNext = 0;
    }
  }
#endif

  usbdev::micPush(s, length);  // 컴퓨터가 듣는 동안에만 넘어간다

  // 같은 덩어리를 다시 맡긴다. 이미 다른 하나가 줄 서 있으므로 그 사이 소리가 끊기지 않는다.
  // (여기서는 기다리지 않는 호출이라 드물게 실패할 수 있다 — 둘 다 실패해 멎으면 poll() 이 되살린다.)
  M5.Mic.record((int16_t*)data, length);
}

}  // namespace

namespace mic {

bool begin() {
  auto cfg = M5.Mic.config();
  cfg.magnification = MIC_MAGNIFICATION;
  M5.Mic.config(cfg);
  M5.Mic.setBufferReleaseCallback(nullptr, onChunk);
  lastChunkMs.store((uint32_t)(esp_timer_get_time() / 1000), std::memory_order_relaxed);
  if (!M5.Mic.record(chunkA, CHUNK, MIC_SAMPLE_RATE)) return false;
  return M5.Mic.record(chunkB, CHUNK, MIC_SAMPLE_RATE);
}

void pause() {
  paused = true;
  M5.Mic.end();
  // 내려 둔 동안에는 소리가 없다: 마지막 덩어리의 크기를 들고 있으면, 그 값이 말소리였을 때 "말소리가 들린다"가
  // 그동안 내내 이어진다(독립 검토).
  lastRms.store(0, std::memory_order_relaxed);
  lastRawRms.store(0, std::memory_order_relaxed);
}

void resume() {
  paused = false;
  begin();
}

bool poll(uint32_t nowMs) {
  if (paused) return false;
  // lastChunkMs 는 마이크 태스크가 시계를 따로 읽어 적은 값이라 nowMs 보다 뒤일 수 있다 → 마감 비교.
  if (!reachedMs(nowMs, lastChunkMs.load(std::memory_order_relaxed) + STALL_REARM_MS)) return false;
  lastChunkMs.store(nowMs, std::memory_order_relaxed);
  // 덩어리가 한동안 오지 않았다(멎었다): 마지막 크기를 들고 있지 않는다 — 그 값이 말소리였다면 되살아날 때까지
  // "말소리가 들린다"가 이어진다(pause 와 같은 까닭. 독립 검토). 되살아나면 새 덩어리가 값을 다시 쓴다.
  lastRms.store(0, std::memory_order_relaxed);
  lastRawRms.store(0, std::memory_order_relaxed);
  // 맡겨 둔 덩어리가 남아 있으면 건드리지 않는다: 그때 더 맡기면 자리가 날 때까지 loop 가 멈춘다.
  // (맡긴 것이 하나도 없을 때만 = 다시 맡기기에 실패해 멎은 때만 되살린다.)
  if (M5.Mic.isRecording() != 0) return false;
  M5.Mic.record(chunkA, CHUNK, MIC_SAMPLE_RATE);
  M5.Mic.record(chunkB, CHUNK, MIC_SAMPLE_RATE);
  return true;
}

void setTestRamp(bool on) {
#if DEV_BUILD
  testRamp.store(on, std::memory_order_relaxed);
#else
  (void)on;
#endif
}

uint16_t level() { return lastRms.load(std::memory_order_relaxed); }
uint16_t rawLevel() { return lastRawRms.load(std::memory_order_relaxed); }

}  // namespace mic
