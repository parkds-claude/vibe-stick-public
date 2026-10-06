#include "ui.h"

#include <M5Unified.h>

#include "debug_trace.h"
#include "logic/clock.h"
#include "logic/screen_draw.h"
#include "logic/screen_state.h"

namespace {

const uint32_t DIM_AFTER_MS = 30000;
// 이 기종의 밝기 값은 백라이트 전압으로 바뀐다(M5GFX: ((값>>1)+8)/13+5 → ×0.1V+1.8V).
// 128 → 2.8V, 40 → 2.5V(공식 최저 단계). 12 는 2.4V 로 사실상 꺼짐이라 쓰지 않는다.
const uint8_t BRIGHT = 128;
const uint8_t DIM    = 40;

// 그리기 코드(logic/screen_draw)의 "사각형 채우기"를 LCD 로 옮긴다.
class LcdCanvas : public Canvas {
 public:
  void fillRect(int x, int y, int w, int h, uint16_t color) override {
    // color 는 16비트 값(RGB565)으로 넘긴다. 32비트 uint32_t 로 넘기면 RGB888 로 해석된다.
    M5.Display.fillRect(x, y, w, h, color);
  }
};

ScreenState   state;
LcdCanvas     canvas;
screen::Frame drawing{};               // 지금 그리고 있는 화면
int           nextBand = screen::BANDS;  // 다음에 그릴 띠(BANDS 면 다 그렸음)
uint32_t      drawUs   = 0;            // 진단용: 이 화면을 그리는 데 든 시간 합
bool        keepAwake    = false;
uint32_t    lastActiveMs = 0;
bool        dimmed       = false;

}  // namespace

namespace ui {

void begin() {
  auto& d = M5.Display;
  d.setRotation(0);  // 세로, 조이스틱이 위쪽
  d.setBrightness(BRIGHT);
  d.fillScreen(TFT_BLACK);
  lastActiveMs = millis();
  dimmed = false;
}

void setLink(LinkState l) { state.setLink(l); }
void setBattery(int pct) { state.setBattery(pct); }
void sent(Ev e) { state.onSent(e, millis()); }
void notSent() { state.onNotSent(millis()); }
void pairReset(bool ok) { state.onPairReset(ok, millis()); }
void setNoStick(bool on) { state.setNoStick(on); }
void setKeepAwake(bool keep) { keepAwake = keep; }

void tick(uint32_t nowMs, bool anyInput) {
  // 밝기부터: 어두운 상태에서 조작으로 깨울 때 새 화면이 어둡게 그려지지 않게 한다.
  if (anyInput || keepAwake) {
    lastActiveMs = nowMs;
    if (dimmed) {
      M5.Display.setBrightness(BRIGHT);
      dimmed = false;
    }
  } else if (!dimmed && elapsedMs(nowMs, lastActiveMs, DIM_AFTER_MS)) {
    M5.Display.setBrightness(DIM);
    dimmed = true;
  }

  // 그림이 달라졌으면 맨 위 띠부터 다시 그린다. 한 틱에 한 띠만 그려 loop 가 오래 막히지 않게 한다
  // (한 번에 다 그리면 그동안 버튼·조이스틱을 읽지 못한다 — 2차 검토 I1).
  screen::Frame f;
  if (state.poll(nowMs, f)) {
    drawing = f;
    nextBand = 0;
    drawUs = 0;
  }
  if (nextBand < screen::BANDS) {
    const uint32_t t0 = micros();
    M5.Display.startWrite();  // 한 띠를 한 번에 묶어 보낸다
    screen::drawFrameBand(canvas, drawing, nextBand);
    M5.Display.endWrite();
    drawUs += micros() - t0;
    if (++nextBand == screen::BANDS) {
      DBG_LOG("[ui] scene=%d link=%d draw=%luus\n", (int)drawing.scene, (int)drawing.link,
              (unsigned long)drawUs);
    }
  }
}

}  // namespace ui
