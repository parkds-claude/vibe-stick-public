#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "config.h"
#include "logic/dispatch.h"
#include "logic/input_logic.h"
#include "logic/joy_filter.h"
#include "logic/keymap.h"
#include "logic/link_tracker.h"

// ---- keymap ----

// hid:: 상수가 USB HID Usage Tables(Keyboard/Keypad 페이지 0x07)의 값과 같은지. 다른 시험은 코드의 상수와
// 같은 상수를 비교하므로(순환) 여기서만 표의 숫자를 그대로 적는다. (공개 전 검토 B7)
static void test_hid_constants_match_usb_usage_table() {
  CHECK(hid::KEY_D == 0x07);      // Keyboard d and D
  CHECK(hid::KEY_ENTER == 0x28);  // Keyboard Return (ENTER)
  CHECK(hid::KEY_ESC == 0x29);    // Keyboard ESCAPE
  CHECK(hid::KEY_GRAVE == 0x35);  // Keyboard Grave Accent and Tilde
  CHECK(hid::KEY_DOWN == 0x51);   // Keyboard DownArrow
  CHECK(hid::KEY_UP == 0x52);     // Keyboard UpArrow
  CHECK(hid::KEY_F19 == 0x6E);    // Keyboard F19
  // 수정키 비트(보고서 바이트 0): bit0 LeftControl, bit1 LeftShift, bit3 LeftGUI
  CHECK(hid::MOD_LCTRL == 0x01 && hid::MOD_LSHIFT == 0x02 && hid::MOD_LGUI == 0x08);
}

static void test_talk_maps_to_cmd_d_on_mac() {
  KeyAction down = mapEvent(Ev::TalkDown, HostOs::Mac);
  CHECK(down.type == KeyAction::Press);
  CHECK(down.mods == hid::MOD_LGUI);
  CHECK(down.key == hid::KEY_D);

  KeyAction up = mapEvent(Ev::TalkUp, HostOs::Mac);
  CHECK(up.type == KeyAction::Release);

  KeyAction tap = mapEvent(Ev::TalkTap, HostOs::Mac);
  CHECK(tap.type == KeyAction::Tap);
  CHECK(tap.mods == hid::MOD_LGUI);
  CHECK(tap.key == hid::KEY_D);
}

static void test_talk_maps_to_ctrl_d_on_windows() {
  KeyAction down = mapEvent(Ev::TalkDown, HostOs::Windows);
  CHECK(down.type == KeyAction::Press);
  CHECK(down.mods == hid::MOD_LCTRL);
  CHECK(down.key == hid::KEY_D);
}

static void test_plain_keys() {
  KeyAction c = mapEvent(Ev::Confirm, HostOs::Mac);
  CHECK(c.type == KeyAction::Tap && c.mods == 0 && c.key == hid::KEY_ENTER);
  KeyAction x = mapEvent(Ev::Cancel, HostOs::Mac);
  CHECK(x.type == KeyAction::Tap && x.mods == 0 && x.key == hid::KEY_ESC);
  KeyAction u = mapEvent(Ev::Up, HostOs::Mac);
  CHECK(u.type == KeyAction::Tap && u.mods == 0 && u.key == hid::KEY_UP);
  KeyAction d = mapEvent(Ev::Down, HostOs::Mac);
  CHECK(d.type == KeyAction::Tap && d.mods == 0 && d.key == hid::KEY_DOWN);
}

// D21: 조이스틱 오른쪽·왼쪽 = 같은 앱의 다음 창·이전 창으로 바로 이동(맥의 ⌘` / ⇧⌘`)
static void test_window_switch() {
  KeyAction next = mapEvent(Ev::WindowNext, HostOs::Mac);
  CHECK(next.type == KeyAction::Tap && next.mods == hid::MOD_LGUI && next.key == hid::KEY_GRAVE);
  KeyAction prev = mapEvent(Ev::WindowPrev, HostOs::Mac);
  CHECK(prev.type == KeyAction::Tap && prev.mods == (hid::MOD_LGUI | hid::MOD_LSHIFT) &&
        prev.key == hid::KEY_GRAVE);
  // 윈도우에서는 아직 보내지 않는다.
  CHECK(mapEvent(Ev::WindowNext, HostOs::Windows).type == KeyAction::None);
  CHECK(mapEvent(Ev::WindowPrev, HostOs::Windows).type == KeyAction::None);
}

static void test_events_without_keys() {
  CHECK(mapEvent(Ev::None, HostOs::Mac).type == KeyAction::None);
  CHECK(mapEvent(Ev::PairReset, HostOs::Mac).type == KeyAction::None);
}

// ---- input_logic ----
//
// 아래 시험의 시간과 세기는 조정 값(inputcfg)에서 끌어 쓴다. 기기에서 값을 조정해도
// 시험을 다시 쓸 필요가 없고, 시험은 숫자가 아니라 "규칙"을 검사한다.
using namespace inputcfg;

static const uint32_t TICK = TICK_MS;  // Sim 이 흘려 보내는 틱 간격 = 펌웨어의 틱(config.h)
static const int8_t   PUSH = 127; // 끝까지 민 조이스틱

static const uint32_t DIR_SURE_MS  = DIR_DWELL_MS + 2 * TICK;  // 방향이 인정되기에 충분한 시간
static const uint32_t DIR_BRIEF_MS = DIR_DWELL_MS / 2;         // 방향이 인정되지 않는 짧은 시간

// 말하기 누름의 길이 세 가지(D18)
static const uint32_t TALK_TAP_MS  = TALK_TAP_MAX_MS - 3 * TICK;               // 톡 침
static const uint32_t TALK_MID_MS  = (TALK_TAP_MAX_MS + TALK_HOLD_MIN_MS) / 2;  // 어중간
static const uint32_t TALK_LONG_MS = TALK_HOLD_MIN_MS + 4 * TICK;              // 꾹 누름

// 시험이 기대는 전제. 조정 값이 이 범위를 벗어나면 여기서 컴파일이 멈춘다.
static_assert(DIR_OFF > 10 && DIR_ON - 5 >= DIR_OFF && DIR_ON + 5 <= PUSH, "방향 임계값의 범위");
static_assert(DIR_DWELL_MS >= 2 * TICK, "머무는 시간은 두 틱 이상");
static_assert(DIR_DWELL_MS >= TILT_LEAD_MS + 2 * JOY_POLL_MS, "머무는 시간이 누름 인식 지연을 덮어야 한다");
static_assert(JOY_POLL_MS % TICK_MS == 0, "조이스틱 읽기 주기는 틱의 배수");
static_assert(REPEAT_MS >= 8 * TICK, "반복 간격");
static_assert(CLICK_GUARD_MS >= 4 * TICK, "말하기 뒤 보호 시간");
static_assert(TALK_TAP_MAX_MS >= 4 * TICK, "톡 침 한도");
static_assert(TALK_HOLD_MIN_MS - TALK_TAP_MAX_MS >= 6 * TICK, "톡과 꾹 사이의 간격");
static_assert(CANCEL_COOLDOWN_MS >= 40 * TICK, "취소 간격");
static_assert(CHORD_MS >= 16 * TICK && LONG_MS >= 16 * TICK, "길게 누르는 시간");

static RawInput idle() {
  RawInput r{};
  r.joyOk = true;
  return r;
}

// 조이스틱을 (x, y) 로 민 입력
static RawInput stick(int x, int y) {
  RawInput r = idle();
  r.x = (int8_t)x;
  r.y = (int8_t)y;
  return r;
}

// 조이스틱을 누른 입력
static RawInput click() {
  RawInput r = idle();
  r.joyBtn = true;
  return r;
}

// 앞(a)·옆(b) 버튼을 누른 입력
static RawInput buttons(bool a, bool b) {
  RawInput r = idle();
  r.btnA = a;
  r.btnB = b;
  return r;
}

// 같은 입력을 틱마다 흘려 보내며 사건을 모으는 도우미
struct Sim {
  InputLogic lg;
  uint32_t t = 1000;
  std::vector<Ev> got;

  void run(const RawInput& in, uint32_t ms) {
    for (uint32_t e = 0; e < ms; e += TICK) {
      Ev out[InputLogic::MAX_EVENTS];
      int n = lg.update(in, t, out);
      for (int i = 0; i < n; i++) got.push_back(out[i]);
      t += TICK;
    }
  }
  int count(Ev e) const {
    int c = 0;
    for (Ev g : got) {
      if (g == e) ++c;
    }
    return c;
  }
  int total() const { return (int)got.size(); }
};

static void test_click_gives_talk_down_then_up() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_LONG_MS);
  CHECK(s.count(Ev::TalkDown) == 1);
  CHECK(s.count(Ev::TalkUp) == 0);
  CHECK(s.lg.talking());
  s.run(idle(), 50);
  CHECK(s.count(Ev::TalkUp) == 1);
  CHECK(!s.lg.talking());
  CHECK(s.total() == 2);
}

// 검토 초점 1
static void test_click_with_tilt_emits_no_direction() {
  Sim s;
  s.run(idle(), 50);
  RawInput p = stick(PUSH, 0);
  p.joyBtn = true;
  s.run(p, TALK_LONG_MS);                     // 누르면서 옆으로 기울어짐
  const RawInput tilted = stick(PUSH, 0);
  s.run(tilted, CLICK_GUARD_MS + 40 * TICK);  // 뗀 뒤에도 기울어 있음: 보호 시간이 지나도 중앙을 거치지 않았음
  CHECK(s.count(Ev::WindowNext) == 0);
  CHECK(s.count(Ev::Up) == 0 && s.count(Ev::Down) == 0);
  s.run(idle(), 50);                          // 중앙으로 돌아옴
  s.run(tilted, DIR_SURE_MS);                 // 이제는 인정
  CHECK(s.count(Ev::WindowNext) == 1);
}

// 말하기를 뗀 직후(보호 시간 안)에는 중앙을 거쳤더라도 방향을 받지 않는다.
// 떼는 손가락에 스틱이 튕기는 것을 거른다.
static void test_direction_ignored_right_after_talk() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_LONG_MS);
  s.run(idle(), 2 * TICK);             // 뗌(중앙)
  s.run(stick(0, PUSH), DIR_SURE_MS);  // 곧바로 튕김
  CHECK(s.count(Ev::Up) == 0);
  s.run(idle(), CLICK_GUARD_MS);       // 보호 시간이 지남
  s.run(stick(0, PUSH), DIR_SURE_MS);
  CHECK(s.count(Ev::Up) == 1);
}

static void test_tilted_at_boot_emits_nothing() {
  Sim s;
  s.run(stick(0, PUSH), DIR_DWELL_MS + 2 * REPEAT_MS);
  CHECK(s.total() == 0);
}

// 위·아래는 밀고 있으면 REPEAT_MS 마다 반복한다
static void test_up_repeats_at_interval() {
  Sim s;
  s.run(idle(), 50);
  const RawInput up = stick(0, PUSH);
  s.run(up, DIR_SURE_MS);           // 머문 뒤 첫 번째
  CHECK(s.count(Ev::Up) == 1);
  s.run(up, REPEAT_MS - 4 * TICK);  // 아직 반복 간격이 안 됨
  CHECK(s.count(Ev::Up) == 1);
  s.run(up, 4 * TICK);              // 간격을 지남 → 두 번째
  CHECK(s.count(Ev::Up) == 2);
  s.run(up, REPEAT_MS);             // 세 번째
  CHECK(s.count(Ev::Up) == 3);
  CHECK(s.count(Ev::Down) == 0);
}

static void test_short_push_down_once() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(0, -PUSH), DIR_SURE_MS);
  s.run(idle(), 100);
  CHECK(s.count(Ev::Down) == 1);
  CHECK(s.total() == 1);
}

// 검토 초점 3
static void test_side_does_not_repeat() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(PUSH, 0), DIR_DWELL_MS + 4 * REPEAT_MS);
  CHECK(s.count(Ev::WindowNext) == 1);                   // 밀고 있어도 한 번만
  s.run(stick(-PUSH, 0), DIR_DWELL_MS + 2 * REPEAT_MS);  // 중앙을 거치지 않고 반대쪽으로
  CHECK(s.count(Ev::WindowNext) == 1 && s.count(Ev::WindowPrev) == 0);
  s.run(idle(), 50);
  s.run(stick(-PUSH, 0), DIR_SURE_MS);                   // 왼쪽 = 이전 창
  CHECK(s.count(Ev::WindowPrev) == 1);
  CHECK(s.total() == 2);
}

// 검토 초점 3: 경계값(DIR_ON) 주변에서 떨려도 한 번만
static void test_hysteresis_no_chatter() {
  Sim s;
  s.run(idle(), 50);
  const uint32_t ticks = (DIR_DWELL_MS + REPEAT_MS / 2) / TICK;
  for (uint32_t i = 0; i < ticks; i++) {
    s.run(stick(0, (i % 2 == 0) ? DIR_ON + 5 : DIR_ON - 5), TICK);
  }
  CHECK(s.count(Ev::Up) == 1);
}

// 검토 초점 3
static void test_small_drift_ignored() {
  Sim s;  // 중앙 근처에서 흔들림
  s.run(stick(DIR_OFF - 10, -(DIR_OFF - 5)), 1000);
  CHECK(s.total() == 0);

  Sim s2;  // 임계값에 조금 못 미치게 밀고 있음
  s2.run(idle(), 50);
  s2.run(stick(DIR_ON - 1, -(DIR_ON - 1)), 1000);
  CHECK(s2.total() == 0);
}

// 검토 초점 2
static void test_joystick_failure_releases_talk() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_LONG_MS);
  CHECK(s.count(Ev::TalkDown) == 1);
  RawInput broken{};         // 읽기 실패: joyOk=false, 값은 쓰레기
  broken.joyOk = false;
  broken.x = -128;
  broken.y = -128;
  broken.joyBtn = true;
  s.run(broken, CLICK_GUARD_MS + 20 * TICK);
  CHECK(s.count(Ev::TalkUp) == 1);
  CHECK(s.total() == 2);     // 방향 사건이 생기지 않음
}

static void test_front_button_confirms_on_release() {
  Sim s;
  s.run(buttons(true, false), 100);
  CHECK(s.total() == 0);     // 누르는 동안에는 아무것도 없음 (D12)
  s.run(idle(), 20);
  CHECK(s.count(Ev::Confirm) == 1);
  CHECK(s.total() == 1);
}

static void test_side_button_cancels_on_release() {
  Sim s;
  s.run(buttons(false, true), 100);
  s.run(idle(), 20);
  CHECK(s.count(Ev::Cancel) == 1);
  CHECK(s.total() == 1);
}

// 검토 초점 4
static void test_chord_emits_no_keys() {
  Sim s;
  const RawInput both = buttons(true, true);
  s.run(both, CHORD_MS - 4 * TICK);
  CHECK(s.total() == 0);     // 아직 시간이 안 됨
  s.run(both, 14 * TICK);
  CHECK(s.count(Ev::PairReset) == 1);
  s.run(both, CHORD_MS + 10 * TICK);  // 계속 눌러도 한 번만
  CHECK(s.count(Ev::PairReset) == 1);
  s.run(idle(), 50);
  CHECK(s.count(Ev::Confirm) == 0);
  CHECK(s.count(Ev::Cancel) == 0);
  CHECK(s.total() == 1);
}

// 검토 초점 4
static void test_short_chord_emits_nothing() {
  Sim s;
  s.run(buttons(true, false), 100);          // 앞 버튼 먼저
  s.run(buttons(true, true), CHORD_MS / 4);  // 옆 버튼도 같이(짝 초기화 시간에는 못 미침)
  s.run(buttons(true, false), 50);           // 옆 버튼만 뗌
  s.run(idle(), 50);                         // 앞 버튼도 뗌
  CHECK(s.total() == 0);
}

static void test_long_press_talks_only_without_joystick() {
  RawInput noJoy{};          // joyOk=false
  RawInput noJoyA = noJoy;
  noJoyA.btnA = true;

  Sim s1;
  s1.run(noJoyA, LONG_MS + 10 * TICK);
  s1.run(noJoy, 20);
  CHECK(s1.count(Ev::TalkTap) == 1);
  CHECK(s1.count(Ev::Confirm) == 0);

  Sim s2;
  s2.run(noJoyA, LONG_MS / 4);
  s2.run(noJoy, 20);
  CHECK(s2.count(Ev::Confirm) == 1);
  CHECK(s2.count(Ev::TalkTap) == 0);

  Sim s3;                    // 조이스틱이 있으면 길게 눌러도 전송
  s3.run(buttons(true, false), LONG_MS + 10 * TICK);
  s3.run(idle(), 20);
  CHECK(s3.count(Ev::Confirm) == 1);
  CHECK(s3.count(Ev::TalkTap) == 0);
}

static void test_reset_clears_talk_and_pending_buttons() {
  Sim s;
  s.run(idle(), 50);
  RawInput p = click();
  p.btnA = true;
  s.run(p, 100);
  CHECK(s.lg.reset());       // 말하는 중이었음
  CHECK(!s.lg.talking());
  CHECK(!s.lg.reset());      // 이미 비어 있음
  s.run(buttons(true, false), 50);  // 앞 버튼은 계속 눌린 채
  s.run(idle(), 50);         // 뗌 → 초기화 전에 누른 것이므로 키 없음
  CHECK(s.count(Ev::Confirm) == 0);
}

// 버튼을 누른 채 연결이 끊겨 reset() 이 불리면, 그 버튼을 뗄 때 키가 나가지 않는다
static void test_reset_spoils_held_buttons() {
  Sim a;
  a.run(buttons(true, false), 100);
  a.lg.reset();
  a.run(buttons(true, false), 50);
  a.run(idle(), 50);
  CHECK(a.total() == 0);

  Sim b;
  b.run(buttons(false, true), 100);
  b.lg.reset();
  b.run(buttons(false, true), 50);
  b.run(idle(), 50);
  CHECK(b.total() == 0);
}

static void test_time_wraparound() {
  Sim s;
  s.t = 0xFFFFFFFFu - 200;   // millis() 가 한 바퀴 도는 구간
  s.run(idle(), 50);
  s.run(stick(0, PUSH), DIR_DWELL_MS + 2 * REPEAT_MS + 3 * TICK);
  CHECK(s.count(Ev::Up) == 3);
}

// 접점 떨림: 한 틱짜리 튐은 누름으로 치지 않는다
static void test_one_tick_glitch_is_ignored() {
  Sim s;
  s.run(buttons(true, false), TICK);  // 한 틱만 눌린 것처럼 보임(잡음)
  s.run(idle(), 100);
  s.run(buttons(false, true), TICK);
  s.run(idle(), 100);
  CHECK(s.total() == 0);
}

// 누르고 뗄 때 접점이 떨려도 전송은 한 번뿐이어야 한다(이중 전송 방지)
static void test_bounce_on_press_and_release_fires_once() {
  Sim s;
  const RawInput a = buttons(true, false);
  s.run(a, TICK);       // 눌림
  s.run(idle(), TICK);  // 튐
  s.run(a, 100);        // 눌림 유지
  s.run(idle(), TICK);  // 뗌
  s.run(a, TICK);       // 튐
  s.run(idle(), 50);    // 뗌 유지
  CHECK(s.count(Ev::Confirm) == 1);
  CHECK(s.total() == 1);
}

// 검토 초점 1 보강: 스틱이 먼저 기울고 곧바로(머무는 시간 안에) 눌리면 방향은 내지 않는다
static void test_tilt_then_click_within_dwell_emits_no_direction() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(PUSH, 0), DIR_BRIEF_MS);  // 누르려고 힘을 주는 순간 먼저 기울어짐
  RawInput p = stick(PUSH, 0);
  p.joyBtn = true;
  s.run(p, TALK_LONG_MS);
  CHECK(s.count(Ev::WindowNext) == 0);
  CHECK(s.count(Ev::TalkDown) == 1);
}

// 방향은 DIR_DWELL_MS 만큼 머문 뒤에 인정한다
static void test_direction_fires_after_dwell() {
  Sim s;
  s.run(idle(), 50);
  const RawInput up = stick(0, PUSH);
  s.run(up, DIR_DWELL_MS);
  CHECK(s.count(Ev::Up) == 0);  // 아직 머문 시간이 모자람(마지막 틱이 한 틱 모자란 시각)
  s.run(up, TICK);
  CHECK(s.count(Ev::Up) == 1);
}

// 잠깐 튄 값(한두 표본)은 방향으로 치지 않는다
static void test_direction_spike_is_ignored() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(PUSH, 0), DIR_BRIEF_MS);
  s.run(idle(), 200);
  CHECK(s.total() == 0);
}

// 머무는 동안 다른 쪽으로 옮겨 가면, 처음 방향은 버리고 새 방향을 처음부터 다시 잰다
// (중앙으로 돌아가야만 다시 받는 것이 아니다)
static void test_direction_change_during_dwell_restarts() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(0, PUSH), DIR_BRIEF_MS);                 // 위로 밀기 시작(아직 인정 전)
  s.run(stick(PUSH, 0), 2 * DIR_DWELL_MS + 5 * TICK);  // 중앙을 거치지 않고 옆으로 옮김
  CHECK(s.count(Ev::Up) == 0);
  CHECK(s.count(Ev::WindowNext) == 1);
}

// 두 축이 비슷하게 밀려 있으면(대각선) 어느 방향인지 분명하지 않다 → 방향으로 치지 않는다.
// (옆 방향은 곧바로 창이 넘어가므로 잘못 고르면 비싸다 — 2차 검토 M1)
static void test_diagonal_is_not_a_direction() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(100, 95), 1000);
  s.run(idle(), 50);
  s.run(stick(-PUSH, PUSH), 1000);
  CHECK(s.total() == 0);
  s.run(idle(), 50);
  s.run(stick(PUSH, 40), DIR_SURE_MS);  // 한쪽이 뚜렷하면(작은 쪽의 1.5배 이상) 받는다
  CHECK(s.count(Ev::WindowNext) == 1);
}

// 방향은 머무는 시간이 끝날 때의 모습으로 정한다. 처음에 살짝 옆으로 치우쳤더라도
// 끝까지 민 쪽이 위라면 "위"다.
static void test_direction_is_decided_when_dwell_ends() {
  Sim s;
  s.run(idle(), 50);
  s.run(stick(92, 60), 2 * TICK);   // 밀기 시작: 옆이 조금 더 큼
  s.run(stick(45, 120), 3 * DIR_DWELL_MS);  // 실제로는 위로 미는 중
  CHECK(s.count(Ev::WindowNext) == 0);
  CHECK(s.count(Ev::Up) >= 1);
}

// 말하는 중 연결이 끊겨 reset() 이 불려도 조이스틱을 계속 누르고 있으면 TalkDown 이 다시 난다.
// (끊긴 동안이라 전송은 버려진다. 다시 연결되면 모든 키를 떼므로 ⌘D 가 되살아나지 않는다.)
static void test_reset_while_still_pressed_refires_talk_down() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_LONG_MS);
  CHECK(s.lg.reset());
  s.run(click(), TALK_LONG_MS);  // 계속 누르고 있음
  CHECK(s.count(Ev::TalkDown) == 2);
  CHECK(s.count(Ev::TalkUp) == 0);
  s.run(idle(), 50);
  CHECK(s.count(Ev::TalkUp) == 1);
}

// 말하는 동안(조이스틱을 누른 채)에는 앞·옆 버튼을 무시한다.
// 그 키가 나가면 ⌘D 유지가 풀려 녹음이 중간에 끊긴다.
static void test_buttons_ignored_while_talking() {
  Sim s;
  s.run(idle(), 50);
  const RawInput talk = click();
  s.run(talk, 100);
  RawInput talkA = talk;
  talkA.btnA = true;
  s.run(talkA, 100);  // 말하는 중 앞 버튼
  s.run(talk, 50);
  RawInput talkB = talk;
  talkB.btnB = true;
  s.run(talkB, 100);  // 말하는 중 옆 버튼
  s.run(talk, 50);
  CHECK(s.count(Ev::Confirm) == 0);
  CHECK(s.count(Ev::Cancel) == 0);
  CHECK(s.total() == 1);  // TalkDown 뿐

  // 버튼을 누른 채 말하기를 시작해도, 그 버튼을 뗄 때 키가 나가지 않는다
  Sim s2;
  s2.run(buttons(true, false), 100);
  s2.run(talkA, 100);
  s2.run(talk, 100);  // 앞 버튼만 뗌
  CHECK(s2.count(Ev::Confirm) == 0);
}

// 짝 초기화는 한 번 쥐고 있는 동안 한 번만 (연결이 끊겨 reset() 이 불려도)
static void test_pair_reset_fires_once_across_reset() {
  Sim s;
  const RawInput both = buttons(true, true);
  s.run(both, CHORD_MS + 10 * TICK);
  CHECK(s.count(Ev::PairReset) == 1);
  s.lg.reset();                       // 초기화로 연결이 끊김
  s.run(both, CHORD_MS + 50 * TICK);  // 계속 쥐고 있음
  CHECK(s.count(Ev::PairReset) == 1);
  s.run(idle(), 50);
  CHECK(s.total() == 1);
}

// D18: 톡 침(TALK_TAP_MAX_MS 미만)은 바로 뗀다 → 컴퓨터가 "켜고 끄기"로 본다
static void test_quick_tap_releases_immediately() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_TAP_MS);
  s.run(idle(), TICK);
  CHECK(s.count(Ev::TalkDown) == 1);
  CHECK(s.count(Ev::TalkUp) == 1);
}

// 어중간한 길이로 눌렀다 뗀 뒤, 뗌이 "⌘D 가 눌린 지 TALK_HOLD_MIN_MS" 까지 늦춰지는지 본다.
// lead = 조이스틱을 누른 뒤 ⌘D 가 실제로 눌리기까지의 시간(초점 키를 쓰지 않으면 0)
static void expectStretchedRelease(Sim& s, uint32_t lead) {
  s.run(click(), lead + TALK_MID_MS);
  s.run(idle(), TALK_HOLD_MIN_MS - TALK_MID_MS - 2 * TICK);  // 아직 뗌을 내지 않음
  CHECK(s.count(Ev::TalkUp) == 0);
  CHECK(s.lg.talking());
  s.run(idle(), 4 * TICK);  // TALK_HOLD_MIN_MS 를 지남
  CHECK(s.count(Ev::TalkUp) == 1);
  CHECK(!s.lg.talking());
}

// D18: 어중간한 길이(TALK_TAP_MAX_MS 이상 TALK_HOLD_MIN_MS 미만)로 눌렀다 떼면 뗌을 늦춘다
// → 컴퓨터가 반드시 "누르는 동안"으로 보고 녹음을 끝낸다(켜진 채 방치되지 않음)
static void test_borderline_press_is_stretched_to_hold() {
  Sim s;
  s.run(idle(), 50);
  expectStretchedRelease(s, 0);
}

// 충분히 길게(TALK_HOLD_MIN_MS 이상) 눌렀다 떼면 바로 뗀다
static void test_long_hold_releases_immediately() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_LONG_MS);
  s.run(idle(), TICK);
  CHECK(s.count(Ev::TalkUp) == 1);
}

// D18 의 두 경계: 바로 아래와 바로 위가 서로 다르게 처리되는지
static void test_talk_boundaries() {
  struct Case {
    uint32_t pressMs;
    bool     stretched;
  };
  const Case cases[] = {
      {TALK_TAP_MAX_MS - TICK, false},  // 톡 침: 바로 뗌
      {TALK_TAP_MAX_MS, true},          // 여기부터 늦춤
      {TALK_HOLD_MIN_MS - TICK, true},
      {TALK_HOLD_MIN_MS, false},        // 여기부터 꾹 누름: 바로 뗌
  };
  for (const Case& c : cases) {
    Sim s;
    s.run(idle(), 50);
    s.run(click(), c.pressMs);
    s.run(idle(), TICK);  // 뗀 첫 틱
    CHECK(s.count(Ev::TalkUp) == (c.stretched ? 0 : 1));
    s.run(idle(), TALK_HOLD_MIN_MS);
    CHECK(s.count(Ev::TalkUp) == 1);  // 늦췄더라도 결국 한 번 낸다
  }
}

// 뗌이 미뤄진 사이에 다시 누르면: 뗀 적이 없는 것처럼 계속 누른 것으로 잇는다.
// (그 순간 미룬 뗌을 내 버리면 ⌘D 가 눌려 있던 시간이 톡과 꾹의 경계 근처가 되어
//  컴퓨터가 어느 쪽으로 볼지 알 수 없게 된다 — D18 이 막으려던 바로 그 상황)
static void test_press_during_stretched_release_continues_hold() {
  Sim s;
  s.run(idle(), 50);
  s.run(click(), TALK_MID_MS);
  s.run(idle(), 2 * TICK);
  CHECK(s.count(Ev::TalkUp) == 0);  // 뗌이 미뤄져 있음
  s.run(click(), TALK_LONG_MS);     // 다시 누름
  CHECK(s.count(Ev::TalkDown) == 1);
  CHECK(s.count(Ev::TalkUp) == 0);
  CHECK(s.lg.talking());
  s.run(idle(), 50);                // 이제 충분히 길게 눌렀으므로 바로 뗀다
  CHECK(s.count(Ev::TalkUp) == 1);
  CHECK(s.total() == 2);
}

// D10 + D18: 초점 키를 쓰면 ⌘D 가 실제로 눌리는 시각이 그만큼 늦어진다.
// 톡/꾹 구분은 "⌘D 가 눌려 있던 시간"으로 해야 컴퓨터의 판정과 맞는다.
static void test_talk_lead_shifts_tap_hold_boundary() {
  const uint32_t lead = 330;  // 예: 초점 키를 누르는 30ms + 초점이 옮겨지길 기다리는 300ms

  Sim tap;  // ⌘D 가 눌린 뒤 톡 침 길이만에 뗌 → 바로 뗀다
  tap.lg.setTalkLeadMs(lead);
  tap.run(idle(), 50);
  tap.run(click(), lead + TALK_TAP_MS);
  tap.run(idle(), TICK);
  CHECK(tap.count(Ev::TalkUp) == 1);

  Sim mid;  // ⌘D 가 눌린 뒤 어중간한 길이에 뗌 → ⌘D 기준으로 늦춘다
  mid.lg.setTalkLeadMs(lead);
  mid.run(idle(), 50);
  expectStretchedRelease(mid, lead);

  Sim early;  // 초점 키를 보내는 동안 이미 뗌 → 톡 침으로 본다
  early.lg.setTalkLeadMs(lead);
  early.run(idle(), 50);
  early.run(click(), lead / 3);
  early.run(idle(), TICK);
  CHECK(early.count(Ev::TalkUp) == 1);
}

// D17: 취소(Esc)는 CANCEL_COOLDOWN_MS 에 한 번만.
// Claude 는 Esc 두 번(0.8초 안)을 "되감기 창 열기"로 받는다
static void test_cancel_is_rate_limited() {
  Sim s;
  const RawInput b = buttons(false, true);
  s.run(b, 100);
  s.run(idle(), 50);
  CHECK(s.count(Ev::Cancel) == 1);
  s.run(idle(), CANCEL_COOLDOWN_MS - 35 * TICK);
  s.run(b, 100);  // 첫 취소 뒤 간격이 지나기 전: 무시
  s.run(idle(), 50);
  CHECK(s.count(Ev::Cancel) == 1);
  s.run(idle(), CANCEL_COOLDOWN_MS);
  s.run(b, 100);  // 간격이 지난 뒤: 인정
  s.run(idle(), 50);
  CHECK(s.count(Ev::Cancel) == 2);
}
// 켠 직후(시각이 CANCEL_COOLDOWN_MS 보다 작을 때)에도 첫 취소는 나간다
static void test_first_cancel_right_after_boot() {
  Sim s;
  s.t = 0;
  s.run(buttons(false, true), 100);
  s.run(idle(), 50);
  CHECK(s.count(Ev::Cancel) == 1);
}

// ---- 말하기 방식: 누를 때마다 켜고 끄기 (D19) ----

// 조이스틱을 누르면(얼마나 오래 누르든) ⌘D 를 한 번 톡 친 것으로 보낸다 → 컴퓨터가 녹음을 켠다.
// 다시 누르면 한 번 더 톡 → 끈다. 누른 채 버틸 필요가 없다.
static void test_toggle_click_emits_one_tap_regardless_of_length() {
  const uint32_t lengths[] = {5 * TICK, TALK_MID_MS, TALK_LONG_MS, 100 * TICK};
  for (uint32_t len : lengths) {
    Sim s;
    s.lg.setTalkMode(TalkMode::Toggle);
    s.run(idle(), 50);
    s.run(click(), len);
    CHECK(s.count(Ev::TalkTap) == 1);  // 누르는 순간 바로 한 번
    CHECK(s.lg.talking());             // 누르고 있는 동안(조이스틱 불빛용)
    s.run(idle(), TALK_HOLD_MIN_MS + 10 * TICK);
    CHECK(!s.lg.talking());
    CHECK(s.total() == 1);             // 뗄 때는 아무것도 보내지 않는다(TalkDown·TalkUp 없음)
  }
}

static void test_toggle_second_click_emits_second_tap() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  s.run(click(), 100);
  s.run(idle(), TALK_TAP_GAP_MS + 10 * TICK);
  s.run(click(), 100);
  s.run(idle(), 100);
  CHECK(s.count(Ev::TalkTap) == 2);
  CHECK(s.total() == 2);
}

// 접점이 튀거나 손이 떨려 곧바로 한 번 더 눌린 것은 무시한다(켜자마자 꺼지는 것을 막는다)
static void test_toggle_ignores_immediate_second_press() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  s.run(click(), 3 * TICK);
  s.run(idle(), 3 * TICK);
  s.run(click(), 3 * TICK);  // 첫 누름 뒤 TALK_TAP_GAP_MS 안
  s.run(idle(), 100);
  CHECK(s.count(Ev::TalkTap) == 1);
  s.run(idle(), TALK_TAP_GAP_MS);
  s.run(click(), 3 * TICK);  // 충분히 지난 뒤에는 받는다
  CHECK(s.count(Ev::TalkTap) == 2);
}

// 누르는 동안과 뗀 직후에는 방향·버튼을 받지 않는다(누르는 방식과 같은 보호)
static void test_toggle_suppresses_direction_and_buttons_while_pressed() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  RawInput p = stick(PUSH, 0);
  p.joyBtn = true;
  s.run(p, TALK_LONG_MS);                          // 누르면서 기울어짐
  RawInput pa = p;
  pa.btnA = true;
  s.run(pa, 100);                                  // 누른 채 앞 버튼
  s.run(p, 50);
  s.run(stick(PUSH, 0), CLICK_GUARD_MS + 40 * TICK);  // 뗀 뒤에도 기울어 있음
  CHECK(s.count(Ev::TalkTap) == 1);
  CHECK(s.total() == 1);
  s.run(idle(), 50);
  s.run(stick(PUSH, 0), DIR_SURE_MS);              // 중앙을 거친 뒤에는 방향을 받는다
  CHECK(s.count(Ev::WindowNext) == 1);
}

// 누르고 있는 중에 연결이 끊겼다 이어져도(reset) 다시 톡 치지 않는다 — 치면 녹음이 뒤집힌다
static void test_toggle_reset_while_pressed_does_not_refire() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  s.run(click(), 100);
  s.lg.reset();
  s.run(click(), 500);  // 계속 누르고 있음
  s.run(idle(), 100);
  CHECK(s.count(Ev::TalkTap) == 1);
  s.run(idle(), TALK_TAP_GAP_MS);
  s.run(click(), 100);  // 새로 누르면 받는다
  CHECK(s.count(Ev::TalkTap) == 2);
}

// 누른 채 연결이 끊겼다 이어져도(reset), 누르고 있는 동안의 보호(방향·버튼 무시)는 유지된다
static void test_toggle_reset_keeps_guard_while_pressed() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  s.run(click(), 100);
  s.lg.reset();
  RawInput tilted = stick(PUSH, 0);
  tilted.joyBtn = true;
  s.run(tilted, 3 * DIR_DWELL_MS);        // 누른 채 기울어짐
  RawInput withA = tilted;
  withA.btnA = true;
  s.run(withA, 100);                      // 누른 채 앞 버튼
  s.run(tilted, 50);
  s.run(stick(PUSH, 0), DIR_SURE_MS);     // 뗀 직후에도 기울어 있음
  CHECK(s.count(Ev::TalkTap) == 1);
  CHECK(s.total() == 1);
}

// 누르고 있는 중에 조이스틱 읽기가 잠깐 실패했다 돌아와도 한 번 더 톡 치지 않는다(치면 녹음이 뒤집힌다)
static void test_toggle_read_failure_does_not_refire() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  s.run(click(), 100);
  RawInput broken{};
  broken.joyBtn = true;  // joyOk=false
  s.run(broken, 300);
  s.run(click(), 300);   // 읽기가 돌아왔고, 그동안 계속 누르고 있었다
  s.run(idle(), 100);
  CHECK(s.count(Ev::TalkTap) == 1);
  s.run(idle(), TALK_TAP_GAP_MS);
  s.run(click(), 100);   // 뗐다가 새로 누르면 받는다
  CHECK(s.count(Ev::TalkTap) == 2);
}

// 조이스틱 읽기가 실패하면 누름이 풀린 것으로 본다(걸린 채 남지 않는다)
static void test_toggle_read_failure_releases() {
  Sim s;
  s.lg.setTalkMode(TalkMode::Toggle);
  s.run(idle(), 50);
  s.run(click(), 100);
  RawInput broken{};
  broken.joyBtn = true;  // joyOk=false
  s.run(broken, 100);
  CHECK(!s.lg.talking());
  CHECK(s.total() == 1);
}

// ---- joy_filter ----

static RawInput filled(const JoyFilter& f) {
  RawInput r{};
  f.fill(r);
  return r;
}

// 처음엔 "아직 모름": 값은 없지만 lost 도 아니다. 실패가 한도를 넘어야 lost (D31 — 첫 실패 한 번으로
// 5초 동안 조이스틱을 잃던 결함, 공개 전 검토)
static void test_joy_filter_starts_unknown_not_lost() {
  JoyFilter f;
  CHECK(!f.lost());
  CHECK(!filled(f).joyOk);
  for (int i = 0; i < JoyFilter::MAX_MISSES; i++) {
    f.feed(false, 0, 0, false);
    CHECK(!f.lost());
    CHECK(!filled(f).joyOk);
  }
  f.feed(false, 0, 0, false);  // 한도를 넘음
  CHECK(f.lost());
  f.feed(false, 0, 0, false);  // 계속 실패해도 lost 그대로(횟수가 넘치지 않는다)
  CHECK(f.lost());
  f.feed(true, 3, 4, false);   // 한 번 읽히면 바로 복귀
  CHECK(!f.lost() && filled(f).joyOk && filled(f).x == 3);
}

// 검토 초점 2 보강: 읽기가 잠깐 실패해도 직전 값으로 버텨 말하기가 끊기지 않는다
static void test_joy_filter_bridges_short_read_failures() {
  JoyFilter f;
  f.feed(true, 5, -3, true);
  f.feed(true, 5, -3, true);  // 누름은 두 번 연속 읽혀야 인정
  CHECK(filled(f).joyOk && filled(f).joyBtn && filled(f).x == 5 && filled(f).y == -3);
  for (int i = 0; i < JoyFilter::MAX_MISSES; i++) {
    f.feed(false, 0, 0, false);
    CHECK(!f.lost());
    CHECK(filled(f).joyOk && filled(f).joyBtn && filled(f).x == 5);
  }
  f.feed(false, 0, 0, false);  // 한도를 넘음 → 없음으로 판정, 누름도 풀림
  CHECK(f.lost());
  CHECK(!filled(f).joyOk && !filled(f).joyBtn && filled(f).x == 0);
}

static void test_joy_filter_recovers_after_good_read() {
  JoyFilter f;
  f.feed(true, 1, 1, false);
  for (int i = 0; i <= JoyFilter::MAX_MISSES; i++) f.feed(false, 0, 0, false);
  CHECK(f.lost());
  f.feed(true, 7, 8, false);
  CHECK(!f.lost());
  CHECK(filled(f).joyOk && filled(f).x == 7 && filled(f).y == 8);
}

// 한 번만 튄 누름은 무시하고, 뗌도 두 번 연속 읽혀야 인정한다
static void test_joy_filter_debounces_click() {
  JoyFilter f;
  f.feed(true, 0, 0, false);
  f.feed(true, 0, 0, true);  // 한 번 튐
  CHECK(!filled(f).joyBtn);
  f.feed(true, 0, 0, false);
  CHECK(!filled(f).joyBtn);
  f.feed(true, 0, 0, true);
  f.feed(true, 0, 0, true);
  CHECK(filled(f).joyBtn);
  f.feed(true, 0, 0, false);  // 뗌이 한 번만 읽힘
  CHECK(filled(f).joyBtn);
  f.feed(true, 0, 0, false);
  CHECK(!filled(f).joyBtn);
}

// 조이스틱 방향 보정(축 바꿈, y 뒤집기)은 JoyFilter 가 맡는다
static void test_joy_filter_orientation() {
  JoyFilter inv(false, true);  // y 뒤집기
  inv.feed(true, 10, 20, false);
  CHECK(filled(inv).x == 10 && filled(inv).y == -20);
  inv.feed(true, 0, -128, false);
  CHECK(filled(inv).y == 127);  // -128 을 뒤집어도 넘치지 않는다

  JoyFilter sw(true, false);  // 축 바꿈
  sw.feed(true, 10, 20, false);
  CHECK(filled(sw).x == 20 && filled(sw).y == 10);

  JoyFilter both(true, true);  // 바꾼 뒤 뒤집기
  both.feed(true, 10, 20, false);
  CHECK(filled(both).x == 20 && filled(both).y == -10);
  JoyFilter invx(false, false, true);  // x 뒤집기(왼쪽·오른쪽이 반대로 나올 때)
  invx.feed(true, 10, 20, false);
  CHECK(filled(invx).x == -10 && filled(invx).y == 20);
  invx.feed(true, -128, 0, false);
  CHECK(filled(invx).x == 127);

  both.reset();  // reset 은 방향 설정을 지우지 않는다
  both.feed(true, 10, 20, false);
  CHECK(filled(both).x == 20 && filled(both).y == -10);
}

static void test_joy_filter_reset_forgets_everything() {
  JoyFilter f;
  f.feed(true, 9, 9, true);
  f.feed(true, 9, 9, true);
  f.reset();
  CHECK(!f.lost());  // reset 은 "아직 모름"으로 — 다시 찾은 직후 첫 실패로 잃지 않게
  CHECK(!filled(f).joyOk && !filled(f).joyBtn);
}

// ---- 조이스틱 판독 + 입력 판독: 기기에서의 실제 흐름 ----
//
// 기기의 loop 는 TICK_MS 마다 사건을 처리하고, JOY_POLL_MS 마다 조이스틱을 읽어 JoyFilter 에 넣는다.
// 기울임(x·y)은 읽는 즉시 반영되지만, 누름은 두 번 연속 읽혀야 인정되므로 최대 2 × JOY_POLL_MS 늦다.
// 아래 도우미는 그 흐름을 그대로 흉내 낸다. 조이스틱의 물리 상태는 ms 시각의 함수로 준다:
// 300ms 까지 중앙, 300ms 부터 위로 밀고, pressAtMs 부터 누른다(pressAtMs = 0 이면 누르지 않는다).
struct DeviceRun {
  bool     direction = false;  // 방향 사건이 하나라도 나왔는가
  bool     talk      = false;  // 말하기가 시작됐는가
  uint32_t firstDirMs = 0;     // 첫 방향 사건의 시각
};

static DeviceRun runDevice(uint32_t pressAtMs, uint32_t tickPhase, uint32_t pollPhase, uint32_t endMs) {
  const uint32_t pollEvery = JOY_POLL_MS / TICK_MS;  // 조이스틱은 몇 틱마다 읽는가
  InputLogic lg;
  JoyFilter  joy;
  DeviceRun  r;
  uint32_t   k = 0;
  for (uint32_t ms = tickPhase; ms < endMs; ms += TICK_MS, k++) {
    if (k % pollEvery == pollPhase) {
      joy.feed(true, 0, (ms >= 300) ? PUSH : 0, pressAtMs != 0 && ms >= pressAtMs);
    }
    RawInput in{};
    joy.fill(in);
    Ev out[InputLogic::MAX_EVENTS];
    const int n = lg.update(in, 1000 + ms, out);
    for (int i = 0; i < n; i++) {
      if (out[i] == Ev::TalkDown) r.talk = true;
      if (out[i] == Ev::Up || out[i] == Ev::Down || out[i] == Ev::WindowNext ||
          out[i] == Ev::WindowPrev) {
        if (!r.direction) r.firstDirMs = ms;
        r.direction = true;
      }
    }
  }
  return r;
}

// 검토 초점 1 (통합): 누르려고 힘을 줄 때 스틱이 먼저 기울어진다. 기울임이 누름보다
// TILT_LEAD_MS 이내로 앞서면, 누름 인식이 늦는 것을 감안해도 방향 사건이 나가면 안 된다.
// (방향이 먼저 나가면 ⌘D 앞에 ↑ 나 창 목록 키가 끼어든다.)
static void test_tilt_before_click_through_joy_filter() {
  int cases = 0, leaked = 0, talked = 0;
  for (uint32_t lead = 0; lead <= TILT_LEAD_MS; lead += 5) {
    for (uint32_t tickPhase = 0; tickPhase < TICK_MS; tickPhase++) {
      for (uint32_t pollPhase = 0; pollPhase < JOY_POLL_MS / TICK_MS; pollPhase++) {
        const DeviceRun r = runDevice(300 + lead, tickPhase, pollPhase, 300 + lead + 400);
        ++cases;
        leaked += r.direction;
        talked += r.talk;
      }
    }
  }
  CHECK(leaked == 0);      // 방향 사건이 한 번도 새지 않았다
  CHECK(talked == cases);  // 말하기는 매번 시작됐다
}

// 같은 흐름에서 누르지 않고 밀기만 하면 방향이 나온다: 머무는 시간 + 읽기 주기 + 한 틱 안에
static void test_direction_through_joy_filter() {
  int cases = 0, fired = 0, late = 0;
  for (uint32_t tickPhase = 0; tickPhase < TICK_MS; tickPhase++) {
    for (uint32_t pollPhase = 0; pollPhase < JOY_POLL_MS / TICK_MS; pollPhase++) {
      const DeviceRun r = runDevice(0, tickPhase, pollPhase, 300 + DIR_DWELL_MS + 200);
      ++cases;
      fired += r.direction;
      late += r.direction && r.firstDirMs > 300 + DIR_DWELL_MS + JOY_POLL_MS + TICK_MS;
    }
  }
  CHECK(fired == cases);
  CHECK(late == 0);
}

// ---- link_tracker ----

static const uint32_t SETTLE = 300, CONNECT_LIMIT = 30000;

// 처음 짝을 맺을 때의 순서: 연결됨 → (짝 맺기) → 암호화 확인 → 정착 시간 뒤 준비됨
static void test_link_first_pairing_order() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  CHECK(lt.state(0, false) == LinkState::Pairing);
  CHECK(lt.state(0, true) == LinkState::Searching);
  lt.onConnected(1000, false);
  CHECK(lt.state(1000, false) == LinkState::Connecting);
  CHECK(lt.state(2900, false) == LinkState::Connecting);  // 암호화 전에는 시간이 지나도 준비 아님
  lt.onSecured(3000);
  CHECK(lt.state(3000, true) == LinkState::Connecting);
  CHECK(lt.state(3000 + SETTLE - 1, true) == LinkState::Connecting);
  CHECK(lt.state(3000 + SETTLE, true) == LinkState::Ready);
}

// 다시 연결될 때의 순서(맥에서 실측): 암호화 확인이 "연결됨"보다 먼저 온다.
// 이 순서에서도 준비됨에 이르러야 한다 — 처음 구현은 "연결 중"에 머물렀다.
static void test_link_reconnect_secured_before_connected() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onSecured(993);
  CHECK(lt.state(1000, true) == LinkState::Searching);  // 아직 "연결됨"이 오지 않았다
  lt.onConnected(1113, true);
  CHECK(lt.state(1120, true) == LinkState::Connecting);
  CHECK(lt.state(993 + SETTLE, true) == LinkState::Ready);  // 정착 시간은 암호화 확인부터 잰다
  CHECK(!lt.connectTimedOut(1113 + CONNECT_LIMIT));         // 암호화된 연결은 끊지 않는다
}

// "연결됨"이 왔을 때 이미 암호화돼 있으면, 따로 확인 사건이 없어도 준비됨에 이른다
static void test_link_connected_already_secured() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onConnected(500, true);
  CHECK(lt.state(500, true) == LinkState::Connecting);
  CHECK(lt.state(500 + SETTLE, true) == LinkState::Ready);
}

// 앞선 연결에서 남은 암호화 표시를 새 연결에 물려주지 않는다
static void test_link_stale_security_is_not_inherited() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onSecured(100);            // 암호화 확인만 오고 "연결됨"은 끝내 오지 않았다
  lt.onConnected(5000, false);  // 다른 연결: 암호화되지 않음
  CHECK(lt.state(5000 + SETTLE, true) == LinkState::Connecting);
  CHECK(!lt.secured());

  LinkTracker lt2(SETTLE, CONNECT_LIMIT);
  lt2.onConnected(0, true);
  lt2.onDisconnected();         // 끊기면 표시도 지운다
  CHECK(lt2.state(1000, true) == LinkState::Searching);
  lt2.onConnected(2000, false);
  CHECK(lt2.state(2000 + SETTLE, true) == LinkState::Connecting);
}

// 상태를 묻는 시각이 암호화 확인 시각보다 조금 앞서도(다른 태스크가 방금 기록) 정착 시간을
// 건너뛰지 않는다
static void test_link_settle_not_skipped_by_stale_time() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onConnected(1000, false);
  lt.onSecured(1005);
  CHECK(lt.state(1003, true) == LinkState::Connecting);
  CHECK(lt.state(1005 + SETTLE, true) == LinkState::Ready);
}

// 이미 준비된 연결에서 암호화 사건이 또 와도 "연결 중"으로 내려가지 않는다
static void test_link_resecure_keeps_ready() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onConnected(0, false);
  lt.onSecured(100);
  CHECK(lt.state(100 + SETTLE, true) == LinkState::Ready);
  lt.onSecured(60000);
  CHECK(lt.state(60000, true) == LinkState::Ready);
}

// 한 번 준비되면 시각이 한 바퀴 돌아도(약 49.7일) 준비 상태로 남는다
static void test_link_ready_survives_clock_wrap() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onConnected(1000, true);
  CHECK(lt.state(1000 + SETTLE, true) == LinkState::Ready);
  CHECK(lt.state(1000 + 100, true) == LinkState::Ready);  // 한 바퀴 돈 뒤의 시각(연결 시각 직후처럼 보임)
  CHECK(lt.state(900, true) == LinkState::Ready);
}

// 암호화가 끝나지 않는 연결은 시간 제한에 한 번씩만 걸린다
static void test_link_connect_timeout() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  CHECK(!lt.connectTimedOut(CONNECT_LIMIT * 2));  // 연결이 없으면 걸리지 않는다
  lt.onConnected(1000, false);
  CHECK(!lt.connectTimedOut(900));                 // 연결 시각보다 앞선(낡은) 시각이 와도 걸리지 않는다
  CHECK(!lt.connectTimedOut(1000 + CONNECT_LIMIT - 1));
  CHECK(lt.connectTimedOut(1000 + CONNECT_LIMIT));
  CHECK(!lt.connectTimedOut(1000 + CONNECT_LIMIT + 10));  // 끊는 중에 다시 걸리지 않는다
  CHECK(lt.connectTimedOut(1000 + 2 * CONNECT_LIMIT));
  lt.onSecured(70000);
  CHECK(!lt.connectTimedOut(70000 + CONNECT_LIMIT));
}

// 결합을 지우면 지금 연결도 준비 상태가 아니다
static void test_link_drop_security() {
  LinkTracker lt(SETTLE, CONNECT_LIMIT);
  lt.onConnected(0, true);
  CHECK(lt.state(SETTLE, true) == LinkState::Ready);
  lt.dropSecurity();
  CHECK(lt.state(SETTLE + 10, false) == LinkState::Connecting);
}

// ---- dispatch ----

// 블루투스 대신 쓰는 가짜: 불린 순서를 기록한다
struct FakePort : KeyPort {
  bool up = true;  // 연결됨 여부
  std::vector<std::string> log;

  bool send(uint8_t mods, uint8_t key) override {
    char b[32];
    std::snprintf(b, sizeof(b), "send %02X %02X", mods, key);
    log.push_back(b);
    return up;
  }
  bool releaseAll() override {
    log.push_back("release");
    return up;
  }
  void hold(uint32_t ms) override { log.push_back("hold " + std::to_string(ms)); }
};

// 결합(짝) 정보를 지우는 쪽의 가짜
struct FakePeers : PeerControl {
  bool ok = true;
  int  calls = 0;
  bool forgetPeers() override {
    ++calls;
    return ok;
  }
};

static const KeyTiming TIMING = {30, 12};  // 누르고 있기 30ms, 다음 보고까지 12ms
static const HostProfile MAC = {HostOs::Mac, 0, 0};
static const HostProfile WIN = {HostOs::Windows, 0, 0};
static const HostProfile MAC_FOCUS = {HostOs::Mac, 0x6E, 300};  // F19 로 초점 이동 뒤 300ms

static std::string sentAs(Ev e) { return "sent " + std::to_string((int)e); }

struct FakeScreen : ActionScreen {
  // 화면이 들은 것을 순서대로 적는다
  std::vector<std::string> shown;
  void sent(Ev e) override { shown.push_back("sent " + std::to_string((int)e)); }
  void notSent() override { shown.push_back("not sent"); }
  void pairReset(bool ok) override { shown.push_back(ok ? "reset ok" : "reset failed"); }
};

static void test_dispatch_tap_presses_holds_releases() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::Confirm, MAC, TIMING, port, peers, scr);
  CHECK(port.log.size() == 4);
  CHECK(port.log[0] == "send 00 28");
  CHECK(port.log[1] == "hold 30");
  CHECK(port.log[2] == "release");
  CHECK(port.log[3] == "hold 12");  // 다음 보고와 붙지 않게 띄운다
  CHECK(scr.shown.size() == 1 && scr.shown[0] == sentAs(Ev::Confirm));
}

static void test_dispatch_talk_press_then_release() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::TalkDown, MAC, TIMING, port, peers, scr);
  CHECK(port.log.size() == 1 && port.log[0] == "send 08 07");  // ⌘D 누른 채 유지
  CHECK(scr.shown.size() == 1 && scr.shown[0] == sentAs(Ev::TalkDown));
  dispatchEvent(Ev::TalkUp, MAC, TIMING, port, peers, scr);
  CHECK(port.log.size() == 2 && port.log[1] == "release");
  // 뗌도 화면에 알린다: 누르는 동안 방식에서 TALK 화면을 내릴 때를 알아야 한다
  CHECK(scr.shown.size() == 2 && scr.shown[1] == sentAs(Ev::TalkUp));
}

// 검토 초점 5: 연결이 없으면 버리고 알려준다. 쌓아 두지 않는다.
static void test_dispatch_when_disconnected_drops_and_tells() {
  FakePort port;
  port.up = false;
  FakeScreen scr;
  FakePeers peers;
  for (int i = 0; i < 3; i++) dispatchEvent(Ev::Confirm, MAC, TIMING, port, peers, scr);
  CHECK(port.log.size() == 3);  // 시도 3번뿐: 누르고 있기·떼기가 뒤따르지 않음
  for (const std::string& l : port.log) CHECK(l == "send 00 28");
  CHECK(scr.shown.size() == 3);
  for (const std::string& t : scr.shown) CHECK(t == "not sent");
  // 다시 연결된 뒤 다른 사건을 처리해도 밀린 키가 나오지 않는다
  port.up = true;
  dispatchEvent(Ev::Up, MAC, TIMING, port, peers, scr);
  CHECK(port.log.size() == 7);
  CHECK(port.log[3] == "send 00 52");
}

static void test_dispatch_release_when_disconnected_is_silent() {
  FakePort port;
  port.up = false;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::TalkUp, MAC, TIMING, port, peers, scr);
  CHECK(port.log.size() == 1 && port.log[0] == "release");
  // "보내지 못함"을 띄우지 않는다. 뗌 자체는 화면에 알린다(TALK 화면을 내리도록).
  CHECK(scr.shown.size() == 1 && scr.shown[0] == sentAs(Ev::TalkUp));
}

static void test_dispatch_pair_reset_forgets_without_keys() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::PairReset, MAC, TIMING, port, peers, scr);
  CHECK(peers.calls == 1);
  CHECK(port.log.empty());  // 키는 보내지 않는다
  CHECK(scr.shown.size() == 1 && scr.shown[0] == "reset ok");
}

// 짝 초기화가 실패하면(결합이 지워지지 않으면) 성공한 것처럼 보여주지 않는다
static void test_dispatch_pair_reset_failure_is_shown() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  peers.ok = false;
  dispatchEvent(Ev::PairReset, MAC, TIMING, port, peers, scr);
  CHECK(peers.calls == 1);
  CHECK(scr.shown.size() == 1 && scr.shown[0] == "reset failed");
}

static void test_dispatch_events_without_keys_do_nothing() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::WindowNext, WIN, TIMING, port, peers, scr);  // 윈도우에는 창 이동 키가 아직 없음
  dispatchEvent(Ev::None, MAC, TIMING, port, peers, scr);
  CHECK(port.log.empty());
  CHECK(scr.shown.empty());
}

// D10: 초점 키가 설정돼 있으면 말하기 앞에 먼저 보낸다(입력창으로 초점을 옮김)
static void test_dispatch_focus_key_before_talk() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::TalkDown, MAC_FOCUS, TIMING, port, peers, scr);
  CHECK(port.log.size() == 5);
  CHECK(port.log[0] == "send 00 6E");  // F19
  CHECK(port.log[1] == "hold 30");
  CHECK(port.log[2] == "release");
  CHECK(port.log[3] == "hold 300");  // 초점이 옮겨질 시간
  CHECK(port.log[4] == "send 08 07");  // ⌘D
  CHECK(scr.shown.size() == 1 && scr.shown[0] == sentAs(Ev::TalkDown));
}

// 초점 키는 말하기에만 붙는다. Enter 앞에 붙이면 승인창에서 사용자가 고른 선택지를 덮어쓴다
static void test_dispatch_focus_key_not_before_other_keys() {
  FakePort port;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::Confirm, MAC_FOCUS, TIMING, port, peers, scr);
  dispatchEvent(Ev::Cancel, MAC_FOCUS, TIMING, port, peers, scr);
  dispatchEvent(Ev::Up, MAC_FOCUS, TIMING, port, peers, scr);
  dispatchEvent(Ev::WindowNext, MAC_FOCUS, TIMING, port, peers, scr);
  for (const std::string& l : port.log) CHECK(l != "send 00 6E");
}

static void test_dispatch_focus_key_when_disconnected() {
  FakePort port;
  port.up = false;
  FakeScreen scr;
  FakePeers peers;
  dispatchEvent(Ev::TalkDown, MAC_FOCUS, TIMING, port, peers, scr);
  CHECK(port.log.size() == 1);  // 초점 키 시도 한 번으로 끝
  CHECK(scr.shown.size() == 1 && scr.shown[0] == "not sent");
}

// 검토 초점 2: 연결 상태가 바뀌면 눌린 채 남는 키가 없어야 한다
static void test_link_lost_resets_talk() {
  Sim s;
  s.run(idle(), 50);
  RawInput p = idle();
  p.joyBtn = true;
  s.run(p, 100);
  CHECK(s.lg.talking());
  FakePort port;
  onLinkChange(false, s.lg, port);
  CHECK(!s.lg.talking());
  CHECK(port.log.empty());  // 끊긴 상태에서는 보낼 곳이 없다
}

static void test_link_ready_releases_all_keys() {
  InputLogic lg;
  FakePort port;
  onLinkChange(true, lg, port);
  CHECK(port.log.size() == 1 && port.log[0] == "release");
}

int main() {
  RUN(test_hid_constants_match_usb_usage_table);
  RUN(test_talk_maps_to_cmd_d_on_mac);
  RUN(test_talk_maps_to_ctrl_d_on_windows);
  RUN(test_plain_keys);
  RUN(test_window_switch);
  RUN(test_events_without_keys);

  RUN(test_click_gives_talk_down_then_up);
  RUN(test_click_with_tilt_emits_no_direction);
  RUN(test_direction_ignored_right_after_talk);
  RUN(test_tilted_at_boot_emits_nothing);
  RUN(test_up_repeats_at_interval);
  RUN(test_short_push_down_once);
  RUN(test_side_does_not_repeat);
  RUN(test_hysteresis_no_chatter);
  RUN(test_small_drift_ignored);
  RUN(test_joystick_failure_releases_talk);
  RUN(test_front_button_confirms_on_release);
  RUN(test_side_button_cancels_on_release);
  RUN(test_chord_emits_no_keys);
  RUN(test_short_chord_emits_nothing);
  RUN(test_long_press_talks_only_without_joystick);
  RUN(test_reset_clears_talk_and_pending_buttons);
  RUN(test_reset_spoils_held_buttons);
  RUN(test_time_wraparound);
  RUN(test_one_tick_glitch_is_ignored);
  RUN(test_bounce_on_press_and_release_fires_once);
  RUN(test_tilt_then_click_within_dwell_emits_no_direction);
  RUN(test_direction_fires_after_dwell);
  RUN(test_direction_spike_is_ignored);
  RUN(test_direction_change_during_dwell_restarts);
  RUN(test_diagonal_is_not_a_direction);
  RUN(test_direction_is_decided_when_dwell_ends);
  RUN(test_reset_while_still_pressed_refires_talk_down);
  RUN(test_buttons_ignored_while_talking);
  RUN(test_pair_reset_fires_once_across_reset);
  RUN(test_quick_tap_releases_immediately);
  RUN(test_borderline_press_is_stretched_to_hold);
  RUN(test_long_hold_releases_immediately);
  RUN(test_talk_boundaries);
  RUN(test_press_during_stretched_release_continues_hold);
  RUN(test_talk_lead_shifts_tap_hold_boundary);
  RUN(test_cancel_is_rate_limited);
  RUN(test_first_cancel_right_after_boot);

  RUN(test_toggle_click_emits_one_tap_regardless_of_length);
  RUN(test_toggle_second_click_emits_second_tap);
  RUN(test_toggle_ignores_immediate_second_press);
  RUN(test_toggle_suppresses_direction_and_buttons_while_pressed);
  RUN(test_toggle_reset_while_pressed_does_not_refire);
  RUN(test_toggle_reset_keeps_guard_while_pressed);
  RUN(test_toggle_read_failure_does_not_refire);
  RUN(test_toggle_read_failure_releases);

  RUN(test_joy_filter_starts_unknown_not_lost);
  RUN(test_joy_filter_bridges_short_read_failures);
  RUN(test_joy_filter_recovers_after_good_read);
  RUN(test_joy_filter_debounces_click);
  RUN(test_joy_filter_orientation);
  RUN(test_joy_filter_reset_forgets_everything);

  RUN(test_tilt_before_click_through_joy_filter);
  RUN(test_direction_through_joy_filter);

  RUN(test_link_first_pairing_order);
  RUN(test_link_reconnect_secured_before_connected);
  RUN(test_link_connected_already_secured);
  RUN(test_link_stale_security_is_not_inherited);
  RUN(test_link_settle_not_skipped_by_stale_time);
  RUN(test_link_resecure_keeps_ready);
  RUN(test_link_ready_survives_clock_wrap);
  RUN(test_link_connect_timeout);
  RUN(test_link_drop_security);

  RUN(test_dispatch_tap_presses_holds_releases);
  RUN(test_dispatch_talk_press_then_release);
  RUN(test_dispatch_when_disconnected_drops_and_tells);
  RUN(test_dispatch_release_when_disconnected_is_silent);
  RUN(test_dispatch_pair_reset_forgets_without_keys);
  RUN(test_dispatch_pair_reset_failure_is_shown);
  RUN(test_dispatch_events_without_keys_do_nothing);
  RUN(test_dispatch_focus_key_before_talk);
  RUN(test_dispatch_focus_key_not_before_other_keys);
  RUN(test_dispatch_focus_key_when_disconnected);
  RUN(test_link_lost_resets_talk);
  RUN(test_link_ready_releases_all_keys);
  return finish();
}
