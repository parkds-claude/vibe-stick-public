// CoreS3 판의 순수 로직 시험: 버튼 배치, 터치 → 사건, 화면 상태, 그리기, 로그 포트의 줄 읽기,
// 개발판의 안전장치 판정.
// 실행: cores3/test/host/run.sh
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "art_data.h"
#include "audio_ring.h"
#include "hist_ring.h"
#include "host_pulse.h"
#include "pane_switch.h"
#include "quiet_gate.h"
#include "speech_ear.h"
#include "key_route.h"
#include "motion.h"
#include "pane_switch.h"
#include "pixmap.h"
#include "screen_power.h"
#include "sound_seq.h"
#include "dev_rescue.h"
#include "line_reader.h"
#include "calendar.h"
#include "log_budget.h"
#include "touch_draw.h"
#include "touch_layout.h"
#include "touch_state.h"
#include "touch_map.h"
#include "update_gate.h"
#include "view_tap_guard.h"
#include "check.h"
#include "logic/input_logic.h"

using namespace touchui;

// cores3/main/config.h 의 GUESS_VOICE_MIN · GUESS_VOICE_RATIO · GUESS_VOICE_RUN 과 같아야 한다
// (config.h 는 기기의 헤더를 들여와 여기서 읽지 못한다 — test_tools.py 가 두 파일의 숫자를 견준다).
static const uint16_t GUESS_MIN = 600;
static const uint8_t  GUESS_RATIO = 3;
static const uint8_t  GUESS_RUN = 4;

// ---------- 도우미 ----------

// 화면에 놓인 조각을 기록한다. 놓이지 않은 점은 NONE 으로 남는다.
struct Buf : Screen {
  static constexpr uint32_t NONE = 0xFFFFFFFF;
  std::vector<uint32_t> px;
  bool outside = false;   // 화면 밖에 놓으려 했거나, 조각이 잘못됐다
  Rect last{0, 0, 0, 0};  // 마지막으로 놓인 조각의 자리
  int  pushes = 0;
  Buf() : px(W * H, (uint32_t)NONE) {}
  void push(int x, int y, int w, int h, const uint16_t* p, int stride) override {
    if (!p || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > W || y + h > H || stride < w) {
      outside = true;
      return;
    }
    last = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    ++pushes;
    for (int r = 0; r < h; r++) {
      for (int c = 0; c < w; c++) px[(y + r) * W + x + c] = p[r * stride + c];
    }
  }
  // r 밖에 놓인 점이 있는가
  bool paintedOutside(const Rect& r) const {
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        if (px[y * W + x] != NONE && !r.contains(x, y)) return true;
      }
    }
    return false;
  }
  // r 안이 빠짐없이 놓였는가
  bool covers(const Rect& r) const {
    for (int y = r.y; y < r.y + r.h; y++) {
      for (int x = r.x; x < r.x + r.w; x++) {
        if (px[y * W + x] == NONE) return false;
      }
    }
    return true;
  }
};

static bool sameRect(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
static bool inside(const Rect& in, const Rect& out) {
  return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

static Rect center(const Rect& r) { return {(int16_t)(r.x + r.w / 2), (int16_t)(r.y + r.h / 2), 1, 1}; }

// 기기의 loop 를 흉내 낸다: 10ms 마다 터치를 읽어 TouchMapper → InputLogic 으로 넘긴다.
struct Rig {
  TouchMapper touch;
  InputLogic  logic;
  uint32_t    now = 1000;
  std::vector<Ev> evs;
  Rig() { logic.setTalkMode(TalkMode::Toggle); }

  void tick(const TouchPoint* pts, int n) {
    touch.update(pts, n, now);
    RawInput in{};
    touch.fill(in);
    Ev out[InputLogic::MAX_EVENTS];
    const int k = logic.update(in, now, out);
    for (int i = 0; i < k; i++) evs.push_back(out[i]);
    now += 10;
  }
  void idle(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) tick(nullptr, 0);
  }
  // loop 가 ms 동안 막힌다(키를 보내거나 화면을 그리느라 터치를 읽지 못한다).
  void stall(uint32_t ms) { now += ms; }
  // (x, y) 를 ms 동안 누른다.
  void hold(int x, int y, uint32_t ms, uint8_t id = 0) {
    const TouchPoint p{id, (int16_t)x, (int16_t)y};
    for (uint32_t t = 0; t < ms; t += 10) tick(&p, 1);
  }
  void holdZone(Zone z, uint32_t ms) {
    const Rect c = center(zoneRect(z));
    hold(c.x, c.y, ms);
  }
  // 자리 하나를 ms 동안 눌렀다 떼고, 판정이 끝나도록 잠깐 쉰다.
  void tap(Zone z, uint32_t ms = 80) {
    holdZone(z, ms);
    idle(300);
  }
  int count(Ev e) const {
    int n = 0;
    for (Ev v : evs) n += (v == e);
    return n;
  }
};

// ---------- 배치 ----------

static void test_layout_fits_screen() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    const Rect& r = ZONE_RECTS[i];
    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= W && r.y + r.h <= H);
    if (isBigZone((Zone)i)) {
      CHECK(r.w >= 80 && r.h >= 80);  // 엄지로 누르는 넷: 짧은 쪽이 80픽셀(10mm) 이상
    } else {
      // 작게 쓰는 셋: 누름으로 치는 범위가 가로 60픽셀(7.6mm), 세로 32픽셀(4mm) 이상
      CHECK(r.w - 2 * HIT_INSET >= 60 && r.h - 2 * HIT_INSET >= 32);
    }
  }
  // 보는 곳 = 상태 막대 + 무대. 화면 맨 위에 있다.
  CHECK(VIEW_RECT.x == 0 && VIEW_RECT.y == 0 && VIEW_RECT.w == W);
  CHECK(STATUS_RECT.y == VIEW_RECT.y && STATUS_RECT.h == STATUS_H && STATUS_RECT.w == VIEW_RECT.w);
  CHECK(STAGE_RECT.y == STATUS_RECT.y + STATUS_RECT.h && STAGE_RECT.y + STAGE_RECT.h == VIEW_RECT.y + VIEW_RECT.h);
}

static void test_layout_zones_keep_gaps() {
  auto apart = [](const Rect& p, const Rect& q) {
    // 두 사각형 사이의 가로·세로 거리(겹치면 음수)
    const int dx = (p.x > q.x ? p.x - (q.x + q.w) : q.x - (p.x + p.w));
    const int dy = (p.y > q.y ? p.y - (q.y + q.h) : q.y - (p.y + p.h));
    return dx >= GAP || dy >= GAP;
  };
  for (int a = 0; a < ZONE_COUNT; a++) {
    CHECK(apart(ZONE_RECTS[a], VIEW_RECT));  // 버튼은 보는 곳과도 떨어져 있다
    for (int b = a + 1; b < ZONE_COUNT; b++) CHECK(apart(ZONE_RECTS[a], ZONE_RECTS[b]));
  }
}

static void test_layout_hit_test() {
  for (int i = 0; i < ZONE_COUNT; i++) {
    const Rect& r = ZONE_RECTS[i];
    Zone z{};
    CHECK(zoneAt(r.x + r.w / 2, r.y + r.h / 2, z) && z == (Zone)i);
    // 테두리에서 HIT_INSET 안쪽부터 누름이다.
    CHECK(zoneAt(r.x + HIT_INSET, r.y + HIT_INSET, z) && z == (Zone)i);
    CHECK(!zoneAt(r.x + HIT_INSET - 1, r.y + r.h / 2, z));
    CHECK(!zoneAt(r.x + r.w / 2, r.y + r.h - HIT_INSET, z));
  }
  Zone z{};
  CHECK(!zoneAt(120, 40, z));   // 보는 곳은 누르는 자리가 아니다
  CHECK(!zoneAt(120, 180, z));  // « 와 » 사이 틈
  CHECK(!zoneAt(60, 228, z));   // « 와 말하기 사이 틈
  CHECK(!zoneAt(120, 275, z));  // 말하기와 확인 사이 틈: 잘못 누르면 가장 비싼 경계
  CHECK(!zoneAt(-1, 100, z));
  CHECK(!zoneAt(300, 100, z));
}

// D55: 엄지로 누르는 넷(«·»·말하기·확인)은 크기가 같고 2x2 로 줄이 맞는다. 가장 자주 쓰는 둘이 맨 아랫줄이다.
static void test_layout_big_four_line_up() {
  const Rect& back = zoneRect(Zone::Back);
  const Rect& next = zoneRect(Zone::Next);
  const Rect& talk = zoneRect(Zone::Talk);
  const Rect& ok = zoneRect(Zone::Ok);
  CHECK(back.w == next.w && next.w == talk.w && talk.w == ok.w);
  CHECK(back.h == next.h && next.h == talk.h && talk.h == ok.h);
  CHECK(talk.x == back.x && ok.x == next.x && back.y == next.y && talk.y == ok.y);
  CHECK(talk.y > back.y && talk.y + talk.h == H);  // 말하기·확인이 손에 가장 가까운 줄
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (!isBigZone((Zone)i)) CHECK(ZONE_RECTS[i].y + ZONE_RECTS[i].h <= back.y);  // 작은 셋은 그 위에
  }
}

// 조각 그림(art_data, tools/gen_art.py 가 만든다)이 이 배치로 만들어졌는가.
static void test_layout_matches_art() {
  const Rect& v = VIEW_RECT;
  CHECK(art::LAYOUT[0][0] == v.x && art::LAYOUT[0][1] == v.y && art::LAYOUT[0][2] == v.w && art::LAYOUT[0][3] == v.h);
  CHECK(art::WALL.w == W && art::WALL.h == H && art::WALL.px != nullptr);
  CHECK(art::VIEW_LIVE.w == v.w && art::VIEW_LIVE.h == v.h && art::VIEW_LIVE.px != nullptr);
  for (int i = 0; i < ZONE_COUNT; i++) {
    const Rect& r = ZONE_RECTS[i];
    CHECK(art::LAYOUT[i + 1][0] == r.x && art::LAYOUT[i + 1][1] == r.y && art::LAYOUT[i + 1][2] == r.w &&
          art::LAYOUT[i + 1][3] == r.h);
    for (int look = 0; look < art::LOOK_COUNT; look++) {
      const art::Image& im = art::KEY[i][look];
      const bool must = look != art::LOOK_LIVE || (Zone)i == Zone::Talk;  // "듣는 중"은 말하기에만 있다
      CHECK((im.px != nullptr) == must);
      if (im.px) CHECK(im.w == r.w && im.h == r.h);
    }
  }
}

// ---------- 터치 → 사건 ----------

static void test_touch_tap_face_is_one_talk() {
  Rig r;
  r.tap(Zone::Talk);
  CHECK(r.count(Ev::TalkTap) == 1);
  CHECK(r.evs.size() == 1);
  r.tap(Zone::Talk);  // 다시 누르면 끄는 톡
  CHECK(r.count(Ev::TalkTap) == 2);
}

static void test_touch_very_short_tap_still_counts() {
  Rig r;
  r.tap(Zone::Talk, 20);  // 20ms: 터치 칩이 두 번 본 것
  CHECK(r.count(Ev::TalkTap) == 1);
  r.tap(Zone::Ok, 20);
  CHECK(r.count(Ev::Confirm) == 1);
  r.tap(Zone::Up, 20);
  CHECK(r.count(Ev::Up) == 1);
}

static void test_touch_single_glitch_is_ignored() {
  Rig r;
  r.holdZone(Zone::Ok, 10);  // 한 번만 보인 값
  r.idle(300);
  CHECK(r.evs.empty());
}

static void test_touch_ok_fires_on_release() {
  Rig r;
  r.holdZone(Zone::Ok, 200);
  CHECK(r.evs.empty());  // 누르고 있는 동안에는 아직
  r.idle(100);
  CHECK(r.count(Ev::Confirm) == 1);
}

static void test_touch_cancel_once_per_second() {
  Rig r;
  r.tap(Zone::Nope);
  r.tap(Zone::Nope);  // 0.4초 뒤: D17 로 막힌다
  CHECK(r.count(Ev::Cancel) == 1);
  r.idle(1000);
  r.tap(Zone::Nope);
  CHECK(r.count(Ev::Cancel) == 2);
}

static void test_touch_arrows() {
  Rig r;
  r.tap(Zone::Up);
  r.tap(Zone::Down);
  r.tap(Zone::Next);
  r.tap(Zone::Back);
  CHECK(r.count(Ev::Up) == 1);
  CHECK(r.count(Ev::Down) == 1);
  CHECK(r.count(Ev::WindowNext) == 1);
  CHECK(r.count(Ev::WindowPrev) == 1);
  CHECK(r.evs.size() == 4);
}

static void test_touch_arrow_repeats_while_held() {
  Rig r;
  r.holdZone(Zone::Down, 1000);  // 0.1초쯤에 한 번, 그 뒤 0.4초마다
  r.idle(300);
  CHECK(r.count(Ev::Down) == 3);
  // 창 넘기기는 누르고 있어도 한 번만
  Rig w;
  w.holdZone(Zone::Next, 1000);
  w.idle(300);
  CHECK(w.count(Ev::WindowNext) == 1);
}

static void test_touch_two_quick_arrow_taps() {
  Rig r;
  r.holdZone(Zone::Down, 60);
  r.idle(200);  // 0.26초 간격으로 두 번
  r.holdZone(Zone::Down, 60);
  r.idle(300);
  CHECK(r.count(Ev::Down) == 2);
}

// 화살표를 짧게 톡 친 직후에 loop 가 길게 막혀도(키 전송 42ms, 얼굴 다시 그리기) 톡을 놓치지 않는다.
static void test_touch_arrow_tap_survives_stalls() {
  for (uint32_t block = 30; block <= 300; block += 30) {
    Rig r;
    r.holdZone(Zone::Down, 30);  // 30ms 톡
    r.stall(block);              // 곧바로 loop 가 막힌다
    r.idle(400);
    CHECK(r.count(Ev::Down) == 1);
    CHECK(r.evs.size() == 1);
  }
  // 머무는 도중에 여러 번 막혀도 같다.
  Rig r;
  r.holdZone(Zone::Up, 20);
  for (int i = 0; i < 4; i++) {
    r.stall(45);
    r.idle(20);
  }
  r.idle(400);
  CHECK(r.count(Ev::Up) == 1);
  // 기기처럼: 사건이 나올 때마다 키를 보내느라 42ms 막힌다. 톡 다섯 번 = 사건 다섯 개.
  Rig k;
  for (int i = 0; i < 5; i++) {
    const size_t before = k.evs.size();
    k.holdZone(Zone::Down, 40);
    for (int t = 0; t < 30; t++) {
      k.tick(nullptr, 0);
      if (k.evs.size() != before) {  // 방금 사건이 나왔다 → 키를 보내는 동안 막힌다
        k.stall(42);
        break;
      }
    }
    k.idle(150);
  }
  CHECK(k.count(Ev::Down) == 5);
}

// 막힌 시간은 이어 주는 시간에서 빼지 않지만, 평소에는 제때 내려간다(눌린 채 남지 않는다).
static void test_touch_arrow_hold_ends() {
  Rig r;
  r.holdZone(Zone::Next, 20);
  r.idle(TouchMapper::ARROW_HOLD_MS + 40);
  CHECK(r.touch.pressed() == 0);
  CHECK(r.count(Ev::WindowNext) == 1);
}

static void test_touch_bound_to_first_zone() {
  Rig r;
  // 확인 버튼에서 시작해 ▼ 로 미끄러져도 확인이다.
  const Rect a = center(zoneRect(Zone::Ok)), b = center(zoneRect(Zone::Down));
  r.hold(a.x, a.y, 50);
  r.hold(b.x, b.y, 200);
  CHECK(r.touch.pressed() == bit(Zone::Ok));
  r.idle(300);
  CHECK(r.count(Ev::Confirm) == 1);
  CHECK(r.count(Ev::Down) == 0);
}

static void test_touch_started_in_gap_never_presses() {
  Rig r;
  const Rect b = center(zoneRect(Zone::Ok));
  r.hold(120, 180, 50);    // « 와 » 사이 틈에서 시작
  r.hold(b.x, b.y, 200);   // 확인 버튼으로 미끄러져 들어옴
  CHECK(r.touch.pressed() == 0);
  CHECK(r.touch.touching());  // 닿아 있다는 것은 안다(화면 깨우기)
  r.idle(300);
  CHECK(r.evs.empty());
  CHECK(!r.touch.touching());
}

static void test_touch_brief_dropout_is_bridged() {
  Rig r;
  r.holdZone(Zone::Ok, 100);
  r.idle(20);  // 터치 칩이 두 번 놓침
  r.holdZone(Zone::Ok, 100);
  r.idle(300);
  CHECK(r.count(Ev::Confirm) == 1);  // 한 번의 누름이다
  Rig t;
  t.holdZone(Zone::Talk, 100);
  t.idle(20);
  t.holdZone(Zone::Talk, 100);
  t.idle(300);
  CHECK(t.count(Ev::TalkTap) == 1);
}

static void test_touch_two_fingers() {
  Rig r;
  const Rect a = center(zoneRect(Zone::Ok)), b = center(zoneRect(Zone::Nope));
  const TouchPoint both[2] = {{0, a.x, a.y}, {1, b.x, b.y}};
  for (int i = 0; i < 20; i++) r.tick(both, 2);
  CHECK(r.touch.pressed() == (bit(Zone::Ok) | bit(Zone::Nope)));
  r.idle(300);
  // 확인과 취소를 함께 누른 것은 어느 쪽도 아니다(StickC 의 "동시 누름"과 같다).
  CHECK(r.count(Ev::Confirm) == 0);
  CHECK(r.count(Ev::Cancel) == 0);
}

static void test_touch_face_held_blocks_buttons() {
  Rig r;
  const Rect a = center(zoneRect(Zone::Talk)), b = center(zoneRect(Zone::Ok));
  const TouchPoint face{0, a.x, a.y};
  const TouchPoint both[2] = {{0, a.x, a.y}, {1, b.x, b.y}};
  for (int i = 0; i < 10; i++) r.tick(&face, 1);
  for (int i = 0; i < 10; i++) r.tick(both, 2);
  for (int i = 0; i < 10; i++) r.tick(&face, 1);
  r.idle(300);
  CHECK(r.count(Ev::TalkTap) == 1);
  CHECK(r.count(Ev::Confirm) == 0);  // 얼굴을 누른 채 누른 확인은 무효
}

static void test_touch_fill_values() {
  TouchMapper m;
  const Rect u = center(zoneRect(Zone::Up)), n = center(zoneRect(Zone::Next));
  const TouchPoint up{0, u.x, u.y};
  m.update(&up, 1, 0);
  m.update(&up, 1, 10);
  RawInput in{};
  m.fill(in);
  CHECK(in.joyOk && in.y == 127 && in.x == 0 && !in.joyBtn && !in.btnA && !in.btnB);
  m.reset();
  const TouchPoint next{0, n.x, n.y};
  m.update(&next, 1, 20);
  m.update(&next, 1, 30);
  m.fill(in);
  CHECK(in.x == 127 && in.y == 0);
  m.reset();
  CHECK(m.pressed() == 0 && !m.touching());
}

// D71 보는 곳(캐릭터가 있는 위쪽)을 톡 치면 "화면만 바로잡기". 누르는 자리(Zone)가 아니므로 키는 나가지 않는다.
static void test_layout_view_tap_area() {
  CHECK(inside(VIEW_TAP_RECT, VIEW_RECT));
  // 버튼의 누름 인정 범위와는, 버튼끼리의 죽은 띠(GAP + 2*HIT_INSET)보다 넓게 떨어져 있다:
  // 바로 아래의 작은 버튼(▲▼✕)을 누르려다 조금 위에 닿은 손가락이 표시를 뒤집지 않게.
  for (int i = 0; i < ZONE_COUNT; i++) {
    const Rect hit = ZONE_RECTS[i].inset(HIT_INSET);
    CHECK(hit.y - (VIEW_TAP_RECT.y + VIEW_TAP_RECT.h) > GAP + 2 * HIT_INSET);
  }
  // 어느 점도 "버튼"과 "보는 곳 톡" 두 가지로 읽히지 않는다.
  int both = 0, view = 0;
  Zone z;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const bool v = viewTapAt(x, y);
      view += v;
      both += (v && zoneAt(x, y, z));
    }
  }
  CHECK(both == 0);
  CHECK(view == VIEW_TAP_RECT.w * VIEW_TAP_RECT.h);
  CHECK(viewTapAt(120, 50));                       // 캐릭터
  CHECK(viewTapAt(120, 14));                       // 상태 막대
  CHECK(!viewTapAt(120, 86) && !viewTapAt(120, 90));  // 버튼과의 사이
  CHECK(!viewTapAt(0, 50) && !viewTapAt(239, 50) && !viewTapAt(120, 0));  // 화면 가장자리(쥔 손·선이 닿는 곳)
}

static void test_touch_view_tap() {
  const int vx = 120, vy = 50;  // 캐릭터가 있는 곳
  {  // 톡: 뗀 뒤에 한 번
    Rig r;
    r.hold(vx, vy, 80);
    CHECK(!r.touch.takeViewTap());  // 닿아 있는 동안에는 아직
    CHECK(r.touch.pressed() == 0 && r.touch.touching());
    r.idle(100);
    CHECK(r.touch.takeViewTap());
    CHECK(!r.touch.takeViewTap());  // 한 번 읽으면 지워진다
    r.idle(300);
    CHECK(r.evs.empty());           // 키가 될 사건은 없다
    CHECK(!r.touch.takeViewTap());
  }
  {  // 아주 짧은 톡(두 틱)도 받고, 한 번 튄 값(한 틱)은 받지 않는다
    Rig r;
    r.hold(vx, vy, 20);
    r.idle(100);
    CHECK(r.touch.takeViewTap());
    r.hold(vx, vy, 10);
    r.idle(100);
    CHECK(!r.touch.takeViewTap());
  }
  {  // 오래 대고 있던 것(쥐느라 닿은 손)은 톡이 아니다
    Rig r;
    r.hold(vx, vy, TouchMapper::VIEW_TAP_MAX_MS + 100);
    r.idle(100);
    CHECK(!r.touch.takeViewTap());
    r.hold(vx, vy, TouchMapper::VIEW_TAP_MAX_MS - 100);  // 그보다 짧으면 톡
    r.idle(100);
    CHECK(r.touch.takeViewTap());
  }
  {  // 터치 칩이 잠깐 놓쳐도 한 번의 톡이다
    Rig r;
    r.hold(vx, vy, 60);
    r.idle(20);
    r.hold(vx, vy, 60);
    r.idle(100);
    CHECK(r.touch.takeViewTap());
    CHECK(!r.touch.takeViewTap());
  }
  {  // 처음 닿은 곳에 묶인다: 보는 곳에서 시작해 버튼으로 미끄러져도 버튼은 눌리지 않고, 톡이다
    Rig r;
    const Rect no = center(zoneRect(Zone::Nope));
    r.hold(vx, vy, 40);
    r.hold(no.x, no.y, 100);
    CHECK(r.touch.pressed() == 0);
    r.idle(300);
    CHECK(r.evs.empty());
    CHECK(r.touch.takeViewTap());
  }
  {  // 버튼에서 시작해 보는 곳으로 미끄러진 것은 그 버튼의 누름이다(톡이 아니다)
    Rig r;
    const Rect no = center(zoneRect(Zone::Nope));
    r.hold(no.x, no.y, 40);
    r.hold(vx, vy, 100);
    r.idle(300);
    CHECK(r.count(Ev::Cancel) == 1);
    CHECK(!r.touch.takeViewTap());
  }
  {  // 보는 곳과 버튼 사이의 띠에서 시작한 손가락은 아무것도 아니다
    Rig r;
    r.hold(120, 88, 80);
    r.idle(300);
    CHECK(r.evs.empty() && !r.touch.takeViewTap());
  }
  {  // 두 손가락(쥐느라 닿은 손 + 누르는 손가락)이면 톡이 아니고, 버튼의 누름은 그대로다
    Rig r;
    const Rect ok = center(zoneRect(Zone::Ok));
    const TouchPoint view{0, (int16_t)vx, (int16_t)vy};
    const TouchPoint both[2] = {{0, (int16_t)vx, (int16_t)vy}, {1, ok.x, ok.y}};
    for (int i = 0; i < 5; i++) r.tick(&view, 1);
    for (int i = 0; i < 10; i++) r.tick(both, 2);
    r.idle(300);
    CHECK(!r.touch.takeViewTap());
    CHECK(r.count(Ev::Confirm) == 1);
    // 버튼을 누르는 중에 보는 곳에 닿았다 뗀 것도 톡이 아니다
    const TouchPoint btn{1, ok.x, ok.y};
    for (int i = 0; i < 5; i++) r.tick(&btn, 1);
    for (int i = 0; i < 5; i++) r.tick(both, 2);
    for (int i = 0; i < 10; i++) r.tick(&btn, 1);
    CHECK(!r.touch.takeViewTap());
    r.idle(300);
    CHECK(!r.touch.takeViewTap());
    // 그 뒤의 한 손가락 톡은 다시 받는다
    r.hold(vx, vy, 80);
    r.idle(100);
    CHECK(r.touch.takeViewTap());
  }
  {  // reset 하면 받아 둔 톡도 버린다
    Rig r;
    r.hold(vx, vy, 80);
    r.idle(100);
    r.touch.reset();
    CHECK(!r.touch.takeViewTap());
  }
}

// ---------- 화면 상태 ----------

static bool poll(TouchState& s, uint32_t now, Frame& f) {
  Dirty d = 0;
  return s.poll(now, f, d);
}

static void test_state_idle_faces() {
  TouchState s;
  Frame f{};
  Dirty d = 0;
  CHECK(s.poll(0, f, d));
  CHECK(d == DIRTY_ALL);  // 처음에는 전체를 그린다
  CHECK(f.face == Face::Plug);
  CHECK(!s.poll(10, f, d));  // 달라진 것이 없으면 그리지 않는다
  s.setReady(true);
  CHECK(s.poll(20, f, d));
  CHECK(f.face == Face::Hi);
  CHECK(d == DIRTY_ALL);  // 연결되면 버튼이 모두 켜지고 캐릭터와 상태 막대도 바뀐다
}

static void test_state_talk_follows_real_mic() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  s.onSent(Ev::TalkTap, 100);
  CHECK(poll(s, 100, f) && f.face == Face::Talk && f.micLive && f.guess);  // 누르자마자 듣는 중(미루어 — D65)
  s.setMicLive(true);  // 컴퓨터가 마이크를 열었다 → 이제 사실이다
  CHECK(poll(s, 400, f) && f.face == Face::Talk && f.micLive && !f.guess);
  CHECK(!poll(s, 60000, f));  // 열려 있는 동안 내내 TALK (기다리던 시간이 지나도 MIC? 가 뜨지 않는다)
  s.onSent(Ev::TalkTap, 60000);  // 다시 눌러 끈다
  CHECK(!poll(s, 60010, f));     // 아직 열려 있으면 그대로
  s.setMicLive(false);
  CHECK(poll(s, 60300, f) && f.face == Face::Hi && !f.micLive);
  CHECK(!poll(s, 70000, f));  // 끈 것을 "켜 달라"로 착각해 MIC? 를 띄우지 않는다
}

// D65 컴퓨터가 이 기기의 마이크를 열지 않아도(다른 마이크로 듣는다 · 블루투스로 쓴다) 말하기를 누르면
// 듣는 중으로 보인다. (써 보니: 말하기를 눌렀는데 빨갛게 바뀌지 않는다는 지적이 거듭 나왔다.)
static void test_state_guess_when_mic_never_opens() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  s.onSent(Ev::TalkTap, 1000);
  CHECK(poll(s, 1000, f) && f.face == Face::Talk && f.micLive && f.guess);
  s.setHeard(true);  // 말하고 있다
  // 마이크가 끝내 열리지 않아도 그대로 듣는 중이다("마이크 안 열림"으로 바뀌지 않는다).
  for (uint32_t t = 1100; t <= 9000; t += 100) {
    poll(s, t, f);
    CHECK(f.face == Face::Talk && f.micLive && f.guess);
  }
  CHECK(f.mouthOpen);  // 목소리에 맞춰 입과 파형이 움직인다(기기 마이크가 직접 듣는다)
  // 다시 누르면 꺼진다.
  s.onSent(Ev::TalkTap, 9100);
  CHECK(poll(s, 9100, f) && f.face == Face::Hi && !f.micLive && !f.guess);
  // 그리고 또 누르면 켜진다.
  s.onSent(Ev::TalkTap, 12000);
  CHECK(poll(s, 12000, f) && f.micLive && f.guess);
}

static void test_state_guess_ends() {
  const uint32_t quiet = TouchState::GUESS_QUIET_MS, most = TouchState::GUESS_MAX_MS;
  Frame f{};
  {  // 확인(Enter)을 누르면 컴퓨터가 받아쓰기를 끝내고 보낸다 → 끈다.
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    poll(s, 1000, f);
    s.onSent(Ev::Confirm, 3000);
    CHECK(poll(s, 3000, f) && f.face == Face::Ok && !f.micLive);
    CHECK(poll(s, 3000 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Hi && !f.micLive);
  }
  {  // 조용한 채로 오래 있으면(컴퓨터의 받아쓰기가 스스로 꺼지는 때) 내린다.
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    poll(s, 1000, f);
    poll(s, 1000 + quiet - 10, f);
    CHECK(f.micLive);
    CHECK(poll(s, 1000 + quiet, f) && !f.micLive && f.face == Face::Hi);
  }
  {  // 말이 이어지는 동안에는 내리지 않는다. 말이 그친 때부터 센다.
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    s.setHeard(true);
    for (uint32_t t = 1000; t <= 60000; t += 500) poll(s, t, f);
    CHECK(f.micLive && f.guess);
    s.setHeard(false);  // 60초에 말이 그쳤다
    poll(s, 60500, f);
    poll(s, 60000 + quiet - 10, f);
    CHECK(f.micLive);
    poll(s, 60000 + quiet, f);
    CHECK(!f.micLive);
  }
  {  // 말이 끊이지 않아도 가장 길어야 GUESS_MAX_MS(받아쓰기의 최대 길이).
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    s.setHeard(true);
    for (uint32_t t = 1000; t < 1000 + most; t += 500) poll(s, t, f);
    CHECK(f.micLive);
    poll(s, 1000 + most, f);
    CHECK(!f.micLive);
  }
  {  // 연결이 끊기면 내린다.
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    poll(s, 1000, f);
    s.setReady(false);
    CHECK(poll(s, 1100, f) && !f.micLive && f.face == Face::Plug);
    s.setReady(true);
    CHECK(poll(s, 1200, f) && !f.micLive && f.face == Face::Hi);  // 다시 붙어도 되살아나지 않는다
  }
  {  // 다른 버튼(▲▼, 창 넘기기, 취소)은 끄지 않는다: 받아쓰기가 끝났는지 알 수 없으면 켜진 쪽으로 둔다
     // (꺼진 줄 알고 말하는 것보다, 켜진 줄 알고 조심하는 편이 낫다).
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    poll(s, 1000, f);
    const Ev others[] = {Ev::Up, Ev::Down, Ev::WindowNext, Ev::WindowPrev, Ev::Cancel};
    uint32_t t = 1500;
    for (Ev e : others) {
      s.onSent(e, t);
      poll(s, t, f);
      CHECK(f.micLive && f.guess);
      t += 300;
    }
  }
  {  // 누르는 동안만 말하는 방식(Hold)에서는 떼면 꺼진다.
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkDown, 1000);
    CHECK(poll(s, 1000, f) && f.micLive && f.guess);
    s.onSent(Ev::TalkUp, 2500);
    CHECK(poll(s, 2500, f) && !f.micLive);
  }
}

// 방의 소리(입을 여닫는 문턱에는 걸리지만 말소리는 아닌 크기)로는 미룸이 이어지지 않는다.
// 써 보니 빨간 화면이 켜진 채로 남았다 — 주변 소리가 말소리로 쳐져 꺼지지 않았다.
static void test_state_guess_ignores_room_noise() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  s.onSent(Ev::TalkTap, 1000);
  s.setVoice(true);    // 입을 여닫는 문턱은 넘었다(주변 소리)
  s.setHeard(false);   // 그러나 말소리로 가려낸 것은 아니다
  for (uint32_t t = 1000; t < 1000 + TouchState::GUESS_QUIET_MS; t += 100) poll(s, t, f);
  CHECK(f.micLive && f.guess);
  CHECK(!f.mouthOpen);  // 미루는 동안의 입·파형도 말소리에만 움직인다
  poll(s, 1000 + TouchState::GUESS_QUIET_MS, f);
  CHECK(!f.micLive);
  CHECK(s.takeGuessEnd() == TouchState::GuessEnd::Quiet);
  CHECK(s.takeGuessEnd() == TouchState::GuessEnd::None);  // 한 번만 알린다
  // 말소리가 들리면 입이 움직이고, 그치면 잠깐 뒤 닫힌다.
  s.onSent(Ev::TalkTap, 50000);
  s.setHeard(true);
  poll(s, 50010, f);
  CHECK(f.mouthOpen && f.guess);
  s.setHeard(false);
  poll(s, 50100, f);
  CHECK(f.mouthOpen);  // 낱말 사이의 짧은 틈에는 닫지 않는다
  poll(s, 50010 + TouchState::HEARD_HOLD_MS + 10, f);
  CHECK(!f.mouthOpen && f.micLive);
}

// D71 화면만 바로잡기: 미루어 듣는 중(D65)이 실제와 어긋났을 때, 키를 보내지 않고 표시만 뒤집는다.
static void test_state_flip_guess() {
  const uint32_t quiet = TouchState::GUESS_QUIET_MS;
  const auto FLIPPED = TouchState::ViewTap::Flipped, IGNORED = TouchState::ViewTap::Ignored,
             CLEARED = TouchState::ViewTap::NoteCleared;
  Frame f{};
  {  // 꺼져 보이는데 실제로는 받아쓰는 중이다 → 톡 → 듣는 중(미룸)
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    CHECK(s.flipGuess(1000) == FLIPPED);
    CHECK(poll(s, 1000, f) && f.face == Face::Talk && f.micLive && f.guess);
    CHECK(s.guessing() && s.takeGuessEnd() == TouchState::GuessEnd::None);
    // 그 뒤로는 말하기를 눌러 켠 것과 같다: 말소리가 없으면 15초에 내려간다.
    poll(s, 1000 + quiet - 10, f);
    CHECK(f.micLive);
    CHECK(poll(s, 1000 + quiet, f) && !f.micLive && f.face == Face::Hi);
    CHECK(s.takeGuessEnd() == TouchState::GuessEnd::Quiet);
  }
  {  // 빨간데 실제로는 꺼져 있다 → 톡 → 내린다. 그러면 말하기가 다시 "켜기"가 된다(어긋남이 풀렸다).
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    CHECK(poll(s, 1000, f) && f.micLive && f.guess);
    CHECK(s.flipGuess(2000) == FLIPPED);
    CHECK(poll(s, 2000, f) && f.face == Face::Hi && !f.micLive && !f.guess);
    CHECK(s.takeGuessEnd() == TouchState::GuessEnd::Fixed);
    s.onSent(Ev::TalkTap, 3000);
    CHECK(poll(s, 3000, f) && f.micLive && f.guess);
  }
  {  // 톡으로 켠 뒤 말하기를 누르면(실제로는 끄는 것이다) 내려간다
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.flipGuess(1000);
    poll(s, 1000, f);
    s.onSent(Ev::TalkTap, 2000);
    CHECK(poll(s, 2000, f) && !f.micLive);
    CHECK(s.takeGuessEnd() == TouchState::GuessEnd::Again);
  }
  {  // 사실을 아는 동안(컴퓨터가 이 기기의 마이크를 열었다)에는 뒤집지 않는다: 사실은 틀릴 수 없다
    TouchState s;
    s.setReady(true);
    s.setMicLive(true);
    CHECK(poll(s, 0, f) && f.micLive && !f.guess);
    CHECK(s.flipGuess(1000) == IGNORED);
    CHECK(!poll(s, 1000, f));  // 화면이 바뀌지 않는다
    s.setMicLive(false);
    CHECK(poll(s, 2000, f) && !f.micLive && !f.guess);  // 닫히면 내려간다(톡이 미룸을 남기지 않았다)
  }
  {  // 연결이 없으면 뒤집지 않는다(받아쓰기를 켤 길이 없다)
    TouchState s;
    poll(s, 0, f);
    CHECK(s.flipGuess(1000) == IGNORED);
    CHECK(!poll(s, 1000, f) && !s.guessing());
  }
  {  // 펌웨어 물음이 떠 있는 동안에는 뒤집지 않는다
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    CHECK(s.askUpdate(1000));
    CHECK(poll(s, 1000, f) && f.face == Face::Update);
    CHECK(s.flipGuess(1100) == IGNORED);
    CHECK(!s.guessing());
  }
  {  // 잠깐 떠 있는 장면(방금 누른 ▲) 위에서 쳐도 빨강은 곧바로 바뀐다. 그 장면은 건드리지 않는다(제 시간에 내려간다).
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::Up, 1000);
    CHECK(poll(s, 1000, f) && f.face == Face::Up && !f.micLive);
    CHECK(s.flipGuess(1100) == FLIPPED);
    CHECK(poll(s, 1100, f) && f.face == Face::Up && f.micLive && f.guess);
    CHECK(poll(s, 1000 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Talk && f.micLive && f.guess);
  }
  { // Claude 의 알림이 떠 있을 때의 톡은 그 알림만 거둔다 — 표시는 뒤집지 않는다(독립 검토:
     // 알림을 보려고 만진 손이 알림을 지우면서 빨강까지 켰다). 그다음 톡부터 뒤집는다.
    for (int ask = 0; ask < 2; ask++) {
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onNote(ask ? TouchState::Note::Ask : TouchState::Note::Done, 1000);
      CHECK(poll(s, 1000, f) && f.face == (ask ? Face::Ask : Face::Done));
      CHECK(s.flipGuess(1500) == CLEARED);
      CHECK(poll(s, 1500, f) && f.face == Face::Hi && !f.micLive && !s.guessing());
      CHECK(s.flipGuess(2500) == FLIPPED);
      CHECK(poll(s, 2500, f) && f.face == Face::Talk && f.micLive && f.guess);
    }
    // 빨간 채로 알림이 왔다 → 톡 → 알림만 거두고 빨강은 그대로
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    s.onSent(Ev::TalkTap, 1000);
    s.onNote(TouchState::Note::Ask, 2000);
    CHECK(poll(s, 2000, f) && f.face == Face::Ask && f.micLive);
    CHECK(s.flipGuess(3000) == CLEARED);
    CHECK(poll(s, 3000, f) && f.face == Face::Talk && f.micLive && f.guess);
    CHECK(s.takeGuessEnd() == TouchState::GuessEnd::None);
    // 연결이 없어도, 컴퓨터가 이 기기의 마이크로 듣는 중이어도 알림은 톡으로 거둘 수 있다(뒤집지는 않는다):
    // 그때 못 거두면 `Claude needs you` 가 60초 동안 화면을 가린다(독립 검토).
    TouchState off;
    poll(off, 0, f);
    off.onNote(TouchState::Note::Ask, 1000);
    CHECK(poll(off, 1000, f) && f.face == Face::Ask);
    CHECK(off.flipGuess(1500) == CLEARED);
    CHECK(poll(off, 1500, f) && f.face == Face::Plug && !off.guessing());
    CHECK(off.flipGuess(2500) == IGNORED);
    TouchState live;
    live.setReady(true);
    live.setMicLive(true);
    poll(live, 0, f);
    live.onNote(TouchState::Note::Ask, 1000);
    CHECK(poll(live, 1000, f) && f.face == Face::Ask && f.micLive);
    CHECK(live.flipGuess(1500) == CLEARED);
    CHECK(poll(live, 1500, f) && f.face == Face::Talk && f.micLive && !f.guess);
    CHECK(live.flipGuess(2500) == IGNORED);
    // 펌웨어 물음이 떠 있으면 알림도 건드리지 않는다
    TouchState ask;
    ask.setReady(true);
    poll(ask, 0, f);
    ask.onNote(TouchState::Note::Ask, 1000);
    CHECK(ask.askUpdate(1100));
    CHECK(ask.flipGuess(1500) == IGNORED);
  }
  {  // D74 선으로 쓰고, 말하기를 누르면 맥이 "이 기기의 마이크"를 여는 것이 확인됐다(사실을 따르는 중):
     // 쉬는 화면의 톡은 빨강을 켜지 않는다 — 꺼져 있다는 것도 사실이다. (사용자가 캐릭터를 눌러 본 뒤:
     // "위에 캐릭터를 눌러도 녹음이 되는거지?" — 녹음은 안 되는데 화면만 빨개졌다.)
    const uint32_t win = TouchState::TRUTH_WINDOW_MS;
    TouchState s;
    s.setReady(true);
    poll(s, 0, f);
    CHECK(!s.truthKnown());
    CHECK(s.flipGuess(500) == FLIPPED && s.flipGuess(600) == FLIPPED);  // 확인되기 전에는 지금까지와 같다(켜고 끔)
    s.onSent(Ev::TalkTap, 1000);
    poll(s, 1000, f);
    CHECK(f.micLive && f.guess && !s.truthKnown());
    s.setMicLive(true);                 // 0.15초 뒤 맥이 마이크를 열었다
    CHECK(poll(s, 1150, f) && f.micLive && !f.guess && s.truthKnown());
    CHECK(s.flipGuess(2000) == IGNORED);  // 듣는 중에도(채운 점)
    s.setMicLive(false);                // 맥이 받아쓰기를 끝냈다
    CHECK(poll(s, 16000, f) && !f.micLive);
    CHECK(s.flipGuess(17000) == IGNORED);  // 쉬는 화면의 톡: 켜지 않는다
    CHECK(!poll(s, 17000, f) && !s.guessing());
    // 알림은 그때도 톡으로 거둘 수 있다
    s.onNote(TouchState::Note::Ask, 18000);
    poll(s, 18000, f);
    CHECK(s.flipGuess(18500) == CLEARED);
    CHECK(poll(s, 18500, f) && f.face == Face::Hi);
    // 말하기는 그대로다. 다시 확인되면 계속 사실을 따른다.
    s.onSent(Ev::TalkTap, 20000);
    s.setMicLive(true);
    poll(s, 20150, f);
    s.setMicLive(false);
    poll(s, 30000, f);
    CHECK(s.truthKnown() && s.flipGuess(31000) == IGNORED);
    // 맥의 소리 입력이 바뀌었다: 말하기를 눌렀는데 마이크가 열리지 않는다 → 미룸으로 돌아간다.
    s.onSent(Ev::TalkTap, 40000);
    CHECK(poll(s, 40000, f) && f.micLive && f.guess);
    CHECK(s.flipGuess(41000) == FLIPPED);        // 켜져 있는 미룸을 톡으로 끄는 것은 언제나 된다
    CHECK(poll(s, 41000, f) && !f.micLive);
    CHECK(s.flipGuess(40000 + win - 10) == IGNORED);  // 기다리는 시간이 다 지나기 전: 아직 "사실을 따르는 중"으로 본다
    CHECK(s.flipGuess(40000 + win) == FLIPPED);       // 지나면: 열리지 않았으니 다시 어림 — 톡을 받는다
    CHECK(!s.truthKnown());
    CHECK(poll(s, 40000 + win, f) && f.micLive && f.guess);
  }
  {  // D74 확인은 "말하기를 누른 직후에 열렸을 때"만이다
    const uint32_t win = TouchState::TRUTH_WINDOW_MS;
    {  // 한참 뒤에 열린 마이크(다른 프로그램이 열었다)는 확인이 아니다
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      poll(s, 1000 + win, f);
      s.setMicLive(true);
      poll(s, 1000 + win + 10, f);
      CHECK(f.micLive && !f.guess && !s.truthKnown());
      s.setMicLive(false);
      poll(s, 20000, f);
      CHECK(s.flipGuess(21000) == FLIPPED);
    }
    {  // 말하기를 누르지 않았는데 열린 마이크(화상 회의 · 키보드로 켠 받아쓰기)도 확인이 아니다
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.setMicLive(true);
      poll(s, 1000, f);
      s.setMicLive(false);
      poll(s, 2000, f);
      CHECK(!s.truthKnown() && s.flipGuess(3000) == FLIPPED);
    }
    {  // 기다리는 시간 안에 열리면, 그 사이 poll 이 없었어도 확인이다(끝 무렵에 열림)
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      poll(s, 1000 + win - 10, f);
      s.setMicLive(true);
      poll(s, 1000 + win + 500, f);
      CHECK(s.truthKnown());
    }
    {  // 선을 뽑아 블루투스로 넘어가면(또는 연결이 끊기면) 확인은 지워진다: 무선에서는 사실을 알 길이 없다
      for (int how = 0; how < 2; how++) {
        TouchState s;
        s.setLink(LinkState::Ready, 0, false);
        poll(s, 0, f);
        s.onSent(Ev::TalkTap, 1000);
        s.setMicLive(true);
        poll(s, 1150, f);
        s.setMicLive(false);
        poll(s, 5000, f);
        CHECK(s.truthKnown());
        if (how == 0) {
          s.setLink(LinkState::Ready, 6000, true);       // 블루투스로
        } else {
          s.setLink(LinkState::Searching, 6000, false);  // 끊겼다가
          s.setLink(LinkState::Ready, 7000, false);      // 다시 선으로
        }
        poll(s, 8000, f);
        CHECK(!s.truthKnown() && s.flipGuess(9000) == FLIPPED);
      }
    }
    {  // 무선에서는 말하기를 눌러도 확인이 생기지 않는다(마이크가 열릴 수 없다)
      TouchState s;
      s.setLink(LinkState::Ready, 0, true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      poll(s, 1000 + win + 100, f);
      CHECK(!s.truthKnown());
      CHECK(s.flipGuess(1000 + win + 200) == FLIPPED);  // 끄고
      CHECK(s.flipGuess(1000 + win + 300) == FLIPPED);  // 켠다
      // 무선으로 쓰는 중에 (선이 꽂힌 다른 일로) 마이크가 열렸다는 알림이 와도, 그것은 이 말하기의 확인이 아니다
      TouchState w;
      w.setLink(LinkState::Ready, 0, true);
      poll(w, 0, f);
      w.onSent(Ev::TalkTap, 1000);
      w.setMicLive(true);
      poll(w, 1150, f);
      w.setMicLive(false);
      poll(w, 5000, f);
      CHECK(!w.truthKnown());
    }
    {  // 톡으로 켜 둔 미룸 위에서 말하기를 눌렀다(기기는 "껐다"로 읽지만 실제로는 받아쓰기를 켠 것일 수 있다):
       // 곧 마이크가 열리면 그것도 확인이다 — 말하기를 누른 직후의 열림은 미룸이 어떤 상태였든 같은 뜻이다(독립 검토).
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      CHECK(s.flipGuess(500) == FLIPPED);   // 톡으로 켬(빈 고리)
      CHECK(poll(s, 500, f) && f.micLive && f.guess);
      s.onSent(Ev::TalkTap, 1000);          // 말하기 → 미룸은 내려간다
      CHECK(poll(s, 1000, f) && !f.micLive);
      s.setMicLive(true);                   // 그런데 맥이 마이크를 열었다
      CHECK(poll(s, 1150, f) && f.micLive && !f.guess && s.truthKnown());
      s.setMicLive(false);
      poll(s, 9000, f);
      CHECK(s.flipGuess(10000) == IGNORED);
    }
    {  // 말하기를 눌러 미룸을 끈 것이 정말 "다른 마이크로 하던 받아쓰기를 끈 것"이면 마이크는 열리지 않는다 → 확인이 지워진다
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      s.setMicLive(true);
      poll(s, 1150, f);
      s.setMicLive(false);
      poll(s, 5000, f);
      CHECK(s.truthKnown());
      // (맥의 입력이 다른 마이크로 바뀌었다) 말하기 → 미룸 켜짐 → 말하기 → 미룸 꺼짐. 두 번 다 열리지 않았다.
      s.onSent(Ev::TalkTap, 10000);
      poll(s, 10000, f);
      s.onSent(Ev::TalkTap, 11000);
      CHECK(poll(s, 11000, f) && !f.micLive && s.truthKnown());  // 아직 기다리는 중
      poll(s, 11000 + win - 10, f);
      CHECK(s.truthKnown());                                     // 두 번째 누름부터 다시 센다
      poll(s, 11000 + win, f);
      CHECK(!s.truthKnown() && s.flipGuess(11000 + win + 100) == FLIPPED);
    }
    {  // 말하기를 잇달아 두 번 눌렀다(켰다 바로 껐다): 첫 누름의 열림이 늦게 와도 사실이므로 확인으로 받는다
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      poll(s, 1000, f);
      s.onSent(Ev::TalkTap, 1100);
      poll(s, 1100, f);
      s.setMicLive(true);
      poll(s, 1150, f);
      CHECK(f.micLive && !f.guess && s.truthKnown());
    }
    {  // 사실로 듣는 중에 누른 말하기(끄기)는 확인을 건드리지 않는다
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      s.setMicLive(true);
      poll(s, 1150, f);
      s.onSent(Ev::TalkTap, 5000);  // 끄기
      s.setMicLive(false);
      poll(s, 5200, f);
      poll(s, 5000 + win + 100, f);
      CHECK(s.truthKnown() && !f.micLive && s.flipGuess(5000 + win + 200) == IGNORED);
    }
    {  // 확인된 뒤 오래(시계가 반 바퀴 돌 만큼) 지나도 그대로다
      TouchState s;
      s.setReady(true);
      poll(s, 0, f);
      s.onSent(Ev::TalkTap, 1000);
      s.setMicLive(true);
      poll(s, 1150, f);
      s.setMicLive(false);
      poll(s, 5000, f);
      poll(s, 5000u + 0x80000000u, f);
      CHECK(s.truthKnown() && s.flipGuess(6000u + 0x80000000u) == IGNORED);
    }
  }
}

// 키를 보내지 못했다(연결이 사실상 없다)면 미루어 듣는 중도 접는다 — 끌 수 없는 빨강이 남지 않게.
static void test_state_guess_drops_when_keys_fail() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  s.onSent(Ev::TalkTap, 1000);
  s.setHeard(true);
  poll(s, 1000, f);
  CHECK(f.micLive && f.guess);
  s.onNotSent(2000);  // 끄려고 다시 눌렀는데 키가 나가지 않았다
  CHECK(poll(s, 2000, f) && !f.micLive && f.face == Face::NoLink);
  CHECK(s.takeGuessEnd() == TouchState::GuessEnd::NotSent);
  // 까닭을 알려 주는 다른 길들
  s.onSent(Ev::TalkTap, 10000);
  poll(s, 10000, f);
  s.onSent(Ev::TalkTap, 11000);
  poll(s, 11000, f);
  CHECK(s.takeGuessEnd() == TouchState::GuessEnd::Again);
  s.onSent(Ev::TalkTap, 20000);
  poll(s, 20000, f);
  s.setMicLive(true);
  poll(s, 20300, f);
  CHECK(s.takeGuessEnd() == TouchState::GuessEnd::MicOpened);
  s.setMicLive(false);
  s.onSent(Ev::TalkTap, 30000);
  poll(s, 30000, f);
  s.setLink(LinkState::Ready, 30500, true);
  poll(s, 30500, f);
  CHECK(s.takeGuessEnd() == TouchState::GuessEnd::RouteChanged);
}

// 말소리 가리기: 방의 소리에는 속지 않고, 말소리는 잡는다. 수치는 기기에서 잰 것을 본떴다.
//   기기 속 기록(맥북이 놓인 방, 문턱 400 · 한 번만 넘어도 말소리로 치던 판):
//     말한 5초의 최고 21404 · 8374 · 3710. 말을 멈춘 뒤 5초마다의 최고 349 453 421 658 642 487 526 407 538 485 702 558,
//     방의 바닥 42~139. → 몇 초에 한 번씩 순간적으로 400~700 이 튀어 "15초 조용함"이 오지 않았다(빨강이 70초 넘게 남았다).
//   그래서: 한두 덩어리만 큰 소리(딸깍·툭)는 말소리가 아니다. 큰 덩어리가 이어져야(말의 한 음절은 0.1초쯤) 말소리다.
static void test_speech_ear() {
  const uint16_t room[] = {66, 135, 86, 104, 48, 120, 90, 110, 131, 75};
  auto quietTick = [&](int i) { return room[i % 10]; };
  {  // 그 방 그대로: 바닥 소리 + 2~4초에 한 번, 한 덩어리짜리 400~700
    SpeechEar ear(GUESS_MIN, GUESS_RATIO, GUESS_RUN);
    const uint16_t spikes[] = {453, 421, 658, 642, 487, 526, 407, 538, 485, 702, 558, 349};
    int heard = 0;
    for (int i = 0; i < 6000; i++) {  // 60초
      const bool spike = (i % 230) == 117;
      heard += ear.feed(spike ? spikes[(i / 230) % 12] : quietTick(i));
    }
    CHECK(heard == 0);
    // 두 덩어리가 잇달아 튀어도(문 닫는 소리, 기기를 내려놓는 소리) 말소리가 아니다.
    for (int i = 0; i < 6000; i++) {
      const int k = i % 150;
      heard += ear.feed((k == 40 || k == 41) ? 2500 : quietTick(i));
    }
    CHECK(heard == 0);
  }
  {  // 말: 큰 덩어리가 이어지는 음절(0.12초)과 그 사이의 조용한 틈(0.08초)
    SpeechEar ear(GUESS_MIN, GUESS_RATIO, GUESS_RUN);
    for (int i = 0; i < 500; i++) ear.feed(quietTick(i));  // 먼저 방의 소리
    int heard = 0, longestGap = 0, gap = 0;
    for (int i = 0; i < 3000; i++) {  // 30초 동안 말
      const int k = i % 20;
      const uint16_t v = (k < 12) ? (uint16_t)(900 + (k * 617) % 2800) : (uint16_t)(40 + (k * 13) % 60);
      const bool h = ear.feed(v);
      heard += h;
      gap = h ? 0 : gap + 1;
      if (gap > longestGap) longestGap = gap;
    }
    CHECK(heard >= 1000);        // 음절마다 잡는다
    CHECK(longestGap <= 30);     // 말하는 동안 0.3초 넘게 "안 들림"이 이어지지 않는다(15초는 한참 멀다)
    CHECK(ear.threshold() == GUESS_MIN);  // 말하는 동안에도 바닥은 낮게 남는다(틈이 있어서)
    // 말을 멈추면 곧 안 들린다.
    heard = 0;
    for (int i = 0; i < 1500; i++) heard += ear.feed(quietTick(i));
    CHECK(heard <= 6);
    // 작게 말해도(음절의 크기가 문턱을 조금 넘는 정도) 이어지면 잡는다.
    heard = 0;
    for (int i = 0; i < 2000; i++) heard += ear.feed((i % 20) < 12 ? (uint16_t)(GUESS_MIN + 150) : (uint16_t)60);
    CHECK(heard >= 500);
  }
  {  // 늘 시끄러운 곳: 문턱이 따라 올라가, 그 소리 자체는 말소리로 치지 않는다.
    SpeechEar ear(GUESS_MIN, GUESS_RATIO, GUESS_RUN);
    int heard = 0;
    for (int i = 0; i < 3000; i++) heard += ear.feed((uint16_t)(700 + (i % 7) * 20));  // 700~820 의 소음
    CHECK(heard == 0);                 // (첫 덩어리가 바닥이 되므로 처음부터 넘지 못한다)
    CHECK(ear.threshold() > 1500);
    for (int i = 0; i < 6; i++) heard += ear.feed(6000);  // 그보다 훨씬 큰 목소리가 이어지면 잡는다
    CHECK(heard >= 1);
  }
  {  // 조용하다가 큰 소음이 갑자기 시작되면: 바닥이 따라 올라갈 때까지 잠깐(몇 초)은 말소리로 친다 — 알고 둔 한계.
    SpeechEar ear(GUESS_MIN, GUESS_RATIO, GUESS_RUN);
    for (int i = 0; i < 500; i++) ear.feed(quietTick(i));
    int heard = 0;
    for (int i = 0; i < 3000; i++) heard += ear.feed(900);
    CHECK(heard > 0 && heard < 1500);  // 처음에는 잡지만, 30초 내내는 아니다
  }
  {  // 비율을 0 으로 주면 고정 문턱(이어져야 한다는 것은 그대로)
    SpeechEar ear(GUESS_MIN, 0, GUESS_RUN);
    for (int i = 0; i < 1000; i++) ear.feed(GUESS_MIN + 100);
    CHECK(ear.threshold() == GUESS_MIN && ear.feed(GUESS_MIN));
    SpeechEar one(GUESS_MIN, 0, GUESS_RUN);
    CHECK(!one.feed(GUESS_MIN));       // 한 덩어리로는 아직
    CHECK(!one.feed(GUESS_MIN - 1));
  }
  {  // 마이크가 쉬는 동안(알림음을 내느라 내려 둠 — 값이 0 이다)은 "조용한 순간"이 아니다: 바닥을 내리지 않는다.
     // 내리면 시끄러운 방에서 알림음이 날 때마다 문턱이 바닥값으로 떨어져, 그 방의 소음이 몇 초 동안 말소리가 된다
     // (독립 검토).
    SpeechEar ear(GUESS_MIN, GUESS_RATIO, GUESS_RUN);
    for (int i = 0; i < 3000; i++) ear.feed((uint16_t)(250 + (i % 5) * 4));  // 늘 250쯤인 방: 문턱 750쯤
    const uint16_t thr = ear.threshold();
    CHECK(thr > 700 && thr < 800);
    int heard = 0;
    for (int i = 0; i < 100; i++) heard += ear.feed(0);  // 알림음 1초
    CHECK(heard == 0 && ear.threshold() == thr);
    for (int i = 0; i < 500; i++) heard += ear.feed((uint16_t)(650 + (i % 5) * 10));  // 그 방의 조금 큰 소음(문턱 아래)
    CHECK(heard == 0);
    // 말하다가 쉬면(0) 곧 "안 들린다"
    for (int i = 0; i < 20; i++) ear.feed(5000);
    heard = 0;
    for (int i = 0; i < 20; i++) heard += ear.feed(0);
    CHECK(heard <= GUESS_RUN + 2);
  }
  {  // 이어져야 하는 길이는 고를 수 있다: 1 이면 덩어리 하나로 정한다(예전 판)
    SpeechEar one(GUESS_MIN, GUESS_RATIO, 1);
    one.feed(60);
    CHECK(one.feed(GUESS_MIN) && !one.feed(60));
    SpeechEar zero(GUESS_MIN, GUESS_RATIO, 0);  // 0 은 1 로 읽는다
    zero.feed(60);
    CHECK(zero.feed(GUESS_MIN));
  }
}

// 기록용 셈(D70 의 문턱을 고를 자료): 한 구간에서 "크다"였던 틱의 수와 가장 길게 이어진 길이.
static void test_loud_stats() {
  LoudStats st;
  CHECK(st.count == 0 && st.longest == 0);
  const int pattern[] = {0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0};
  for (int v : pattern) st.feed(v != 0);
  CHECK(st.count == 6 && st.longest == 3);
  st.reset();
  CHECK(st.count == 0 && st.longest == 0);
  for (int i = 0; i < 5; i++) st.feed(true);
  CHECK(st.count == 5 && st.longest == 5);
  st.feed(false);
  st.feed(true);
  CHECK(st.count == 6 && st.longest == 5);
  // 구간을 넘어 이어지던 것은 새 구간에서 처음부터 센다
  st.reset();
  st.feed(true);
  CHECK(st.count == 1 && st.longest == 1);
  // 아주 길어도 넘치지 않는다
  LoudStats big;
  for (int i = 0; i < 70000; i++) big.feed(true);
  CHECK(big.count == 65535 && big.longest == 65535);
}

// 같은 종류의 기록 줄이 기기 속 기록(고리)을 채우지 않게 하는 예산(logic/log_budget.h).
static void test_log_budget() {
  LogBudget b(3, 10000);  // 10초에 세 줄까지
  CHECK(b.allow(1000) && b.allow(1100) && b.allow(1200));
  CHECK(!b.allow(1300) && !b.allow(5000) && !b.allow(10999));
  CHECK(b.allow(11000));            // 창이 지나면 다시 적는다
  CHECK(b.takeSkipped() == 3);      // 그동안 못 적은 수를 함께 알린다
  CHECK(b.takeSkipped() == 0);      // 읽으면 지워진다
  CHECK(b.allow(11100) && b.allow(11200) && !b.allow(11300));
  // 한참 조용하다가(창이 여러 번 지난 뒤) 다시 시작해도 예산은 한 창의 것이다
  CHECK(b.allow(500000) && b.allow(500001) && b.allow(500002) && !b.allow(500003));
  // 못 적은 수는 넘치지 않는다
  LogBudget c(1, 1000);
  c.allow(0);
  for (int i = 0; i < 70000; i++) c.allow(1);
  CHECK(c.takeSkipped() == 65535);
  // 시계가 한 바퀴 도는 때를 걸쳐도 창은 제 길이다
  LogBudget w(2, 10000);
  const uint32_t t0 = 0xFFFFFFFFu - 3000;
  CHECK(w.allow(t0) && w.allow(t0 + 1000) && !w.allow(t0 + 5000));  // t0+5000 은 한 바퀴 돈 뒤다
  CHECK(!w.allow(t0 + 9999) && w.allow(t0 + 10000));
}

// vibe:time 이 받는 날짜: 달마다의 날 수까지 본다(2월 31일 같은 날짜를 시계 칩에 쓰지 않는다).
static void test_calendar() {
  CHECK(daysInMonth(2026, 1) == 31 && daysInMonth(2026, 4) == 30 && daysInMonth(2026, 12) == 31);
  CHECK(daysInMonth(2026, 2) == 28 && daysInMonth(2028, 2) == 29 && daysInMonth(2100, 2) == 28 && daysInMonth(2000, 2) == 29);
  CHECK(daysInMonth(2026, 0) == 0 && daysInMonth(2026, 13) == 0);
  CHECK(validDateTime(2026, 10, 11, 16, 30, 0));
  CHECK(validDateTime(2028, 2, 29, 23, 59, 59));
  CHECK(!validDateTime(2026, 2, 29, 0, 0, 0) && !validDateTime(2026, 2, 31, 0, 0, 0) && !validDateTime(2026, 4, 31, 0, 0, 0));
  CHECK(!validDateTime(2026, 0, 1, 0, 0, 0) && !validDateTime(2026, 13, 1, 0, 0, 0) && !validDateTime(2026, 1, 0, 0, 0, 0));
  CHECK(!validDateTime(2026, 1, 1, 24, 0, 0) && !validDateTime(2026, 1, 1, 0, 60, 0) && !validDateTime(2026, 1, 1, 0, 0, 60));
  CHECK(!validDateTime(2025, 12, 31, 0, 0, 0) && !validDateTime(2100, 1, 1, 0, 0, 0));  // 받는 해: 2026~2099
  CHECK(!validDateTime(2026, -1, 1, 0, 0, 0) && !validDateTime(2026, 1, 1, -1, 0, 0));
}

// D68 선 너머에 컴퓨터가 있는가: 박자의 번호가 바뀌는 동안만 "있다".
// 기기 속 기록: 선을 뽑은 6분 동안 기기는 USB 로 연결돼 있다고 믿었다.
static void test_host_pulse() {
  HostPulse p(150);
  CHECK(!p.alive(0));  // 아직 한 번도 보지 못했다
  uint32_t now = 1000;
  uint16_t frame = 100;
  for (int i = 0; i < 500; i++) {  // 연결돼 있다: 10ms 마다 읽을 때마다 번호가 10씩 는다
    p.feed(frame, now);
    CHECK(p.alive(now));
    frame = (uint16_t)((frame + 10) & 0x3FFF);  // 번호는 14비트에서 한 바퀴 돈다
    now += 10;
  }
  // 선을 뽑았다: 번호가 멎는다 → 150ms 뒤 "없다".
  const uint32_t pulled = now;
  for (; now < pulled + 140; now += 10) {
    p.feed(frame, now);
    CHECK(p.alive(now));
  }
  for (; now < pulled + 5000; now += 10) {
    p.feed(frame, now);
    if (now >= pulled + 150) CHECK(!p.alive(now));
  }
  // 다시 꽂았다: 번호가 바뀌는 순간부터 "있다".
  p.feed((uint16_t)(frame + 1), now);
  CHECK(p.alive(now));
  // loop 가 잠깐 막혀(키를 보내는 42ms) 읽지 못해도, 그 사이 번호가 바뀌었으면 "있다".
  now += 60;
  p.feed((uint16_t)(frame + 61), now);
  CHECK(p.alive(now) && p.alive(now + 100) && !p.alive(now + 150));
  // 시계가 한 바퀴 도는 때를 걸쳐도 맞다.
  HostPulse q(150);
  uint32_t t = 0xFFFFFF00u;
  uint16_t f = 0;
  for (int i = 0; i < 100; i++) {
    q.feed(f++, t);
    CHECK(q.alive(t));
    t += 10;  // 0 을 넘어간다
  }
  CHECK(q.alive(t + 100) && !q.alive(t + 200));
}

// 기기 속 기록(D67): 가장 최근 것만 남고, 밀려나 잘린 첫 줄은 내주지 않는다.
static void test_hist_ring() {
  HistRing<32> h;
  char out[64] = {};
  CHECK(h.size() == 0 && h.copy(out, sizeof(out)) == 0);
  h.add("one\n", 4);
  h.add("two\n", 4);
  size_t n = h.copy(out, sizeof(out));
  CHECK(n == 8 && memcmp(out, "one\ntwo\n", 8) == 0);
  // 넘치면 오래된 것부터 밀려난다. 밀려난 뒤의 첫 줄은 앞이 잘렸을 수 있으므로 빼고, 그다음 줄부터 준다.
  h.add("three three three\n", 18);
  h.add("four four\n", 10);  // 지금까지 36바이트 → 앞의 4바이트("one\n")가 밀려났다
  n = h.copy(out, sizeof(out));
  CHECK(n == 28 && memcmp(out, "three three three\nfour four\n", 28) == 0);  // 맨 앞의 "two\n" 은 뺀다
  h.add("5\n", 2);  // 2바이트가 더 밀려나 맨 앞이 "o\n" 으로 잘린다 → 그 조각은 내주지 않는다
  n = h.copy(out, sizeof(out));
  CHECK(n == 30 && memcmp(out, "three three three\nfour four\n5\n", 30) == 0);
  CHECK(h.size() == 32);
  // 받는 쪽 자리가 모자라면 그만큼만
  CHECK(h.copy(out, 4) == 4 && memcmp(out, "thre", 4) == 0);
  // 고리보다 긴 글은 뒷부분만 남는다
  HistRing<8> t;
  t.add("0123456789\n", 11);
  CHECK(t.size() == 8);
  t.add("ab\n", 3);
  n = t.copy(out, sizeof(out));
  CHECK(n == 3 && memcmp(out, "ab\n", 3) == 0);
  // 수없이 넣어도 온전한 줄만 나온다
  HistRing<64> big;
  for (int i = 0; i < 1000; i++) {
    char line[24];
    const int len = snprintf(line, sizeof(line), "[ev] %d t=%d\n", i % 9, i * 37);
    big.add(line, (size_t)len);
    n = big.copy(out, sizeof(out));
    CHECK(n > 0 && out[n - 1] == '\n' && out[0] == '[');
  }
}

static void test_state_mic_opened_by_someone_else() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  s.setMicLive(true);  // 키보드로 ⌘D 를 눌렀거나 다른 프로그램이 열었다
  CHECK(poll(s, 10, f) && f.face == Face::Talk && f.micLive);
  s.setMicLive(false);
  CHECK(poll(s, 20, f) && f.face == Face::Hi);
}

static void test_state_actions_show_briefly() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  const struct {
    Ev   ev;
    Face face;
  } cases[] = {{Ev::Confirm, Face::Ok},     {Ev::Cancel, Face::Nope},     {Ev::Up, Face::Up},
               {Ev::Down, Face::Down},      {Ev::WindowNext, Face::Next}, {Ev::WindowPrev, Face::Back}};
  uint32_t t = 100;
  for (const auto& c : cases) {
    s.onSent(c.ev, t);
    CHECK(poll(s, t, f) && f.face == c.face);
    CHECK(!poll(s, t + TouchState::ACTION_SHOW_MS - 10, f));
    CHECK(poll(s, t + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Hi);
    t += 5000;
  }
  // 키를 다 보낸 뒤의 시각이 틱 시작 시각보다 뒤여도 곧바로 사라지지 않는다(clock.h 의 함정).
  s.onSent(Ev::Confirm, t + 42);
  CHECK(poll(s, t, f) && f.face == Face::Ok);
  CHECK(!poll(s, t + 10, f));
}

static void test_state_enter_while_listening() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  s.setMicLive(true);
  poll(s, 0, f);
  s.onSent(Ev::Confirm, 100);  // 말하다가 확인: 컴퓨터가 녹음을 끄고 보낸다
  CHECK(poll(s, 100, f) && f.face == Face::Ok);
  s.setMicLive(false);
  CHECK(poll(s, 300, f) && f.face == Face::Ok && !f.micLive);  // 마이크 배지만 바뀐다
  CHECK(poll(s, 100 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Hi);
  // 말하기를 켠 직후(마이크가 아직 안 열림) 확인을 누르면 듣는 중이 남지 않는다.
  s.onSent(Ev::TalkTap, 10000);
  s.onSent(Ev::Confirm, 10500);
  poll(s, 10500, f);
  CHECK(poll(s, 10500 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Hi && !f.micLive);
  CHECK(!poll(s, 10000 + 3100, f));
}

static void test_state_arrow_while_listening_returns_to_talk() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  s.setMicLive(true);
  poll(s, 0, f);
  s.onSent(Ev::Down, 100);
  CHECK(poll(s, 100, f) && f.face == Face::Down);
  CHECK(poll(s, 100 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Talk);
}

static void test_state_link() {
  TouchState s;
  Frame f{};
  poll(s, 0, f);
  s.onNotSent(100);  // 꽂혀 있지 않을 때 누름
  CHECK(poll(s, 100, f) && f.face == Face::NoLink);
  s.setReady(true);  // 연결되면 바로 내린다
  CHECK(poll(s, 200, f) && f.face == Face::Hi);
  // 말하기를 켠 직후에 끊기면 듣는 중이 남지 않는다.
  s.onSent(Ev::TalkTap, 1000);
  poll(s, 1000, f);
  s.setReady(false);
  CHECK(poll(s, 1100, f) && f.face == Face::Plug && !f.micLive);
  CHECK(!poll(s, 1000 + 3100, f));
}

// 컴퓨터가 기기를 알아보고도 쓰기 시작하지 않으면(맥북의 "액세서리 연결 허용" 창) ALLOW? 로 알린다.
static void test_state_allow_hint() {
  TouchState s;
  Frame f{};
  poll(s, 0, f);
  CHECK(f.face == Face::Plug);
  s.setLink(LinkState::Connecting, 1000);  // 컴퓨터가 설명자를 읽어 갔다
  // 평소에는 곧 쓰기 시작하므로 ALLOW? 를 바로 띄우지 않는다. 얼굴은 그대로이고 상태 줄만 WAIT 로 바뀐다(D54).
  Dirty d = 0;
  CHECK(s.poll(1000, f, d) && d == DIRTY_STATUS);
  CHECK(f.face == Face::Plug && f.status.link == LinkState::Connecting);
  CHECK(!poll(s, 1000 + TouchState::ALLOW_AFTER_MS - 10, f));
  CHECK(poll(s, 1000 + TouchState::ALLOW_AFTER_MS, f) && f.face == Face::Allow);
  s.setLink(LinkState::Connecting, 9000);  // 같은 상태가 이어지면 시계를 다시 돌리지 않는다
  CHECK(!poll(s, 9000, f));
  s.onNotSent(9500);                       // 이때 누르면 NO LINK
  CHECK(poll(s, 9500, f) && f.face == Face::NoLink);
  CHECK(poll(s, 9500 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Allow);
  s.setLink(LinkState::Ready, 20000);      // 사람이 허용을 눌렀다
  CHECK(poll(s, 20000, f) && f.face == Face::Hi);
  s.setLink(LinkState::Searching, 30000);  // 뽑았다
  CHECK(poll(s, 30000, f) && f.face == Face::Plug);
  // 평소의 꽂기: 알아본 뒤 0.5초 만에 쓰기 시작 → ALLOW? 가 스치지 않는다.
  s.setLink(LinkState::Connecting, 40000);
  CHECK(s.poll(40400, f, d) && d == DIRTY_STATUS && f.face == Face::Plug);  // 얼굴은 그대로(상태 줄만)
  s.setLink(LinkState::Ready, 40500);
  CHECK(poll(s, 40500, f) && f.face == Face::Hi);
}

static void test_state_update_prompt() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  s.onSent(Ev::Up, 100);  // 다른 장면이 떠 있어도
  poll(s, 100, f);
  CHECK(s.askUpdate(200));
  CHECK(s.updateAsking(200));
  // 뜨자마자 누른 확인은 "예"로 받지 않는다(다른 일로 누르려던 손가락일 수 있다).
  CHECK(!s.updateArmed(200) && !s.updateArmed(200 + TouchState::UPDATE_ARM_MS - 10));
  CHECK(s.updateArmed(200 + TouchState::UPDATE_ARM_MS));
  CHECK(poll(s, 200, f) && f.face == Face::Update);  // UPDATE? 가 앞선다
  s.onSent(Ev::Down, 250);  // 묻는 동안 다른 동작 장면이 생겨도 가려지지 않는다
  CHECK(!poll(s, 250, f));
  s.setMicLive(true);
  CHECK(poll(s, 300, f) && f.face == Face::Update && f.micLive);  // 듣는 중이어도 그대로
  s.setMicLive(false);
  poll(s, 310, f);
  CHECK(!poll(s, 200 + TouchState::UPDATE_WAIT_MS - 10, f));
  // 답이 없으면 저절로 거둔다.
  CHECK(poll(s, 200 + TouchState::UPDATE_WAIT_MS, f) && f.face == Face::Hi);
  CHECK(!s.updateAsking(200 + TouchState::UPDATE_WAIT_MS));
  CHECK(!s.updateArmed(200 + TouchState::UPDATE_WAIT_MS));  // 거둔 뒤에는 "예"도 없다
  // 취소하면 곧바로 거둔다.
  CHECK(s.askUpdate(50000));
  poll(s, 50000, f);
  s.cancelUpdate(50005);
  CHECK(!s.updateAsking(50010));
  CHECK(poll(s, 50010, f) && f.face == Face::Hi);
  // 거절한 직후에는 다시 묻지 않는다(요청을 퍼부어 화면을 묶어 두지 못하게).
  CHECK(!s.askUpdate(50100));
  CHECK(!s.askUpdate(50005 + TouchState::UPDATE_COOLDOWN_MS - 10));
  CHECK(s.askUpdate(50005 + TouchState::UPDATE_COOLDOWN_MS));
  // 묻는 중에 온 요청은 무시한다: 시계가 다시 돌지 않아, 처음 뜬 때부터 1.5초 뒤면 확인을 받는다.
  const uint32_t t0 = 50005 + TouchState::UPDATE_COOLDOWN_MS;
  for (uint32_t t = t0 + 100; t < t0 + 3000; t += 100) CHECK(!s.askUpdate(t));
  CHECK(s.updateArmed(t0 + TouchState::UPDATE_ARM_MS));
  // 그리고 요청을 계속 보내도 20초 뒤에는 저절로 거둔다.
  for (uint32_t t = t0 + 3000; t < t0 + TouchState::UPDATE_WAIT_MS; t += 500) s.askUpdate(t);
  CHECK(!s.updateAsking(t0 + TouchState::UPDATE_WAIT_MS));
  // 시간 초과로 끝난 뒤에도 뜸을 둔다.
  CHECK(!s.askUpdate(t0 + TouchState::UPDATE_WAIT_MS + 100));
  CHECK(s.askUpdate(t0 + TouchState::UPDATE_WAIT_MS + TouchState::UPDATE_COOLDOWN_MS));
}

// 기기의 loop 를 UPDATE? 물음까지 흉내 낸다(main.cpp 의 tick 과 같은 순서):
// 물음 상태를 본다 → 터치 → 사건 → 갈래(키로 내보냄 / 삼킴 / 예 / 아니오).
struct UpdateRig : Rig {
  TouchState state;
  UpdateGate gate;
  std::vector<Ev> keys;  // 키로 나간 사건
  int yes = 0, no = 0;

  void tickU(const TouchPoint* pts, int n) {
    if (gate.begin(now, state.updateAsking(now), state.updateArmed(now), touch.touching())) logic.reset();
    touch.update(pts, n, now);
    const uint8_t m = touch.pressed();
    gate.press(now, (m & bit(Zone::Ok)) != 0, (m & (uint8_t)~bit(Zone::Ok)) != 0);  // D75
    RawInput in{};
    touch.fill(in);
    Ev out[InputLogic::MAX_EVENTS];
    const int k = logic.update(in, now, out);
    for (int i = 0; i < k; i++) {
      switch (gate.judge(out[i])) {
        case UpdateVerdict::Pass:    keys.push_back(out[i]); break;
        case UpdateVerdict::Swallow: break;
        case UpdateVerdict::Yes:     ++yes; break;
        case UpdateVerdict::No:      ++no; state.cancelUpdate(now); break;
      }
    }
    now += 10;
  }
  void idleU(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) tickU(nullptr, 0);
  }
  void holdU(Zone z, uint32_t ms) {
    const Rect c = center(zoneRect(z));
    const TouchPoint p{0, c.x, c.y};
    for (uint32_t t = 0; t < ms; t += 10) tickU(&p, 1);
  }
  int keyCount(Ev e) const {
    int n = 0;
    for (Ev v : keys) n += (v == e);
    return n;
  }
};

static void test_update_ok_after_armed_is_yes() {
  UpdateRig r;
  r.idleU(100);
  CHECK(r.state.askUpdate(r.now));
  r.idleU(TouchState::UPDATE_ARM_MS + 100);  // 화면을 보고 알아챌 시간이 지났다
  r.holdU(Zone::Ok, UpdateGate::HOLD_MS + 100);  // 꾹 눌렀다 뗀다(D75)
  r.idleU(100);
  CHECK(r.yes == 1);
  CHECK(r.keys.empty());  // 묻는 동안에는 아무것도 키로 나가지 않는다
}

// D75 "예"는 확인을 HOLD_MS 넘게 꾹 눌렀다 떼야 한다. 평소처럼 톡 친 확인은 "예"가 아니다.
//   까닭(보안 점검): 확인(✓)은 Enter 로 하루에도 수십 번 누르는 자리다. 같은 컴퓨터의 프로그램이 알림 소리로
//   사람을 불러 놓고 곧바로 물음을 띄우면, 습관대로 톡 친 ✓ 가 "펌웨어를 바꿔도 좋다"가 된다 — 그 뒤 그 프로그램은
//   이 기기에 무엇이든 구울 수 있다(무엇이든 치는 키보드).
static void test_update_yes_needs_a_long_press() {
  const uint32_t hold = UpdateGate::HOLD_MS;
  // 톡(길이를 바꿔 가며): 어느 것도 "예"가 아니고, Enter 로도 나가지 않고, 물음은 그대로 떠 있다
  for (uint32_t ms = 20; ms + 60 <= hold; ms += 40) {
    UpdateRig r;
    r.idleU(100);
    CHECK(r.state.askUpdate(r.now));
    r.idleU(TouchState::UPDATE_ARM_MS + 100);
    r.holdU(Zone::Ok, ms);
    r.idleU(200);
    CHECK(r.yes == 0 && r.keys.empty() && r.state.updateAsking(r.now));
    r.holdU(Zone::Ok, 80);  // 잇달아 톡 쳐도(답답해서 두드려도) 마찬가지다
    r.idleU(100);
    r.holdU(Zone::Ok, 80);
    r.idleU(200);
    CHECK(r.yes == 0 && r.keys.empty());
    r.holdU(Zone::Ok, hold + 60);  // 그 뒤에 꾹 누르면 "예"
    r.idleU(200);
    CHECK(r.yes == 1);
  }
  {  // 꾹 누르는 도중에 다른 자리가 함께 눌리면(쥔 손이 닿았다) 그 누름은 "예"가 아니다
    UpdateRig r;
    r.idleU(100);
    CHECK(r.state.askUpdate(r.now));
    r.idleU(TouchState::UPDATE_ARM_MS + 100);
    const Rect ok = center(zoneRect(Zone::Ok)), up = center(zoneRect(Zone::Up));
    const TouchPoint one{0, ok.x, ok.y};
    const TouchPoint two[2] = {{0, ok.x, ok.y}, {1, up.x, up.y}};
    for (uint32_t t = 0; t < hold / 2; t += 10) r.tickU(&one, 1);
    for (int i = 0; i < 10; i++) r.tickU(two, 2);
    for (uint32_t t = 0; t < hold + 200; t += 10) r.tickU(&one, 1);
    r.idleU(300);
    CHECK(r.yes == 0 && r.keys.empty());
    r.holdU(Zone::Ok, hold + 60);  // 모두 뗀 뒤 새로 꾹 누르면 "예"
    r.idleU(200);
    CHECK(r.yes == 1);
  }
  {  // 터치 칩이 잠깐 놓쳐도(두 틱) 한 번의 꾹이다
    UpdateRig r;
    r.idleU(100);
    CHECK(r.state.askUpdate(r.now));
    r.idleU(TouchState::UPDATE_ARM_MS + 100);
    r.holdU(Zone::Ok, hold / 2);
    r.idleU(20);
    r.holdU(Zone::Ok, hold / 2 + 100);
    r.idleU(200);
    CHECK(r.yes == 1);
  }
  {  // 물음이 없을 때의 꾹 누른 확인은 그냥 Enter 다(달라지지 않았다)
    UpdateRig r;
    r.idleU(100);
    r.holdU(Zone::Ok, hold + 500);
    r.idleU(200);
    CHECK(r.yes == 0 && r.keyCount(Ev::Confirm) == 1);
  }
  {  // 경계: 누른 길이를 한 틱씩 늘려 가면 어느 길이부터 "예"가 되고, 그보다 짧은 것은 하나도 "예"가 아니다.
     // 그 길이는 HOLD_MS 언저리다(터치가 뗌을 몇 틱 늦게 알아채는 만큼의 차이만 있다).
    int firstYes = -1;
    bool gapAfterYes = false;
    for (int ms = (int)hold - 150; ms <= (int)hold + 150; ms += 10) {
      UpdateRig r;
      r.idleU(100);
      CHECK(r.state.askUpdate(r.now));
      r.idleU(TouchState::UPDATE_ARM_MS + 100);
      r.holdU(Zone::Ok, (uint32_t)ms);
      r.idleU(200);
      CHECK(r.keys.empty());
      if (r.yes == 1 && firstYes < 0) firstYes = ms;
      if (r.yes == 0 && firstYes >= 0) gapAfterYes = true;
    }
    CHECK(firstYes >= (int)hold - 60 && firstYes <= (int)hold + 20);
    CHECK(!gapAfterYes);
  }
  {  // 꾹 누르고 있는 사이에 물음이 거둬졌다(선이 뽑혔다 — main 이 cancelUpdate 를 부른다): "예"도 Enter 도 아니다
    UpdateRig r;
    r.idleU(100);
    CHECK(r.state.askUpdate(r.now));
    r.idleU(TouchState::UPDATE_ARM_MS + 100);
    r.holdU(Zone::Ok, hold / 2);
    r.state.cancelUpdate(r.now);
    r.holdU(Zone::Ok, hold);
    r.idleU(300);
    CHECK(r.yes == 0 && r.keys.empty() && !r.state.updateAsking(r.now));
  }
}

// D75 × D69: 뜸(누름을 받지 않는 때) 동안 꾹 누른 확인은 "예"가 아니다 — main 과 같은 순서로(뜸이 닫혀 있으면 입력 판정을
// 비우고 사건을 버린다).
struct UpdateQuietRig : UpdateRig {
  QuietGate quiet;
  void tickUQ(const TouchPoint* pts, int n) {
    const bool drain = gate.begin(now, state.updateAsking(now), state.updateArmed(now), touch.touching());
    const bool closed = quiet.closed(now, touch.touching());
    if (drain || closed) logic.reset();
    touch.update(pts, n, now);
    const uint8_t m = touch.pressed();
    gate.press(now, (m & bit(Zone::Ok)) != 0, (m & (uint8_t)~bit(Zone::Ok)) != 0);
    RawInput in{};
    touch.fill(in);
    Ev out[InputLogic::MAX_EVENTS];
    int k = logic.update(in, now, out);
    if (closed) k = 0;
    for (int i = 0; i < k; i++) {
      switch (gate.judge(out[i])) {
        case UpdateVerdict::Pass:    keys.push_back(out[i]); break;
        case UpdateVerdict::Swallow: break;
        case UpdateVerdict::Yes:     ++yes; break;
        case UpdateVerdict::No:      ++no; state.cancelUpdate(now); break;
      }
    }
    now += 10;
  }
  void holdUQ(Zone z, uint32_t ms) {
    const Rect c = center(zoneRect(z));
    const TouchPoint p{0, c.x, c.y};
    for (uint32_t t = 0; t < ms; t += 10) tickUQ(&p, 1);
  }
  void idleUQ(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) tickUQ(nullptr, 0);
  }
};

static void test_update_long_press_respects_quiet() {
  const uint32_t hold = UpdateGate::HOLD_MS;
  UpdateQuietRig r;
  r.idleUQ(100);
  CHECK(r.state.askUpdate(r.now));
  r.idleUQ(TouchState::UPDATE_ARM_MS + 100);
  r.quiet.arm(r.now, 3000, 1000);     // 뜸이 걸렸다(예: 기기가 막 켜졌다)
  r.holdUQ(Zone::Ok, hold + 300);     // 그동안 꾹 눌러도
  r.idleUQ(300);
  CHECK(r.yes == 0 && r.keys.empty());
  // 뜸에 걸친 채 누르기 시작해, 뜸이 풀린 뒤에 떼어도 아니다(뜸은 손을 떼야 풀린다)
  r.holdUQ(Zone::Ok, 3500);
  r.idleUQ(1500);
  CHECK(r.yes == 0 && r.keys.empty());
  CHECK(!r.quiet.isClosed() && r.state.updateAsking(r.now));
  r.holdUQ(Zone::Ok, hold + 100);     // 풀린 뒤 새로 꾹 누르면 "예"
  r.idleUQ(200);
  CHECK(r.yes == 1);
}

static void test_update_ok_before_armed_is_swallowed() {
  UpdateRig r;
  r.idleU(100);
  CHECK(r.state.askUpdate(r.now));
  r.idleU(200);
  r.holdU(Zone::Ok, 80);  // 뜨자마자 눌렀다 뗐다: "예"도 Enter 도 아니다
  r.idleU(100);
  CHECK(r.yes == 0 && r.keys.empty());
  CHECK(r.state.updateAsking(r.now));  // 물음은 그대로 떠 있다
}

// 지적(코덱스): 물음이 뜨기 전부터 누르고 있던 확인을 나중에 떼면 "예"가 됐다.
static void test_update_ok_held_from_before_is_not_yes() {
  UpdateRig r;
  r.idleU(100);
  r.holdU(Zone::Ok, 200);  // 다른 일로 확인을 누르고 있다
  CHECK(r.state.askUpdate(r.now));  // 그 사이에 물음이 떴다
  r.holdU(Zone::Ok, TouchState::UPDATE_ARM_MS + 500);  // 계속 누른 채 1.5초를 넘긴다
  r.idleU(100);  // 뗀다
  CHECK(r.yes == 0);
  CHECK(r.keys.empty());  // Enter 로도 나가지 않는다
  CHECK(r.state.updateAsking(r.now));  // 물음은 그대로다
  // 손을 뗐다가 새로 꾹 누르면 "예"다.
  r.holdU(Zone::Ok, UpdateGate::HOLD_MS + 100);
  r.idleU(100);
  CHECK(r.yes == 1);
}

// 물음이 뜬 뒤, 확인을 받기 전에 누르기 시작해 1.5초를 넘겨 뗀 것도 "예"가 아니다.
// 화면을 보고 알아챌 시간(UPDATE_ARM_MS)이 지난 뒤에 새로 누른 것만 받는다.
static void test_update_ok_pressed_before_armed_held_past_is_not_yes() {
  UpdateRig r;
  r.idleU(100);
  CHECK(r.state.askUpdate(r.now));
  r.idleU(300);
  r.holdU(Zone::Ok, TouchState::UPDATE_ARM_MS);  // 0.3초에 눌러 1.8초에 뗀다
  r.idleU(100);
  CHECK(r.yes == 0 && r.keys.empty());
}

static void test_update_nope_cancels_and_leaks_no_key() {
  UpdateRig r;
  r.idleU(100);
  CHECK(r.state.askUpdate(r.now));
  r.idleU(300);
  r.holdU(Zone::Nope, 80);  // 취소는 뜨자마자 눌러도 받는다
  r.idleU(100);
  CHECK(r.no == 1 && r.yes == 0);
  CHECK(!r.state.updateAsking(r.now));
  CHECK(r.keys.empty());  // Esc 로 나가지 않는다
  // 물음이 끝나고 뜸(QUIET_MS)이 지난 뒤에는 평소처럼 키가 나간다.
  r.idleU(UpdateGate::QUIET_MS + 100);
  r.holdU(Zone::Ok, 80);
  r.idleU(100);
  CHECK(r.keyCount(Ev::Confirm) == 1 && r.yes == 0);
}

// 답하려고 누른 확인을 물음이 시간 초과로 사라진 뒤에 떼어도 Enter 가 나가지 않는다.
static void test_update_ok_released_after_timeout_is_no_key() {
  UpdateRig r;
  r.idleU(100);
  CHECK(r.state.askUpdate(r.now));
  r.idleU(TouchState::UPDATE_WAIT_MS - 200);
  r.holdU(Zone::Ok, 400);  // 누르고 있는 사이에 20초가 지난다
  r.idleU(100);
  CHECK(!r.state.updateAsking(r.now));
  CHECK(r.yes == 0 && r.keys.empty());
}

// 물음의 경계(확인을 받기 시작하는 순간, 물음이 사라지는 순간) 앞뒤로 누르는 때와 길이를 한 틱씩
// 바꿔 가며 모두 해 본다. 터치와 버튼의 떨림 거르기 때문에 누름은 닿은 지 몇 틱 뒤에야 인식되는데,
// 그 "인식되는 중"인 누름이 경계를 빠져나가면 안 된다(코덱스 검토).
static void test_update_edges_swept() {
  // (1) 확인을 받기 시작하는 순간: 그 전에 닿은 손가락은 언제 떼든 "예"가 아니다.
  for (int before = 1; before <= 12; before++) {      // 경계보다 몇 틱 먼저 닿았는가
    for (int after = 0; after <= 12; after++) {       // 경계 뒤로 몇 틱 더 눌렀는가
      UpdateRig r;
      r.idleU(100);
      CHECK(r.state.askUpdate(r.now));
      const uint32_t armAt = r.now + TouchState::UPDATE_ARM_MS;
      while (r.now + (uint32_t)before * 10 < armAt) r.tickU(nullptr, 0);
      r.holdU(Zone::Ok, (uint32_t)(before + after) * 10);
      r.idleU(300);
      CHECK(r.yes == 0);
      CHECK(r.keys.empty());
      // 손을 뗀 뒤 새로 꾹 누르면 "예"다(막힌 채로 남지 않는다).
      r.holdU(Zone::Ok, UpdateGate::HOLD_MS + 100);
      r.idleU(100);
      CHECK(r.yes == 1);
    }
  }
  // (2) 물음이 시간 초과로 사라지는 순간: 그 전에 닿은 손가락은 언제 떼든 키로 나가지 않는다.
  const Zone zones[] = {Zone::Ok, Zone::Nope, Zone::Talk, Zone::Up, Zone::Next};
  for (Zone z : zones) {
    for (int before = 1; before <= 12; before++) {
      for (int after = 0; after <= 40; after += 4) {
        UpdateRig r;
        r.idleU(100);
        CHECK(r.state.askUpdate(r.now));
        const uint32_t endAt = r.now + TouchState::UPDATE_WAIT_MS;
        while (r.now + (uint32_t)before * 10 < endAt) r.tickU(nullptr, 0);
        r.holdU(z, (uint32_t)(before + after) * 10);
        r.idleU(400);
        CHECK(r.keys.empty());
      }
    }
  }
  // (3) 물음이 사라진 직후(화면이 아직 바뀌는 중일 수 있다)에 닿은 손가락도 키로 나가지 않는다.
  for (Zone z : zones) {
    for (int late = 0; late <= 20; late += 2) {  // 사라진 뒤 몇 틱 만에 닿았는가(0.2초까지)
      UpdateRig r;
      r.idleU(100);
      CHECK(r.state.askUpdate(r.now));
      r.idleU(TouchState::UPDATE_WAIT_MS + (uint32_t)late * 10);
      r.holdU(z, 100);
      r.idleU(400);
      CHECK(r.keys.empty());
    }
  }
  // (4) 취소로 끝낸 뒤에도 같다: 취소를 뗀 직후에 닿은 확인은 Enter 로 나가지 않는다.
  for (int late = 0; late <= 20; late += 2) {
    UpdateRig r;
    r.idleU(100);
    CHECK(r.state.askUpdate(r.now));
    r.idleU(300);
    r.holdU(Zone::Nope, 80);
    while (r.no == 0 && r.now < 100000) r.tickU(nullptr, 0);
    CHECK(r.no == 1);
    for (int i = 0; i < late; i++) r.tickU(nullptr, 0);
    r.holdU(Zone::Ok, 100);
    r.idleU(400);
    CHECK(r.keys.empty() && r.yes == 0);
  }
  // (5) 그리고 넉넉히 지난 뒤에는 평소처럼 키가 나간다.
  {
    UpdateRig r;
    r.idleU(100);
    CHECK(r.state.askUpdate(r.now));
    r.idleU(TouchState::UPDATE_WAIT_MS + 1500);
    r.holdU(Zone::Ok, 80);
    r.idleU(100);
    CHECK(r.keyCount(Ev::Confirm) == 1);
  }
}

// 취소는 확인을 받기 시작하는 순간을 걸쳐 눌러도 받는다("아니오"는 언제나 통한다).
static void test_update_nope_across_arming_still_cancels() {
  for (int before = 1; before <= 12; before++) {
    for (int after = 0; after <= 12; after++) {
      if (before + after < 2) continue;  // 한 틱만 닿은 것은 튄 값으로 걸러진다(누름이 아니다)
      UpdateRig r;
      r.idleU(100);
      CHECK(r.state.askUpdate(r.now));
      const uint32_t armAt = r.now + TouchState::UPDATE_ARM_MS;
      while (r.now + (uint32_t)before * 10 < armAt) r.tickU(nullptr, 0);
      r.holdU(Zone::Nope, (uint32_t)(before + after) * 10);
      r.idleU(300);
      CHECK(r.no == 1 && r.yes == 0);
      CHECK(r.keys.empty());
    }
  }
}

// 갈래는 틱이 시작될 때의 상태로 정한다.
static void test_update_gate_verdicts() {
  UpdateGate g;
  CHECK(!g.begin(0, false, false, false));
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Pass);
  CHECK(g.judge(Ev::Up) == UpdateVerdict::Pass);
  CHECK(!g.begin(10, true, false, false));  // 물음이 떴다(아직 확인을 받지 않는다)
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Swallow);
  CHECK(g.judge(Ev::TalkTap) == UpdateVerdict::Swallow);
  CHECK(g.judge(Ev::Cancel) == UpdateVerdict::No);
  CHECK(!g.begin(20, true, true, false));  // 확인을 받기 시작한다(화면에 손가락이 없었다)
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Swallow);  // 꾹 누른 적이 없다(D75)
  g.press(20, true, false);                               // 확인만 누르기 시작
  g.press(20 + UpdateGate::HOLD_MS - 10, true, false);
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Swallow);  // 아직 모자라다
  g.press(20 + UpdateGate::HOLD_MS, true, false);
  g.press(20 + UpdateGate::HOLD_MS + 10, false, false);   // 뗐다
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Yes);
  g.press(5000, true, false);                             // 새로 누르면 앞의 꾹은 지워진다
  g.press(5010, false, false);
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Swallow);
  CHECK(g.judge(Ev::Cancel) == UpdateVerdict::No);
  CHECK(g.judge(Ev::Up) == UpdateVerdict::Swallow);  // "아니오" 뒤에 온 같은 틱의 사건도 삼킨다
  CHECK(g.begin(30, false, false, false));  // 물음이 끝난 순간: 눌려 있던 것은 무효
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Swallow);  // 끝난 직후에는 키로도 나가지 않는다
  CHECK(g.begin(30 + UpdateGate::QUIET_MS - 10, false, false, false));
  CHECK(!g.begin(30 + UpdateGate::QUIET_MS, false, false, false));
  CHECK(g.judge(Ev::Confirm) == UpdateVerdict::Pass);

  // 손가락이 닿은 채 확인을 받기 시작하면, 손가락을 뗄 때까지 "예"가 없다(취소는 받는다).
  UpdateGate h;
  h.begin(0, true, false, true);
  h.begin(10, true, true, true);
  CHECK(h.judge(Ev::Confirm) == UpdateVerdict::Swallow);
  CHECK(h.judge(Ev::Cancel) == UpdateVerdict::No);
  for (int i = 0; i < UpdateGate::LIFT_TICKS - 1; i++) {
    h.begin(20 + 10 * (uint32_t)i, true, true, false);
    CHECK(h.judge(Ev::Confirm) == UpdateVerdict::Swallow);
  }
  h.begin(100, true, true, false);
  CHECK(h.judge(Ev::Confirm) == UpdateVerdict::Swallow);
  h.press(100, true, false);
  h.press(100 + UpdateGate::HOLD_MS, true, false);
  h.press(110 + UpdateGate::HOLD_MS, false, false);
  CHECK(h.judge(Ev::Confirm) == UpdateVerdict::Yes);
  // 다른 자리가 함께 눌린 적이 있는 누름은 길어도 "예"가 아니다
  h.press(9000, true, false);
  h.press(9100, true, true);
  h.press(9200 + UpdateGate::HOLD_MS, true, false);
  h.press(9300 + UpdateGate::HOLD_MS, false, false);
  CHECK(h.judge(Ev::Confirm) == UpdateVerdict::Swallow);

  // 받지 않은 확인의 까닭을 읽을 수 있다(기기 속 기록에 남긴다 — "눌렀는데 안 된다"가 무엇 때문인지 가린다).
  UpdateGate w;
  w.begin(0, true, false, false);
  CHECK(!w.armed() && !w.stale() && !w.okLong() && !w.okSpoiled());
  w.begin(10, true, true, true);          // 확인을 받기 시작하는 순간 손가락이 닿아 있었다
  CHECK(w.armed() && w.stale());
  for (int i = 0; i < UpdateGate::LIFT_TICKS; i++) w.begin(20 + 10 * (uint32_t)i, true, true, false);
  CHECK(w.armed() && !w.stale());
  w.press(1000, true, false);
  w.press(1100, true, true);              // 다른 자리가 함께 눌렸다
  CHECK(w.okSpoiled() && !w.okLong() && w.okHeldMs(1100) == 100);
  w.press(1200 + UpdateGate::HOLD_MS, true, false);
  CHECK(w.okSpoiled() && !w.okLong());
  w.press(5000, false, false);
  w.press(6000, true, false);
  w.press(6000 + UpdateGate::HOLD_MS, true, false);
  CHECK(!w.okSpoiled() && w.okLong() && w.okHeldMs(6000 + UpdateGate::HOLD_MS) == UpdateGate::HOLD_MS);
  w.press(6010 + UpdateGate::HOLD_MS, false, false);  // 뗀 뒤에도 방금 누름의 길이를 읽을 수 있다
  CHECK(w.okLong() && w.okHeldMs(9999999) == UpdateGate::HOLD_MS);

  // 물음이 끝날 때 닿아 있던 손가락은 뜸이 지나도, 뗄 때까지 받지 않는다.
  UpdateGate k;
  k.begin(0, true, true, true);
  CHECK(k.begin(10, false, false, true));
  CHECK(k.begin(10 + UpdateGate::QUIET_MS + 500, false, false, true));
  CHECK(k.judge(Ev::Confirm) == UpdateVerdict::Swallow);
  for (int i = 0; i < UpdateGate::LIFT_TICKS; i++) k.begin(2000 + 10 * (uint32_t)i, false, false, false);
  CHECK(k.judge(Ev::Confirm) == UpdateVerdict::Pass);
}

// D69 누름을 받지 않는 뜸. main.cpp 의 순서를 흉내 낸다: 닫혀 있으면 입력 판정을 비우고 사건을 버린다.
struct QuietRig : Rig {
  QuietGate quiet;
  std::vector<Ev> keys;
  void tickQ(const TouchPoint* pts, int n) {
    const bool closed = quiet.closed(now, touch.touching());
    if (closed) logic.reset();
    touch.update(pts, n, now);
    RawInput in{};
    touch.fill(in);
    Ev out[InputLogic::MAX_EVENTS];
    const int k = logic.update(in, now, out);
    if (!closed) {
      for (int i = 0; i < k; i++) keys.push_back(out[i]);
    }
    now += 10;
  }
  void idleQ(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) tickQ(nullptr, 0);
  }
  void holdQ(Zone z, uint32_t ms) {
    const Rect c = center(zoneRect(z));
    const TouchPoint p{0, c.x, c.y};
    for (uint32_t t = 0; t < ms; t += 10) tickQ(&p, 1);
  }
  void until(uint32_t t) {
    while (now < t) tickQ(nullptr, 0);
  }
};

static void test_quiet_gate() {
  // (1) 실제로 있었던 일: 굽고 재시작한 기기에서 ✓ 를 2.33 · 2.87 · 3.40초에 눌렀다 → Enter 세 번.
  {
    QuietRig r;
    r.now = 600;                      // loop 가 돌기 시작하는 때
    r.quiet.arm(0, 3000, 1000);       // 켜질 때: 3초 + 손을 떼고 1초
    const uint32_t taps[] = {2330, 2870, 3400};
    for (uint32_t t : taps) {
      r.until(t);
      r.holdQ(Zone::Ok, 80);
    }
    r.idleQ(900);
    CHECK(r.keys.empty());            // 잇달아 두드리는 동안에는 받지 않는다
    CHECK(r.quiet.isClosed());        // 마지막 누름 뒤 1초가 안 됐다
    r.idleQ(300);
    CHECK(!r.quiet.isClosed());
    r.holdQ(Zone::Ok, 80);            // 손을 떼고 1초 뒤의 누름은 받는다
    r.idleQ(100);
    CHECK(r.keys.size() == 1 && r.keys[0] == Ev::Confirm);
  }
  // (2) 아무것도 누르지 않고 켜면 3초 뒤부터 받는다.
  {
    QuietRig r;
    r.now = 600;
    r.quiet.arm(0, 3000, 1000);
    r.until(2900);
    CHECK(r.quiet.isClosed());
    r.until(3050);
    CHECK(!r.quiet.isClosed());
    r.holdQ(Zone::Talk, 80);
    r.idleQ(100);
    CHECK(r.keys.size() == 1 && r.keys[0] == Ev::TalkTap);
  }
  // (3) 연결이 쓸 수 있게 되는 순간을 걸친 누름: 연결 전에 닿은 손가락을 연결 뒤에 떼어도 키가 아니다.
  //     닿는 때와 떼는 때를 한 틱씩 바꿔 가며 모두 본다(확인은 뗄 때, 말하기는 누를 때 나간다).
  const Zone zones[] = {Zone::Ok, Zone::Nope, Zone::Talk, Zone::Up, Zone::Next};
  for (Zone z : zones) {
    for (int before = 1; before <= 10; before++) {
      for (int after = 0; after <= 30; after += 3) {
        QuietRig r;
        r.idleQ(5000);                           // 켜진 지 오래됐다(연결은 없었다)
        r.holdQ(z, (uint32_t)before * 10);       // 연결되기 전에 닿았다
        r.keys.clear();                          // (그 전에 나온 사건은 기기에서는 "보내지 못함"이 된다 — 여기서는 보지 않는다)
        r.quiet.arm(r.now, 0, 500);              // 연결됐다
        r.holdQ(z, (uint32_t)after * 10);        // 그대로 더 누르다 뗀다
        r.idleQ(400);
        CHECK(r.keys.empty());
        r.idleQ(200);                            // 손을 뗀 지 0.5초가 넘었다
        r.holdQ(Zone::Ok, 80);
        r.idleQ(100);
        CHECK(r.keys.size() == 1 && r.keys[0] == Ev::Confirm);  // 그 뒤의 누름은 받는다(막힌 채 남지 않는다)
      }
    }
  }
  // (4) 꺼진 화면을 깨운 누름은 깨우기만 한다: 그 손가락을 뗀 뒤의 다음 누름부터 받는다.
  {
    QuietRig r;
    r.idleQ(5000);
    const Rect c = center(zoneRect(Zone::Ok));
    const TouchPoint p{0, c.x, c.y};
    r.tickQ(&p, 1);                  // 닿았다(화면이 꺼져 있었다)
    r.quiet.arm(r.now, 0, 300);      // main 이 다음 틱에 뜸을 건다
    r.holdQ(Zone::Ok, 150);
    r.idleQ(250);
    CHECK(r.keys.empty() && r.quiet.isClosed());
    r.idleQ(100);
    CHECK(!r.quiet.isClosed());
    r.holdQ(Zone::Ok, 80);
    r.idleQ(100);
    CHECK(r.keys.size() == 1);
  }
  // (6) 화면에 무언가 계속 닿아 있어도(쥔 손의 살이 가장자리에 닿음 · 터치 칩의 헛것) 영영 닫혀 있지 않는다:
  //     상한(QuietGate::STUCK_MS)이 지나면 연다. 그때 닿아 있던 것은 죽은 채로 남고, 새 누름은 받는다.
  {
    QuietRig r;
    r.idleQ(5000);
    const TouchPoint palm{1, 2, 300};              // 버튼 자리가 아닌 가장자리에 닿아 있는 살
    r.tickQ(&palm, 1);
    r.quiet.arm(r.now, 0, 500);                    // 연결이 바뀌었다
    for (int i = 0; i < 300; i++) r.tickQ(&palm, 1);   // 3초: 아직 닫혀 있다
    CHECK(r.quiet.isClosed());
    const Rect c = center(zoneRect(Zone::Ok));
    for (int i = 0; i < 300; i++) r.tickQ(&palm, 1);   // 6초: 상한을 넘겼다
    CHECK(!r.quiet.isClosed());
    CHECK(r.keys.empty());                         // 닿아 있던 것은 사건이 되지 않는다
    for (int i = 0; i < 10; i++) {                 // 살은 닿은 채, 엄지로 확인을 누른다(두 번째 손가락)
      const TouchPoint both[2] = {palm, {0, c.x, c.y}};
      r.tickQ(both, 2);
    }
    for (int i = 0; i < 20; i++) r.tickQ(&palm, 1);
    CHECK(r.keys.size() == 1 && r.keys[0] == Ev::Confirm);
    // 확인을 누른 채로 상한을 넘긴 경우: 그 누름은 뗄 때 키가 되지 않는다(닫힌 동안 무효가 됐다).
    QuietRig h;
    h.idleQ(5000);
    h.quiet.arm(h.now, 0, 500);
    h.holdQ(Zone::Ok, QuietGate::STUCK_MS + 1500);
    CHECK(!h.quiet.isClosed());
    h.idleQ(300);
    CHECK(h.keys.empty());
    h.holdQ(Zone::Ok, 80);
    h.idleQ(100);
    CHECK(h.keys.size() == 1);
  }
  // (5) 걸려 있는 뜸에 또 걸면 더 긴 쪽을 따른다(짧은 것이 긴 것을 줄이지 않는다).
  {
    QuietGate g;
    g.arm(0, 3000, 1000);
    g.arm(500, 0, 300);              // 켜지는 중에 연결이 됐다
    CHECK(g.closed(2900, false));
    CHECK(g.closed(2990, false));
    CHECK(!g.closed(3000, false));   // 3초(그동안 아무것도 닿지 않았다)
    g.arm(10000, 0, 500);
    CHECK(g.closed(10400, false) && !g.closed(10500, false));
    // 시계가 한 바퀴 도는 때를 걸쳐도 풀린다.
    g.arm(0xFFFFFF00u, 0, 500);
    CHECK(g.closed(0xFFFFFFF0u, false));
    CHECK(!g.closed(0x00000100u, false));
  }
}

// D71 보는 곳 톡을 받아도 되는 때(logic/view_tap_guard.h): 사람이 "지금의 표시를 보고" 친 것만 받는다.
static void test_view_tap_guard() {
  const uint32_t seen = ViewTapGuard::SEEN_MS;
  // 손가락 하나의 톡: start 부터 ms 동안 닿았다 떼고, 뗀 뒤 3틱째에 톡이 알려진다(TouchMapper 와 같다).
  // lit(t)·blocked(t) 는 그 시각의 화면·막힘. 반환: 그 톡을 받았는가
  struct Run {
    ViewTapGuard g;
    uint32_t now;
    bool accepted = false;
    explicit Run(uint32_t t0) : now(t0) {}
    void rest(uint32_t ms, bool lit, bool blocked = false) {
      for (uint32_t t = 0; t < ms; t += 10, now += 10) accepted |= g.feed(now, false, lit, blocked, false);
    }
    void touch(uint32_t ms, bool lit, bool blocked = false) {
      for (uint32_t t = 0; t < ms; t += 10, now += 10) accepted |= g.feed(now, true, lit, blocked, false);
    }
    bool lift(bool lit, bool blocked = false) {  // 손을 뗐다: 두 틱은 아직 닿은 것으로 보이고, 세 번째 틱에 톡이 알려진다
      accepted |= g.feed(now, true, lit, blocked, false); now += 10;
      accepted |= g.feed(now, true, lit, blocked, false); now += 10;
      const bool ok = g.feed(now, false, lit, blocked, true); now += 10;
      accepted = false;
      return ok;
    }
  };
  {  // 밝게 켜진 화면을 보고 친 톡은 받는다
    Run r(1000);
    r.rest(seen + 100, true);
    r.touch(80, true);
    CHECK(r.lift(true));
    r.rest(100, true);
    r.touch(80, true);
    CHECK(r.lift(true));  // 잇달아 쳐도
  }
  {  // 어두워졌거나 꺼진 화면에 닿은 손가락은 화면을 깨우는 것이다: 닿자마자 밝아져도 받지 않는다
    Run r(1000);
    r.rest(2000, false);
    r.touch(10, false);   // 닿은 그 틱에는 아직 어둡다(화면은 그 틱의 끝에 켜진다)
    r.touch(70, true);
    CHECK(!r.lift(true));
    r.rest(seen, true);   // 켜진 화면을 본 뒤의 톡은 받는다
    r.touch(80, true);
    CHECK(r.lift(true));
    // 실제로 일어나는 순서: 한참 밝게 켜져 있다가(= "봤다") 손대지 않아 어두워졌고, 그 화면을 친다 → 받지 않는다.
    // 어두워지는 순간 "봤다"를 지워야 한다(남겨 두면 어두운 화면의 톡이 그대로 통한다).
    r.rest(5000, true);
    r.rest(3000, false);
    r.touch(10, false);
    r.touch(70, true);
    CHECK(!r.lift(true));
    // 어두워졌다가 손대지 않고 다시 켜진 직후(SEEN_MS 안)의 톡도 받지 않는다
    r.rest(5000, true);
    r.rest(3000, false);
    r.rest(seen - 100, true);
    r.touch(80, true);
    CHECK(!r.lift(true));
  }
  {  // 손이 다가오는 것만으로 화면이 켜졌다(근접 센서·집어 듦): 켜진 지 SEEN_MS 가 안 됐으면 아직 본 것이 아니다
    for (uint32_t before = 0; before <= seen + 200; before += 50) {
      Run r(1000);
      r.rest(2000, false);
      r.rest(before, true);
      r.touch(80, true);
      CHECK(r.lift(true) == (before >= seen));
    }
  }
  {  // 닿아 있는 동안 한 번이라도 막혔으면(뜸 · 펌웨어 물음) 받지 않는다: 닿을 때, 중간에, 뗄 때
    for (int when = 0; when < 3; when++) {
      Run q(1000);
      q.rest(seen + 100, true);
      q.touch(30, true, when == 0);
      q.touch(30, true, when == 1);
      q.touch(30, true, false);
      CHECK(!q.lift(true, when == 2));
      q.rest(100, true);  // 막힘이 풀린 뒤의 새 톡은 받는다
      q.touch(80, true);
      CHECK(q.lift(true));
    }
    {  // 닿아 있는 동안에는 열려 있다가, 톡이 알려지는 바로 그 틱에 막혔다(그 틱에 연결이 바뀌어 뜸이 걸렸다)
      Run q(1000);
      q.rest(seen + 100, true);
      q.touch(80, true);
      q.g.feed(q.now, true, true, false, false); q.now += 10;
      q.g.feed(q.now, true, true, false, false); q.now += 10;
      CHECK(!q.g.feed(q.now, false, true, true, true));
      q.now += 10;
      q.rest(100, true);
      q.touch(80, true);
      CHECK(q.lift(true));
    }
    // 손가락이 없을 때의 막힘은 다음 톡에 남지 않는다
    Run r(1000);
    r.rest(seen + 100, true);
    r.rest(500, true, true);
    r.rest(50, true, false);
    r.touch(80, true);
    CHECK(r.lift(true));
  }
  {  // 톡이 알려지지 않으면 어떤 경우에도 "받았다"가 나오지 않는다
    Run r(1000);
    r.rest(seen + 100, true);
    r.touch(500, true);
    r.rest(500, true);
    CHECK(!r.accepted);
  }
  {  // 오래 켜 둔 채 시계가 한 바퀴 돌아도(49.7일) 받는다
    Run r(0xFFFFFFFFu - 3000);
    r.rest(seen + 100, true);
    for (int i = 0; i < 100; i++) {
      r.touch(80, true);
      CHECK(r.lift(true));
      r.rest(50, true);
    }
    CHECK(r.now < 0x80000000u);  // 정말 한 바퀴 돌았다
    // 켜진 시각을 들고 있다가 반 바퀴(24.8일) 뒤에 "아직 안 켜졌다"로 읽지 않는다
    ViewTapGuard g;
    uint32_t t = 5000;
    for (int i = 0; i < 60; i++, t += 10) g.feed(t, false, true, false, false);
    t += 0x80000000u;
    g.feed(t, true, true, false, false);
    g.feed(t + 10, true, true, false, false);
    CHECK(g.feed(t + 20, false, true, false, true));
  }
}

// D71 을 main 이 쓰는 순서대로: 뜸(D69) · 펌웨어 물음(D51) 동안의 톡, 어두워졌거나 꺼진 화면을 깨운 톡은
// 표시를 뒤집지 않는다. main 과 같은 부품(QuietGate · UpdateGate · ViewTapGuard)을 같은 순서로 부른다.
struct ViewRig : QuietRig {
  ViewTapGuard guard;
  UpdateGate   gate;
  int  flips = 0;
  enum { Bright, Dim, Off } light = Bright;
  bool asking = false;  // `Update firmware?` 가 떠 있다
  void tickV(const TouchPoint* pts, int n) {
    const bool drain = gate.begin(now, asking, asking, touch.touching());
    const bool closed = quiet.closed(now, touch.touching());
    if (drain || closed) logic.reset();
    touch.update(pts, n, now);
    if (light == Off && touch.touching()) quiet.arm(now, 0, 300);  // config.h 의 QUIET_WAKE_CALM_MS
    flips += guard.feed(now, touch.touching(), light == Bright, closed || gate.blocking(), touch.takeViewTap());
    RawInput in{};
    touch.fill(in);
    Ev out[InputLogic::MAX_EVENTS];
    const int k = logic.update(in, now, out);
    if (!closed) {
      for (int i = 0; i < k; i++) {
        if (gate.judge(out[i]) == UpdateVerdict::Pass) keys.push_back(out[i]);
      }
    }
    if (touch.touching()) light = Bright;  // 화면은 그 틱의 끝(ui::tick)에 켜진다
    now += 10;
  }
  void tapV(uint32_t ms, uint32_t restMs) {
    const TouchPoint p{0, 120, 50};
    for (uint32_t t = 0; t < ms; t += 10) tickV(&p, 1);
    for (uint32_t t = 0; t < restMs; t += 10) tickV(nullptr, 0);
  }
};

static void test_view_tap_respects_quiet() {
  const uint32_t seen = ViewTapGuard::SEEN_MS;
  {  // 평소: 톡 한 번에 한 번 뒤집는다. 키는 없다.
    ViewRig r;
    r.tapV(0, seen + 100);
    r.tapV(80, 200);
    r.tapV(80, 200);
    CHECK(r.flips == 2 && r.keys.empty());
  }
  {  // 꺼진 화면, 어두워지기만 한 화면을 깨운 톡은 화면만 켠다(아무리 짧게, 길게 쳐도). 그다음 톡부터 받는다.
    for (int dim = 0; dim < 2; dim++) {
      for (uint32_t ms = 20; ms <= 600; ms += 20) {
        ViewRig r;
        r.light = dim ? ViewRig::Dim : ViewRig::Off;
        r.tapV(ms, seen + 100);
        CHECK(r.flips == 0);
        r.tapV(80, 200);
        CHECK(r.flips == 1 && r.keys.empty());
      }
    }
  }
  {  // 쓰다가 손을 뗀 채 두어 어두워진(또는 꺼진) 화면: 깨우는 톡은 뒤집지 않고, 그다음 톡은 뒤집는다
    for (int dim = 0; dim < 2; dim++) {
      ViewRig r;
      r.tapV(0, seen + 100);
      r.tapV(80, 200);
      CHECK(r.flips == 1);
      r.tapV(0, 5000);
      r.light = dim ? ViewRig::Dim : ViewRig::Off;
      r.tapV(0, 1000);
      r.tapV(80, seen + 100);
      CHECK(r.flips == 1);
      r.tapV(80, 200);
      CHECK(r.flips == 2 && r.keys.empty());
    }
  }
  {  // 켜진 직후의 뜸 동안, 그리고 뜸에 걸쳐 있던 톡은 받지 않는다
    ViewRig r;
    r.quiet.arm(r.now, 3000, 1000);
    r.tapV(80, 200);
    r.tapV(80, 200);
    CHECK(r.flips == 0);
    r.until(r.now + 5000);
    CHECK(!r.quiet.isClosed());
    r.tapV(80, 200);
    CHECK(r.flips == 1);
    // 톡 하는 중에 뜸이 걸렸다(연결이 바뀌었다) → 그 톡은 버린다
    const TouchPoint p{0, 120, 50};
    for (int i = 0; i < 4; i++) r.tickV(&p, 1);
    r.quiet.arm(r.now, 0, 500);
    for (int i = 0; i < 4; i++) r.tickV(&p, 1);
    for (int i = 0; i < 100; i++) r.tickV(nullptr, 0);
    CHECK(r.flips == 1);
  }
  {  // `Update firmware?` 가 떠 있는 동안과 사라진 직후(0.7초), 그리고 그 경계에 걸친 톡은 받지 않는다
    ViewRig r;
    r.tapV(0, seen + 100);
    r.asking = true;
    r.tapV(80, 300);
    CHECK(r.flips == 0);
    const TouchPoint p{0, 120, 50};
    for (int i = 0; i < 5; i++) r.tickV(&p, 1);   // 물음이 떠 있을 때 닿은 손가락을
    r.asking = false;
    for (int i = 0; i < 5; i++) r.tickV(&p, 1);   // 물음이 사라진 뒤에 뗀다
    r.tapV(0, 100);
    CHECK(r.flips == 0);
    r.tapV(80, 100);                              // 사라진 직후의 뜸(0.7초) 안
    CHECK(r.flips == 0);
    r.tapV(0, UpdateGate::QUIET_MS + 200);
    r.tapV(80, 200);
    CHECK(r.flips == 1 && r.keys.empty());
    // 물음이 뜨기 전에 닿은 손가락을 물음이 뜬 뒤에 떼도 받지 않는다
    for (int i = 0; i < 5; i++) r.tickV(&p, 1);
    r.asking = true;
    for (int i = 0; i < 5; i++) r.tickV(&p, 1);
    r.tapV(0, 100);
    CHECK(r.flips == 1);
  }
}

static void test_cancel_guard() {
  CancelGuard g(1000);
  CHECK(g.allow(5000));              // 버튼의 취소
  CHECK(!g.allow(5300));             // 0.3초 뒤 흔들림: 두 번째 Esc 로 나가지 않는다
  CHECK(!g.allow(5990));
  CHECK(g.allow(6000));
  CHECK(!g.allow(6800));             // 흔들기 → 0.8초 뒤 버튼: 역시 막는다
  CHECK(g.allow(7000));
  CancelGuard w(1000);               // 시계가 한 바퀴 도는 때
  CHECK(w.allow(0xFFFFFF00u));
  CHECK(!w.allow(0x00000010u));
  CHECK(w.allow(0x00000300u));
}

// 펌웨어 물음이 떠 있는 동안에는 Back·Next 도 키로 나가지 않는다(독립 검토:
// Next 를 누르고 있는 사이에 물음이 뜨면, 떼는 순간 물음이 떠 있는데도 F18 이 나갔다).
static void test_update_prompt_blocks_pane_keys() {
  for (int hold = 150; hold <= 900; hold += 250) {   // 짧게(패널) · 길게(창) 모두
    UpdateRig r;
    PaneSwitch ps;
    int actions = 0;
    auto tick = [&](const TouchPoint* pts, int n) {
      const size_t had = r.keys.size();
      const uint32_t t = r.now;
      r.tickU(pts, n);
      for (size_t i = had; i < r.keys.size(); i++) ps.onEvent(r.keys[i], t);
      if (r.gate.blocking()) ps.reset();             // main.cpp 와 같은 자리
      if (ps.waiting()) {
        const Zone z = ps.waitingNext() ? Zone::Next : Zone::Back;
        if (ps.poll(t, (r.touch.pressed() & bit(z)) != 0) != PaneAction::None) ++actions;
      }
    };
    const Rect c = center(zoneRect(Zone::Next));
    const TouchPoint p{0, c.x, c.y};
    for (int i = 0; i < 10; i++) tick(nullptr, 0);
    for (int i = 0; i < 14; i++) tick(&p, 1);        // Next 를 누르기 시작(사건이 나올 만큼)
    CHECK(ps.waiting());                             // 짧게인지 길게인지 기다리는 중
    CHECK(r.state.askUpdate(r.now));                 // 그 사이에 물음이 떴다
    for (int i = 0; i < hold / 10; i++) tick(&p, 1);
    for (int i = 0; i < 40; i++) tick(nullptr, 0);   // 뗀다
    CHECK(actions == 0);
    CHECK(r.state.updateAsking(r.now));
  }
  // 물음이 없을 때는 예전처럼 나간다.
  UpdateRig r;
  PaneSwitch ps;
  int actions = 0;
  const Rect c = center(zoneRect(Zone::Next));
  const TouchPoint p{0, c.x, c.y};
  for (int i = 0; i < 60; i++) {
    const size_t had = r.keys.size();
    const uint32_t t = r.now;
    r.tickU(i >= 10 && i < 24 ? &p : nullptr, i >= 10 && i < 24 ? 1 : 0);
    for (size_t k = had; k < r.keys.size(); k++) ps.onEvent(r.keys[k], t);
    if (r.gate.blocking()) ps.reset();
    if (ps.waiting() && ps.poll(t, (r.touch.pressed() & bit(Zone::Next)) != 0) != PaneAction::None) ++actions;
  }
  CHECK(actions == 1);
}

// 낡은 시각을 들고 있지 않는다: 시계가 반 바퀴(약 24.8일) 돌면 "마감에 이르렀나"가 거꾸로 읽힌다(독립 검토).
static void test_state_old_times_do_not_come_back() {
  const uint32_t days26 = 26u * 24 * 3600 * 1000;
  {  // 26일 전에 들린 말소리가 "방금 들렸다"가 되지 않는다.
    TouchState s;
    Frame f{};
    s.setReady(true);
    poll(s, 0, f);
    s.setHeard(true);
    poll(s, 1000, f);
    s.setHeard(false);
    for (uint32_t t = 1100; t < 3000; t += 100) poll(s, t, f);
    s.onSent(Ev::TalkTap, days26);
    poll(s, days26, f);
    CHECK(f.micLive && f.guess && !f.mouthOpen);
  }
  {  // 26일 전에 거절한 물음 때문에 새 물음이 막히지 않는다.
    TouchState s;
    Frame f{};
    s.setReady(true);
    poll(s, 0, f);
    CHECK(s.askUpdate(1000));
    s.cancelUpdate(2000);
    for (uint32_t t = 2100; t < 2000 + TouchState::UPDATE_COOLDOWN_MS + 500; t += 100) poll(s, t, f);
    poll(s, days26 - 10, f);
    CHECK(s.askUpdate(days26));
  }
}

static void test_state_dirty_regions() {
  TouchState s;
  Frame f{};
  Dirty d = 0;
  s.setReady(true);
  s.poll(0, f, d);
  s.setPressed(bit(Zone::Ok));
  CHECK(s.poll(10, f, d) && d == bit(Zone::Ok));  // 눌린 버튼만
  s.setPressed(bit(Zone::Up));
  CHECK(s.poll(20, f, d) && d == (bit(Zone::Ok) | bit(Zone::Up)));
  s.setPressed(0);
  CHECK(s.poll(30, f, d) && d == bit(Zone::Up));
  // 목소리가 들려도 컴퓨터가 듣고 있지 않으면 아무것도 바뀌지 않는다.
  s.setVoice(true);
  CHECK(!s.poll(40, f, d));
  // 컴퓨터가 마이크를 열었다: 말하기 버튼(듣는 중 모습) + 무대 + 상태 막대.
  s.setMicLive(true);
  CHECK(s.poll(50, f, d) && d == (bit(Zone::Talk) | DIRTY_STAGE | DIRTY_STATUS) && f.mouthOpen);
  s.setVoice(false);
  CHECK(s.poll(60, f, d) && d == DIRTY_STAGE && !f.mouthOpen && f.wave == 0);
  s.setVoice(true);
  CHECK(s.poll(70, f, d) && d == DIRTY_STAGE && f.mouthOpen);
  // 목소리가 이어지는 동안 파형이 흐른다: WAVE_STEP_MS 마다 무대만 다시 그린다.
  const uint8_t w0 = f.wave;
  CHECK(!s.poll(75, f, d));
  CHECK(s.poll(70 + TouchState::WAVE_STEP_MS, f, d) && d == DIRTY_STAGE && f.wave != w0 && f.wave < WAVE_STEPS);
  // 다른 장면이 떠 있는 동안에는 입도 파형도 움직이지 않는다.
  s.onSent(Ev::Up, 200);
  CHECK(s.poll(200, f, d) && d == DIRTY_STAGE && !f.mouthOpen && f.wave == 0);
  CHECK(!s.poll(200 + TouchState::WAVE_STEP_MS, f, d));
  s.setVoice(false);
  CHECK(!s.poll(400, f, d));
}

static void test_voice_gate() {
  VoiceGate g(600, 300, 120);
  CHECK(!g.feed(100, 0));
  CHECK(!g.feed(500, 20));   // 켜지는 문턱 아래
  CHECK(g.feed(700, 40));
  CHECK(g.feed(400, 60));    // 꺼지는 문턱 위: 유지
  CHECK(g.feed(100, 80));    // 조용해졌지만 아직 기다린다
  CHECK(g.feed(100, 170));
  CHECK(!g.feed(100, 180));  // 0.12초 조용하면 닫는다
  CHECK(g.feed(900, 200));
  g.reset();
  CHECK(!g.open());
}

// ---------- 소리 버퍼 ----------

static void fillSeq(std::vector<int16_t>& v, int16_t& next, size_t n) {
  v.resize(n);
  for (size_t i = 0; i < n; i++) v[i] = next++;
}

static void test_ring_passes_in_order() {
  static AudioRing r;
  int16_t next = 1;
  std::vector<int16_t> in;
  int16_t out[64];
  CHECK(r.pop(out, 64) == 0);  // 비어 있으면 0개
  fillSeq(in, next, 160);
  CHECK(r.push(in.data(), in.size()));
  CHECK(r.fill() == 160);
  CHECK(r.pop(out, 64) == 64 && out[0] == 1 && out[63] == 64);
  CHECK(r.pop(out, 64) == 64 && out[0] == 65);
  CHECK(r.pop(out, 64) == 32 && out[0] == 129 && out[31] == 160);  // 남은 만큼만
  CHECK(r.fill() == 0);
  // 끝을 넘어 돌아가도 순서가 이어진다.
  for (int k = 0; k < 40; k++) {
    fillSeq(in, next, 160);
    CHECK(r.push(in.data(), in.size()));
    int16_t first = in[0];
    size_t got = 0;
    while (got < 160) {
      const size_t n = r.pop(out, 64);
      CHECK(n > 0 && out[0] == (int16_t)(first + got));
      got += n;
    }
  }
}

static void test_ring_overrun_drops_new() {
  static AudioRing r;
  int16_t next = 1;
  std::vector<int16_t> in;
  int16_t out[4];
  fillSeq(in, next, AudioRing::SIZE);
  CHECK(r.push(in.data(), in.size()));  // 꼭 맞게 가득
  fillSeq(in, next, 10);
  CHECK(!r.push(in.data(), in.size()));  // 넘치면 통째로 버린다
  CHECK(r.overruns() == 10);
  CHECK(r.fill() == AudioRing::SIZE);
  CHECK(r.pop(out, 4) == 4 && out[0] == 1 && out[3] == 4);  // 먼저 넣은 것은 그대로다
}

static void test_ring_discard() {
  static AudioRing r;
  int16_t next = 1;
  std::vector<int16_t> in;
  int16_t out[16];
  fillSeq(in, next, 1000);
  r.push(in.data(), in.size());
  r.discard();  // 컴퓨터가 마이크를 새로 열었다: 옛 소리는 버린다
  CHECK(r.fill() == 0 && r.pop(out, 16) == 0);
  fillSeq(in, next, 16);
  r.push(in.data(), in.size());
  CHECK(r.pop(out, 16) == 16 && out[0] == 1001);  // 그 뒤에 들어온 소리부터
}

// 쓰는 쪽과 읽는 쪽이 따로 도는 상황에서 순서가 깨지거나 값이 섞이지 않는가.
static void test_ring_two_threads_keep_order() {
  static AudioRing r;
  const int total = 400000;
  std::atomic<bool> giveUp{false};
  std::thread writer([&] {
    int16_t chunk[160];
    int sent = 0;
    while (sent < total && !giveUp.load()) {
      for (int i = 0; i < 160; i++) chunk[i] = (int16_t)((sent + i) & 0x7FFF);
      if (r.push(chunk, 160)) {
        sent += 160;
      } else {
        std::this_thread::yield();
      }
    }
  });
  const auto start = std::chrono::steady_clock::now();
  int got = 0, bad = 0;
  int16_t out[48];
  while (got < total) {
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(20)) break;  // 멈춤 방지
    const size_t n = r.pop(out, 48);
    if (n == 0) {
      std::this_thread::yield();
      continue;
    }
    for (size_t i = 0; i < n; i++) bad += (out[i] != (int16_t)((got + (int)i) & 0x7FFF));
    got += (int)n;
  }
  giveUp.store(true);
  writer.join();
  CHECK(got == total);
  CHECK(bad == 0);
}

static void test_gain() {
  CHECK(gainQ8FromDb(0) == 256);
  CHECK(gainQ8FromDb(6) == 511);    // 약 2배
  CHECK(gainQ8FromDb(-6) == 128);   // 약 절반
  CHECK(gainQ8FromDb(20) == 2560);  // 10배
  CHECK(gainQ8FromDb(-20) == 26);   // 약 1/10
  CHECK(gainQ8FromDb(99) == gainQ8FromDb(24) && gainQ8FromDb(-99) == gainQ8FromDb(-24));
  for (int db = -24; db < 24; db++) CHECK(gainQ8FromDb(db) < gainQ8FromDb(db + 1));  // 단조 증가
  int16_t s[4] = {1000, -1000, 20000, -20000};
  applyGain(s, 4, 256);
  CHECK(s[0] == 1000 && s[3] == -20000);  // 1배는 그대로
  applyGain(s, 4, 512);
  CHECK(s[0] == 2000 && s[1] == -2000 && s[2] == 32767 && s[3] == -32768);  // 넘치면 끝값
  applyGain(s, 4, 0);
  CHECK(s[0] == 0 && s[2] == 0);
}

// ---------- 그리기 (조각을 붙이는 방식 — D57) ----------

// 상태 막대의 여러 모습: 연결 셋 x 배터리(모름·가득·낮음·충전)
static const Status STATUSES[] = {
    {LinkState::Searching, -1, false},  {LinkState::Ready, 100, false},    {LinkState::Ready, 9, false},
    {LinkState::Connecting, 55, true},  {LinkState::Ready, 100, true},     {LinkState::Searching, 15, false},
    {LinkState::Ready, 87, false},
};
static const int STATUS_KINDS = sizeof(STATUSES) / sizeof(STATUSES[0]);

static Frame withStatus(Frame f, LinkState link, int pct, bool charging) {
  f.status.link = link;
  f.status.batteryPct = (int8_t)pct;
  f.status.charging = charging;
  return f;
}

// 나올 수 있는 화면을 골고루 만든다.
static std::vector<Frame> someFrames() {
  std::vector<Frame> v;
  const uint8_t masks[] = {0,
                           bit(Zone::Talk),
                           bit(Zone::Up),
                           bit(Zone::Down),
                           bit(Zone::Nope),
                           bit(Zone::Back),
                           bit(Zone::Next),
                           bit(Zone::Ok),
                           (uint8_t)(bit(Zone::Ok) | bit(Zone::Nope)),
                           (uint8_t)(bit(Zone::Talk) | bit(Zone::Down))};
  for (int face = 0; face < FACE_COUNT; face++) {
    for (uint8_t m : masks) {
      for (int live = 0; live < 2; live++) {
        for (int mouth = 0; mouth < 2; mouth++) {
          Frame f{(Face)face, m, live != 0, mouth != 0};
          f.status = STATUSES[v.size() % STATUS_KINDS];
          f.wave = (uint8_t)((live && mouth) ? v.size() % WAVE_STEPS : 0);
          v.push_back(f);
        }
      }
    }
  }
  return v;
}

// a 를 그린 화면에서, dirty 가 가리킨 곳만 b 로 다시 그리면 b 를 통째로 그린 것과 점 하나까지 같은가.
static bool partialEqualsFull(const Frame& a, const Frame& b, const pixelart::Mascot& mascot) {
  Buf screen;
  drawFrame(screen, a, mascot);
  const Dirty d = dirty(a, b);
  for (int z = 0; z < ZONE_COUNT; z++) {
    if (d & (1u << z)) drawZone(screen, b, (Zone)z);
  }
  if (d & DIRTY_STAGE) drawStage(screen, b, mascot);
  if (d & DIRTY_STATUS) drawStatus(screen, b);
  Buf fresh;
  drawFrame(fresh, b, mascot);
  return screen.px == fresh.px && !screen.outside;
}

static void test_draw_covers_whole_screen() {
  for (int m = 0; m < pixelart::MASCOT_COUNT; m++) {
    for (int face = 0; face < FACE_COUNT; face++) {
      Buf b;
      drawFrame(b, Frame{(Face)face, 0, face == (int)Face::Talk, false}, pixelart::mascotAt(m));
      CHECK(!b.outside);
      CHECK(b.covers(Rect{0, 0, W, H}));
    }
  }
  // 바탕만으로도 화면이 빠짐없이 덮인다(띠를 모두 그리면).
  Buf wall;
  for (int band = 0; band < BACKDROP_BANDS; band++) drawBackdropBand(wall, band);
  CHECK(!wall.outside && wall.covers(Rect{0, 0, W, H}));
}

static void test_draw_zone_pushes_exactly_its_rect() {
  const std::vector<Frame> frames = someFrames();
  for (size_t i = 0; i < frames.size(); i += 7) {  // 골고루 뽑아서
    for (int z = 0; z < ZONE_COUNT; z++) {
      Buf b;
      drawZone(b, frames[i], (Zone)z);
      CHECK(!b.outside);
      CHECK(b.pushes == 1 && sameRect(b.last, zoneRect((Zone)z)));  // 조각 하나를 그 자리에 딱 맞게
    }
  }
}

static void test_draw_view_parts_stay_in_their_rects() {
  const std::vector<Frame> frames = someFrames();
  for (int m = 0; m < pixelart::MASCOT_COUNT; m++) {
    for (size_t i = (size_t)m; i < frames.size(); i += 11) {
      Buf stage, status;
      drawStage(stage, frames[i], pixelart::mascotAt(m));
      CHECK(!stage.outside && stage.pushes == 1 && sameRect(stage.last, STAGE_RECT));
      drawStatus(status, frames[i]);
      CHECK(!status.outside && status.pushes == 1 && sameRect(status.last, STATUS_RECT));
    }
  }
}

// 부분만 다시 그린 화면이 전체를 새로 그린 화면과 점 하나까지 같은가.
// (기기는 달라진 곳만 다시 그린다 — 이것이 틀리면 화면에 옛 그림이 남는다.)
static void test_draw_partial_equals_full() {
  const std::vector<Frame> frames = someFrames();
  std::srand(20261009);
  for (int m = 0; m < pixelart::MASCOT_COUNT; m += 5) {  // 첫 마스코트와 기본(Claude)
    const pixelart::Mascot& mascot = pixelart::mascotAt(m);
    for (int k = 0; k < 300; k++) {
      CHECK(partialEqualsFull(frames[std::rand() % frames.size()], frames[std::rand() % frames.size()], mascot));
    }
  }
  // 한 가지만 다른 쌍(위의 제비뽑기에서는 거의 나오지 않는다 — 독립 검토 의 지적).
  const pixelart::Mascot& mascot = pixelart::mascot();
  for (size_t i = 0; i < frames.size(); i += 3) {
    const Frame& a = frames[i];
    for (int k = 0; k < STATUS_KINDS; k++) {  // 상태 막대만
      Frame b = a;
      b.status = STATUSES[k];
      CHECK(partialEqualsFull(a, b, mascot));
    }
    Frame b = a;
    b.wave = (uint8_t)((a.wave + 3) % WAVE_STEPS);  // 파형만
    CHECK(partialEqualsFull(a, b, mascot));
    b = a;
    b.mouthOpen = !a.mouthOpen;  // 입만
    CHECK(partialEqualsFull(a, b, mascot));
    b = a;
    b.micLive = !a.micLive;  // 듣는 중만
    CHECK(partialEqualsFull(a, b, mascot));
    b = a;
    b.pressed = (uint8_t)(a.pressed ^ bit(Zone::Next));  // 버튼 하나만
    CHECK(partialEqualsFull(a, b, mascot));
  }
}

// 한 틱에 한 조각씩 그리는 도중에 새 화면이 끼어들어도, 다 그리고 나면 마지막 화면과 점 하나까지 같은가.
static void test_painter_converges_with_interleaving() {
  const std::vector<Frame> frames = someFrames();
  std::srand(20261010);
  const pixelart::Mascot& mascot = pixelart::mascot();
  for (int round = 0; round < 80; round++) {
    Buf screen;
    Painter painter;
    Frame shown{};
    bool have = false;
    for (int k = 0; k < 12; k++) {
      const Frame& next = frames[std::rand() % frames.size()];
      painter.submit(next, have ? dirty(shown, next) : DIRTY_ALL);
      shown = next;
      have = true;
      // 다 그리기 전에 다음 화면이 온다: 0~3걸음만 그린다.
      for (int steps = std::rand() % 4; steps > 0; steps--) painter.step(screen, mascot);
    }
    int guard = 0;
    while (painter.busy() && guard++ < 50) painter.step(screen, mascot);
    CHECK(!painter.busy());
    CHECK(!painter.step(screen, mascot));  // 더 그릴 것이 없다
    Buf fresh;
    drawFrame(fresh, shown, mascot);
    CHECK(screen.px == fresh.px);
    CHECK(!screen.outside);
  }
}

static void test_painter_order() {
  const pixelart::Mascot& mascot = pixelart::mascot();
  Buf screen;
  Painter painter;
  const Frame f = withStatus(Frame{Face::Talk, bit(Zone::Ok), true, false}, LinkState::Ready, 80, false);
  painter.submit(f, DIRTY_ALL);
  for (int i = 0; i < BACKDROP_BANDS; i++) CHECK(painter.step(screen, mascot));  // 1) 바탕(띠 하나씩)
  CHECK(screen.covers(Rect{0, 0, W, H}));
  CHECK(painter.pending() == DIRTY_ALL);
  CHECK(painter.step(screen, mascot));  // 2) 버튼부터: 확인(맨 끝 번호)
  CHECK(sameRect(screen.last, zoneRect(Zone::Ok)));
  CHECK((painter.pending() & bit(Zone::Ok)) == 0);
  for (int i = 0; i < ZONE_COUNT - 1; i++) painter.step(screen, mascot);  // 나머지 버튼
  CHECK(painter.pending() == (DIRTY_STAGE | DIRTY_STATUS));
  CHECK(painter.step(screen, mascot) && sameRect(screen.last, STAGE_RECT));   // 3) 무대
  CHECK(painter.step(screen, mascot) && sameRect(screen.last, STATUS_RECT));  // 4) 상태 막대
  CHECK(painter.pending() == 0 && !painter.busy());
  // 배터리만 달라진 경우: 상태 막대만 그린다.
  Frame g = f;
  g.status.batteryPct = 79;
  painter.submit(g, dirty(f, g));
  CHECK(painter.pending() == DIRTY_STATUS);
  Buf only;
  CHECK(painter.step(only, mascot) && !only.paintedOutside(STATUS_RECT) && !painter.busy());
  // 뜻 없는 비트를 넘겨도 끝난다.
  painter.submit(g, (Dirty)0xFE00);
  CHECK(!painter.busy());
  // 화면이 다른 것으로 덮였다(개발판의 시안 그림): 바탕부터 전부 다시 그린다.
  Buf again;
  painter.invalidate();
  CHECK(painter.busy());
  int guard = 0;
  while (painter.busy() && guard++ < 50) painter.step(again, mascot);
  Buf fresh;
  drawFrame(fresh, g, mascot);
  CHECK(again.px == fresh.px && again.covers(Rect{0, 0, W, H}));
}

static void test_draw_dirty_is_minimal() {
  const Frame base = withStatus(Frame{Face::Talk, 0, true, true}, LinkState::Ready, 80, false);
  const Dirty allKeys = (Dirty)((1u << ZONE_COUNT) - 1);
  CHECK(dirty(base, base) == 0);
  Frame f = base;
  f.mouthOpen = false;
  CHECK(dirty(base, f) == DIRTY_STAGE);
  f = base;
  f.wave = 3;
  CHECK(dirty(base, f) == DIRTY_STAGE);  // 파형이 흐를 때는 무대만
  f = base;
  f.pressed = bit(Zone::Ok);
  CHECK(dirty(base, f) == bit(Zone::Ok));
  f = base;
  f.pressed = bit(Zone::Talk);
  CHECK(dirty(base, f) == bit(Zone::Talk));
  f = base;
  f.face = Face::Ok;
  CHECK(dirty(base, f) == DIRTY_STAGE);  // 표정만 바뀌면 무대만
  f = base;
  f.micLive = false;  // 컴퓨터가 마이크를 닫았다: 말하기 버튼 + 무대 + 상태 막대(보는 곳의 바탕이 통째로 바뀐다)
  CHECK(dirty(base, f) == (bit(Zone::Talk) | DIRTY_STAGE | DIRTY_STATUS));
  f = base;
  f.status.batteryPct = 79;
  CHECK(dirty(base, f) == DIRTY_STATUS);  // 배터리만 바뀌면 상태 막대만
  f = base;
  f.status.link = LinkState::Searching;  // 끊겼다: 버튼이 모두 꺼지고 상태 막대가 바뀐다(표정은 그대로)
  CHECK(dirty(base, f) == (allKeys | DIRTY_STATUS));
  const Frame idle = withStatus(Frame{Face::Hi, 0, false, false}, LinkState::Ready, 80, false);
  f = idle;
  f.face = Face::Update;  // 펌웨어를 바꿀지 묻는다: 예·아니오를 뺀 버튼이 꺼진다
  CHECK(dirty(idle, f) == ((allKeys & (Dirty) ~(bit(Zone::Nope) | bit(Zone::Ok))) | DIRTY_STAGE));
}

// 버튼의 모습: 눌림 > 꺼짐 > 듣는 중 > 평소
static void test_draw_looks() {
  const Frame ready = withStatus(Frame{Face::Hi, 0, false, false}, LinkState::Ready, 80, false);
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(lookFor(ready, (Zone)z) == art::LOOK_NORMAL);
  Frame f = ready;
  f.status.link = LinkState::Searching;  // 연결이 없으면 눌러도 아무 일이 없다 → 꺼진 모습
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(lookFor(f, (Zone)z) == art::LOOK_OFF);
  f.status.link = LinkState::Connecting;
  for (int z = 0; z < ZONE_COUNT; z++) CHECK(lookFor(f, (Zone)z) == art::LOOK_OFF);
  f.pressed = bit(Zone::Next);  // 그래도 눌림은 보인다(촉감이 없으므로 유일한 신호)
  CHECK(lookFor(f, Zone::Next) == art::LOOK_PRESSED && lookFor(f, Zone::Back) == art::LOOK_OFF);
  f = ready;
  f.micLive = true;  // 듣는 중: 말하기 버튼만 모습이 바뀐다
  CHECK(lookFor(f, Zone::Talk) == art::LOOK_LIVE && lookFor(f, Zone::Ok) == art::LOOK_NORMAL);
  f.pressed = bit(Zone::Talk);
  CHECK(lookFor(f, Zone::Talk) == art::LOOK_PRESSED);
  f = ready;
  f.face = Face::Update;  // 물음에는 예(확인)·아니오(취소)만 켠다
  for (int z = 0; z < ZONE_COUNT; z++) {
    const bool answer = (Zone)z == Zone::Ok || (Zone)z == Zone::Nope;
    CHECK(lookFor(f, (Zone)z) == (answer ? art::LOOK_NORMAL : art::LOOK_OFF));
  }
  // 상태 막대의 낱말
  CHECK(wordFor(ready) == art::WORD_READY);
  CHECK(wordFor(withStatus(ready, LinkState::Connecting, 80, false)) == art::WORD_WAIT);
  CHECK(wordFor(withStatus(ready, LinkState::Searching, 80, false)) == art::WORD_OFF);
  CHECK(wordFor(withStatus(ready, LinkState::Pairing, 80, false)) == art::WORD_PAIR);  // 짝을 맺은 컴퓨터가 없다(D59)
  f = ready;
  f.status.wireless = true;  // 블루투스로 쓰는 중
  CHECK(wordFor(f) == art::WORD_BT && lookFor(f, Zone::Ok) == art::LOOK_NORMAL);
  f = ready;
  f.micLive = true;
  CHECK(wordFor(f) == art::WORD_LIVE);
}

// 캐릭터·파형·안내 말이 무대 안에 들어가고 서로 겹치지 않는다(모든 마스코트, 모든 장면).
static void test_draw_stage_fits() {
  for (int m = 0; m < pixelart::MASCOT_COUNT; m++) {
    const pixelart::Mascot& mascot = pixelart::mascotAt(m);
    for (int face = 0; face < FACE_COUNT; face++) {
      for (int live = 0; live < 2; live++) {
        const Frame f{(Face)face, 0, live != 0, false};
        const Rect mr = mascotRect(f, mascot);
        CHECK(mascotScale(f, mascot) >= 3);  // 표정이 보이는 크기
        CHECK(mr.w > 0 && mr.h > 0 && inside(mr, STAGE_RECT));
        const bool hint = specFor((Face)face).hint != art::HINT_NONE;
        if (hint) {
          const art::Mask& mk = art::HINT[specFor((Face)face).hint];
          CHECK(mk.w <= STAGE_RECT.w - 16);                               // 안내 말이 화면 폭에 들어간다
          CHECK(mr.y + mr.h <= STAGE_RECT.y + STAGE_RECT.h - mk.h - 1);  // 캐릭터가 안내 말을 덮지 않는다
        }
        const Rect wr = waveRect(f, mascot);
        CHECK((wr.w > 0) == (live && !hint));  // 파형은 듣는 중이고 안내 말이 없을 때만
        if (wr.w > 0) {
          CHECK(inside(wr, STAGE_RECT));
          CHECK(wr.x >= mr.x + mr.w + 8);  // 캐릭터와 떨어져 있다
        }
      }
    }
  }
}

// 상태 막대: 낱말은 배터리와 겹치지 않고, 숫자는 배터리 그림 안에 들어간다.
static void test_draw_status_fits() {
  const Rect bat = batteryRect();
  CHECK(inside(bat, STATUS_RECT));
  for (int w = 0; w < art::WORD_COUNT; w++) {
    Frame f{Face::Hi, 0, w == art::WORD_LIVE, false};
    f.status.link = (w == art::WORD_READY || w == art::WORD_BT) ? LinkState::Ready
                    : w == art::WORD_WAIT ? LinkState::Connecting
                    : w == art::WORD_PAIR ? LinkState::Pairing : LinkState::Searching;
    f.status.wireless = (w == art::WORD_BT || w == art::WORD_PAIR);
    CHECK(wordFor(f) == (art::Word)w);
    const Rect wr = statusWordRect(f);
    CHECK(inside(wr, STATUS_RECT));
    CHECK(wr.x + wr.w + 8 <= bat.x);
  }
  for (int pct = 0; pct <= 100; pct++) {
    const Rect dr = batteryDigitsRect(pct);
    CHECK(dr.w > 0 && inside(dr, bat));
    CHECK(dr.x >= bat.x + 2 && dr.x + dr.w <= bat.x + 30 - 2);  // 몸통(30픽셀) 안, 둥근 모서리를 피한다
  }
  CHECK(batteryDigitsRect(-1).w == 0);  // 값을 모르면 숫자가 없다
  CHECK(sameRect(batteryDigitsRect(250), batteryDigitsRect(100)));
}

// 색이 곧 뜻이다: 글을 읽지 않아도 초록이면 연결, 빨강이면 배터리 부족.
// D64 듣는 중은 빨갛게 보인다(받아쓰는 중이라는 것이 한눈에 보여야 한다).
// 색의 정확한 값이 아니라 눈에 보이는 성질을 지킨다: "빨강" = 붉은 빛이 세고 초록·파랑이 약하다.
static bool isRed(uint32_t c) {
  const int r = (int)((c >> 11) & 31), g = (int)((c >> 5) & 63), b = (int)(c & 31);
  return r >= 26 && g <= 24 && b <= 12;
}
// r 안에서 빨간 점이 차지하는 비율(%)
static int redShare(const Buf& b, const Rect& r) {
  int n = 0;
  for (int y = r.y; y < r.y + r.h; y++) {
    for (int x = r.x; x < r.x + r.w; x++) n += isRed(b.px[y * W + x]);
  }
  return n * 100 / (r.w * r.h);
}

static void test_draw_listening_is_red() {
  const Frame idle = withStatus(Frame{Face::Hi, 0, false, false}, LinkState::Ready, 80, false);
  Frame live = idle;
  live.face = Face::Talk;
  live.micLive = true;
  const Rect view{0, 0, (int16_t)W, (int16_t)(STATUS_RECT.h + STAGE_RECT.h)};
  const Rect talk = zoneRect(Zone::Talk);
  const Zone others[] = {Zone::Up, Zone::Down, Zone::Nope, Zone::Back, Zone::Next, Zone::Ok};
  Buf a, b;
  drawFrame(a, idle);
  drawFrame(b, live);
  // 평소 화면에는 빨간 곳이 거의 없다(캐릭터는 코랄, 연결 점은 초록).
  CHECK(redShare(a, view) <= 2);
  CHECK(redShare(a, talk) <= 2);
  // 듣는 중에는 보는 곳과 말하기 버튼의 대부분이 빨갛다.
  CHECK(redShare(b, view) >= 60);
  CHECK(redShare(b, talk) >= 50);
  // 다른 버튼은 그대로다(빨강은 "듣고 있다" 한 가지에만 넓게 쓴다).
  for (Zone z : others) CHECK(redShare(b, zoneRect(z)) <= 2);
  // 목소리가 들어와 파형이 움직여도, 다른 장면(확인을 누른 순간 등)이 겹쳐도 듣는 동안은 빨갛다.
  for (int wave = 0; wave < WAVE_STEPS; wave++) {
    Frame voiced = live;
    voiced.mouthOpen = true;
    voiced.wave = (uint8_t)wave;
    Buf c;
    drawFrame(c, voiced);
    CHECK(redShare(c, view) >= 55 && redShare(c, talk) >= 50);
  }
  for (int face = 0; face < FACE_COUNT; face++) {
    Frame f = live;
    f.face = (Face)face;
    Buf c;
    drawFrame(c, f);
    CHECK(redShare(c, view) >= 50);
  }
  // 말하기 버튼을 누르고 있는 동안에는 눌린 모습이 앞선다(손끝의 반응). 떼면 다시 빨갛다.
  Frame held = live;
  held.pressed = bit(Zone::Talk);
  Buf h;
  drawFrame(h, held);
  CHECK(redShare(h, talk) <= 2 && redShare(h, view) >= 60);

  // 빨간 바탕 위에서도 상태 막대가 읽힌다: 점과 배터리 채움이 빨강·초록이 아니라 흰색이다
  // (부족의 빨강은 바탕에 묻히고, 충전의 초록은 빨강 위에서 튄다).
  const uint16_t white = gfx::rgb565(0xFF, 0xFF, 0xFF), green = gfx::rgb565(0x30, 0xD1, 0x58);
  auto count = [](const Buf& s, const Rect& r, uint16_t col) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
      for (int x = r.x; x < r.x + r.w; x++) n += (s.px[y * W + x] == col);
    }
    return n;
  };
  const Rect left{0, 0, 26, STATUS_H}, bat = batteryRect();
  const struct {
    int  pct;
    bool charging;
  } cases[] = {{9, false}, {20, false}, {50, true}, {87, false}, {100, true}};
  int lastWhite = 0;
  for (const auto& k : cases) {
    Buf s;
    drawStatus(s, withStatus(live, LinkState::Ready, k.pct, k.charging));
    CHECK(count(s, left, white) > 20);   // 흰 점(녹음 중)
    CHECK(count(s, bat, green) == 0);
    const int w = count(s, bat, white);  // 흰 채움: 많이 찰수록 넓다
    CHECK(w > 0 && w >= lastWhite);
    lastWhite = w;
  }
}

// D65 미루어 듣는 중과 사실로 듣는 중은 같은 빨강이고, 상태 막대의 점만 다르다(빈 고리 / 채운 점).
// 개발판 표시: 개발판(DEV_BUILD=1)이 올라가 있으면 화면 맨 위에 자홍·검정 띠가 늘 보인다.
//   개발판은 컴퓨터의 아무 프로그램이 키를 치게 할 수 있고 묻지 않고 다시 구워진다. 그런 판이 올라가 있는 줄
//   모르고 쓰는 일이 없게, 화면만 봐도 알 수 있어야 한다(공개 전 검토 — 그 전에는 `vibe:status` 의
//   dev=1 로만 알 수 있었다).
static void test_draw_dev_band() {
  const uint16_t MAGENTA = 0xF81F, BLACK565 = 0x0000;
  auto bandPixels = [&](const Frame& f, int& magenta, int& black, int& other) {
    Buf b;
    drawStatus(b, f);
    magenta = black = other = 0;
    for (int y = 0; y < DEV_BAND_H; y++) {
      for (int x = 0; x < W; x++) {
        const uint32_t v = b.px[y * W + x];
        magenta += (v == MAGENTA);
        black += (v == BLACK565);
        other += (v != MAGENTA && v != BLACK565);
      }
    }
    // 띠 아래(낱말·점·배터리가 있는 곳)에는 자홍이 없다
    int below = 0;
    for (int y = DEV_BAND_H; y < STATUS_H; y++) {
      for (int x = 0; x < W; x++) below += (b.px[y * W + x] == MAGENTA);
    }
    return below;
  };
  for (int live = 0; live < 2; live++) {  // 평소 화면과 빨간(듣는 중) 화면 모두에서
    Frame f{Face::Hi, 0, live != 0, false};
    f.status.link = LinkState::Ready;
    f.status.batteryPct = 100;
    int m = 0, k = 0, o = 0;
    CHECK(bandPixels(f, m, k, o) == 0);
    CHECK(m == 0);                      // 평소 판에는 띠가 없다
    f.dev = true;
    CHECK(bandPixels(f, m, k, o) == 0);
    CHECK(o == 0 && m + k == W * DEV_BAND_H);  // 맨 윗줄 전체가 띠다
    CHECK(m >= W * DEV_BAND_H / 3 && k >= W * DEV_BAND_H / 3);  // 두 색이 번갈아(어느 바탕에서도 보이게)
  }
  // 화면 상태가 "개발판인가"를 싣고, 달라지면 상태 막대를 다시 그린다
  Frame a{Face::Hi, 0, false, false}, d = a;
  d.dev = true;
  CHECK(a != d && (dirty(a, d) & DIRTY_STATUS));
  TouchState s;
  Frame f{};
  s.setReady(true);
  CHECK(poll(s, 0, f) && !f.dev);
  s.setDev(true);
  CHECK(poll(s, 10, f) && f.dev);
  // 띠는 누르는 자리·톡 자리와 겹치지 않는다(보는 곳의 맨 위 가장자리 — 톡 자리는 4픽셀 아래에서 시작한다)
  CHECK(DEV_BAND_H <= VIEW_TAP_RECT.y && DEV_BAND_H <= 5);
}

static void test_draw_guess_is_a_ring() {
  Frame live = withStatus(Frame{Face::Talk, 0, true, false}, LinkState::Ready, 80, false);
  Frame guess = live;
  guess.guess = true;
  const Rect view{0, 0, (int16_t)W, (int16_t)(STATUS_RECT.h + STAGE_RECT.h)};
  Buf a, b;
  drawFrame(a, live);
  drawFrame(b, guess);
  CHECK(redShare(b, view) >= 60 && redShare(b, zoneRect(Zone::Talk)) >= 50);  // 미루어도 똑같이 빨갛다
  const uint16_t white = gfx::rgb565(0xFF, 0xFF, 0xFF);
  auto whites = [&](const Buf& s) {
    int n = 0;
    for (int y = 0; y < STATUS_H; y++) {
      for (int x = 0; x < 24; x++) n += (s.px[y * W + x] == white);
    }
    return n;
  };
  CHECK(whites(a) > 30);                        // 채운 점
  CHECK(whites(b) > 8 && whites(b) < whites(a) - 10);  // 빈 고리: 흰 점이 훨씬 적다
  // 바뀌는 것은 상태 막대뿐이다(무대와 버튼은 다시 그리지 않는다 — 깜빡이지 않는다).
  CHECK(dirty(guess, live) == DIRTY_STATUS && dirty(live, guess) == DIRTY_STATUS);
  CHECK(guess != live);
}

static void test_draw_status_colors() {
  const uint16_t green = gfx::rgb565(0x30, 0xD1, 0x58), red = gfx::rgb565(0xFF, 0x45, 0x3A);
  const Frame base{Face::Hi, 0, false, false};
  const Rect bat = batteryRect();
  auto at = [](const Buf& b, int x, int y) { return (uint16_t)b.px[y * W + x]; };
  auto count = [](const Buf& b, const Rect& r, uint16_t col) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) {
      for (int x = r.x; x < r.x + r.w; x++) n += (b.px[y * W + x] == col);
    }
    return n;
  };
  const Rect left{0, 0, 26, STATUS_H};
  {
    Buf b;
    drawStatus(b, withStatus(base, LinkState::Ready, 80, false));
    CHECK(count(b, left, green) > 20);  // 초록 점
    CHECK(count(b, bat, green) == 0 && count(b, bat, red) == 0);
  }
  {
    Buf b;
    drawStatus(b, withStatus(base, LinkState::Searching, 80, false));
    CHECK(count(b, left, green) == 0 && count(b, left, red) > 5);  // 빨간 빈 고리
  }
  {  // 충전 중: 초록 채움. 많이 찰수록 넓다.
    Buf lo, hi;
    drawStatus(lo, withStatus(base, LinkState::Ready, 20, true));
    drawStatus(hi, withStatus(base, LinkState::Ready, 90, true));
    CHECK(count(lo, bat, green) > 0 && count(hi, bat, green) > count(lo, bat, green));
    CHECK(count(lo, bat, red) == 0);  // 낮아도 차고 있으면 빨갛지 않다
  }
  {  // 배터리 부족(20% 이하, 충전 아님): 빨강
    Buf b, ok;
    drawStatus(b, withStatus(base, LinkState::Ready, 20, false));
    drawStatus(ok, withStatus(base, LinkState::Ready, 21, false));
    CHECK(count(b, bat, red) > 0 && count(ok, bat, red) == 0);
  }
  {  // 값을 모르면 배터리를 그리지 않는다(바탕 그대로)
    Buf b, wall;
    drawStatus(b, withStatus(base, LinkState::Ready, -1, false));
    drawBackdropBand(wall, 0);
    bool same = true;
    for (int y = bat.y; y < bat.y + bat.h; y++) {
      for (int x = bat.x; x < bat.x + bat.w; x++) same = same && at(b, x, y) == at(wall, x, y);
    }
    CHECK(same);
  }
}

static void test_state_status_follows_link_and_battery() {
  const Dirty allKeys = (Dirty)((1u << ZONE_COUNT) - 1);
  TouchState s;
  Frame f{};
  Dirty d = 0;
  CHECK(s.poll(0, f, d));
  CHECK(f.status.link == LinkState::Searching && f.status.batteryPct == -1 && !f.status.charging);
  s.setBattery(87, false);
  CHECK(s.poll(10, f, d) && d == DIRTY_STATUS && f.status.batteryPct == 87);
  CHECK(!s.poll(20, f, d));  // 같은 값이면 그리지 않는다
  s.setBattery(87, false);
  CHECK(!s.poll(25, f, d));
  s.setBattery(87, true);
  CHECK(s.poll(30, f, d) && d == DIRTY_STATUS && f.status.charging);
  s.setBattery(150, true);  // 전원 칩이 엉뚱한 값을 주어도 0~100 을 넘지 않는다
  CHECK(s.poll(40, f, d) && f.status.batteryPct == 100);
  s.setBattery(-7, false);
  CHECK(s.poll(50, f, d) && f.status.batteryPct == -1 && !f.status.charging);
  // 연결되면 버튼이 모두 켜지고, 캐릭터(감은 눈 → 평소)와 상태 막대가 바뀐다.
  s.setLink(LinkState::Ready, 100);
  CHECK(s.poll(100, f, d) && d == DIRTY_ALL);
  CHECK(f.face == Face::Hi && f.status.link == LinkState::Ready);
  // 동작 장면(확인)이 떠 있는 동안 끊기면: 표정은 그대로, 버튼이 꺼지고 상태 막대가 바뀐다.
  s.onSent(Ev::Confirm, 200);
  CHECK(s.poll(200, f, d) && d == DIRTY_STAGE && f.face == Face::Ok);
  s.setLink(LinkState::Searching, 210);
  CHECK(s.poll(210, f, d) && d == (allKeys | DIRTY_STATUS));
  CHECK(f.face == Face::Ok && f.status.link == LinkState::Searching);
  s.setLink(LinkState::Connecting, 220);
  CHECK(s.poll(220, f, d) && d == DIRTY_STATUS && f.status.link == LinkState::Connecting);
}

// ---------- 시계와 알림 (D62·D63) ----------

static void test_clock_shows_only_when_resting() {
  const pixelart::Mascot& mascot = pixelart::mascot();
  TouchState s;
  Frame f{};
  Dirty d = 0;
  s.setReady(true);
  s.poll(0, f, d);
  CHECK(f.clockMin == -1);  // 시각을 모르면 보이지 않는다
  s.setClock(15 * 60 + 52);
  CHECK(s.poll(10, f, d) && d == DIRTY_STAGE && f.clockMin == 952);  // 무대만 다시 그린다
  CHECK(!s.poll(20, f, d));
  s.setClock(15 * 60 + 53);  // 분이 바뀔 때마다
  CHECK(s.poll(30, f, d) && d == DIRTY_STAGE && f.clockMin == 953);
  // 동작 장면이 뜨면 시각은 물러난다(그 자리는 표정의 것이다).
  s.onSent(Ev::Confirm, 100);
  CHECK(s.poll(100, f, d) && f.face == Face::Ok && f.clockMin == -1);
  CHECK(s.poll(100 + TouchState::ACTION_SHOW_MS, f, d) && f.face == Face::Hi && f.clockMin == 953);
  // 듣는 중에는 그 자리에 파형이 온다.
  s.setMicLive(true);
  CHECK(s.poll(5000, f, d) && f.clockMin == -1);
  s.setMicLive(false);
  s.poll(6000, f, d);
  // 엉뚱한 값은 보이지 않는다.
  s.setClock(24 * 60);
  CHECK(s.poll(7000, f, d) && f.clockMin == -1);
  s.setClock(-5);
  CHECK(!s.poll(7010, f, d));
  // 그리기: 시각은 무대 안, 캐릭터의 오른쪽에 놓이고 모든 시각이 들어간다.
  Frame g{Face::Hi, 0, false, false};
  CHECK(clockRect(g, mascot).w == 0);
  for (int m = 0; m < 24 * 60; m += 7) {
    g.clockMin = (int16_t)m;
    const Rect cr = clockRect(g, mascot), mr = mascotRect(g, mascot);
    CHECK(cr.w > 0 && inside(cr, STAGE_RECT) && inside(mr, STAGE_RECT));
    CHECK(cr.x >= mr.x + mr.w + 8 && cr.x + cr.w <= STAGE_RECT.x + STAGE_RECT.w - 8);
  }
  g.clockMin = 23 * 60 + 59;
  g.face = Face::Ok;
  CHECK(clockRect(g, mascot).w == 0);
  g.face = Face::Hi;
  g.micLive = true;
  CHECK(clockRect(g, mascot).w == 0);
  // 부분만 다시 그려도 통째로 그린 것과 같다(분이 바뀔 때, 시각이 나타나고 사라질 때).
  Frame a{Face::Hi, 0, false, false}, b = a;
  a.status.link = b.status.link = LinkState::Ready;
  a.clockMin = 9 * 60 + 5;
  b.clockMin = 9 * 60 + 6;
  CHECK(partialEqualsFull(a, b, mascot));
  b.clockMin = -1;
  CHECK(partialEqualsFull(a, b, mascot) && partialEqualsFull(b, a, mascot));
  b.clockMin = 10 * 60;  // 한 자리 시 → 두 자리 시(너비가 달라진다)
  CHECK(partialEqualsFull(a, b, mascot));
}

static void test_notes_from_claude() {
  TouchState s;
  Frame f{};
  s.setReady(true);
  poll(s, 0, f);
  // 끝났다는 알림은 잠깐.
  s.onNote(TouchState::Note::Done, 1000);
  CHECK(poll(s, 1000, f) && f.face == Face::Done && specFor(Face::Done).hint == art::HINT_DONE);
  CHECK(!poll(s, 1000 + TouchState::NOTE_DONE_MS - 10, f));
  CHECK(poll(s, 1000 + TouchState::NOTE_DONE_MS, f) && f.face == Face::Hi);
  // 답을 기다린다는 알림은 오래 — 사람이 누르면 그 장면이 대신한다.
  s.onNote(TouchState::Note::Ask, 20000);
  CHECK(poll(s, 20000, f) && f.face == Face::Ask && specFor(Face::Ask).hint == art::HINT_ASK);
  CHECK(!poll(s, 20000 + 30000, f));
  s.onSent(Ev::Confirm, 51000);
  CHECK(poll(s, 51000, f) && f.face == Face::Ok);
  CHECK(poll(s, 51000 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Hi);  // 확인 장면은 평소 길이로 끝난다
  // 누르지 않으면 1분 뒤 스스로 내린다.
  s.onNote(TouchState::Note::Ask, 100000);
  poll(s, 100000, f);
  CHECK(poll(s, 100000 + TouchState::NOTE_ASK_MS, f) && f.face == Face::Hi);
  // 펌웨어 물음이 떠 있는 동안에는 알림이 그것을 가리지 않는다.
  CHECK(s.askUpdate(200000));
  s.onNote(TouchState::Note::Done, 200100);
  CHECK(poll(s, 200100, f) && f.face == Face::Update);
  s.cancelUpdate(200200);
  CHECK(poll(s, 200200, f) && f.face == Face::Hi);
}

// ---------- 화면 켜고 끄기·밝기 (logic/screen_power.h, D60·D61) ----------

static void test_power_dims_then_turns_off() {
  const uint32_t DIM = 60000, OFF = 300000;
  PowerInput idle{};
  idle.usable = true;  // 선이 꽂혀 쓸 수 있다
  ScreenPower p(DIM, OFF);
  p.begin(1000);
  CHECK(p.poll(1000 + DIM - 10, idle) == Light::Bright);
  CHECK(p.poll(1000 + DIM, idle) == Light::Dim);       // 1분 손대지 않으면 어둡게
  CHECK(p.poll(1000 + OFF + 5000, idle) == Light::Dim);  // 선이 꽂혀 쓸 수 있는 동안에는 끄지 않는다(책상 위의 화면)
  PowerInput bat = idle;
  bat.onBattery = true;  // 배터리로 도는 중이면 5분 뒤 끈다
  CHECK(p.poll(1000 + OFF + 6000, bat) == Light::Off);
  PowerInput none{};     // 컴퓨터가 없을 때도 끈다
  ScreenPower q(DIM, OFF);
  q.begin(0);
  CHECK(q.poll(OFF - 10, none) == Light::Dim && q.poll(OFF, none) == Light::Off);
}

static void test_power_wakes_without_a_touch() {
  const uint32_t DIM = 60000, OFF = 300000;
  PowerInput bat{};
  bat.usable = true;
  bat.onBattery = true;
  // 꺼진 화면을 깨우는 것들: 손가락, 손이 가까이 옴, 기기를 집어 듦, 장면 바뀜, 컴퓨터가 듣기 시작함.
  const int sources = 5;
  for (int k = 0; k < sources; k++) {
    ScreenPower p(DIM, OFF);
    p.begin(0);
    CHECK(p.poll(OFF, bat) == Light::Off);
    PowerInput in = bat;
    (k == 0 ? in.touching : k == 1 ? in.near : k == 2 ? in.moving : k == 3 ? in.sceneChanged : in.micLive) = true;
    CHECK(p.poll(OFF + 100, in) == Light::Bright);
    // 그 순간부터 다시 센다.
    CHECK(p.poll(OFF + 100 + DIM - 10, bat) == Light::Bright);
    CHECK(p.poll(OFF + 100 + DIM, bat) == Light::Dim);
  }
  // 쥐고 있는 동안(계속 움직임)에는 어두워지지 않는다.
  ScreenPower p(DIM, OFF);
  p.begin(0);
  PowerInput held = bat;
  held.moving = true;
  for (uint32_t t = 0; t <= 2 * OFF; t += 1000) CHECK(p.poll(t, held) == Light::Bright);
}

static void test_power_proximity_and_brightness_do_not_flicker() {
  Proximity near(120, 60);
  CHECK(!near.feed(0) && !near.feed(119));
  CHECK(near.feed(120));
  CHECK(near.feed(90) && near.feed(61));  // 문턱 사이에서는 그대로(경계에서 떨지 않는다)
  CHECK(!near.feed(60));
  CHECK(!near.feed(100));
  AutoBright b(AutoBright::Limits{8, 20, 1000, 600});
  CHECK(b.level() == AutoBright::Normal);
  CHECK(b.feed(9) == AutoBright::Normal && b.feed(8) == AutoBright::Dark);
  CHECK(b.feed(19) == AutoBright::Dark && b.feed(20) == AutoBright::Normal);
  CHECK(b.feed(999) == AutoBright::Normal && b.feed(1000) == AutoBright::Sunny);
  CHECK(b.feed(601) == AutoBright::Sunny && b.feed(600) == AutoBright::Normal);
  CHECK(b.feed(3) == AutoBright::Dark);  // 보통에서 곧바로 어두움으로
  CHECK(b.feed(5000) == AutoBright::Normal && b.feed(5000) == AutoBright::Sunny);  // 어두움에서 아주 밝음은 한 단계씩
}

// ---------- 움직임·흔들기 (logic/motion.h, D61) ----------

static void test_motion_still_and_moving() {
  MotionDetector m(60, 2000);
  uint32_t t = 0;
  // 책상에 놓임: 잡음(±10mg)만 있다 → 움직이지 않는다.
  for (int i = 0; i < 500; i++, t += 10) CHECK(!m.feed((int16_t)(i % 2 ? 8 : -6), (int16_t)(i % 3 ? 5 : -7), (int16_t)(1000 + (i % 2 ? 9 : -9)), t));
  // 집어 듦: 한 번의 큰 변화면 충분하다.
  CHECK(m.feed(150, -80, 950, t));
  t += 10;
  // 다시 가만히: 2초 동안은 움직이는 중으로 본다(쥔 손이 잠깐 멎어도 꺼지지 않게).
  for (int i = 0; i < 199; i++, t += 10) CHECK(m.feed(150, -80, 950, t));
  CHECK(!m.feed(150, -80, 950, t + 10));
}

// 흔들기를 흉내 낸다: 한 축으로 ±amp 를 half 틱마다 번갈아. 알아본 횟수를 돌려준다.
static int shake(ShakeDetector& s, uint32_t& t, int amp, int half, int swings, int axis = 0) {
  int fired = 0;
  for (int k = 0; k < swings; k++) {
    for (int i = 0; i < half; i++, t += 10) {
      const int16_t v = (int16_t)(k % 2 ? -amp : amp);
      fired += s.feed(axis == 0 ? v : 0, axis == 1 ? v : 0, (int16_t)(1000 + (axis == 2 ? v : 0)), t);
    }
  }
  return fired;
}

static void test_shake_is_recognised_once() {
  const ShakeDetector::Limits lim{1200, 4, 800, 1500};
  for (int axis = 0; axis < 3; axis++) {
    ShakeDetector s(lim);
    uint32_t t = 0;
    for (int i = 0; i < 50; i++, t += 10) CHECK(!s.feed(0, 0, 1000, t));  // 가만히
    CHECK(shake(s, t, 2200, 6, 8, axis) == 1);  // 세게 여덟 번(0.48초): 한 번만 알아본다
    for (int i = 0; i < 20; i++, t += 10) CHECK(!s.feed(0, 0, 1000, t));
    CHECK(shake(s, t, 2200, 6, 8, axis) == 0);  // 바로 또 흔들어도 쉬는 동안에는 알아보지 않는다
    t += 2000;
    for (int i = 0; i < 20; i++, t += 10) s.feed(0, 0, 1000, t);
    CHECK(shake(s, t, 2200, 6, 8, axis) == 1);  // 쉬고 나면 다시
  }
}

static void test_shake_is_not_fooled() {
  const ShakeDetector::Limits lim{1200, 4, 800, 1500};
  {  // 약하게 흔듦(걷거나 손짓): 넘지 않는다.
    ShakeDetector s(lim);
    uint32_t t = 0;
    for (int i = 0; i < 50; i++, t += 10) s.feed(0, 0, 1000, t);
    CHECK(shake(s, t, 700, 6, 20) == 0);
  }
  {  // 떨어뜨리거나 탁 내려놓음: 큰 충격이 한두 번 — 방향이 번갈아 여러 번이 아니다.
    ShakeDetector s(lim);
    uint32_t t = 0;
    for (int i = 0; i < 50; i++, t += 10) s.feed(0, 0, 1000, t);
    CHECK(shake(s, t, 6000, 3, 2) == 0);
    for (int i = 0; i < 300; i++, t += 10) CHECK(!s.feed(0, 0, 1000, t));
  }
  {  // 천천히 크게 휘두름(방향이 바뀌는 데 0.5초씩): 정해진 시간 안에 네 번이 안 된다.
    ShakeDetector s(lim);
    uint32_t t = 0;
    for (int i = 0; i < 50; i++, t += 10) s.feed(0, 0, 1000, t);
    CHECK(shake(s, t, 2200, 50, 6) == 0);
  }
  {  // 기기를 뒤집어 놓음(중력의 방향이 천천히 바뀜): 흔들기가 아니다.
    ShakeDetector s(lim);
    uint32_t t = 0;
    int fired = 0;
    for (int i = 0; i <= 200; i++, t += 10) fired += s.feed(0, 0, (int16_t)(1000 - i * 10), t);
    CHECK(fired == 0);
  }
}

// ---------- 선을 뽑으면 버튼은 블루투스로 (logic/key_route.h, D59) ----------

static void test_route_usb_first_then_bluetooth() {
  const LinkState all[] = {LinkState::Pairing, LinkState::Searching, LinkState::Connecting, LinkState::Ready};
  for (LinkState ble : all) {
    CHECK(pickRoute(LinkState::Ready) == Route::Usb);  // USB 가 되면 블루투스가 어떻든 USB
    const LinkView v = viewLink(LinkState::Ready, ble);
    CHECK(v.link == LinkState::Ready && !v.wireless);
  }
  CHECK(pickRoute(LinkState::Searching) == Route::Ble && pickRoute(LinkState::Connecting) == Route::Ble);
  // 선이 없고 블루투스가 붙어 있다 → 블루투스로 쓴다.
  LinkView v = viewLink(LinkState::Searching, LinkState::Ready);
  CHECK(v.link == LinkState::Ready && v.wireless);
  // 선은 꽂혀 있는데 컴퓨터가 허용을 기다린다: 블루투스가 되면 그것으로 쓰고, 안 되면 "허용 대기"를 알린다.
  v = viewLink(LinkState::Connecting, LinkState::Ready);
  CHECK(v.link == LinkState::Ready && v.wireless);
  v = viewLink(LinkState::Connecting, LinkState::Pairing);
  CHECK(v.link == LinkState::Connecting && !v.wireless);
  // 선도 없고 블루투스도 없다: 블루투스의 사정을 그대로 보인다(짝이 없다 / 짝은 있는데 연결이 없다 / 붙는 중).
  v = viewLink(LinkState::Searching, LinkState::Pairing);
  CHECK(v.link == LinkState::Pairing && v.wireless);
  v = viewLink(LinkState::Searching, LinkState::Searching);
  CHECK(v.link == LinkState::Searching && v.wireless);
  v = viewLink(LinkState::Searching, LinkState::Connecting);
  CHECK(v.link == LinkState::Connecting && v.wireless);
}

static void test_state_wireless() {
  TouchState s;
  Frame f{};
  // 짝을 맺은 컴퓨터가 없다: 블루투스 설정에서 연결하라고 알린다.
  s.setLink(LinkState::Pairing, 0, true);
  CHECK(poll(s, 0, f) && f.face == Face::Pair && f.status.link == LinkState::Pairing && f.status.wireless);
  CHECK(specFor(Face::Pair).hint == art::HINT_PAIR);
  // 블루투스가 붙는 중: 사람이 할 일이 없다(허용 안내를 띄우지 않는다 — 그것은 USB 의 일이다).
  s.setLink(LinkState::Connecting, 1000, true);
  CHECK(poll(s, 1000, f) && f.face == Face::Hi);
  CHECK(!poll(s, 1000 + TouchState::ALLOW_AFTER_MS + 100, f));
  // 블루투스로 쓰는 중.
  s.setLink(LinkState::Ready, 5000, true);
  CHECK(poll(s, 5000, f) && f.face == Face::Hi && f.status.wireless && f.status.link == LinkState::Ready);
  // 말하기: 컴퓨터가 마이크를 열었는지 알 길이 없다 → 누른 것으로 미루어 듣는 중을 보인다(D65).
  s.onSent(Ev::TalkTap, 6000);
  CHECK(poll(s, 6000, f) && f.face == Face::Talk && f.micLive && f.guess && f.status.wireless);
  poll(s, 6000 + 5000, f);
  CHECK(f.face == Face::Talk && f.micLive);  // "보냈다"로 잠깐 떴다 사라지지 않는다
  s.onSent(Ev::TalkTap, 12000);  // 다시 눌러 끈다
  CHECK(poll(s, 12000, f) && f.face == Face::Hi && !f.micLive);
  // 선을 꽂으면(USB) 사실을 따른다: 누르면 미루어 듣는 중, 맥이 마이크를 열면 사실로.
  s.setLink(LinkState::Ready, 20000, false);
  CHECK(poll(s, 20000, f) && !f.status.wireless);
  s.onSent(Ev::TalkTap, 21000);
  CHECK(poll(s, 21000, f) && f.face == Face::Talk && f.micLive && f.guess);
  s.setMicLive(true);
  CHECK(poll(s, 21400, f) && f.micLive && !f.guess);
  s.setMicLive(false);
  CHECK(poll(s, 25000, f) && !f.micLive && f.face == Face::Hi);
  // 미루어 듣는 중에 길이 바뀌면(선을 뽑았다 · 꽂았다) 접는다: 받아쓰기가 어떻게 됐는지 알 수 없다.
  s.onSent(Ev::TalkTap, 30000);
  poll(s, 30000, f);
  CHECK(f.micLive && f.guess);
  s.setLink(LinkState::Ready, 30500, true);
  CHECK(poll(s, 30500, f) && !f.micLive && f.status.wireless);
  // 짝을 지웠다(확인+취소 3초).
  s.onPairReset(true, 40000);
  CHECK(poll(s, 40000, f) && f.face == Face::PairCleared && specFor(Face::PairCleared).hint == art::HINT_CLEARED);
  // 지우지 못했다: "키가 나가지 않았다"(NoLink)와는 다른 일이므로 다른 장면·다른 말이다(D72).
  s.onPairReset(false, 50000);
  CHECK(poll(s, 50000, f) && f.face == Face::PairFail && specFor(Face::PairFail).hint == art::HINT_NOTCLEARED);
  // 연결이 살아 있어도 그 말은 제 시간만큼 보인다(연결되면 곧바로 내리는 것은 "키가 나가지 않았다"뿐이다).
  s.setLink(LinkState::Ready, 50100, true);
  CHECK(!poll(s, 50100, f) && f.face == Face::PairFail);
  CHECK(poll(s, 50000 + TouchState::ACTION_SHOW_MS, f) && f.face == Face::Hi);
}

// D72 화면의 말 하나는 한 가지 뜻으로만 쓴다.
//   앞선 판은 `Not connected` 를 세 곳에 썼다: 상태 막대의 "연결이 없다" · 캐릭터 아래의 "키가 나가지 않았다" ·
//   "짝을 지우지 못했다". 블루투스로 연결된 채 키만 나가지 않았을 때도 `Not connected` 가 떠서,
//   연결돼 있는데 왜 "연결 없음"이라고 나오느냐는 혼란이 있었다.
static bool sameMask(const art::Mask& a, const art::Mask& b) {
  return a.w == b.w && a.h == b.h && memcmp(a.a, b.a, (size_t)a.w * a.h) == 0;
}
static void test_words_have_one_meaning_each() {
  CHECK(specFor(Face::NoLink).hint == art::HINT_NOTSENT);
  CHECK(specFor(Face::PairFail).hint == art::HINT_NOTCLEARED);
  // 화면에 찍히는 글자 그림이 서로 다르다: 안내 말끼리, 낱말끼리, 안내 말과 낱말 사이에도 같은 것이 없다.
  int same = 0;
  for (int i = 0; i < art::HINT_COUNT; i++) {
    for (int j = i + 1; j < art::HINT_COUNT; j++) same += sameMask(art::HINT[i], art::HINT[j]);
    for (int j = 0; j < art::WORD_COUNT; j++) same += sameMask(art::HINT[i], art::WORD[j]);
  }
  for (int i = 0; i < art::WORD_COUNT; i++) {
    for (int j = i + 1; j < art::WORD_COUNT; j++) same += sameMask(art::WORD[i], art::WORD[j]);
  }
  CHECK(same == 0);
  // 새 말도 무대의 폭에 들어간다(좌우 4픽셀씩 남긴다).
  CHECK(art::HINT[art::HINT_NOTSENT].w <= W - 8 && art::HINT[art::HINT_NOTCLEARED].w <= W - 8);
  // 키가 나가지 않았을 때: 상태 막대는 "지금의 연결"을, 캐릭터 아래는 "방금의 일"을 말한다.
  TouchState s;
  Frame f{};
  s.setLink(LinkState::Ready, 0, true);  // 블루투스로 연결돼 있다
  poll(s, 0, f);
  s.onNotSent(1000);                     // 그런데 키가 나가지 않았다
  CHECK(poll(s, 1000, f) && f.face == Face::NoLink);
  CHECK(wordFor(f) == art::WORD_BT && specFor(f.face).hint == art::HINT_NOTSENT);
  s.setLink(LinkState::Searching, 2000, false);  // 연결이 없다
  s.onNotSent(2000);
  CHECK(poll(s, 2000, f) && wordFor(f) == art::WORD_OFF && specFor(f.face).hint == art::HINT_NOTSENT);
}

// ---------- Back·Next: 짧게 = 옆 패널, 길게 = 다른 창 (logic/pane_switch.h, D58) ----------

static void test_pane_tap_is_pane_on_release() {
  PaneSwitch p;
  CHECK(p.onEvent(Ev::WindowNext, 1000) && p.waiting() && p.waitingNext());
  CHECK(p.poll(1010, true) == PaneAction::None);   // 누르고 있는 동안에는 아직 모른다
  CHECK(p.poll(1200, true) == PaneAction::None);
  CHECK(p.poll(1210, false) == PaneAction::PaneNext);  // 뗀 순간 "옆 패널로"
  CHECK(!p.waiting());
  CHECK(p.poll(1220, false) == PaneAction::None);  // 한 번만
  CHECK(p.onEvent(Ev::WindowPrev, 2000) && !p.waitingNext());
  CHECK(p.poll(2010, false) == PaneAction::PanePrev);  // 아주 짧은 톡도 같다
}

static void test_pane_hold_is_window_once() {
  PaneSwitch p;
  p.onEvent(Ev::WindowPrev, 1000);
  CHECK(p.poll(1000 + PaneSwitch::HOLD_MS - 10, true) == PaneAction::None);
  CHECK(p.poll(1000 + PaneSwitch::HOLD_MS, true) == PaneAction::WindowPrev);  // 누른 채로 "다른 창으로"
  CHECK(p.poll(1000 + PaneSwitch::HOLD_MS + 500, true) == PaneAction::None);  // 계속 눌러도 한 번만
  CHECK(p.poll(5000, false) == PaneAction::None);                             // 떼어도 패널 키가 따라 나가지 않는다
  p.onEvent(Ev::WindowNext, 6000);
  CHECK(p.poll(6000 + PaneSwitch::HOLD_MS, true) == PaneAction::WindowNext);
  // loop 가 막혀 마감을 지난 뒤에야 보았고 그 사이에 손을 뗐다면: 길게가 아니라 짧게다(눌려 있는 것을 본 적이 없다).
  p.onEvent(Ev::WindowNext, 8000);
  CHECK(p.poll(8000 + PaneSwitch::HOLD_MS + 300, false) == PaneAction::PaneNext);
}

static void test_pane_leaves_other_events_alone() {
  PaneSwitch p;
  const Ev others[] = {Ev::TalkTap, Ev::TalkDown, Ev::TalkUp, Ev::Confirm, Ev::Cancel, Ev::Up, Ev::Down, Ev::PairReset, Ev::None};
  for (Ev e : others) CHECK(!p.onEvent(e, 100) && !p.waiting());
  CHECK(p.poll(200, true) == PaneAction::None);
  // 기다리던 누름을 버린다(연결이 끊기거나 펌웨어 물음이 뜰 때): 뗀 뒤에도 아무것도 나가지 않는다.
  p.onEvent(Ev::WindowNext, 1000);
  p.reset();
  CHECK(!p.waiting() && p.poll(1100, false) == PaneAction::None);
  // 시계가 한 바퀴 도는 자리에서도 같다.
  p.onEvent(Ev::WindowNext, 0xFFFFFF00u);
  CHECK(p.poll(0xFFFFFF00u + 100, true) == PaneAction::None);
  CHECK(p.poll(0xFFFFFF00u + PaneSwitch::HOLD_MS, true) == PaneAction::WindowNext);
}

// ---------- 로그 포트의 명령 줄 읽기 (logic/line_reader.h) ----------

// 컴퓨터가 로그 포트로 보낸 바이트를 흉내 낸다(기기에서는 USB 가 이 자리다).
struct Bytes {
  std::vector<uint8_t> data;
  size_t pos = 0;
  void   add(const std::string& s) { data.insert(data.end(), s.begin(), s.end()); }
  int    read() { return pos < data.size() ? data[pos++] : -1; }
  size_t left() const { return data.size() - pos; }
};

// 남은 것을 한 번에 budget 바이트씩 끝까지 읽어, 받아들인 줄을 차례로 모은다.
template <size_t N>
static std::vector<std::string> pumpAll(Bytes& src, LineReader<N>& reader, int budget = 256) {
  std::vector<std::string> lines;
  while (src.left() > 0) {
    if (pumpLines(src, reader, budget, [&](const char* s) { lines.push_back(s); }) <= 0) break;
  }
  return lines;
}

static void test_line_reads_commands() {
  LineReader<48> r;
  Bytes src;
  src.add("vibe:ping\nvibe:status\n");
  std::vector<std::string> got = pumpAll(src, r);
  CHECK(got.size() == 2 && got[0] == "vibe:ping" && got[1] == "vibe:status");
  // 줄바꿈이 오기 전에는 줄이 아니다. 나머지가 뒤늦게 와도 한 줄로 이어진다.
  src.add("vibe:bo");
  CHECK(pumpAll(src, r).empty());
  src.add("ot\n");
  got = pumpAll(src, r);
  CHECK(got.size() == 1 && got[0] == "vibe:boot");
  // 빈칸이 든 명령과 0x80 이상의 바이트도 그대로 넘긴다(무슨 명령인지는 받는 쪽이 가른다).
  src.add("vibe:ev ok\n\xEA\xB0\x80\n");
  got = pumpAll(src, r);
  CHECK(got.size() == 2 && got[0] == "vibe:ev ok" && got[1] == "\xEA\xB0\x80");
}

static void test_line_endings_and_empty_lines() {
  // \n, \r, \r\n 어느 것으로 끝나도 한 줄이고, 빈 줄은 줄로 치지 않는다(\r\n 의 뒤쪽 \n 포함).
  LineReader<48> r;
  Bytes src;
  src.add("vibe:ping\r\nvibe:status\r\n\n\n\r\rvibe:boot\r");
  const std::vector<std::string> got = pumpAll(src, r);
  CHECK(got.size() == 3 && got[0] == "vibe:ping" && got[1] == "vibe:status" && got[2] == "vibe:boot");
  // 한 글자씩 넣어 보면: 줄바꿈에서만, 모은 것이 있을 때만 true.
  LineReader<8> q;
  CHECK(!q.feed('\n'));
  CHECK(!q.feed('\r'));
  CHECK(!q.feed('a'));
  CHECK(q.feed('\r'));
  CHECK(std::strcmp(q.line(), "a") == 0);
  CHECK(!q.feed('\n'));
  CHECK(!q.feed('b'));
  CHECK(!q.feed('c'));
  CHECK(q.feed('\n'));
  CHECK(std::strcmp(q.line(), "bc") == 0);
}

static void test_line_too_long_is_dropped_whole() {
  // 코덱스 검토의 재현: x 48개 + vibe:boot. 예전에는 47자를 넘는 순간 모은 것을 비우고
  // 이어서 받아, 뒷부분이 새 명령 vibe:boot 로 읽혔다. 넘친 줄은 줄바꿈까지 통째로 버려야 한다.
  LineReader<48> r;
  Bytes src;
  src.add(std::string(48, 'x') + "vibe:boot\n");
  CHECK(pumpAll(src, r).empty());
  // 버린 뒤 다음 줄은 정상으로 받는다.
  src.add("vibe:ping\n");
  const std::vector<std::string> got = pumpAll(src, r);
  CHECK(got.size() == 1 && got[0] == "vibe:ping");
}

static void test_line_length_boundary() {
  // 받을 수 있는 글자는 N-1 개다(끝의 0 자리 하나). 꼭 N-1 자는 받고, N 자부터는 버린다.
  for (size_t len = 1; len <= 2 * 48 + 2; len++) {
    LineReader<48> r;
    Bytes src;
    const std::string body(len, 'a');
    src.add(body + "\nvibe:ping\n");
    const std::vector<std::string> got = pumpAll(src, r);
    if (len <= 47) {
      CHECK(got.size() == 2 && got[0] == body && got[1] == "vibe:ping");
    } else {
      CHECK(got.size() == 1 && got[0] == "vibe:ping");
    }
  }
  // 크기가 다른 판독기도 같은 규칙이다.
  LineReader<4> small;
  Bytes src;
  src.add("abc\nabcd\nab\n");
  const std::vector<std::string> got = pumpAll(src, small);
  CHECK(got.size() == 2 && got[0] == "abc" && got[1] == "ab");
}

static void test_line_tail_of_long_line_is_never_a_command() {
  // 긴 줄이 어디서 넘치든 그 꼬리는 명령이 되지 않는다: 앞에 x 를 0~144개 붙인 "vibe:boot".
  for (size_t pad = 0; pad <= 144; pad++) {
    LineReader<48> r;
    Bytes src;
    const std::string whole = std::string(pad, 'x') + "vibe:boot";
    src.add(whole + "\n");
    const std::vector<std::string> got = pumpAll(src, r);
    if (whole.size() <= 47) {
      CHECK(got.size() == 1 && got[0] == whole);  // 들어가는 길이면 줄 전체를 그대로(꼬리만이 아니라)
    } else {
      CHECK(got.empty());
    }
  }
}

static void test_line_with_nul_is_dropped() {
  // 줄 안의 0 바이트: C 문자열은 거기서 끝나므로 "vibe:boot<0>junk" 가 vibe:boot 로 보였다(코덱스 검토).
  // 그런 줄은 통째로 버린다 — 0 이 앞·가운데·끝 어디에 있든.
  const std::string nul(1, '\0');
  const std::string cases[] = {"vibe:boot" + nul + "junk", nul + "vibe:boot", "vibe:boot" + nul,
                               "vibe:" + nul + "boot",     nul,               nul + nul + "vibe:ping" + nul};
  for (const std::string& c : cases) {
    LineReader<48> r;
    Bytes src;
    src.add(c + "\n");
    CHECK(pumpAll(src, r).empty());
    // 버린 뒤 다음 줄은 정상으로 받는다.
    src.add("vibe:ping\r\n");
    const std::vector<std::string> got = pumpAll(src, r);
    CHECK(got.size() == 1 && got[0] == "vibe:ping");
  }
  // 0 바이트가 든 줄이 길이까지 넘겨도 버리는 것은 그 한 줄뿐이다.
  LineReader<8> r;
  Bytes src;
  src.add("ab" + nul + std::string(20, 'c') + "\nok\n");
  const std::vector<std::string> got = pumpAll(src, r);
  CHECK(got.size() == 1 && got[0] == "ok");
}

static void test_line_recovers_after_long_lines() {
  // 긴 줄이 연달아 와도 그 뒤의 정상 줄은 받는다: 버리는 상태는 줄바꿈에서 끝난다.
  LineReader<16> r;
  Bytes src;
  src.add(std::string(16, 'a') + "\n" + std::string(100, 'b') + "\r\n" + std::string(15, 'c') + "\n" +
          std::string(17, 'd') + "\rvibe:ping\n");
  const std::vector<std::string> got = pumpAll(src, r);
  CHECK(got.size() == 2 && got[0] == std::string(15, 'c') && got[1] == "vibe:ping");
}

static void test_line_budget_limits_one_call() {
  // 한 번 부를 때 budget 바이트까지만 읽는다: 컴퓨터가 쉬지 않고 보내도 loop 가 읽기에 갇히지 않는다
  // (코덱스 검토 — 예전에는 온 것이 남아 있는 한 계속 읽었다).
  LineReader<48> r;
  Bytes src;
  for (int i = 0; i < 100; i++) src.add("vibe:ping\n");  // 1000바이트
  int lines = 0, wrong = 0;
  const auto count = [&](const char* s) {
    ++lines;
    if (std::strcmp(s, "vibe:ping") != 0) ++wrong;
  };
  CHECK(pumpLines(src, r, 256, count) == 256);
  CHECK(src.pos == 256);  // 한도만큼만 가져갔다(나머지는 USB 에 남아 다음 틱을 기다린다)
  CHECK(lines == 25);     // 10바이트짜리 25줄. 읽다 만 26번째 줄(6바이트)은 아직 줄이 아니다
  // 이어서 부르면 끊긴 자리에서 줄이 이어진다.
  CHECK(pumpLines(src, r, 256, count) == 256);
  CHECK(lines == 51);
  CHECK(pumpLines(src, r, 256, count) == 256);
  CHECK(pumpLines(src, r, 256, count) == 232);  // 남은 것이 한도보다 적으면 있는 만큼만
  CHECK(lines == 100 && wrong == 0);
  CHECK(pumpLines(src, r, 256, count) == 0);  // 온 것이 없으면 0
  // 한도가 0 이하면 아무것도 읽지 않는다.
  src.add("vibe:ping\n");
  CHECK(pumpLines(src, r, 0, count) == 0);
  CHECK(pumpLines(src, r, -1, count) == 0);
  CHECK(src.left() == 10 && lines == 100);
}

static void test_line_budget_split_keeps_lines_whole() {
  // 한도가 입력의 어디를 자르든 결과는 같다: 정상 줄, 넘치는 줄, 0 바이트가 든 줄, \r\n 을 섞어서.
  const std::string nul(1, '\0');
  const std::string input = "vibe:ping\r\n" + std::string(48, 'x') + "vibe:boot\n" + "vibe:status\n" +
                            "vibe:boot" + nul + "junk\n" + std::string(47, 'y') + "\n\n" +
                            std::string(200, 'z') + "\r\nvibe:ev ok\n";
  const std::vector<std::string> want = {"vibe:ping", "vibe:status", std::string(47, 'y'), "vibe:ev ok"};
  for (int budget = 1; budget <= 300; budget++) {
    LineReader<48> r;
    Bytes src;
    src.add(input);
    CHECK(pumpAll(src, r, budget) == want);
  }
}

// ---------- (개발판) 안전장치 판정 (logic/dev_rescue.h, D42) ----------

using RV = DevRescueVerdict;

// 기기의 loop 를 흉내 낸다: 10ms 마다 USB 의 상태를 읽어 판정에 넘긴다.
struct RescueRig {
  static constexpr uint32_t MOUNT_WAIT = 15000;    // config.h 의 DEV_MOUNT_WAIT_MS
  static constexpr uint32_t ALLOW_WAIT = 1800000;  // DEV_ALLOW_WAIT_MS
  static constexpr uint32_t RESCUE = 180000;       // DEV_RESCUE_MS
  DevRescue rescue{MOUNT_WAIT, ALLOW_WAIT, RESCUE};
  uint32_t  now;
  bool      connected = false;  // 컴퓨터가 말을 걸어 왔다(tud_connected)
  bool      mounted = false;    // 컴퓨터가 기기를 쓰고 있다(tud_mounted)
  uint32_t  mounts = 0;         // 잡힌 횟수(tud_mount_cb 가 올린다)

  explicit RescueRig(uint32_t bootMs = 0) : now(bootMs) { rescue.begin(bootMs); }

  RV tick() {
    now += 10;
    return rescue.poll(now, connected, mounted, mounts);
  }
  // ms 동안 돌린다. 물러나라는 판정이 나오면 그 틱에서 멈추고 그 판정을 돌려준다(now 는 그 틱의 시각).
  RV run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
      const RV v = tick();
      if (v != RV::Stay) return v;
    }
    return RV::Stay;
  }
  void mount() {
    connected = true;
    mounted = true;
    ++mounts;
  }
  void unplug() {
    connected = false;
    mounted = false;
  }
};

static void test_rescue_no_host_after_15s() {
  // (가) 컴퓨터가 말조차 걸어 오지 않으면 15초에 물러난다(충전기에 꽂혔거나 USB 가 잘못됐다).
  RescueRig rig;
  CHECK(rig.run(14990) == RV::Stay);
  CHECK(rig.tick() == RV::NoHost);
  CHECK(rig.now == 15000);
  // "켠 때"는 시계의 0 이 아니라 begin() 이 불린 때다.
  RescueRig late(2000);
  CHECK(late.run(14990) == RV::Stay);
  CHECK(late.tick() == RV::NoHost);
  CHECK(late.now == 17000);
}

static void test_rescue_flapping_host_is_not_no_host() {
  // (나) 컴퓨터가 버스를 초기화하고 다시 알아보는 동안 tud_connected() 는 잠깐 거짓이 된다.
  // 그 순간에 물러나면 막 받아들여지려던 참에 사라진다 → "말이 없음"은 15초 이어져야 인정한다.
  RescueRig rig;
  rig.connected = true;
  CHECK(rig.run(60000) == RV::Stay);  // 1분 동안 말을 걸어 왔다(아직 쓰기 시작하지는 않음)
  rig.connected = false;
  CHECK(rig.run(300) == RV::Stay);  // 버스 초기화: 0.3초 끊김
  rig.connected = true;
  CHECK(rig.run(1000) == RV::Stay);
  // 짧게 붙었다가 길게(15초에 조금 못 미치게) 끊기기를 되풀이해도 물러나지 않는다.
  for (int i = 0; i < 40; i++) {
    rig.connected = true;
    CHECK(rig.tick() == RV::Stay);
    rig.connected = false;
    CHECK(rig.run(14980) == RV::Stay);
  }
  // 마지막으로 말을 걸어 온 때부터 15초가 차면 그때 물러난다.
  rig.connected = true;
  CHECK(rig.tick() == RV::Stay);
  const uint32_t last = rig.now;
  rig.connected = false;
  CHECK(rig.run(14990) == RV::Stay);
  CHECK(rig.tick() == RV::NoHost);
  CHECK(rig.now == last + 15000);
}

static void test_rescue_waits_30min_for_allow() {
  // (다) 말은 걸어 왔는데 쓰기 시작하지 않는다(맥이 잠겨 있거나 "허용"을 기다린다): 30분까지 기다린다.
  // 맥북이 잠겨 있어 15초 만에 물러난 일이 있었다.
  RescueRig rig;
  rig.connected = true;
  CHECK(rig.run(1799990) == RV::Stay);
  CHECK(rig.tick() == RV::NeverMounted);
  CHECK(rig.now == 1800000);
}

static void test_rescue_no_host_comes_before_never_mounted() {
  // 한 번도 잡히지 않은 채 두 마감이 같은 틱에 차면 "말이 없음"이 먼저다(옛 코드의 순서 그대로).
  RescueRig rig;
  rig.connected = true;
  CHECK(rig.run(1785000) == RV::Stay);  // 29분 45초까지 말을 걸어 왔다
  rig.connected = false;
  CHECK(rig.run(14990) == RV::Stay);
  CHECK(rig.tick() == RV::NoHost);  // 30분 정각: 두 마감이 함께 찼다
  CHECK(rig.now == 1800000);
  // 말이 끊긴 지 15초가 안 됐으면 30분의 마감이 먼저 온다.
  RescueRig other;
  other.connected = true;
  CHECK(other.run(1790000) == RV::Stay);
  other.connected = false;
  CHECK(other.run(9990) == RV::Stay);
  CHECK(other.tick() == RV::NeverMounted);
  CHECK(other.now == 1800000);
}

static void test_rescue_first_mount_at_6m29s_does_not_trip() {
  // (라) 에 실제로 있었던 일(블랙박스: 6분 29초, 까닭 4): 맥이 잠금을 풀고서야 기기를
  // 받아들였는데, "명령이 없었던 시간"을 켠 때부터 세고 있어서 받아들인 바로 그 순간에 물러났다.
  // 잡힌 때부터 세어야 한다.
  RescueRig rig;
  rig.connected = true;
  CHECK(rig.run(388990) == RV::Stay);
  rig.mount();
  CHECK(rig.tick() == RV::Stay);  // 6분 29초: 처음 잡힌 순간
  CHECK(rig.now == 389000);
  CHECK(rig.run(179990) == RV::Stay);
  CHECK(rig.tick() == RV::NoCommand);  // 잡힌 때부터 3분
  CHECK(rig.now == 389000 + 180000);
}

static void test_rescue_command_restarts_count() {
  // (마) 명령이 오면 그때부터 다시 3분.
  RescueRig rig;
  rig.mount();
  CHECK(rig.run(170000) == RV::Stay);
  rig.rescue.onCommand(rig.now);
  CHECK(rig.run(179990) == RV::Stay);
  CHECK(rig.tick() == RV::NoCommand);
  CHECK(rig.now == 170000 + 180000);
  // 명령이 3분 안쪽으로 계속 오면 물러나지 않는다(tools/vibectl.py keepalive 가 하는 일).
  // 30분을 넘겨도 그렇다: 한 번 잡힌 뒤에는 "한 번도 잡히지 않음"의 마감이 없다.
  RescueRig kept;
  kept.mount();
  for (int i = 0; i < 20; i++) {
    CHECK(kept.run(170000) == RV::Stay);
    kept.rescue.onCommand(kept.now);
  }
  // 잡힌 뒤에는 선이 뽑혀도 "말이 없음"(15초)이 아니라 "명령 없음"(3분)으로 센다.
  kept.unplug();
  CHECK(kept.run(179990) == RV::Stay);
  CHECK(kept.tick() == RV::NoCommand);
}

static void test_rescue_remount_between_ticks_restarts_count() {
  // (바) loop 가 막혀 있는 동안(키 보내기·화면 그리기) 떼였다가 다시 잡히면 두 틱 모두 "쓰는 중"으로
  // 보인다. 잡힌 횟수가 늘었으면 다시 잡힌 것이다 → 그때부터 다시 센다(코덱스 검토).
  // 옛 방식(직전 틱과 지금의 "쓰는 중"만 견줌)이면 다시 잡힌 지 1초 만에 물러난다.
  RescueRig rig;
  rig.mount();
  CHECK(rig.run(179000) == RV::Stay);
  ++rig.mounts;  // 두 틱 사이에 떼였다 다시 잡혔다: mounted 는 계속 참
  CHECK(rig.tick() == RV::Stay);
  const uint32_t again = rig.now;
  CHECK(rig.run(179990) == RV::Stay);
  CHECK(rig.tick() == RV::NoCommand);
  CHECK(rig.now == again + 180000);
}

static void test_rescue_mount_seen_only_by_count() {
  // 잡혔다가 곧 떼인 것을 loop 가 한 번도 "쓰는 중"으로 보지 못해도, 횟수가 늘었으면 잡혔던 것이다:
  // "한 번도 잡히지 않음"(15초·30분)이 아니라 "명령 없음"(3분)으로 센다 — loop 가 봤을 때와 같은 결과.
  RescueRig rig;
  rig.connected = true;
  CHECK(rig.run(5000) == RV::Stay);
  ++rig.mounts;
  rig.connected = false;
  CHECK(rig.tick() == RV::Stay);
  const uint32_t seen = rig.now;
  CHECK(rig.run(179990) == RV::Stay);  // 15초가 지나도 "말이 없음"으로 물러나지 않는다
  CHECK(rig.tick() == RV::NoCommand);
  CHECK(rig.now == seen + 180000);
}

static void test_rescue_clock_read_later_does_not_invert() {
  // (사) begin() 과 onCommand() 가 받는 시각은 poll() 이 든 "틱 시작 시각"보다 조금 뒤일 수 있다
  // (시계를 따로 읽어 적은 값). "얼마나 지났나"로 세면 그 차이가 아주 큰 값으로 뒤집혀 곧바로
  // 물러난다(logic/clock.h 의 함정 — 기기에 올리기 전 검토가 잡은 버그: 개발판이 첫 명령을 받자마자
  // 물러난다, 설계서 7.1).
  {
    DevRescue r(RescueRig::MOUNT_WAIT, RescueRig::ALLOW_WAIT, RescueRig::RESCUE);
    r.begin(1005);
    CHECK(r.poll(1000, false, false, 0) == RV::Stay);  // 켠 때보다 5ms 앞선 시각을 든 틱
    CHECK(r.poll(16000, false, false, 0) == RV::Stay);  // 켠 때(1005)부터 15초는 16005
    CHECK(r.poll(16010, false, false, 0) == RV::NoHost);
  }
  {
    DevRescue r(RescueRig::MOUNT_WAIT, RescueRig::ALLOW_WAIT, RescueRig::RESCUE);
    r.begin(1005);
    CHECK(r.poll(1000, true, true, 1) == RV::Stay);  // 첫 틱에 이미 잡혀 있어도 같다
  }
  {
    // 명령을 받은 시각이 그 틱의 시각보다 뒤여도 같다.
    RescueRig rig;
    rig.mount();
    CHECK(rig.run(60000) == RV::Stay);
    rig.rescue.onCommand(rig.now + 13);  // 다음 틱의 시각(now + 10)보다 3ms 뒤
    CHECK(rig.tick() == RV::Stay);
    CHECK(rig.run(179990) == RV::Stay);
    CHECK(rig.tick() == RV::Stay);  // 명령 뒤 3분은 13ms 더 뒤다
    CHECK(rig.tick() == RV::NoCommand);
  }
}

static void test_rescue_survives_clock_wrap() {
  // 시계(32비트 ms)가 한 바퀴 도는 자리(약 49.7일)에 걸쳐도 같은 때에 판정한다.
  RescueRig a(uint32_t(0) - 10000u);  // 한 바퀴 돌기 10초 전에 켰다
  CHECK(a.run(14990) == RV::Stay);
  CHECK(a.tick() == RV::NoHost);
  CHECK(a.now == 5000);
  RescueRig b(uint32_t(0) - 100000u);
  b.mount();
  CHECK(b.tick() == RV::Stay);
  CHECK(b.run(179990) == RV::Stay);
  CHECK(b.tick() == RV::NoCommand);
  CHECK(b.now == 80010);
  RescueRig c(uint32_t(0) - 1000000u);
  c.connected = true;
  CHECK(c.run(1799990) == RV::Stay);
  CHECK(c.tick() == RV::NeverMounted);
  CHECK(c.now == 800000);
}

// ---------- D63 소리를 내는 순서(logic/sound_seq.h) ----------

struct FakeOut : soundseq::Out {
  std::string log;  // "O" 열림, "T<hz>" 음, "C" 닫힘
  int depth = 0;    // 열린 채인가(0 또는 1 이어야 한다)
  bool bad = false; // 닫힌 채 음을 냈거나, 열린 채 또 열었거나, 닫힌 채 또 닫았다
  void open() override {
    if (depth != 0) bad = true;
    depth++;
    log += "O";
  }
  void tone(uint16_t hz, uint16_t) override {
    if (depth != 1) bad = true;
    log += "T" + std::to_string(hz);
  }
  void close() override {
    if (depth != 1) bad = true;
    depth--;
    log += "C";
  }
};
static const soundseq::Tone SEQ_CLICK[] = {{1900, 14}};
static const soundseq::Tone SEQ_DONE[] = {{880, 90}, {1320, 150}};
static const soundseq::Tone SEQ_ASK[] = {{1320, 90}, {990, 90}, {1320, 150}};

static void test_sound_click_opens_and_closes() {
  soundseq::Player p(30);
  FakeOut o;
  for (uint32_t t = 0; t < 100; t += 10) p.poll(t, false, o);
  CHECK(o.log.empty());  // 청하지 않으면 아무 일도 없다(마이크를 건드리지 않는다)
  p.request(0, SEQ_CLICK, 1);
  p.poll(100, false, o);
  CHECK(o.log == "OT1900");
  CHECK(p.playing());
  p.poll(140, false, o);  // 14 + 30 = 144ms 까지는 그대로
  CHECK(o.log == "OT1900");
  p.poll(144, false, o);
  CHECK(o.log == "OT1900C");
  CHECK(!p.playing());
  for (uint32_t t = 150; t < 400; t += 10) p.poll(t, false, o);
  CHECK(o.log == "OT1900C");  // 한 번만
  CHECK(!o.bad && o.depth == 0);
}

static void test_sound_sequence_timing() {
  soundseq::Player p(30);
  FakeOut o;
  p.request(2, SEQ_ASK, 3);
  p.poll(1000, false, o);
  CHECK(o.log == "OT1320");
  p.poll(1089, false, o);
  CHECK(o.log == "OT1320");
  p.poll(1090, false, o);
  CHECK(o.log == "OT1320T990");
  p.poll(1180, false, o);
  CHECK(o.log == "OT1320T990T1320");
  p.poll(1180 + 150 + 29, false, o);  // 마지막 음 뒤에만 여유를 둔다
  CHECK(o.log == "OT1320T990T1320");
  p.poll(1180 + 150 + 30, false, o);
  CHECK(o.log == "OT1320T990T1320C");
  CHECK(!o.bad && o.depth == 0);
}

static void test_sound_never_while_mic_is_live() {
  {  // 듣는 중에 청한 소리는 버린다 — 듣기가 끝난 뒤 뒤늦게 내지도 않는다.
    soundseq::Player p(30);
    FakeOut o;
    p.request(1, SEQ_DONE, 2);
    p.poll(0, true, o);
    for (uint32_t t = 10; t < 500; t += 10) p.poll(t, false, o);
    CHECK(o.log.empty());
  }
  {  // 내는 도중에 컴퓨터가 듣기 시작하면 그 자리에서 끊고 마이크를 돌려준다. 청해 둔 것도 버린다.
    soundseq::Player p(30);
    FakeOut o;
    p.request(1, SEQ_DONE, 2);
    p.poll(0, false, o);
    p.request(2, SEQ_ASK, 3);
    p.poll(40, true, o);
    CHECK(o.log == "OT880C");
    CHECK(!p.playing());
    for (uint32_t t = 50; t < 800; t += 10) p.poll(t, t < 300, o);
    CHECK(o.log == "OT880C");
    CHECK(!o.bad && o.depth == 0);
  }
}

static void test_sound_priority() {
  {  // 같은 틱에 여럿을 청하면 중요한 것 하나만 낸다(순서와 상관없이).
    soundseq::Player p(30);
    FakeOut o;
    p.request(2, SEQ_ASK, 3);
    p.request(0, SEQ_CLICK, 1);
    p.poll(0, false, o);
    CHECK(o.log == "OT1320");
  }
  {  // 알림음 도중의 누름 소리는 버린다(끝난 뒤 뒤늦게 "톡" 하지 않는다).
    soundseq::Player p(30);
    FakeOut o;
    p.request(1, SEQ_DONE, 2);
    p.poll(0, false, o);
    p.request(0, SEQ_CLICK, 1);
    for (uint32_t t = 10; t < 800; t += 10) p.poll(t, false, o);
    CHECK(o.log == "OT880T1320C");
  }
  {  // 누름 소리 도중에 온 알림은 버리지 않는다: 누름 소리가 끝난 뒤 낸다(마이크를 한 번 올렸다가).
    soundseq::Player p(30);
    FakeOut o;
    p.request(0, SEQ_CLICK, 1);
    p.poll(0, false, o);
    p.request(1, SEQ_DONE, 2);
    for (uint32_t t = 10; t < 800; t += 10) p.poll(t, false, o);
    CHECK(o.log == "OT1900COT880T1320C");
    CHECK(!o.bad && o.depth == 0);
  }
  {  // 잘못된 청은 받지 않는다.
    soundseq::Player p(30);
    FakeOut o;
    p.request(0, nullptr, 1);
    p.request(0, SEQ_CLICK, 0);
    p.request(-1, SEQ_CLICK, 1);
    p.poll(0, false, o);
    CHECK(o.log.empty());
  }
}

static void test_sound_survives_clock_wrap() {
  soundseq::Player p(30);
  FakeOut o;
  uint32_t t = 0xFFFFFFFFu - 100;  // 시계가 한 바퀴 도는 곳을 지나며
  p.request(2, SEQ_ASK, 3);
  for (int i = 0; i < 60; i++, t += 10) p.poll(t, false, o);
  CHECK(o.log == "OT1320T990T1320C");
  CHECK(!o.bad && o.depth == 0);
}

int main() {
  RUN(test_layout_fits_screen);
  RUN(test_layout_zones_keep_gaps);
  RUN(test_layout_hit_test);
  RUN(test_layout_big_four_line_up);
  RUN(test_layout_matches_art);
  RUN(test_layout_view_tap_area);
  RUN(test_touch_tap_face_is_one_talk);
  RUN(test_touch_very_short_tap_still_counts);
  RUN(test_touch_single_glitch_is_ignored);
  RUN(test_touch_ok_fires_on_release);
  RUN(test_touch_cancel_once_per_second);
  RUN(test_touch_arrows);
  RUN(test_touch_arrow_repeats_while_held);
  RUN(test_touch_two_quick_arrow_taps);
  RUN(test_touch_arrow_tap_survives_stalls);
  RUN(test_touch_arrow_hold_ends);
  RUN(test_touch_bound_to_first_zone);
  RUN(test_touch_started_in_gap_never_presses);
  RUN(test_touch_brief_dropout_is_bridged);
  RUN(test_touch_two_fingers);
  RUN(test_touch_face_held_blocks_buttons);
  RUN(test_touch_fill_values);
  RUN(test_touch_view_tap);
  RUN(test_state_idle_faces);
  RUN(test_state_talk_follows_real_mic);
  RUN(test_state_guess_when_mic_never_opens);
  RUN(test_state_guess_ends);
  RUN(test_state_guess_ignores_room_noise);
  RUN(test_state_guess_drops_when_keys_fail);
  RUN(test_state_flip_guess);
  RUN(test_speech_ear);
  RUN(test_loud_stats);
  RUN(test_log_budget);
  RUN(test_calendar);
  RUN(test_hist_ring);
  RUN(test_host_pulse);
  RUN(test_quiet_gate);
  RUN(test_cancel_guard);
  RUN(test_view_tap_guard);
  RUN(test_view_tap_respects_quiet);
  RUN(test_update_prompt_blocks_pane_keys);
  RUN(test_state_old_times_do_not_come_back);
  RUN(test_state_mic_opened_by_someone_else);
  RUN(test_state_actions_show_briefly);
  RUN(test_state_enter_while_listening);
  RUN(test_state_arrow_while_listening_returns_to_talk);
  RUN(test_state_link);
  RUN(test_state_allow_hint);
  RUN(test_state_update_prompt);
  RUN(test_update_ok_after_armed_is_yes);
  RUN(test_update_yes_needs_a_long_press);
  RUN(test_update_long_press_respects_quiet);
  RUN(test_update_ok_before_armed_is_swallowed);
  RUN(test_update_ok_held_from_before_is_not_yes);
  RUN(test_update_ok_pressed_before_armed_held_past_is_not_yes);
  RUN(test_update_nope_cancels_and_leaks_no_key);
  RUN(test_update_ok_released_after_timeout_is_no_key);
  RUN(test_update_edges_swept);
  RUN(test_update_nope_across_arming_still_cancels);
  RUN(test_update_gate_verdicts);
  RUN(test_state_dirty_regions);
  RUN(test_voice_gate);
  RUN(test_ring_passes_in_order);
  RUN(test_ring_overrun_drops_new);
  RUN(test_ring_discard);
  RUN(test_ring_two_threads_keep_order);
  RUN(test_gain);
  RUN(test_draw_covers_whole_screen);
  RUN(test_draw_zone_pushes_exactly_its_rect);
  RUN(test_draw_view_parts_stay_in_their_rects);
  RUN(test_draw_partial_equals_full);
  RUN(test_painter_converges_with_interleaving);
  RUN(test_painter_order);
  RUN(test_draw_dirty_is_minimal);
  RUN(test_draw_looks);
  RUN(test_draw_stage_fits);
  RUN(test_draw_status_fits);
  RUN(test_draw_status_colors);
  RUN(test_draw_listening_is_red);
  RUN(test_draw_guess_is_a_ring);
  RUN(test_draw_dev_band);
  RUN(test_state_status_follows_link_and_battery);
  RUN(test_clock_shows_only_when_resting);
  RUN(test_notes_from_claude);
  RUN(test_power_dims_then_turns_off);
  RUN(test_power_wakes_without_a_touch);
  RUN(test_power_proximity_and_brightness_do_not_flicker);
  RUN(test_motion_still_and_moving);
  RUN(test_shake_is_recognised_once);
  RUN(test_shake_is_not_fooled);
  RUN(test_route_usb_first_then_bluetooth);
  RUN(test_state_wireless);
  RUN(test_words_have_one_meaning_each);
  RUN(test_pane_tap_is_pane_on_release);
  RUN(test_pane_hold_is_window_once);
  RUN(test_pane_leaves_other_events_alone);
  RUN(test_line_reads_commands);
  RUN(test_line_endings_and_empty_lines);
  RUN(test_line_too_long_is_dropped_whole);
  RUN(test_line_length_boundary);
  RUN(test_line_tail_of_long_line_is_never_a_command);
  RUN(test_line_with_nul_is_dropped);
  RUN(test_line_recovers_after_long_lines);
  RUN(test_line_budget_limits_one_call);
  RUN(test_line_budget_split_keeps_lines_whole);
  RUN(test_rescue_no_host_after_15s);
  RUN(test_rescue_flapping_host_is_not_no_host);
  RUN(test_rescue_waits_30min_for_allow);
  RUN(test_rescue_no_host_comes_before_never_mounted);
  RUN(test_rescue_first_mount_at_6m29s_does_not_trip);
  RUN(test_rescue_command_restarts_count);
  RUN(test_rescue_remount_between_ticks_restarts_count);
  RUN(test_rescue_mount_seen_only_by_count);
  RUN(test_rescue_clock_read_later_does_not_invert);
  RUN(test_rescue_survives_clock_wrap);
  RUN(test_sound_click_opens_and_closes);
  RUN(test_sound_sequence_timing);
  RUN(test_sound_never_while_mic_is_live);
  RUN(test_sound_priority);
  RUN(test_sound_survives_clock_wrap);
  return finish();
}
