// vibe-stick CoreS3 판: 화면 터치 → 사건 → 키 → USB 키보드, 기기 마이크 → USB 마이크
#include <M5Unified.h>
#include <NimBLEDevice.h>

#include "ble_hid.h"
#include "boot_guard.h"
#include "config.h"
#include "dev_show.h"
#include "diag.h"
#include "download_mode.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logic/clock.h"
#include "logic/dispatch.h"
#include "logic/input_logic.h"
#include "key_route.h"
#include "mic.h"
#include "motion.h"
#include "screen_power.h"
#include "services/gap/ble_svc_gap.h"
#include "sound.h"
#include "speech_ear.h"
#include "nvs_flash.h"
#include "pane_switch.h"
#include "quiet_gate.h"
#include "sensors.h"
#include "touch_map.h"
#include "touch_state.h"
#include "ui.h"
#include "update_gate.h"
#include "view_tap_guard.h"
#include "log_budget.h"
#include "usb_dev.h"

namespace {

inline uint32_t millis32() { return (uint32_t)(esp_timer_get_time() / 1000); }

// 사건 처리(logic/dispatch)가 쓰는 "키를 내보내는 쪽": USB 가 쓸 수 있으면 USB, 아니면 블루투스(D59, logic/key_route.h)
bool bleOn = false;  // 블루투스를 켰는가(저장 영역을 열지 못하면 켜지 않는다 — 그때는 USB 로만 쓴다)
class RoutedKeys : public KeyPort {
 public:
  bool send(uint8_t mods, uint8_t key) override {
    const bool ok = (route == touchui::Route::Usb) ? usbdev::send(mods, key) : (bleOn && blehid::send(mods, key));
    // 키가 나가지 않은 때를 기기 속 기록에 남긴다(D67): 어느 길이었고 그때 두 길의 상태가 무엇이었나.
    // 10초에 네 줄까지만(연결 없이 ▲ 를 누르고 있으면 0.4초마다 실패해 기록을 채운다 — logic/log_budget.h).
    // more: 앞서 적지 못한 실패의 수.
    if (!ok && notSentLog.allow((uint32_t)(esp_timer_get_time() / 1000))) {
      usbdev::logf("[key] not sent route=%s usb=%d ble=%d mods=%02x key=%02x more=%u\n",
                   route == touchui::Route::Usb ? "usb" : "ble", (int)usbdev::link(),
                   bleOn ? (int)blehid::link() : -1, mods, key, (unsigned)notSentLog.takeSkipped());
    }
    return ok;
  }
  bool releaseAll() override {
    if (route == touchui::Route::Usb) return usbdev::releaseAll();
    return bleOn && blehid::releaseAll();
  }
  void hold(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
  touchui::Route route = touchui::Route::Usb;

 private:
  touchui::LogBudget notSentLog{4, 10000};
};

// 블루투스의 짝(결합)을 지운다. 지운 뒤에는 재시작이 가장 확실하다(StickC 판과 같다): 깨끗한 상태에서 새 컴퓨터를 기다린다.
uint32_t restartAtMs = 0;  // 0 이 아니면 그 시각에 재시작한다
const uint32_t RESTART_DELAY_MS = 1500;  // "Pairing cleared" 를 볼 시간
class BlePeers : public PeerControl {
 public:
  bool forgetPeers() override {
    const bool ok = bleOn && blehid::forgetPeers();
    if (ok) restartAtMs = (uint32_t)(esp_timer_get_time() / 1000) + RESTART_DELAY_MS;
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

InputLogic           logic;
touchui::TouchMapper touch;
touchui::VoiceGate   voice(VOICE_ON_LEVEL, VOICE_OFF_LEVEL, VOICE_HOLD_MS);
touchui::SpeechEar   ear(GUESS_VOICE_MIN, GUESS_VOICE_RATIO, GUESS_VOICE_RUN);  // 방의 소리에서 말소리를 가린다(D65·D70)
bool                 lastListening = false;  // 직전 틱에 화면이 듣는 중이었는가(기록용)
bool                 lastTruth = false;      // 직전 틱에 "사실을 따르는 중"(D74)이었는가(기록용)
Every                earTimer;               // 듣는 동안 소리 크기를 가끔 적는다(기록용)
uint32_t             listenAtMs = 0;         // 듣는 중이 시작된 시각([ear] 줄의 간격을 정한다)
uint16_t             earTicks = 0;           // 그 사이의 틱 수(틱이 10ms 보다 길어질 수 있어, 셈을 읽을 때 필요하다)
touchui::LogBudget   viewLog(6, 10000);      // [view] 줄: 10초에 여섯 줄까지(위쪽을 계속 두드려도 기록을 채우지 않게)
touchui::LogBudget   updateLog(4, 10000);    // [update] 줄(물음 중에 받지 않은 ✓)
uint16_t             earPeak = 0;            // 그 사이 가장 컸던 날 소리
touchui::LoudStats   earLoud;                // 그 사이 문턱을 넘은 틱(수 · 가장 길게 이어진 길이)
touchui::LoudStats   earProbe;               // 그 사이 낮은 후보 문턱(EAR_PROBE_LEVEL)을 넘은 틱
uint16_t             earHeard = 0;           // 그 사이 말소리로 친 틱 수(D70)
RoutedKeys           keys;
BlePeers             peers;
UiScreen             screen;

int  lastLink = -1;
bool lastMic  = false;
bool lastLogOpen = false;
touchui::UpdateGate updateGate;  // 펌웨어 물음(`Update firmware?`)을 묻는 동안의 손가락 처리(D47, D51)
touchui::PaneSwitch paneSwitch;  // Back·Next: 짧게 = 옆 패널, 길게 = 다른 창(D58)
touchui::QuietGate  quiet;       // 누름을 받지 않는 뜸: 켜진 직후·연결이 바뀐 직후·꺼진 화면을 깨운 누름(D69)
touchui::ViewTapGuard viewGuard; // 보는 곳의 톡을 받아도 되는 때인가(D71)
bool                lastQuiet = false;  // 직전 틱에 뜸이 닫혀 있었는가(기록용)
touchui::CancelGuard cancelGuard(inputcfg::CANCEL_COOLDOWN_MS);  // 취소는 1초에 한 번 — 버튼과 흔들기를 함께 센다(D17)
// 놀고 있던 부품들(D60~D62): 손이 가까이 있는가, 기기가 움직이는가, 흔들었는가
touchui::Proximity      prox(PROX_NEAR, PROX_FAR);
touchui::MotionDetector motion(MOTION_MOVE_MG, MOTION_HOLD_MS);
touchui::ShakeDetector  shaker(touchui::ShakeDetector::Limits{SHAKE_STRONG_MG, SHAKE_SWINGS, SHAKE_WINDOW_MS, SHAKE_COOLDOWN_MS});
Every    proxTimer, clockTimer;
bool     nearNow = false, movingNow = false;
uint16_t lastPs = 0, lastAls = 0;          // 마지막으로 읽은 값(개발판의 기록용)
int16_t  lastAx = 0, lastAy = 0, lastAz = 0;
uint8_t  lastPressed = 0;                  // 지난 틱에 눌려 있던 자리들(누름 소리를 누르는 순간에만 내려고)
Every batteryTimer;  // 배터리를 가끔 읽는다(D54)
int   batteryPct = -1;
bool  batteryCharging = false;
#if DEV_BUILD
Every statTimer;  // 상태를 가끔 찍는다
bool  healthy = false;  // 한동안 멀쩡히 돌았다고 알렸는가(boot_guard)

// 켠 뒤 이만큼 멀쩡히 돌면 "켜지자마자 죽기"로 세던 횟수를 지운다.
const uint32_t HEALTHY_AFTER_MS = 10000;
// "죽어서 재시작"이 연달아 이만큼이면 내려받기 모드로 물러난다(D48). 개발판에만 있다(D52).
const uint32_t CRASH_LIMIT = 2;
#endif

// 화면에 닿아 있는 손가락을 읽어 TouchMapper 에 넣는다.
void readTouch(uint32_t now) {
  touchui::TouchPoint pts[touchui::TouchMapper::MAX_TOUCH];
  int n = 0;
  const int count = M5.Touch.getCount();
  for (int i = 0; i < count && n < touchui::TouchMapper::MAX_TOUCH; i++) {
    const auto& d = M5.Touch.getDetail(i);
    if (!d.isPressed()) continue;
    pts[n].id = (uint8_t)d.id;
    pts[n].x = d.x;
    pts[n].y = d.y;
    ++n;
  }
  touch.update(pts, n, now);
}

#if DEV_BUILD
// (개발판) 컴퓨터가 보낸 시안 그림을 화면에 띄운다(dev_show.h, tools/vibectl.py show).
// 그림이 떠 있는 동안 평소 화면은 그리지 않고, 어떤 누름도 키로 나가지 않는다. 화면을 누르면 거둔다.
uint32_t showLastByteMs = 0;    // 마지막으로 그림 바이트가 온 시각
uint32_t showQuietUntilMs = 0;  // 그림을 거둔 손가락이 키로 새지 않게 이때까지 누름을 버린다
bool     showQuiet = false;

void endShow(uint32_t now, const char* why) {
  devshow::end();
  usbdev::setRawMode(false);
  ui::hold(false);  // 바탕부터 전부 다시 그린다
  showQuiet = true;
  showQuietUntilMs = now + 500;
  usbdev::logf("vibe:img off (%s)\n", why);
}

void devShowTick(uint32_t now) {
  const int cmd = usbdev::takeImageCmd();
  if (cmd >= 0) {
    devshow::begin(cmd);
    ui::hold(true);
    showLastByteMs = now;
    usbdev::logf("vibe:img ready %u\n", (unsigned)devshow::IMAGE_BYTES);
  } else if (cmd == usbdev::IMAGE_OFF && devshow::active()) {
    endShow(now, "command");
  }
  if (!devshow::active()) return;
  if (devshow::remaining() > 0) {
    // 한 틱에 7ms 까지 받는다. 그동안은 터치를 읽지 못하지만 그림을 받는 1~2초 동안만이다.
    uint8_t buf[512];
    const int64_t until = esp_timer_get_time() + 7000;
    M5.Display.startWrite();
    while (devshow::remaining() > 0 && esp_timer_get_time() < until) {
      const size_t want = devshow::remaining() < sizeof(buf) ? devshow::remaining() : sizeof(buf);
      const size_t n = usbdev::readRaw(buf, want);
      if (n == 0) continue;  // 아직 오지 않았다: 시간 안에서 기다린다(USB 태스크가 우선순위가 높아 끼어들어 채운다)
      devshow::feed(buf, n);
      showLastByteMs = now;
    }
    M5.Display.endWrite();
    if (devshow::remaining() == 0) {
      usbdev::setRawMode(false);  // 다 받았다: 다시 명령을 듣는다
      usbdev::logf("vibe:img done\n");
    } else if (elapsedMs(now, showLastByteMs, 3000)) {
      endShow(now, "timeout");  // 보내다 만 그림에 묶여 있지 않는다
    }
    return;
  }
  if (touch.touching()) endShow(now, "touch");
}
#endif

// 배터리 남은 양과 충전 여부를 읽어 화면의 상태 줄에 넘긴다(D54).
// 전원 칩(AXP2101)에 I2C 로 묻는다 → 터치와 같은 태스크(loop)에서만 부른다.
void readBattery() {
  batteryPct = (int)M5.Power.getBatteryLevel();
  batteryCharging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  ui::setBattery(batteryPct, batteryCharging);
  if (bleOn && batteryPct >= 0) blehid::setBattery((uint8_t)(batteryPct > 100 ? 100 : batteryPct));  // 컴퓨터의 블루투스 목록에도 보인다
}

// Back·Next 를 짧게 눌렀다: 같은 창 안의 이전·다음 패널로 가는 키(F17·F18)를 한 번 친다(D58).
void sendPaneKey(bool next) {
  if (keys.send(0, next ? KEY_PANE_NEXT : KEY_PANE_PREV)) {
    keys.hold(KEY_TIMING.tapHoldMs);
    keys.releaseAll();
    keys.hold(KEY_TIMING.gapMs);
    ui::sent(next ? Ev::WindowNext : Ev::WindowPrev);  // 캐릭터의 눈이 그쪽을 본다
  } else {
    ui::notSent();
  }
}

void tick(uint32_t now) {
  M5.update();  // 터치를 읽는다(I2C)
  usbdev::poll(now);
  if (batteryTimer.due(now, BATTERY_MS)) readBattery();

  // 놀고 있던 부품들. 모두 기기 안의 I2C 줄에 있어 이 태스크에서 읽는다.
  bool shook = false;
  if (sensors::readAccel(lastAx, lastAy, lastAz)) {  // 움직임(틱마다): 들면 켜지고, 흔들면 취소(D61)
    movingNow = motion.feed(lastAx, lastAy, lastAz, now);
    shook = shaker.feed(lastAx, lastAy, lastAz, now);
  }
  if (proxTimer.due(now, 100) && sensors::readProx(lastPs, lastAls)) {  // 근접·밝기(0.1초마다, D60)
    nearNow = prox.feed(lastPs);
    ui::setAmbient(lastAls);
  }
  if (clockTimer.due(now, 1000)) {  // 시각(1초마다, D62)
    int hh = 0, mm = 0, ss = 0;
    ui::setClock(sensors::readClock(hh, mm, ss) ? hh * 60 + mm : -1);
  }
  int t[6];
  if (usbdev::takeTime(t)) {  // 컴퓨터가 시각을 알려 왔다
    sensors::setClock(t[0], t[1], t[2], t[3], t[4], t[5]);
    clockTimer.last = now - 1000;  // 곧바로 다시 읽어 화면에 반영한다
  }
  const int note = usbdev::takeNote();  // 컴퓨터의 Claude 가 알려 왔다(D63)
  // 펌웨어 물음이 떠 있는 동안의 알림은 통째로 버린다 — 소리도 내지 않는다(D75): 알림 소리로 사람을 불러 물음에
  // 답하게 만드는 길을 좁힌다. (화면의 알림은 물음에 가려 어차피 보이지 않았다.)
  if (note >= 0 && !ui::updateAsking(now)) {
    ui::note(note == 1);
    sound::play(note == 1 ? sound::Kind::Ask : sound::Kind::Done);
  }
#if DEV_BUILD
  devShowTick(now);
#endif

  if (bleOn) blehid::poll(now);
  if (restartAtMs != 0 && reachedMs(now, restartAtMs)) esp_restart();  // 짝을 지운 뒤

  // 키를 보낼 길(USB 가 되면 USB, 아니면 블루투스)과 화면에 보일 연결 상태를 정한다(D59).
  // 길이나 상태가 바뀌면 눌린 채 남은 키가 없게 한다.
  const LinkState usbLink = usbdev::link(), bleLink = bleOn ? blehid::link() : LinkState::Searching;
  const touchui::Route route = touchui::pickRoute(usbLink);
  const touchui::LinkView view = touchui::viewLink(usbLink, bleLink);
  const int linkKey = ((int)view.link << 2) | (view.wireless ? 2 : 0) | (route == touchui::Route::Ble ? 1 : 0);
  if (linkKey != lastLink) {
    if (route != keys.route) {
      keys.releaseAll();  // 옛 길에 눌린 키를 떼고
      keys.route = route;
    }
    onLinkChange(view.link == LinkState::Ready, logic, keys);
    paneSwitch.reset();
    // 막 쓸 수 있게 됐거나 길이 바뀌었다: 그 순간에 걸친 누름은 받지 않는다(D69).
    if (view.link == LinkState::Ready) quiet.arm(now, 0, QUIET_LINK_CALM_MS);
    lastLink = linkKey;
    ui::setLink(view.link, view.wireless);
    usbdev::logf("[link] %s%s usb=%s ble=%s t=%lu heap=%lu\n", linkName(view.link), view.wireless ? "(bt)" : "",
                 linkName(usbLink), linkName(bleLink), (unsigned long)now, (unsigned long)esp_get_free_heap_size());
  }

  // 컴퓨터가 마이크를 열었는가(USB 로 아는 사실, D35)
  const bool micOpen = usbdev::micOpen();
  if (micOpen != lastMic) {
    lastMic = micOpen;
    ui::setMicLive(micOpen);
    if (!micOpen) voice.reset();
    usbdev::logf("[mic] %s t=%lu\n", micOpen ? "open" : "closed", (unsigned long)now);
  }

  // 컴퓨터가 펌웨어를 바꾸겠다고 했다. 개발판은 곧바로 따르고, 평소 판은 화면에서 확인을 받는다(D47).
  if (usbdev::takeBootRequest()) {
#if DEV_BUILD
    usbdev::enterDownloadMode("vibe:boot", DIAG_DL_COMMAND);
#else
    if (usbLink != LinkState::Ready) {
      usbdev::cmdf("vibe:boot ignored — not ready over USB\n");
    } else if (ui::askUpdate()) {
      usbdev::cmdf("vibe:boot waiting — hold OK on the device screen\n");
    } else {
      usbdev::cmdf("vibe:boot ignored — already asking, or just declined\n");
    }
#endif
  }
  // 물음이 떠 있는데 선 쪽 연결이 사라졌다(뽑혔다 · 컴퓨터가 잠들었다): 물음을 거둔다. 펌웨어는 선으로만 올릴 수 있고,
  // 뽑힌 채 주머니에 들어간 기기에 물음이 20초 동안 떠 있을 까닭이 없다(보안 점검).
  if (ui::updateAsking(now) && usbLink != LinkState::Ready) {
    ui::cancelUpdate();
    usbdev::cmdf("vibe:boot cancelled — USB link lost\n");
  }

  // 마이크가 멎었으면 되살린다.
  if (mic::poll(now)) usbdev::logf("[mic] rearmed t=%lu\n", (unsigned long)now);

  // 펌웨어 물음(`Update firmware?`)의 경계에 걸친 손가락은 받지 않는다: 확인을 받기 시작할 때 닿아 있던 손가락은 "예"가
  // 될 수 없고, 물음이 끝난 뒤 한동안은 어떤 누름도 키로 나가지 않는다(logic/update_gate.h, D51).
  const bool promptDrain = updateGate.begin(now, ui::updateAsking(now), ui::updateArmed(now), touch.touching());
  // 뜸(D69): 닫혀 있는 동안에는 누름을 사건으로 만들지 않는다.
  const bool quietNow = quiet.closed(now, touch.touching());
  if (quietNow != lastQuiet) {  // 기기 속 기록에: 누름을 받지 않기 시작한 때와 다시 받기 시작한 때
    lastQuiet = quietNow;
    usbdev::logf("[quiet] %s t=%lu\n", quietNow ? "closed" : "open", (unsigned long)now);
  }
  if (promptDrain || quietNow) {
    logic.reset();
    paneSwitch.reset();
  }

  readTouch(now);
  {  // D75 "예"는 확인을 꾹 눌러야 한다: 확인 자리의 누름이 얼마나 이어졌는지 잰다(다른 자리가 함께 눌리면 무효).
    const uint8_t m = touch.pressed(), ok = touchui::bit(touchui::Zone::Ok);
    updateGate.press(now, (m & ok) != 0, (m & (uint8_t)~ok) != 0);
  }
  // 꺼진 화면에 손가락이 닿았다: 이 누름은 화면만 깨운다(화면은 아래 ui::tick 에서 켜진다).
  // 누름이 인식되기까지 두 틱이 걸리므로, 지금 뜸을 걸면 이 손가락은 사건이 되지 않는다.
  if (ui::dark() && touch.touching()) quiet.arm(now, 0, QUIET_WAKE_CALM_MS);
  // D71 보는 곳(캐릭터가 있는 위쪽)을 톡 쳤다: 키를 보내지 않고 듣는 중 표시만 뒤집는다 — 미루어 듣는 중(D65)이
  // 실제와 어긋났을 때 바로잡는 길이다. 받아도 되는 톡인지는 viewGuard 가 정한다(logic/view_tap_guard.h):
  // 뜸(D69) · 펌웨어 물음(D51) 동안의 톡, 어두워졌거나 꺼진 화면을 깨운 톡은 버린다.
  // ui::bright() 는 이번 틱에 화면을 켜기 전(아래 ui::tick)의 상태다.
  bool viewTap = viewGuard.feed(now, touch.touching(), ui::bright(), quietNow || updateGate.blocking(), touch.takeViewTap());
  RawInput in{};
  touch.fill(in);

  Ev evs[InputLogic::MAX_EVENTS];
  int n = logic.update(in, now, evs);
  if (quietNow) n = 0;  // 뜸(D69): 화면의 누름은 사건이 되지 않는다
  // (개발판) 로그 포트로 들어온 시험용 사건을 같은 길로 흘려 보낸다. 손가락이 아니므로 뜸과 무관하다.
  Ev injected;
  while (n < InputLogic::MAX_EVENTS && usbdev::takeInjected(injected)) evs[n++] = injected;
  // 흔들었다 → 취소(D61). 버튼의 취소와 같은 길로 흘려 보낸다(물음이 떠 있으면 "아니오"가 된다). 뜸 동안에는 받지 않는다.
  // 그리고 화면이 밝게 켜져 있을 때만(D69 와 같은 뜻 — 꺼졌거나 어두워진 화면의 기기를 흔든 것은 "지금 화면을 보고
  // 한 것"이 아니다: 가방 안에서 흔들린 기기가 Esc 를 보내지 않게. 통합 점검). 흔들면 화면은 켜진다.
  if (shook && SHAKE_CANCEL && !quietNow && ui::bright() && n < InputLogic::MAX_EVENTS) evs[n++] = Ev::Cancel;
  // 누름 소리(D63): 버튼을 누르는 순간에. 말하기에는 내지 않는다(마이크를 내리면 첫 마디가 잘릴 수 있다).
  const uint8_t pressedNow = touch.pressed();
  // 화면이 듣는 중일 때도 내지 않는다(미루어 듣는 중에는 컴퓨터의 다른 마이크가 "톡"을 받아 적는다 — D65).
  // 키가 나가지 않는 때(뜸 · 물음이 끝난 직후)에는 소리도 내지 않는다 — 소리가 나면 눌린 줄 안다.
  if ((pressedNow & ~lastPressed & (uint8_t)~touchui::bit(touchui::Zone::Talk)) && !ui::listening() && !quietNow && !promptDrain) {
    sound::play(sound::Kind::Click);
  }
  lastPressed = pressedNow;

#if DEV_BUILD
  // (개발판) 시안 그림이 떠 있는 동안, 그리고 그것을 거둔 손가락을 뗄 때까지는 누름을 버린다.
  if (touch.touching()) showQuietUntilMs = now + 500;
  if (showQuiet && !devshow::active() && !touch.touching() && reachedMs(now, showQuietUntilMs)) showQuiet = false;
  if (devshow::active() || showQuiet) {
    n = 0;
    viewTap = false;
  }
#endif
  if (viewTap) {
    // r: 0 듣지 않음(사실을 안다 · 연결 없음) · 1 표시를 뒤집음 · 2 떠 있던 Claude 의 알림만 거둠
    // truth: 말하기를 누르면 컴퓨터가 이 기기의 마이크를 여는 것이 확인돼 있는가(D74 — 그러면 톡이 빨강을 켜지 않는다)
    // more: 앞서 적지 못한 톡의 수(10초에 여섯 줄까지만 적는다)
    const int r = ui::flipListening();
    if (viewLog.allow(now)) {
      usbdev::logf("[view] tap r=%d truth=%d more=%u t=%lu\n", r, (int)ui::truthKnown(),
                   (unsigned)viewLog.takeSkipped(), (unsigned long)now);
    }
  }
  for (int i = 0; i < n; i++) {
    // 펌웨어 물음을 묻는 동안에는 어떤 누름도 키로 나가지 않는다: 확인은 "예", 취소는 "아니오".
    switch (updateGate.judge(evs[i])) {
      case touchui::UpdateVerdict::Yes:
        usbdev::enterDownloadMode("confirmed on screen", DIAG_DL_CONFIRMED);  // 돌아오지 않는다
        break;
      case touchui::UpdateVerdict::No:
        ui::cancelUpdate();
        usbdev::logf("vibe:boot cancelled on the device\n");
        break;
      case touchui::UpdateVerdict::Swallow:
        // 물음이 떠 있는데 확인(✓)을 받지 않았다: 까닭을 기기 속 기록에 남긴다 — "눌렀는데 안 된다"가 톡이어서인지
        // (held 가 짧다), 다른 자리가 함께 닿아서인지(spoiled), 뜨자마자여서인지(armed=0), 닿아 있던 손가락이어서인지
        // (stale)를 가린다. 누가 물음에 ✓ 를 눌렀다는 사실 자체도 남길 만한 일이다. (10초에 네 줄까지)
        if (evs[i] == Ev::Confirm && ui::updateAsking(now) && updateLog.allow(now)) {
          usbdev::logf("[update] ok not accepted held=%lums armed=%d stale=%d spoiled=%d more=%u t=%lu\n",
                       (unsigned long)updateGate.okHeldMs(now), (int)updateGate.armed(), (int)updateGate.stale(),
                       (int)updateGate.okSpoiled(), (unsigned)updateLog.takeSkipped(), (unsigned long)now);
        }
        break;
      case touchui::UpdateVerdict::Pass:
        // 취소(Esc)는 1초에 한 번만: 버튼의 취소 직후의 흔들림(또는 그 반대)이 두 번째 Esc 가 되지 않게.
        if (evs[i] == Ev::Cancel && !cancelGuard.allow(now)) break;
        usbdev::logf("[ev] %d t=%lu\n", (int)evs[i], (unsigned long)now);
        // 창 넘기기는 곧바로 보내지 않는다: 짧게인지 길게인지 본 뒤에 아래에서 보낸다(D58).
        if (!paneSwitch.onEvent(evs[i], now)) dispatchEvent(evs[i], HOST, KEY_TIMING, keys, peers, screen);
        break;
    }
  }
  // 물음이 떠 있거나 뜸인 동안에는 Back·Next 도 키로 나가지 않는다(누르고 있던 것은 버린다).
  if (updateGate.blocking() || quietNow) paneSwitch.reset();
  if (paneSwitch.waiting()) {
    const touchui::Zone z = paneSwitch.waitingNext() ? touchui::Zone::Next : touchui::Zone::Back;
    switch (paneSwitch.poll(now, (touch.pressed() & touchui::bit(z)) != 0)) {
      case touchui::PaneAction::PaneNext:   sendPaneKey(true); break;
      case touchui::PaneAction::PanePrev:   sendPaneKey(false); break;
      case touchui::PaneAction::WindowNext: dispatchEvent(Ev::WindowNext, HOST, KEY_TIMING, keys, peers, screen); break;
      case touchui::PaneAction::WindowPrev: dispatchEvent(Ev::WindowPrev, HOST, KEY_TIMING, keys, peers, screen); break;
      case touchui::PaneAction::None:       break;
    }
  }

  // 받지 않는 동안(뜸 · 물음이 끝난 직후)에는 눌린 모습도 보이지 않는다
  ui::setPressed((quietNow || promptDrain) ? 0 : touch.pressed());
  // 소리 크기는 컴퓨터가 이 기기의 마이크를 열지 않았어도 늘 잰다(기기 밖으로 나가지 않는다).
  //   - 입을 여닫는 문턱(voice): 컴퓨터가 받는 소리의 크기(입력 음량을 곱한 값). 사실로 듣는 중에 쓴다.
  //   - 말소리 가리기(ear): 방의 소리 그대로에서. 말하기를 누른 것으로 미루어 듣는 중(D65)을 이어 가고,
  //     그동안 입·파형을 움직인다.
  const uint16_t rawNow = mic::rawLevel();
  ui::setVoice(voice.feed(mic::level(), now));
  const bool heardNow = ear.feed(rawNow);
  ui::setHeard(heardNow);
  ui::tick(now, touch.touching(), movingNow, nearNow);
  // 알림음은 컴퓨터가 이 기기의 마이크를 실제로 열었을 때만 막는다(소리 통로를 나눠 쓰기 때문 — D63).
  // 미루어 듣는 중에는 막지 않는다: 사람의 답을 기다린다는 알림이 소리 없이 지나가면 안 된다.
  sound::poll(now, micOpen);

  // 기기 속 기록(D67): 듣는 중이 켜지고 꺼진 때와 까닭, 듣는 동안의 소리 크기.
  const bool listeningNow = ui::listening();
  if (rawNow > earPeak) earPeak = rawNow;
  earLoud.feed(ear.loud());                        // 문턱을 넘은 틱(이어졌는지와 무관)
  earProbe.feed(rawNow >= EAR_PROBE_LEVEL);        // 낮은 후보 문턱을 넘은 틱(문턱을 고를 자료)
  if (heardNow && earHeard < 65535) ++earHeard;    // 말소리로 친 틱(D70)
  if (earTicks < 65535) ++earTicks;
  // 미룸이 내려간 까닭은 그 틱에 곧바로 적는다(모아 두면 나중의 다른 줄에 낡은 까닭이 찍힌다).
  // D74 "말하기를 누르면 컴퓨터가 이 기기의 마이크를 연다"가 확인된 때와 지워진 때(그동안에는 위쪽 톡이 빨강을 켜지 않는다).
  const bool truthNow = ui::truthKnown();
  if (truthNow != lastTruth) {
    lastTruth = truthNow;
    usbdev::logf("[truth] %s t=%lu\n", truthNow ? "on" : "off", (unsigned long)now);
  }
  const int guessWhy = ui::takeGuessEnd();
  if (guessWhy) usbdev::logf("[guess] end why=%d t=%lu\n", guessWhy, (unsigned long)now);
  if (listeningNow != lastListening) {
    lastListening = listeningNow;
    usbdev::logf("[live] %s guess=%d mic=%d raw=%u floor=%u thr=%u t=%lu\n", listeningNow ? "on" : "off",
                 (int)ui::guessing(), (int)micOpen, (unsigned)rawNow, (unsigned)ear.floor(),
                 (unsigned)ear.threshold(), (unsigned long)now);
    earPeak = 0;
    earHeard = 0;
    earTicks = 0;
    earLoud.reset();
    earProbe.reset();
    earTimer.last = now;
    listenAtMs = now;
  } else if (listeningNow && earTimer.due(now, elapsedMs(now, listenAtMs, EAR_LOG_DENSE_MS) ? EAR_LOG_SPARSE_MS : EAR_LOG_MS)) {
    // 듣는 중의 처음 EAR_LOG_DENSE_MS 는 5초마다, 그 뒤로는 1분마다 적는다: 회의 프로그램이 마이크를 한 시간 열어 두면
    // 5초마다의 줄이 기록을 채워, 선이 뽑혀 있던 동안의 연결 줄을 밀어낸다(통합 점검).
    // n: 그 사이의 틱 수(한 틱이 10ms 보다 길어질 수 있다 — 아래의 셈은 틱 수다). loud: 문턱을 넘은 틱 수,
    // run: 그 가운데 가장 길게 이어진 틱 수, heard: "이어져서" 말소리로 친 틱 수(조용한 방이면 0 이어야 한다).
    // lo · lorun: 낮은 후보 문턱(EAR_PROBE_LEVEL)으로 센 같은 것.
    usbdev::logf("[ear] n=%u peak=%u loud=%u run=%u heard=%u lo=%u lorun=%u floor=%u thr=%u guess=%d t=%lu\n",
                 (unsigned)earTicks, (unsigned)earPeak, (unsigned)earLoud.count, (unsigned)earLoud.longest,
                 (unsigned)earHeard, (unsigned)earProbe.count, (unsigned)earProbe.longest, (unsigned)ear.floor(),
                 (unsigned)ear.threshold(), (int)ui::guessing(), (unsigned long)now);
    earPeak = 0;
    earHeard = 0;
    earTicks = 0;
    earLoud.reset();
    earProbe.reset();
  }

#if DEV_BUILD
  if (!healthy && now > HEALTHY_AFTER_MS) {
    healthy = true;
    boot_guard_healthy();
  }
  // (재 보기) 센서의 날 값을 0.25초마다 찍는다 — 문턱값을 실측으로 정하려고(계획서 1단계).
  static Every sensTimer;
  if (sensTimer.due(now, 250)) {
    usbdev::logf("[sens] prox=%d ps=%u als=%u near=%d imu=%d a=%d,%d,%d moving=%d\n", (int)sensors::proxOk(),
                 (unsigned)lastPs, (unsigned)lastAls, (int)nearNow, (int)sensors::imuOk(), (int)lastAx, (int)lastAy,
                 (int)lastAz, (int)movingNow);
  }
  if (shook) usbdev::logf("[sens] SHAKE t=%lu\n", (unsigned long)now);
  if (statTimer.due(now, 2000)) {
    usbdev::logf("[stat] level=%u mic=%d heap=%lu min=%lu\n", (unsigned)mic::level(), (int)micOpen,
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
  }
#endif
}

}  // namespace

extern "C" void app_main(void) {
  diag_boot();
#if DEV_BUILD
  // 켜지자마자 죽기를 되풀이했다면 아무것도 켜 보지 않고 내려받기 모드로 물러난다(D48).
  // D52 평소 판에는 이 길을 두지 않는다.
  //   이유: 평소 판은 "사람이 화면을 누르거나 RST 를 누르지 않으면 다시 구워지지 않는다"(D47)가 약속이다.
  //         죽으면 물러나게 두면, 컴퓨터의 프로그램이 기기를 죽게 만드는 길을 하나만 찾아도
  //         (연달아 몇 번 죽인 뒤) 화면 확인 없이 다시 구울 수 있게 된다.
  //   비용: 평소 판이 켜지자마자 죽기를 되풀이하면 손 없이는 못 고친다 → RST 3초(초록 불) 뒤 굽는다.
  //         USB 를 켜기 전의 코드는 개발판과 같으므로, 개발판으로 먼저 확인하면 겪을 일이 드물다.
  //   탈출구: 이 #if 를 지우고 CRASH_LIMIT 을 판마다 정한다(예전: 평소 판 3, 개발판 2).
  if (boot_guard_tripped(CRASH_LIMIT)) {
    g_vibe_diag.download_reason = DIAG_DL_CRASH_LOOP;
    download_mode_restart(nullptr);
  }
#endif

  auto cfg = M5.config();
  cfg.internal_mic = true;
  // 스피커는 마이크와 같은 소리 통로(I2S)를 나눠 쓴다 → 평소에는 마이크가 쓰고, 소리를 낼 때만 잠깐 넘긴다(sound.cpp, D63).
  cfg.internal_spk = SOUND_ON;
  cfg.internal_imu = true;    // 움직임 센서: 들면 켜지고 가만히 두면 꺼진다(D61)
  cfg.output_power = false;   // 확장 단자로 5V 를 내보내지 않는다
  cfg.led_brightness = 0;
  M5.begin(cfg);
  // USB 단자는 전원을 "받는" 쪽이어야 한다(내보내는 쪽으로 두면 컴퓨터에 5V 를 밀어 넣는다).
  M5.Power.setUsbOutput(false);
  g_vibe_diag.board = (uint32_t)M5.getBoard();
  g_vibe_diag.stage = DIAG_STAGE_BOARD;

  sensors::begin();
  ui::begin();
  readBattery();  // 첫 화면부터 배터리 값이 보이게(그 뒤로는 BATTERY_MS 마다)
  g_vibe_diag.stage = DIAG_STAGE_SCREEN;
  usbdev::begin();
  // 블루투스 키보드(D59): 선을 뽑았을 때 버튼이 가는 길. 짝 정보는 NVS 에 남는다.
  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();  // 예전 펌웨어가 쓰던 내용이라 읽을 수 없다 → 비우고 다시
    nvs = nvs_flash_init();
  }
  if (nvs == ESP_OK) {
    blehid::begin(BLE_NAME);
    // D66 이름을 한 번 더 넣는다.
    //   까닭: blehid::begin 은 이름을 넣은 뒤에 서버를 시작하는데, ESP-IDF 의 NimBLE 은 서버를 시작할 때
    //         (NimBLEServer::start → ble_svc_gap_init → ble_svc_gap_init_name) 이름을 기본값 "nimble" 로
    //         되돌린다. 그래서 찾을 때(광고)는 "vibe-stick S3" 인데 연결하고 나면 맥의 블루투스 목록에
    //         "nimble" 로 떴다(내 기기인지 알아볼 수 없었다).
    //   StickC 판(아두이노용 NimBLE)에는 이 되돌림이 없어 src/ble_hid.cpp 는 고치지 않는다.
    //   탈출구: 이 줄을 지우면 연결 뒤 이름이 다시 "nimble" 이다.
    NimBLEDevice::setDeviceName(BLE_NAME);
    bleOn = true;
  }
  const bool micOk = mic::begin();
  if (micOk) g_vibe_diag.flags |= DIAG_F_MIC_OK;
  g_vibe_diag.stage = DIAG_STAGE_MIC;
  logic.setTalkMode(TALK_MODE);

  // 켜진 직후의 누름은 받지 않는다(D69): 켜진 지 QUIET_BOOT_MS 까지. 준비가 그보다 오래 걸렸더라도 loop 가
  // 돌기 시작한 뒤 적어도 0.5초는 닫아 둔다(첫 틱에는 아직 손가락을 읽지 못했다).
  {
    const uint32_t t0 = millis32();
    quiet.arm(t0, t0 + 500 < QUIET_BOOT_MS ? QUIET_BOOT_MS - t0 : 500, QUIET_BOOT_CALM_MS);
  }
  TickType_t wake = xTaskGetTickCount();
  g_vibe_diag.stage = DIAG_STAGE_LOOP;
  for (;;) {
    const uint32_t now = millis32();
    tick(now);
    // 컴퓨터가 로그 포트를 열 때마다 자기소개를 한 줄 찍는다.
    const bool logOpen = usbdev::logOpen();
    if (logOpen && !lastLogOpen) {
      usbdev::logf("[boot] vibe-stick cores3 dev=%d board=%d mic=%d reset=%d up=%lums heap=%lu bat=%d chg=%d bt=\"%s\"\n",
                   DEV_BUILD, (int)M5.getBoard(), (int)micOk, (int)esp_reset_reason(),
                   (unsigned long)now, (unsigned long)esp_get_free_heap_size(), batteryPct,
                   (int)batteryCharging, bleOn ? ble_svc_gap_device_name() : "");
    }
    lastLogOpen = logOpen;
    // 다음 틱까지 쉰다. 키를 보내느라 늦었으면 쉬지 않고 바로 이어 간다.
    if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS)) == pdFALSE) wake = xTaskGetTickCount();
  }
}
