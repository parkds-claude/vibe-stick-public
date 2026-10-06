// vibe-stick: 버튼·조이스틱 → 사건 → 키 → 블루투스 키보드
#include <M5Unified.h>
#include <esp_system.h>

#include "ble_hid.h"
#include "config.h"
#include "debug_trace.h"
#include "joyc.h"
#include "logic/clock.h"
#include "logic/dispatch.h"
#include "logic/input_logic.h"
#include "logic/joy_filter.h"
#include "ui.h"

namespace {

// 방향을 인정하기 전 머무는 시간은 "누름이 인식되기까지의 지연(조이스틱 읽기 2회)"을 덮어야 한다.
// 그래야 누르려다 스틱이 먼저 기울어진 것을 방향으로 치지 않는다(logic/input_logic.h 의 설명 참고).
static_assert(inputcfg::DIR_DWELL_MS >= inputcfg::TILT_LEAD_MS + 2 * JOY_POLL_MS,
              "DIR_DWELL_MS 가 조이스틱 누름 인식 지연을 덮지 못한다");

const uint32_t RESTART_DELAY_MS = 1200;  // 짝 초기화 뒤 화면 문구를 보여주고 재시작하기까지
const int      USB_MV           = 4000;  // USB 전압이 이 이상이면 USB 전원으로 본다

// 0 이 아니면 그 시각에 재시작한다.
// 결합을 지운 뒤에는 재시작이 가장 확실하다: blehid::forgetPeers() 는 성공하면 광고를
// 멈춘 채로 두므로, 재시작해야 새 컴퓨터를 기다리는 상태가 된다.
uint32_t restartAtMs = 0;

// 사건 처리(logic/dispatch)가 쓰는 "키를 내보내는 쪽" = 블루투스 키보드
class BleKeys : public KeyPort {
 public:
  bool send(uint8_t mods, uint8_t key) override { return blehid::send(mods, key); }
  bool releaseAll() override { return blehid::releaseAll(); }
  void hold(uint32_t ms) override { delay(ms); }
};

// 사건 처리가 쓰는 "짝 정보를 지우는 쪽"
class BlePeers : public PeerControl {
 public:
  bool forgetPeers() override {
    const bool ok = blehid::forgetPeers();
    Serial.printf("[bond] forget %s\n", ok ? "ok -> restart" : "FAILED");
    if (ok) restartAtMs = millis() + RESTART_DELAY_MS;
    return ok;
  }
};

// 사건 처리가 쓰는 "화면"
class UiScreen : public ActionScreen {
 public:
  void sent(Ev e) override { ui::sent(e); }
  void notSent() override { ui::notSent(); }
  void pairReset(bool ok) override { ui::pairReset(ok); }
};

InputLogic logic;
JoyFilter  joyFilter(JOY_SWAP_XY, JOY_INVERT_Y, JOY_INVERT_X);
BleKeys    keys;
BlePeers   peers;
UiScreen   screen;

bool     joyPresent = false;
int      lastLink   = -1;
uint32_t lastTickMs = 0;

// 주기마다 한 번씩 할 일들
Every joyPollTimer;   // 조이스틱 읽기
Every joyRetryTimer;  // 조이스틱이 없을 때 다시 찾기
Every powerTimer;     // 배터리·전원 상태
#if DEBUG_JOY
Every joyLogTimer;
#endif
#if DEBUG_LINK
Every memLogTimer;

// loop 가 멈췄는지 지켜보는 태스크: 1초 동안 한 바퀴도 못 돌았으면 멈춘 단계를 알린다.
void watchTask(void*) {
  uint32_t lastBeat = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const uint32_t beat = dbgBeat;
    if (beat == lastBeat) {
      Serial.printf("[stuck] loop stage=%u t=%lu\n", (unsigned)dbgStage, (unsigned long)millis());
    }
    lastBeat = beat;
  }
}
#endif

void updatePower() {
  int pct = M5.Power.getBatteryLevel();
  if (pct > 100) pct = 100;
  ui::setBattery(pct);
  if (pct >= 0) blehid::setBattery((uint8_t)pct);
  // USB 전원이면 화면을 어둡게 하지 않는다.
  ui::setKeepAwake(M5.Power.getVBUSVoltage() > USB_MV);
}

// 조이스틱을 찾는다. 찾으면 걸러 둔 값을 비우고 안내를 지운다.
void probeJoystick() {
  joyPresent = joyc::begin();
  if (joyPresent) joyFilter.reset();
  ui::setNoStick(!joyPresent);
}

// 조이스틱을 한 번 읽어 JoyFilter 에 넣는다. 연속 실패가 한도를 넘으면 "없음"으로 바꾼다.
void pollJoystick(uint32_t now) {
  int8_t x = 0, y = 0;
  bool   btn = false;
  const bool ok = joyc::read(x, y, btn);
  joyFilter.feed(ok, x, y, btn);
  if (joyFilter.lost()) {
    joyPresent = false;        // 뽑혔거나 계속 읽기 실패 → 버튼만으로 동작
    joyRetryTimer.last = now;  // 다시 찾기는 지금부터 JOY_RETRY_MS 뒤에
    ui::setNoStick(true);
  }
}

}  // namespace

#if DEBUG_LINK
volatile uint16_t dbgStage = 0;
volatile uint32_t dbgBeat  = 0;
#endif

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // 기본값 0 이면 Serial 출력이 아예 나오지 않는다
  cfg.internal_mic = false;      // 내장 마이크의 클럭 핀이 조이스틱 SDA(G0)와 같다
  cfg.internal_imu = false;      // 쓰지 않는다
  cfg.pmic_button = false;       // 전원 버튼 사건을 쓰지 않는다(매 틱 I2C 읽기를 없앤다)
  M5.begin(cfg);
  Serial.printf("\n[boot] vibe-stick reset=%d heap=%u\n", (int)esp_reset_reason(),
                (unsigned)ESP.getFreeHeap());

  ui::begin();
  probeJoystick();  // M5.begin 뒤에 해야 한다(M5.begin 이 G0 를 건드린다)
  logic.setTalkMode(TALK_MODE);
  // 초점 키를 쓰면 ⌘D 가 그만큼 늦게 눌린다 → 톡/꾹 구분이 그 시각을 기준으로 하게 한다.
  // (초점 키를 tapHoldMs 동안 누르고, 뗀 뒤 focusSettleMs 를 기다린 다음 ⌘D 를 누른다.)
  if (HOST.focusKey != 0) {
    logic.setTalkLeadMs(KEY_TIMING.tapHoldMs + HOST.focusSettleMs);
  }
  blehid::begin(DEVICE_NAME);
  updatePower();
#if DEBUG_LINK
  xTaskCreatePinnedToCore(watchTask, "watch", 3072, nullptr, 1, nullptr, 0);
#endif
}

void loop() {
  const uint32_t now = millis();

  if (restartAtMs != 0 && reachedMs(now, restartAtMs)) {
    Serial.println("[boot] restart after pair reset");
    Serial.flush();
    ESP.restart();
  }

  const uint32_t sinceTick = now - lastTickMs;
  if (sinceTick < TICK_MS) {
    delay(TICK_MS - sinceTick);  // 다음 틱까지 쉰다
    return;
  }
  lastTickMs = now;
#if DEBUG_LINK
  ++dbgBeat;
#endif
  DBG_STAGE(1);
  M5.update();
  DBG_STAGE(2);
  blehid::poll(now);
  DBG_STAGE(3);

  // 연결 상태가 바뀌면 눌린 채 남은 키가 없게 한다(검토 초점 2).
  const blehid::Link link = blehid::link();
  if ((int)link != lastLink) {
    DBG_LOG("[link] -> %s\n", linkName(link));
    DBG_STAGE(4);
    onLinkChange(link == blehid::Link::Ready, logic, keys);
    lastLink = (int)link;
    DBG_STAGE(5);
    ui::setLink(link);
    Serial.printf("[link] %s t=%lu heap=%u\n", linkName(link), (unsigned long)now,
                  (unsigned)ESP.getFreeHeap());
  }

  DBG_STAGE(6);
  // 조이스틱: 있으면 JOY_POLL_MS 마다 읽고, 없으면 가끔 다시 찾는다(나중에 꽂아도 동작).
  if (joyPresent) {
    if (joyPollTimer.due(now, JOY_POLL_MS)) pollJoystick(now);
  } else if (joyRetryTimer.due(now, JOY_RETRY_MS)) {
    probeJoystick();
  }

  RawInput in{};
  in.btnA = M5.BtnA.isPressed();
  in.btnB = M5.BtnB.isPressed();
  if (joyPresent) joyFilter.fill(in);

#if DEBUG_JOY
  if (joyLogTimer.due(now, 200)) {
    Serial.printf("[joy] ok=%d x=%d y=%d btn=%d\n", in.joyOk, in.x, in.y, in.joyBtn);
  }
#endif

  DBG_STAGE(7);
  Ev evs[InputLogic::MAX_EVENTS];
  const int n = logic.update(in, now, evs);
  for (int i = 0; i < n; i++) {
    dispatchEvent(evs[i], HOST, KEY_TIMING, keys, peers, screen);
  }

  DBG_STAGE(8);
  // 조이스틱을 누르고 있는 동안 조이스틱 LED 를 켠다(색이 바뀔 때만 통신한다).
  if (TALK_LED && joyPresent) joyc::setLed(logic.talking() ? TALK_LED_LEVEL : 0, 0, 0);

  DBG_STAGE(9);
  ui::tick(now, n > 0 || in.btnA || in.btnB || in.joyBtn);

  DBG_STAGE(10);
  if (powerTimer.due(now, BATTERY_MS)) updatePower();
  DBG_STAGE(11);

#if DEBUG_LINK
  if (memLogTimer.due(now, 5000)) {
    Serial.printf("[mem] heap=%u min=%u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap());
  }
#endif
}
