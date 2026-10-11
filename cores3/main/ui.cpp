#include "ui.h"

#include <M5Unified.h>

#include "touch_draw.h"
#include "screen_power.h"
#include "touch_state.h"
#include "config.h"
#include "logic/clock.h"
#include "usb_dev.h"

namespace {

// 그리기 코드(logic/touch_draw)가 내놓는 조각 그림을 LCD 로 옮긴다(D57).
class LcdScreen : public touchui::Screen {
 public:
  void push(int x, int y, int w, int h, const uint16_t* px, int stride) override {
    // px 는 이 칩의 차례 그대로인 RGB565(rgb565_t)다 — 화면이 받는 차례로 바꾸는 일은 라이브러리가 한다.
    if (stride == w) {
      M5.Display.pushImage(x, y, w, h, (const lgfx::rgb565_t*)px);
      return;
    }
    for (int r = 0; r < h; r++) {
      M5.Display.pushImage(x, y + r, w, 1, (const lgfx::rgb565_t*)(px + (size_t)r * stride));
    }
  }
};

using touchui::Light;

touchui::TouchState state;
LcdScreen           canvas;
touchui::Painter    painter;  // 달라진 자리를 한 틱에 하나씩 그린다(logic/touch_draw.h)
bool                ready = false;
bool                onBattery = false;  // 선 없이(블루투스로) 쓰는 중: 배터리를 아끼려고 화면을 끈다
bool                held = false;   // 그리기를 멈춰 두었다(ui::hold)
// 화면을 언제 밝히고 어둡게 하고 끄는가(logic/screen_power.h — D60·D61), 주변 밝기에 따른 단계(D60)
touchui::ScreenPower power(SCREEN_DIM_AFTER_MS, SCREEN_OFF_AFTER_MS);
touchui::AutoBright  ambient(touchui::AutoBright::Limits{ALS_DARK_BELOW, ALS_DARK_ABOVE, ALS_SUNNY_ABOVE, ALS_SUNNY_BELOW});
int                 applied = -1;       // 지금 화면에 넣어 둔 밝기 값(같으면 다시 쓰지 않는다)
int                 clockMin = -1;      // 시계 칩의 시각(자정부터의 분). 모르면 -1
uint32_t            lastTouchMs = 0;    // 마지막으로 손가락이 닿아 있던 시각(시각은 쉴 때만 보인다 — D62)
bool                faceKnown = false;  // 아래 둘에 값이 들어 있는가
touchui::Face       lastFace = touchui::Face::Hi;
bool                lastLive = false;
Light               lastLight = Light::Bright;  // 지금 화면의 단계(밝음·어두움·꺼짐)

// 화면 단계와 주변 밝기에 맞는 밝기를 넣는다.
void applyLight(Light l) {
  const int level = (int)ambient.level();
  const int want = l == Light::Bright ? SCREEN_BRIGHT_BY_LIGHT[level] : l == Light::Dim ? SCREEN_DIM_BY_LIGHT[level] : 0;
  if (want == applied) return;
  applied = want;
  // 이 기종의 밝기는 전원 칩(AXP2101)에 I2C 로 쓴다 → 터치와 같은 태스크(loop)에서만 부른다.
  M5.Display.setBrightness((uint8_t)want);
}

}  // namespace

namespace ui {

void begin() {
  auto& d = M5.Display;
  d.setRotation(SCREEN_ROTATION);  // 세로 240x320, USB 단자가 위(D55)
  power.begin((uint32_t)(esp_timer_get_time() / 1000));
  state.setDev(DEV_BUILD != 0);  // 개발판이면 화면 맨 위에 표시 띠가 늘 보인다
  applyLight(Light::Bright);
}

void setLink(LinkState link, bool wireless) {
  ready = (link == LinkState::Ready);
  onBattery = wireless;
  state.setLink(link, (uint32_t)(esp_timer_get_time() / 1000), wireless);
}
void pairReset(bool ok) { state.onPairReset(ok, (uint32_t)(esp_timer_get_time() / 1000)); }
void setPressed(uint8_t mask) { state.setPressed(mask); }
void setMicLive(bool live) { state.setMicLive(live); }
void setVoice(bool on) { state.setVoice(on); }
void setHeard(bool on) { state.setHeard(on); }
bool guessing() { return state.guessing(); }
int  flipListening() { return (int)state.flipGuess((uint32_t)(esp_timer_get_time() / 1000)); }
bool truthKnown() { return state.truthKnown(); }
bool bright() { return lastLight == Light::Bright; }
int  takeGuessEnd() { return (int)state.takeGuessEnd(); }
bool listening() { return lastLive; }
bool dark() { return lastLight == Light::Off; }
void setBattery(int pct, bool charging) { state.setBattery(pct, charging); }
void sent(Ev e) { state.onSent(e, (uint32_t)(esp_timer_get_time() / 1000)); }
void notSent() { state.onNotSent((uint32_t)(esp_timer_get_time() / 1000)); }
bool askUpdate() { return state.askUpdate((uint32_t)(esp_timer_get_time() / 1000)); }
void cancelUpdate() { state.cancelUpdate((uint32_t)(esp_timer_get_time() / 1000)); }
bool updateAsking(uint32_t nowMs) { return state.updateAsking(nowMs); }
bool updateArmed(uint32_t nowMs) { return state.updateArmed(nowMs); }

void hold(bool on) {
  if (held && !on) painter.invalidate();  // 덮여 있던 화면을 바탕부터 다시 그린다
  held = on;
}

void setAmbient(uint16_t als) { ambient.feed(als); }
void setClock(int minutesOfDay) { clockMin = minutesOfDay; }
void note(bool ask) {
  state.onNote(ask ? touchui::TouchState::Note::Ask : touchui::TouchState::Note::Done, (uint32_t)(esp_timer_get_time() / 1000));
}

void tick(uint32_t nowMs, bool touching, bool moving, bool near) {
  // 시각은 쉴 때만: 손을 뗀 뒤 조금 지나야 보인다(D62).
  if (touching) lastTouchMs = nowMs;
  state.setClock(elapsedMs(nowMs, lastTouchMs, CLOCK_AFTER_MS) ? clockMin : -1);

  touchui::Frame f;
  touchui::Dirty d = 0;
  bool sceneChanged = false;
  if (state.poll(nowMs, f, d)) {
    painter.submit(f, d);
    // 장면이 바뀌는 것(연결됨, 동작 장면, 알림, 듣기 시작)은 화면을 깨운다.
    // 배터리 값이나 시각의 분이 바뀐 것은 깨우지 않는다.
    sceneChanged = !faceKnown || f.face != lastFace || f.micLive != lastLive;
    faceKnown = true;
    lastFace = f.face;
    lastLive = f.micLive;
  }
  // 밝기부터: 어두운 상태에서 깨울 때 새 화면이 어둡게 그려지지 않게 한다.
  touchui::PowerInput in{};
  in.touching = touching;
  in.micLive = lastLive;  // 듣는 중으로 보이는 동안(사실이든 미룸이든 — D65)에는 화면을 끄지 않는다
  in.sceneChanged = sceneChanged;
  in.moving = moving;
  in.near = near;
  in.usable = ready;
  in.onBattery = onBattery;
  lastLight = power.poll(nowMs, in);
  applyLight(lastLight);

  if (held || !painter.busy()) return;

  // 한 틱에 한 자리만 그려 loop 가 오래 막히지 않게 한다(그동안 터치를 읽지 못한다).
  const int64_t t0 = esp_timer_get_time();
  M5.Display.startWrite();  // 한 자리를 한 번에 묶어 보낸다
  painter.step(canvas);
  M5.Display.endWrite();
  if (DEBUG_UI) {
    const touchui::Frame& now = painter.frame();
    usbdev::logf("[ui] face=%d pressed=%02x live=%d left=%03x draw=%ldus\n", (int)now.face,
                 (unsigned)now.pressed, (int)now.micLive, (unsigned)painter.pending(),
                 (long)(esp_timer_get_time() - t0));
  }
}

}  // namespace ui
