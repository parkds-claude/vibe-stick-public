// 화면 그리기(순수 코드) 시험. 기기가 필요 없다.
#include <cstring>
#include <vector>

// 빌드 때 MASCOT_ID 를 주지 않았는지 기억해 둔다(pixel_art.h 가 기본값을 채우기 전에).
#ifndef MASCOT_ID
#define MASCOT_ID_NOT_GIVEN 1
#else
#define MASCOT_ID_NOT_GIVEN 0
#endif

#include "check.h"
#include "logic/pixel_art.h"
#include "logic/scene.h"
#include "logic/screen_draw.h"
#include "logic/screen_state.h"

using namespace screen;

static const Scene ALL_SCENES[] = {
    Scene::Ready, Scene::Pairing, Scene::Searching, Scene::Connecting, Scene::Talk,
    Scene::Ok,    Scene::Nope,    Scene::Up,        Scene::Down,       Scene::Next,
    Scene::Back,  Scene::NoLink,  Scene::Bye,       Scene::Oops,
};
static const LinkState ALL_LINKS[] = {LinkState::Pairing, LinkState::Searching,
                                      LinkState::Connecting, LinkState::Ready};

// 그린 것을 화면 크기의 점 배열에 담는 캔버스. 화면 밖으로 나간 사각형을 센다.
struct BufCanvas : Canvas {
  static constexpr uint16_t UNTOUCHED = 0x0001;  // 어떤 장면도 쓰지 않는 색
  std::vector<uint16_t> px;
  int outside = 0;
  int rects = 0;
  BufCanvas() : px(W * H, UNTOUCHED) {}
  void fillRect(int x, int y, int w, int h, uint16_t color) override {
    ++rects;
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > W || y + h > H) {
      ++outside;
      return;
    }
    for (int yy = y; yy < y + h; yy++) {
      for (int xx = x; xx < x + w; xx++) px[yy * W + xx] = color;
    }
  }
  uint16_t at(int x, int y) const { return px[y * W + x]; }
  int count(uint16_t color) const {
    int n = 0;
    for (uint16_t p : px) n += (p == color);
    return n;
  }
};

static Frame frame(Scene s, LinkState l = LinkState::Ready, int batt = 72, bool noStick = false) {
  Frame f{};
  f.scene = s;
  f.link = l;
  f.batteryPct = batt;
  f.noStick = noStick;
  return f;
}

// ---- 글자 ----

static void test_every_used_char_has_glyph() {
  for (Scene s : ALL_SCENES) {
    const SceneSpec& sp = specFor(s);
    CHECK(sp.line1 != nullptr && sp.line1[0] != 0);
    for (const char* line : {sp.line1, sp.line2}) {
      for (const char* p = line; p && *p; ++p) CHECK(*p == ' ' || pixelart::hasGlyph(*p));
    }
  }
  for (LinkState l : ALL_LINKS) {
    for (const char* p = linkWord(l); *p; ++p) CHECK(pixelart::hasGlyph(*p));
  }
  for (const char* p = NO_STICK_TEXT; *p; ++p) CHECK(*p == ' ' || pixelart::hasGlyph(*p));
  CHECK(!pixelart::hasGlyph('@'));
}

static void test_bold_glyph_shapes() {
  const pixelart::Glyph i = pixelart::boldGlyph('I');
  CHECK(i.w == 6 && i.h == 8);
  const pixelart::Glyph m = pixelart::boldGlyph('M');
  CHECK(m.w == 7 && m.h == 8);  // 굵게 하면 뭉개지는 글자는 따로 그린 모양을 쓴다
  // 느낌표: 줄기와 점 사이에 빈 줄이 있어야 "I" 로 보이지 않는다
  const pixelart::Glyph ex = pixelart::boldGlyph('!');
  bool gap = false;
  for (int r = 1; r < ex.h - 1; r++) gap = gap || ex.rows[r] == 0;
  CHECK(gap);
  CHECK(ex.rows[0] != 0 && ex.rows[ex.h - 1] != 0);
  // 작은 글자는 높이 7
  CHECK(pixelart::smallGlyph('A').h == 7);
  // 소문자는 대문자와 같은 모양
  CHECK(std::memcmp(pixelart::boldGlyph('a').rows, pixelart::boldGlyph('A').rows, 8) == 0);
  // 너비: 글자 사이 한 칸
  CHECK(pixelart::boldWidth("II") == 6 + 1 + 6);
  CHECK(pixelart::boldWidth("") == 0);
}

// ---- 마스코트 (D26) ----

static const pixelart::Eyes ALL_EYES[] = {
    pixelart::Eyes::Dot,   pixelart::Eyes::Wide,  pixelart::Eyes::Happy, pixelart::Eyes::Sleep,
    pixelart::Eyes::Cross, pixelart::Eyes::Up,    pixelart::Eyes::Down,  pixelart::Eyes::Left,
    pixelart::Eyes::Right, pixelart::Eyes::Heart, pixelart::Eyes::Sad};
static const pixelart::Mouth ALL_MOUTHS[] = {pixelart::Mouth::None, pixelart::Mouth::Open,
                                             pixelart::Mouth::Flat};

static uint16_t c565(pixelart::Rgb c) { return rgb565(c.r, c.g, c.b); }

// 고르는 이름(MascotId)과 표의 순서가 맞는다 — 어긋나면 Cat 을 골랐는데 로봇이 나온다
static void test_mascot_ids_match_table() {
  using pixelart::MascotId;
  CHECK(std::strcmp(pixelart::mascotAt((int)MascotId::Stick).name, "stick") == 0);
  CHECK(std::strcmp(pixelart::mascotAt((int)MascotId::Keycap).name, "keycap") == 0);
  CHECK(std::strcmp(pixelart::mascotAt((int)MascotId::Cat).name, "cat") == 0);
  CHECK(std::strcmp(pixelart::mascotAt((int)MascotId::Robot).name, "robot") == 0);
  CHECK(std::strcmp(pixelart::mascotAt((int)MascotId::Sprout).name, "sprout") == 0);
  CHECK(std::strcmp(pixelart::mascotAt((int)MascotId::Claude).name, "claude") == 0);
  // 범위 밖 번호는 이 빌드의 마스코트로 돌린다
  CHECK(&pixelart::mascotAt(-1) == &pixelart::mascot());
  CHECK(&pixelart::mascotAt(pixelart::MASCOT_COUNT) == &pixelart::mascot());
}

// 기기가 쓰는 마스코트는 빌드 때 준 MASCOT_ID 이고, 주지 않으면 클로드다(D29. 공개판은 까망이 D26)
static void test_build_picks_the_mascot() {
  CHECK(&pixelart::mascot() == &pixelart::mascotAt((int)pixelart::MascotId::MASCOT_ID));
  if (MASCOT_ID_NOT_GIVEN) CHECK(std::strcmp(pixelart::mascot().name, "claude") == 0);
  // 눈·입·테두리의 검정은 한 값이다(따로 적으면 조용히 어긋난다)
  CHECK(c565(pixelart::INK_RGB) == color::INK);
}

// 몸 격자: 줄마다 너비가 같고, 쓰인 글자는 모두 색 표에 있다(없으면 그 칸에 구멍이 난다)
static void test_mascot_grids_are_well_formed() {
  for (int i = 0; i < pixelart::MASCOT_COUNT; i++) {
    const pixelart::Mascot& m = pixelart::mascotAt(i);
    CHECK(m.w > 0 && m.w <= pixelart::MASCOT_MAX_W);
    CHECK(m.h > 0 && m.h <= pixelart::MASCOT_MAX_H);
    int filled = 0;
    for (int r = 0; r < m.h; r++) {
      CHECK((int)std::strlen(m.rows[r]) == m.w);
      for (int k = 0; k < m.w && m.rows[r][k]; k++) {
        pixelart::Rgb col{};
        const bool has = pixelart::mascotCell(m, k, r, col);
        CHECK(has == (m.rows[r][k] != '.'));
        filled += has;
      }
    }
    CHECK(filled > 40);
    pixelart::Rgb col{};  // 격자 밖은 빈칸
    CHECK(!pixelart::mascotCell(m, -1, 0, col) && !pixelart::mascotCell(m, m.w, 0, col));
    CHECK(!pixelart::mascotCell(m, 0, -1, col) && !pixelart::mascotCell(m, 0, m.h, col));
  }
}

// 모든 표정(눈 11 x 입 3)이 몸 위에 놓이고, 그 자리의 몸 색과 달라 눈에 보인다
static void test_mascot_faces_sit_on_body() {
  for (int i = 0; i < pixelart::MASCOT_COUNT; i++) {
    const pixelart::Mascot& m = pixelart::mascotAt(i);
    int checked = 0;
    auto check = [&](const pixelart::Cell* cells, int n) {
      for (int j = 0; j < n; j++) {
        for (int dr = 0; dr < cells[j].h; dr++) {
          for (int dc = 0; dc < cells[j].w; dc++) {
            pixelart::Rgb body{};
            CHECK(pixelart::mascotCell(m, cells[j].col + dc, cells[j].row + m.faceDy + dr, body));
            CHECK(c565(body) != c565(m.face));
            ++checked;
          }
        }
      }
    };
    const pixelart::Cell* cells = nullptr;
    for (pixelart::Eyes e : ALL_EYES) {
      const int n = pixelart::eyeCells(e, cells);
      check(cells, n);
    }
    for (pixelart::Mouth mo : ALL_MOUTHS) {
      const int n = pixelart::mouthCells(mo, cells);
      check(cells, n);
    }
    CHECK(checked > 50);
  }
}

// 마스코트는 어느 장면에서나 위 띠와 말풍선 꼬리 사이, 화면 안쪽에 놓인다(테두리까지)
static void test_mascot_stays_between_bar_and_bubble() {
  const int EDGE = 2, TAIL_TOP = BUBBLE_Y - 13;
  for (int i = 0; i < pixelart::MASCOT_COUNT; i++) {
    const pixelart::Mascot& m = pixelart::mascotAt(i);
    const int top = mascotY(m) - EDGE, bottom = MASCOT_GROUND + EDGE;
    const int left = mascotX(m) - EDGE, right = mascotX(m) + m.w * MASCOT_SCALE + EDGE;
    CHECK(top >= BAR_H + 6);
    CHECK(bottom <= TAIL_TOP - 6);
    CHECK(left >= 4 && right <= W - 4);
    for (Scene s : ALL_SCENES) {
      BufCanvas c;
      drawFrame(c, frame(s), m);
      CHECK(c.outside == 0);
      CHECK(c.count(BufCanvas::UNTOUCHED) == 0);
      // 그 자리 밖(위 띠 아래 ~ 말풍선 꼬리 위)에는 검은 점이 없다 = 테두리가 삐져나오지 않았다
      int stray = 0;
      for (int y = BAR_H; y < TAIL_TOP; y++) {
        for (int x = 0; x < W; x++) {
          const bool inside = y >= top && y < bottom && x >= left && x < right;
          if (!inside && c.at(x, y) == color::INK) ++stray;
        }
      }
      CHECK(stray == 0);
    }
  }
}

// 마스코트는 자기 색으로, 검은 테두리를 두르고 그려진다
static void test_mascot_drawn_with_its_colors_and_outline() {
  for (int i = 0; i < pixelart::MASCOT_COUNT; i++) {
    const pixelart::Mascot& m = pixelart::mascotAt(i);
    BufCanvas c;
    drawFrame(c, frame(Scene::Ready), m);  // 눈은 점 두 개(칸 넷), 입은 없음
    const int s = MASCOT_SCALE, x0 = mascotX(m), y0 = mascotY(m);
    int body = 0, face = 0, edges = 0;
    for (int r = 0; r < m.h; r++) {
      for (int k = 0; k < m.w; k++) {
        pixelart::Rgb col{};
        if (!pixelart::mascotCell(m, k, r, col)) continue;
        const uint16_t px = c.at(x0 + k * s + s / 2, y0 + r * s + s / 2);
        CHECK(px == c565(col) || px == c565(m.face));
        body += px == c565(col);
        face += px == c565(m.face) && c565(col) != c565(m.face);
        pixelart::Rgb leftCell{};
        if (!pixelart::mascotCell(m, k - 1, r, leftCell)) {  // 왼쪽이 빈칸이면 바로 바깥은 테두리
          CHECK(c.at(x0 + k * s - 1, y0 + r * s + s / 2) == color::INK);
          ++edges;
        }
      }
    }
    CHECK(body > 40);
    CHECK(face == 4);
    CHECK(edges >= m.h);
  }
  // 기기가 쓰는 그리기(마스코트를 따로 주지 않는 쪽)는 이 빌드의 마스코트를 그린다
  BufCanvas a, b;
  drawFrame(a, frame(Scene::Talk));
  drawFrame(b, frame(Scene::Talk), pixelart::mascot());
  CHECK(a.px == b.px);
}

// 마스코트마다 그림이 다르다(표를 복사해 놓고 고치지 않은 것을 잡는다)
static void test_mascots_look_different() {
  for (int i = 0; i < pixelart::MASCOT_COUNT; i++) {
    for (int j = i + 1; j < pixelart::MASCOT_COUNT; j++) {
      BufCanvas a, b;
      drawFrame(a, frame(Scene::Ready), pixelart::mascotAt(i));
      drawFrame(b, frame(Scene::Ready), pixelart::mascotAt(j));
      CHECK(a.px != b.px);
    }
  }
}

// 모든 장면의 낱말이 말풍선 안에, 읽을 만한 크기로 들어간다.
// 실제로 그린 그림에서 말풍선 안쪽 가장자리(BUBBLE_PAD 두께)에 글자가 닿지 않았는지 본다.
static void test_bubble_text_fits() {
  for (int i = 0; i < SCENE_COUNT; i++) {
    const Scene s = (Scene)i;
    CHECK(bubbleScale(specFor(s)) >= 3);  // 읽을 만한 크기
    BufCanvas c;
    drawFrame(c, frame(s));
    int touched = 0;
    for (int y = BUBBLE_Y; y < BUBBLE_Y + BUBBLE_H; y++) {
      for (int x = BUBBLE_X; x < BUBBLE_X + BUBBLE_W; x++) {
        const bool edge = x < BUBBLE_X + BUBBLE_PAD || x >= BUBBLE_X + BUBBLE_W - BUBBLE_PAD ||
                          y < BUBBLE_Y + BUBBLE_PAD || y >= BUBBLE_Y + BUBBLE_H - BUBBLE_PAD;
        if (edge && c.at(x, y) != color::WHITE) ++touched;
      }
    }
    CHECK(touched == 0);
    // 말풍선 안에 글자가 실제로 그려졌다
    int ink = 0;
    for (int y = BUBBLE_Y; y < BUBBLE_Y + BUBBLE_H; y++) {
      for (int x = BUBBLE_X; x < BUBBLE_X + BUBBLE_W; x++) ink += c.at(x, y) == color::INK;
    }
    CHECK(ink > 200);
  }
}

// 장면마다 낱말과 표정이 뜻에 맞게 짝지어져 있다(위·아래, 다음·이전이 뒤바뀌지 않았는지)
static void test_scene_words_and_faces() {
  struct Want {
    Scene           s;
    const char*     line1;
    pixelart::Eyes  eyes;
  };
  const Want wants[] = {
      {Scene::Ready, "HI!", pixelart::Eyes::Dot},      {Scene::Pairing, "PAIR", pixelart::Eyes::Heart},
      {Scene::Searching, "MAC?", pixelart::Eyes::Sleep}, {Scene::Connecting, "WAIT", pixelart::Eyes::Dot},
      {Scene::Talk, "TALK", pixelart::Eyes::Wide},     {Scene::Ok, "OK!", pixelart::Eyes::Happy},
      {Scene::Nope, "NOPE", pixelart::Eyes::Cross},    {Scene::Up, "UP", pixelart::Eyes::Up},
      {Scene::Down, "DOWN", pixelart::Eyes::Down},     {Scene::Next, "NEXT", pixelart::Eyes::Right},
      {Scene::Back, "BACK", pixelart::Eyes::Left},     {Scene::NoLink, "NO", pixelart::Eyes::Sad},
      {Scene::Bye, "BYE!", pixelart::Eyes::Happy},     {Scene::Oops, "OOPS", pixelart::Eyes::Cross},
  };
  CHECK((int)(sizeof(wants) / sizeof(wants[0])) == SCENE_COUNT);
  for (const Want& w : wants) {
    const SceneSpec& sp = specFor(w.s);
    CHECK(sp.line1 && std::strcmp(sp.line1, w.line1) == 0);
    CHECK(sp.eyes == w.eyes);
  }
}

// 화면을 띠로 나눠 그려도 한 번에 그린 것과 점 하나까지 같다(한 번에 다 그리면 그동안 버튼을 못 읽는다)
static void test_bands_equal_whole_frame() {
  for (int i = 0; i < SCENE_COUNT; i++) {
    const Frame f = frame((Scene)i, LinkState::Ready, 40, i % 2 == 0);
    BufCanvas whole, banded;
    drawFrame(whole, f);
    for (int b = 0; b < BANDS; b++) {
      BufCanvas one;
      drawFrameBand(one, f, b);
      CHECK(one.outside == 0);
      // 이 띠는 자기 줄만 건드린다
      const int y0 = b * H / BANDS, y1 = (b + 1) * H / BANDS;
      int stray = 0;
      for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
          if ((y < y0 || y >= y1) && one.at(x, y) != BufCanvas::UNTOUCHED) ++stray;
        }
      }
      CHECK(stray == 0);
      drawFrameBand(banded, f, b);
    }
    CHECK(banded.px == whole.px);
  }
}

// ---- 장면 그리기 ----

static void test_draws_inside_screen_and_covers_it() {
  for (Scene s : ALL_SCENES) {
    for (LinkState l : ALL_LINKS) {
      for (int noStick = 0; noStick < 2; noStick++) {
        BufCanvas c;
        drawFrame(c, frame(s, l, 72, noStick != 0));
        CHECK(c.outside == 0);                        // 화면 밖에 그린 것이 없다
        CHECK(c.count(BufCanvas::UNTOUCHED) == 0);    // 화면 전체를 덮는다(앞 장면의 찌꺼기가 남지 않는다)
      }
    }
  }
}

static void test_scene_colors_and_parts() {
  BufCanvas c;
  drawFrame(c, frame(Scene::Talk));
  CHECK(c.at(0, 0) == color::INK);                          // 위 띠
  CHECK(c.at(5, 30) == color::PINK);                        // 바탕(점무늬가 아닌 곳)
  CHECK(c.at(BUBBLE_X + 2, BUBBLE_Y + 2) == color::WHITE);  // 말풍선 안
  CHECK(c.at(BUBBLE_X - 2, BUBBLE_Y + 10) == color::INK);   // 말풍선 테두리
  CHECK(c.count(color::INK) > 500);                         // 글자·테두리가 그려졌다

  BufCanvas ready, nope;
  drawFrame(ready, frame(Scene::Ready));
  drawFrame(nope, frame(Scene::Nope));
  CHECK(ready.at(5, 30) == color::CREAM);
  CHECK(nope.at(5, 30) == color::CORAL);
  CHECK(ready.px != nope.px);
}

static void test_status_bar() {
  BufCanvas ready, search;
  drawFrame(ready, frame(Scene::Ready, LinkState::Ready));
  drawFrame(search, frame(Scene::Searching, LinkState::Searching));
  // 연결됨은 초록 글자, 나머지는 노랑 글자
  int green = 0, yellow = 0;
  for (int y = 0; y < BAR_H; y++) {
    for (int x = 0; x < 100; x++) {
      green += ready.at(x, y) == color::GREEN;
      yellow += search.at(x, y) == color::YELLOW;
    }
  }
  CHECK(green > 20 && yellow > 20);
}

static void test_battery_icon() {
  auto barPixels = [](int pct, uint16_t col) {
    BufCanvas c;
    drawFrame(c, frame(Scene::Ready, LinkState::Ready, pct));
    int n = 0;
    for (int y = 0; y < BAR_H; y++) {
      for (int x = 100; x < W; x++) n += c.at(x, y) == col;
    }
    return n;
  };
  CHECK(barPixels(100, color::WHITE) > barPixels(50, color::WHITE));
  CHECK(barPixels(50, color::WHITE) > barPixels(0, color::WHITE));
  CHECK(barPixels(10, color::RED) > 0);    // 15% 이하는 빨강
  CHECK(barPixels(3, color::RED) > 0);     // 거의 비었을 때도 빨강이 보여야 한다(가장 급할 때)
  CHECK(barPixels(0, color::RED) > 0);
  CHECK(barPixels(50, color::RED) == 0);
  CHECK(barPixels(-1, color::RED) == 0);   // 모르면 빈 그림(빨강도 없음)
}

static void test_no_stick_strip() {
  BufCanvas with, without;
  drawFrame(with, frame(Scene::Ready, LinkState::Ready, 72, true));
  drawFrame(without, frame(Scene::Ready, LinkState::Ready, 72, false));
  CHECK(with.at(2, STRIP_Y + 2) == color::INK);
  CHECK(without.at(2, STRIP_Y + 2) != color::INK);
  int yellow = 0;
  for (int y = STRIP_Y; y < H; y++) {
    for (int x = 0; x < W; x++) yellow += with.at(x, y) == color::YELLOW;
  }
  CHECK(yellow > 20);
}

// ---- 장면 고르기 ----

static void test_scene_for_event() {
  Scene s = Scene::Ready;
  CHECK(sceneFor(Ev::TalkTap, s) && s == Scene::Talk);
  CHECK(sceneFor(Ev::TalkDown, s) && s == Scene::Talk);
  CHECK(sceneFor(Ev::Confirm, s) && s == Scene::Ok);
  CHECK(sceneFor(Ev::Cancel, s) && s == Scene::Nope);
  CHECK(sceneFor(Ev::Up, s) && s == Scene::Up);
  CHECK(sceneFor(Ev::Down, s) && s == Scene::Down);
  CHECK(sceneFor(Ev::WindowNext, s) && s == Scene::Next);
  CHECK(sceneFor(Ev::WindowPrev, s) && s == Scene::Back);
  CHECK(sceneFor(Ev::PairReset, s) && s == Scene::Bye);
  CHECK(!sceneFor(Ev::TalkUp, s));
  CHECK(!sceneFor(Ev::None, s));
}

static void test_idle_scene_for_link() {
  CHECK(idleSceneFor(LinkState::Ready) == Scene::Ready);
  CHECK(idleSceneFor(LinkState::Pairing) == Scene::Pairing);
  CHECK(idleSceneFor(LinkState::Searching) == Scene::Searching);
  CHECK(idleSceneFor(LinkState::Connecting) == Scene::Connecting);
}

// ---- 화면 상태: 지금 무엇을 보여야 하는가 ----

static const uint32_t SHOW = ScreenState::ACTION_SHOW_MS;

static void test_state_first_poll_draws_idle_scene() {
  ScreenState st;
  st.setLink(LinkState::Pairing);
  Frame f{};
  CHECK(st.poll(1000, f));              // 처음에는 반드시 그린다
  CHECK(f.scene == Scene::Pairing && f.link == LinkState::Pairing);
  CHECK(!st.poll(1010, f));             // 달라진 것이 없으면 다시 그리지 않는다
  st.setLink(LinkState::Ready);
  CHECK(st.poll(1020, f));
  CHECK(f.scene == Scene::Ready && f.link == LinkState::Ready);
}

// 동작 장면은 ACTION_SHOW_MS 동안 보였다가 대기 장면으로 돌아간다(D24)
static void test_state_action_shows_then_returns_to_idle() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.poll(0, f);
  st.showAction(Scene::Talk, 5000);
  CHECK(st.poll(5000, f) && f.scene == Scene::Talk);
  CHECK(!st.poll(5000 + SHOW - 1, f));
  CHECK(st.poll(5000 + SHOW, f) && f.scene == Scene::Ready);
  CHECK(!st.poll(5000 + SHOW + 500, f));
}

// 동작 장면이 떠 있는 중에 다른 동작이 오면 그것으로 바꾸고 시간을 다시 잰다
static void test_state_new_action_replaces_and_restarts() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.showAction(Scene::Up, 1000);
  st.poll(1000, f);
  st.showAction(Scene::Ok, 2000);
  CHECK(st.poll(2000, f) && f.scene == Scene::Ok);
  CHECK(!st.poll(1000 + SHOW + 10, f));  // 처음 동작의 시간이 지나도 그대로
  CHECK(st.poll(2000 + SHOW, f) && f.scene == Scene::Ready);
  // 같은 동작이 다시 오면 그림은 그대로지만 시간은 다시 잰다
  st.showAction(Scene::Ok, 10000);
  st.poll(10000, f);
  st.showAction(Scene::Ok, 11000);
  CHECK(!st.poll(11000, f));
  CHECK(!st.poll(10000 + SHOW, f));
  CHECK(st.poll(11000 + SHOW, f) && f.scene == Scene::Ready);
}

// 연결 상태가 바뀌어도 동작 장면은 제 시간을 채운다("BYE!" 가 곧바로 사라지지 않게).
// 위 띠의 연결 상태는 바로 바뀐다.
static void test_state_link_change_during_action() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.showAction(Scene::Bye, 1000);
  st.poll(1000, f);
  st.setLink(LinkState::Pairing);
  CHECK(st.poll(1100, f));
  CHECK(f.scene == Scene::Bye && f.link == LinkState::Pairing);
  CHECK(st.poll(1000 + SHOW, f) && f.scene == Scene::Pairing);
}

// 배터리: 그림이 달라질 때만 다시 그린다(1% 바뀔 때마다 화면 전체를 다시 그리지 않는다)
static void test_state_battery_redraws_only_when_icon_changes() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.setBattery(72);
  CHECK(st.poll(0, f) && f.batteryPct == 72);
  st.setBattery(71);                    // 채움 칸 수가 같다
  CHECK(!st.poll(10, f));
  st.setBattery(60);                    // 한 칸 줄어든다
  CHECK(st.poll(20, f) && f.batteryPct == 60);
  st.setBattery(16);
  st.poll(30, f);
  st.setBattery(15);                    // 15% 이하는 빨강으로 바뀐다
  CHECK(st.poll(40, f) && f.batteryPct == 15);
  st.setBattery(-1);                    // 모름
  CHECK(st.poll(50, f) && f.batteryPct == -1);
}

static void test_state_no_stick_strip() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.poll(0, f);
  CHECK(!f.noStick);
  st.setNoStick(true);
  CHECK(st.poll(10, f) && f.noStick);
  st.setNoStick(true);
  CHECK(!st.poll(20, f));
  st.setNoStick(false);
  CHECK(st.poll(30, f) && !f.noStick);
}

// ---- 말하기는 켜져 있는 동안 TALK 를 유지한다 (사용자 요청) ----

static const uint32_t TALK_MAX = ScreenState::TALK_MAX_MS;

// 켜고 끄기 방식: 조이스틱을 누르면(TalkTap) TALK 가 남고, 다시 누르면 대기 화면으로
static void test_state_talk_stays_until_toggled_off() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.poll(0, f);
  st.onSent(Ev::TalkTap, 1000);
  CHECK(st.poll(1000, f) && f.scene == Scene::Talk);
  CHECK(!st.poll(1000 + SHOW + 100, f));     // 1.5초가 지나도 그대로
  CHECK(!st.poll(1000 + 20000, f));
  st.onSent(Ev::TalkTap, 30000);             // 다시 누름 = 끔
  CHECK(st.poll(30000, f) && f.scene == Scene::Ready);
  st.onSent(Ev::TalkTap, 40000);             // 또 누르면 다시 켬
  CHECK(st.poll(40000, f) && f.scene == Scene::Talk);
}

// 앞 버튼으로 보내면 컴퓨터가 녹음을 끄므로, OK! 를 보인 뒤 대기 화면으로(TALK 로 돌아가지 않는다)
static void test_state_confirm_ends_talk() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.onSent(Ev::TalkTap, 1000);
  st.poll(1000, f);
  st.onSent(Ev::Confirm, 5000);
  CHECK(st.poll(5000, f) && f.scene == Scene::Ok);
  CHECK(st.poll(5000 + SHOW, f) && f.scene == Scene::Ready);
}

// 말하는 중의 다른 동작(취소·방향)은 잠깐 보였다가 TALK 로 돌아온다
static void test_state_other_actions_overlay_talk() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.onSent(Ev::TalkTap, 1000);
  st.poll(1000, f);
  st.onSent(Ev::Cancel, 3000);
  CHECK(st.poll(3000, f) && f.scene == Scene::Nope);
  CHECK(st.poll(3000 + SHOW, f) && f.scene == Scene::Talk);
}

// 다른 동작 장면이 떠 있는 중에 말하기를 켜면 바로 TALK 로 바뀐다(끌 때도 바로)
static void test_state_talk_toggle_replaces_overlay() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.onSent(Ev::Up, 1000);
  CHECK(st.poll(1000, f) && f.scene == Scene::Up);
  st.onSent(Ev::TalkTap, 1200);
  CHECK(st.poll(1200, f) && f.scene == Scene::Talk);
  st.onSent(Ev::Down, 2000);
  st.poll(2000, f);
  st.onSent(Ev::TalkTap, 2100);
  CHECK(st.poll(2100, f) && f.scene == Scene::Ready);
}

// 컴퓨터는 길어도 2분이면 녹음을 끈다 → 그때는 대기 화면으로
static void test_state_talk_times_out() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.onSent(Ev::TalkTap, 1000);
  st.poll(1000, f);
  CHECK(!st.poll(1000 + TALK_MAX - 1, f));
  CHECK(st.poll(1000 + TALK_MAX, f) && f.scene == Scene::Ready);
  st.onSent(Ev::TalkTap, 1000 + TALK_MAX + 5000);  // 그 뒤의 누름은 다시 "켬"
  CHECK(st.poll(1000 + TALK_MAX + 5000, f) && f.scene == Scene::Talk);
}

// 연결이 끊기면 말하기도 끝난 것으로 본다
static void test_state_link_loss_ends_talk() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.onSent(Ev::TalkTap, 1000);
  st.poll(1000, f);
  st.setLink(LinkState::Searching);
  CHECK(st.poll(2000, f) && f.scene == Scene::Searching);
  st.setLink(LinkState::Ready);
  CHECK(st.poll(3000, f) && f.scene == Scene::Ready);
}

// 누르는 동안 방식: 누르면(TalkDown) TALK, 떼면(TalkUp) 대기 화면
static void test_state_hold_mode_talk() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.onSent(Ev::TalkDown, 1000);
  CHECK(st.poll(1000, f) && f.scene == Scene::Talk);
  CHECK(!st.poll(1000 + 5000, f));
  st.onSent(Ev::TalkUp, 7000);
  CHECK(st.poll(7000, f) && f.scene == Scene::Ready);
}

// 보내지 못했으면 NO LINK 를 잠깐 보인다. 말하기 상태는 바꾸지 않는다
static void test_state_not_sent_and_pair_reset() {
  ScreenState st;
  st.setLink(LinkState::Searching);
  Frame f{};
  st.poll(0, f);
  st.onNotSent(1000);
  CHECK(st.poll(1000, f) && f.scene == Scene::NoLink);
  CHECK(st.poll(1000 + SHOW, f) && f.scene == Scene::Searching);
  st.onPairReset(true, 5000);
  CHECK(st.poll(5000, f) && f.scene == Scene::Bye);
  st.onPairReset(false, 6000);
  CHECK(st.poll(6000, f) && f.scene == Scene::Oops);
}

// 보내지 못해 NO LINK 가 떠 있는 중에 연결되면 바로 내린다("READY" 와 "NO LINK" 가 같이 뜨지 않게)
static void test_state_no_link_cleared_when_ready() {
  ScreenState st;
  st.setLink(LinkState::Connecting);
  Frame f{};
  st.poll(0, f);
  st.onNotSent(1000);
  CHECK(st.poll(1000, f) && f.scene == Scene::NoLink);
  st.setLink(LinkState::Ready);
  CHECK(st.poll(1100, f) && f.scene == Scene::Ready);
  // 다른 동작 장면은 연결 상태가 바뀌어도 제 시간을 채운다
  st.onSent(Ev::Confirm, 2000);
  st.poll(2000, f);
  st.setLink(LinkState::Searching);
  st.setLink(LinkState::Ready);
  CHECK(!st.poll(2100, f) || f.scene == Scene::Ok);
}

// 화면에 보일 것이 없는 사건은 그림을 바꾸지 않는다
static void test_state_events_without_scene() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.poll(0, f);
  st.onSent(Ev::None, 1000);
  CHECK(!st.poll(1000, f));
}

// 기기에서는 동작을 알리는 시각(키를 다 보낸 뒤)이 화면을 그릴 때 쓰는 시각(그 틱의 시작)보다
// 조금 늦다. 그래도 동작 장면이 떠야 한다 — 처음 구현은 이때 "이미 시간이 지났다"고 보고
// 곧바로 대기 화면으로 돌아가 버렸다(실물: "HI만 떠 있음").
static void test_state_action_survives_stale_poll_time() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  st.poll(1000, f);
  st.showAction(Scene::Talk, 1042);      // 키를 보내는 데 42ms 걸린 뒤의 시각
  CHECK(st.poll(1000, f) && f.scene == Scene::Talk);   // 같은 틱: 시각은 아직 틱의 시작
  CHECK(!st.poll(1010, f));
  CHECK(!st.poll(1042 + SHOW - 1, f));
  CHECK(st.poll(1042 + SHOW, f) && f.scene == Scene::Ready);
}

static void test_state_time_wraparound() {
  ScreenState st;
  st.setLink(LinkState::Ready);
  Frame f{};
  const uint32_t t0 = 0xFFFFFFFFu - 500;  // 시각이 한 바퀴 도는 구간
  st.showAction(Scene::Ok, t0);
  CHECK(st.poll(t0, f) && f.scene == Scene::Ok);
  CHECK(!st.poll(t0 + 1000, f));           // 한 바퀴 돈 뒤
  CHECK(st.poll(t0 + SHOW, f) && f.scene == Scene::Ready);
}

int main() {
  RUN(test_every_used_char_has_glyph);
  RUN(test_bold_glyph_shapes);
  RUN(test_mascot_ids_match_table);
  RUN(test_build_picks_the_mascot);
  RUN(test_mascot_grids_are_well_formed);
  RUN(test_mascot_faces_sit_on_body);
  RUN(test_mascot_stays_between_bar_and_bubble);
  RUN(test_mascot_drawn_with_its_colors_and_outline);
  RUN(test_mascots_look_different);
  RUN(test_bubble_text_fits);
  RUN(test_scene_words_and_faces);
  RUN(test_bands_equal_whole_frame);
  RUN(test_draws_inside_screen_and_covers_it);
  RUN(test_scene_colors_and_parts);
  RUN(test_status_bar);
  RUN(test_battery_icon);
  RUN(test_no_stick_strip);
  RUN(test_scene_for_event);
  RUN(test_idle_scene_for_link);
  RUN(test_state_first_poll_draws_idle_scene);
  RUN(test_state_action_shows_then_returns_to_idle);
  RUN(test_state_new_action_replaces_and_restarts);
  RUN(test_state_link_change_during_action);
  RUN(test_state_battery_redraws_only_when_icon_changes);
  RUN(test_state_no_stick_strip);
  RUN(test_state_talk_stays_until_toggled_off);
  RUN(test_state_confirm_ends_talk);
  RUN(test_state_other_actions_overlay_talk);
  RUN(test_state_talk_toggle_replaces_overlay);
  RUN(test_state_talk_times_out);
  RUN(test_state_link_loss_ends_talk);
  RUN(test_state_hold_mode_talk);
  RUN(test_state_not_sent_and_pair_reset);
  RUN(test_state_no_link_cleared_when_ready);
  RUN(test_state_events_without_scene);
  RUN(test_state_action_survives_stale_poll_time);
  RUN(test_state_time_wraparound);
  return finish();
}
