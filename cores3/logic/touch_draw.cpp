#include "touch_draw.h"

#include "pixmap.h"

using pixelart::Cell;
using pixelart::Eyes;
using pixelart::Mascot;
using pixelart::Mouth;
using pixelart::Rgb;

namespace touchui {

namespace {

using gfx::Pixmap;
using gfx::rgb565;

// ---- 색(아이폰의 색을 따른다 — 설계서 5.2) ----
const uint16_t INK    = rgb565(0x1C, 0x1C, 0x1E);  // 밝은 바탕 위의 글자·그림
const uint16_t WHITE  = rgb565(0xFF, 0xFF, 0xFF);
const uint16_t BLACK  = rgb565(0x00, 0x00, 0x00);
const uint16_t GREEN  = rgb565(0x30, 0xD1, 0x58);  // 연결됨, 충전 중
const uint16_t AMBER  = rgb565(0xF5, 0xA6, 0x23);  // 허용 대기
const uint16_t RED    = rgb565(0xFF, 0x45, 0x3A);  // 연결 안 됨, 배터리 부족
const uint16_t BLUE   = rgb565(0x0A, 0x84, 0xFF);  // 블루투스로 쓰는 중
const uint16_t GREY   = rgb565(0x8E, 0x8E, 0x93);  // 꺼진 캐릭터
const uint16_t GREY_TEXT = rgb565(0x6C, 0x6C, 0x70);
const uint16_t LIVE_FACE = rgb565(0x3A, 0x1A, 0x1A);  // 하얀 캐릭터의 눈·입

const FaceSpec SPECS[FACE_COUNT] = {
    /* Hi     */ {art::HINT_NONE,   Eyes::Dot,   Mouth::None},
    /* Plug   */ {art::HINT_CABLE,  Eyes::Sleep, Mouth::None},
    /* Talk   */ {art::HINT_NONE,   Eyes::Wide,  Mouth::Flat},  // 입은 목소리에 맞춰 여닫는다
    /* Ok     */ {art::HINT_NONE,   Eyes::Happy, Mouth::None},
    /* Nope   */ {art::HINT_NONE,   Eyes::Cross, Mouth::Flat},
    /* Up     */ {art::HINT_NONE,   Eyes::Up,    Mouth::None},
    /* Down   */ {art::HINT_NONE,   Eyes::Down,  Mouth::None},
    /* Next   */ {art::HINT_NONE,   Eyes::Right, Mouth::None},
    /* Back   */ {art::HINT_NONE,   Eyes::Left,  Mouth::None},
    /* NoLink */ {art::HINT_NOTSENT, Eyes::Sad,  Mouth::Flat},
    /* NoMic  */ {art::HINT_MIC,    Eyes::Sad,   Mouth::Flat},
    /* Update */ {art::HINT_UPDATE, Eyes::Wide,  Mouth::Open},
    /* Allow  */ {art::HINT_ALLOW,  Eyes::Heart, Mouth::None},
    /* Pair   */ {art::HINT_PAIR,   Eyes::Heart, Mouth::None},
    /* PairCleared */ {art::HINT_CLEARED, Eyes::Happy, Mouth::None},
    /* Done   */ {art::HINT_DONE,   Eyes::Happy, Mouth::None},
    /* Ask    */ {art::HINT_ASK,    Eyes::Wide,  Mouth::Open},
    /* PairFail */ {art::HINT_NOTCLEARED, Eyes::Sad, Mouth::Flat},
};

// ---- 자리 ----
const int DEV_DASH = 8;         // 개발판 표시 띠의 한 토막의 폭
const int HINT_H = 18;          // 안내 말 한 줄의 높이(gen_art.py 의 TEXT_BOX_H)
const int LIVE_SHIFT = 34;      // 듣는 중에는 캐릭터를 왼쪽으로 옮겨 파형 자리를 낸다
const int WAVE_BARS = 10, WAVE_PITCH = 7, WAVE_BAR_W = 4, WAVE_GAP = 19;
const int WAVE_H = 36;          // 가장 긴 막대
// 목소리가 들어올 때 막대의 높이. wave 가 하나 오를 때마다 왼쪽으로 한 칸씩 흐른다.
const uint8_t WAVE_HEIGHTS[WAVE_STEPS] = {8, 18, 30, 22, 36, 16, 26, 12, 20, 10};
const int WAVE_QUIET_H = 4;     // 조용할 때
static_assert(WAVE_BARS == WAVE_STEPS, "막대 수와 파형 모습 수가 같아야 한 바퀴가 이어진다");

const int DOT_X = 12, DOT_Y = 10, WORD_X = 26, WORD_Y = 5;
const int BAT_W = 30, BAT_H = 15, BAT_X = W - 16 - BAT_W, BAT_Y = 7;
const int DIGIT_H = 11;

// 보는 곳을 합치는 자리(메모리). 무대와 상태 막대를 따로 둔다.
uint16_t stageBuf[STAGE_RECT.w * STAGE_RECT.h];
uint16_t statusBuf[STATUS_RECT.w * STATUS_RECT.h];

bool hasHint(const Frame& f) { return specFor(f.face).hint != art::HINT_NONE; }

// 시각을 보이는가: 쉬는 장면(Hi)이고, 듣는 중이 아니고, 시각을 안다. (D62)
bool showsClock(const Frame& f) { return f.clockMin >= 0 && f.clockMin < 24 * 60 && f.face == Face::Hi && !f.micLive; }
const int CLOCK_GAP = 12;      // 캐릭터와 시각 사이
const int CLOCK_SHIFT = 46;    // 시각을 보일 때 캐릭터를 왼쪽으로 옮기는 만큼("23:59" 가 들어가게)
const int CLOCK_DIGIT_GAP = 1; // 숫자 사이

// 시각의 글자들(시 한두 자리 + : + 분 두 자리)을 차례로 넘긴다. 반환: 전체 너비.
template <class Fn>
int eachClockGlyph(int minutes, Fn&& fn) {
  const int h = minutes / 60, mnt = minutes % 60;
  const art::Mask* g[5];
  int n = 0;
  if (h >= 10) g[n++] = &art::BIGDIGIT[h / 10];
  g[n++] = &art::BIGDIGIT[h % 10];
  g[n++] = &art::BIGCOLON;
  g[n++] = &art::BIGDIGIT[mnt / 10];
  g[n++] = &art::BIGDIGIT[mnt % 10];
  int x = 0;
  for (int i = 0; i < n; i++) {
    fn(*g[i], x);
    x += g[i]->w + (i + 1 < n ? CLOCK_DIGIT_GAP : 0);
  }
  return x;
}

// 이 장면에서 입의 모양. 말하기 장면에서는 목소리에 맞춰 여닫는다.
Mouth mouthFor(const Frame& f) {
  if (f.face == Face::Talk) return f.mouthOpen ? Mouth::Open : Mouth::Flat;
  return specFor(f.face).mouth;
}

// 보는 곳의 바탕: 듣는 중에는 빨간 판(D64), 아니면 평소 바탕
const art::Image& viewBackdrop(const Frame& f) { return f.micLive ? art::VIEW_LIVE : art::WALL; }

uint16_t textColor(const Frame& f) { return f.micLive ? WHITE : INK; }

void drawMascot(Pixmap& p, int oy, const Frame& f, const Mascot& m) {
  const Rect r = mascotRect(f, m);
  const int s = mascotScale(f, m), x0 = r.x, y0 = r.y - oy;
  const bool grey = (f.face == Face::Plug) && !f.micLive;
  Rgb col{};
  for (int row = 0; row < m.h; row++) {
    for (int k = 0; k < m.w; k++) {
      if (!pixelart::mascotCell(m, k, row, col)) continue;
      const uint16_t body = f.micLive ? WHITE : grey ? GREY : rgb565(col.r, col.g, col.b);
      gfx::fill(p, x0 + k * s, y0 + row * s, s, s, body);
    }
  }
  const uint16_t face = f.micLive ? LIVE_FACE : rgb565(m.face.r, m.face.g, m.face.b);
  const Cell* cells = nullptr;
  int n = pixelart::eyeCells(specFor(f.face).eyes, cells);
  for (int i = 0; i < n; i++) {
    gfx::fill(p, x0 + cells[i].col * s, y0 + (cells[i].row + m.faceDy) * s, cells[i].w * s, cells[i].h * s, face);
  }
  n = pixelart::mouthCells(mouthFor(f), cells);
  for (int i = 0; i < n; i++) {
    gfx::fill(p, x0 + cells[i].col * s, y0 + (cells[i].row + m.faceDy) * s, cells[i].w * s, cells[i].h * s, face);
  }
}

void drawWave(Pixmap& p, int oy, const Frame& f, const Mascot& m) {
  const Rect r = waveRect(f, m);
  if (r.w == 0) return;
  const int cy = r.y + r.h / 2 - oy;
  for (int i = 0; i < WAVE_BARS; i++) {
    const int h = f.mouthOpen ? WAVE_HEIGHTS[(i + f.wave) % WAVE_STEPS] : WAVE_QUIET_H;
    gfx::fill(p, r.x + i * WAVE_PITCH, cy - h / 2, WAVE_BAR_W, h, WHITE);
  }
}

}  // namespace

const FaceSpec& specFor(Face f) { return SPECS[(int)f < FACE_COUNT ? (int)f : 0]; }

art::Look lookFor(const Frame& f, Zone z) {
  if (f.pressed & bit(z)) return art::LOOK_PRESSED;  // 눌림은 언제나 보인다(촉감이 없으므로 유일한 신호)
  bool enabled = (f.status.link == LinkState::Ready);
  // 펌웨어를 바꿀지 묻는 동안에는 예(확인)·아니오(취소)만 켠다.
  if (f.face == Face::Update) enabled = enabled && (z == Zone::Nope || z == Zone::Ok);
  if (!enabled) return art::LOOK_OFF;
  if (z == Zone::Talk && f.micLive) return art::LOOK_LIVE;
  return art::LOOK_NORMAL;
}

art::Word wordFor(const Frame& f) {
  if (f.micLive) return art::WORD_LIVE;
  switch (f.status.link) {
    case LinkState::Ready:      return f.status.wireless ? art::WORD_BT : art::WORD_READY;
    case LinkState::Connecting: return art::WORD_WAIT;  // 컴퓨터가 알아봤지만 아직 쓰지 않는다(D50)
    case LinkState::Pairing:    return art::WORD_PAIR;  // 짝을 맺은 컴퓨터가 없다(D59)
    case LinkState::Searching:  break;
  }
  return art::WORD_OFF;
}

int mascotScale(const Frame& f, const Mascot& m) {
  const int room = STAGE_RECT.h - 2 - (hasHint(f) ? HINT_H : 0);  // 위아래 1픽셀씩 + 안내 말 한 줄
  int s = room / m.h;
  const int most = hasHint(f) ? 4 : 5;
  if (s > most) s = most;
  return s < 1 ? 1 : s;
}

Rect mascotRect(const Frame& f, const Mascot& m) {
  const int s = mascotScale(f, m), w = m.w * s, h = m.h * s;
  // 옆에 파형이나 시각이 놓일 때는 캐릭터를 왼쪽으로 옮긴다(시각이 더 넓어 더 많이).
  const int shift = showsClock(f) ? CLOCK_SHIFT : (f.micLive && !hasHint(f)) ? LIVE_SHIFT : 0;
  const int cx = W / 2 - shift;
  const int room = STAGE_RECT.h - (hasHint(f) ? HINT_H + 1 : 0);
  return {(int16_t)(cx - w / 2), (int16_t)(STAGE_RECT.y + (room - h) / 2), (int16_t)w, (int16_t)h};
}

Rect waveRect(const Frame& f, const Mascot& m) {
  if (!f.micLive || hasHint(f)) return {0, 0, 0, 0};
  const Rect mr = mascotRect(f, m);
  const int w = (WAVE_BARS - 1) * WAVE_PITCH + WAVE_BAR_W;
  return {(int16_t)(mr.x + mr.w + WAVE_GAP), (int16_t)(mr.y + mr.h / 2 - WAVE_H / 2), (int16_t)w, (int16_t)WAVE_H};
}

Rect statusWordRect(const Frame& f) {
  const art::Mask& m = art::WORD[wordFor(f)];
  return {(int16_t)WORD_X, (int16_t)WORD_Y, m.w, m.h};
}

Rect clockRect(const Frame& f, const Mascot& m) {
  if (!showsClock(f)) return {0, 0, 0, 0};
  const Rect mr = mascotRect(f, m);
  const int w = eachClockGlyph(f.clockMin, [](const art::Mask&, int) {});
  const int h = art::BIGDIGIT[0].h;
  return {(int16_t)(mr.x + mr.w + CLOCK_GAP), (int16_t)(mr.y + mr.h / 2 - h / 2), (int16_t)w, (int16_t)h};
}

Rect batteryRect() { return {(int16_t)BAT_X, (int16_t)BAT_Y, (int16_t)(BAT_W + 1 + art::BAT_NUB.w), (int16_t)BAT_H}; }

Rect batteryDigitsRect(int pct) {
  if (pct < 0) return {0, 0, 0, 0};
  if (pct > 100) pct = 100;
  int w = 0;
  for (int v = pct, first = 1; first || v > 0; v /= 10, first = 0) w += art::DIGIT[v % 10].w;
  return {(int16_t)(BAT_X + (BAT_W - w) / 2), (int16_t)(BAT_Y + (BAT_H - DIGIT_H) / 2), (int16_t)w, (int16_t)DIGIT_H};
}

Dirty dirty(const Frame& was, const Frame& now) {
  Dirty d = 0;
  for (int z = 0; z < ZONE_COUNT; z++) {
    if (lookFor(was, (Zone)z) != lookFor(now, (Zone)z)) d |= bit((Zone)z);
  }
  const bool backdrop = was.micLive != now.micLive;  // 보는 곳의 바탕이 통째로 바뀐다
  if (backdrop || was.face != now.face || was.mouthOpen != now.mouthOpen || was.wave != now.wave ||
      was.clockMin != now.clockMin) {
    d |= DIRTY_STAGE;
  }
  if (backdrop || was.status != now.status || was.guess != now.guess || was.dev != now.dev) d |= DIRTY_STATUS;
  return d;
}

void drawBackdropBand(Screen& s, int band) {
  if (band < 0 || band >= BACKDROP_BANDS) return;
  const int y0 = H * band / BACKDROP_BANDS, y1 = H * (band + 1) / BACKDROP_BANDS;
  s.push(0, y0, W, y1 - y0, art::WALL.px + y0 * art::WALL.w, art::WALL.w);
}

void drawZone(Screen& s, const Frame& f, Zone z) {
  const Rect& r = zoneRect(z);
  const art::Image* im = &art::KEY[(int)z][lookFor(f, z)];
  if (!im->px) im = &art::KEY[(int)z][art::LOOK_NORMAL];
  s.push(r.x, r.y, r.w, r.h, im->px, im->w);
}

void drawStage(Screen& s, const Frame& f, const Mascot& m) {
  const Rect& r = STAGE_RECT;
  Pixmap p{stageBuf, r.w, r.h};
  gfx::copyFrom(p, viewBackdrop(f), r.x, r.y);
  drawMascot(p, r.y, f, m);
  drawWave(p, r.y, f, m);
  const Rect cr = clockRect(f, m);
  if (cr.w > 0) {
    eachClockGlyph(f.clockMin, [&](const art::Mask& g, int x) { gfx::blend(p, cr.x + x, cr.y - r.y, g, INK); });
  }
  const art::Hint hint = specFor(f.face).hint;
  if (hint != art::HINT_NONE) {
    const art::Mask& mk = art::HINT[hint];
    const uint16_t col = f.micLive ? WHITE : (f.face == Face::Plug ? GREY_TEXT : INK);
    gfx::blend(p, (r.w - mk.w) / 2, r.h - HINT_H - 1, mk, col);
  }
  s.push(r.x, r.y, r.w, r.h, stageBuf, r.w);
}

void drawStatus(Screen& s, const Frame& f) {
  const Rect& r = STATUS_RECT;
  Pixmap p{statusBuf, r.w, r.h};
  gfx::copyFrom(p, viewBackdrop(f), r.x, r.y);
  const uint16_t tc = textColor(f);
  // 왼쪽: 색 점 + 낱말. 색을 가리지 못해도 구분되게 모양도 다르다(채움 · 반원 · 빈 고리).
  if (f.micLive) {
    // 듣는 중(바탕이 빨갛다 — D64). 사실이면 채운 점, 말하기를 누른 것으로 미룬 것이면 빈 고리(D65).
    gfx::blend(p, DOT_X, DOT_Y, f.guess ? art::RING : art::DOT, WHITE);
  } else if (f.status.link == LinkState::Ready) {
    gfx::blend(p, DOT_X, DOT_Y, art::DOT, f.status.wireless ? BLUE : GREEN);
  } else if (f.status.link == LinkState::Connecting || f.status.link == LinkState::Pairing) {
    gfx::blend(p, DOT_X, DOT_Y, art::HALF, AMBER);
  } else {
    gfx::blend(p, DOT_X, DOT_Y, art::RING, RED);
  }
  gfx::blend(p, WORD_X, WORD_Y, art::WORD[wordFor(f)], tc);
  // 오른쪽: 배터리 그림 안에 숫자(아이폰 방식). 값을 모르면 그리지 않는다.
  const int pct = f.status.batteryPct > 100 ? 100 : f.status.batteryPct;
  if (pct >= 0) {
    const bool low = pct <= 20 && !f.status.charging;
    // 듣는 중에는 바탕이 빨개서(D64) 부족의 빨강은 묻히고 충전의 초록은 튄다 → 그동안은 흰색 하나로.
    const uint16_t fillCol = f.micLive ? WHITE : f.status.charging ? GREEN : low ? RED : tc;
    gfx::blend(p, BAT_X, BAT_Y, art::BAT_BODY, tc, 100);
    int fillW = BAT_W * pct / 100;
    if (pct > 0 && fillW < 2) fillW = 2;
    gfx::blend(p, BAT_X, BAT_Y, art::BAT_BODY, fillCol, 255, fillW);
    gfx::blend(p, BAT_X + BAT_W + 1, BAT_Y + (BAT_H - art::BAT_NUB.h) / 2, art::BAT_NUB, tc, 110);
    const Rect dr = batteryDigitsRect(pct);
    // 숫자는 흰색. 채움이 흰색일 때(듣는 중·충전 아님)만 검게.
    const uint16_t dc = (fillCol == WHITE) ? BLACK : WHITE;
    int x = dr.x + dr.w;
    for (int v = pct, first = 1; first || v > 0; v /= 10, first = 0) {  // 일의 자리부터 오른쪽에서 왼쪽으로
      const art::Mask& dm = art::DIGIT[v % 10];
      x -= dm.w;
      gfx::blend(p, x, dr.y, dm, dc);
    }
  }
  // 개발판 표시: 맨 위에 자홍·검정이 번갈아 가는 띠(밝은 바탕에서도 빨간 바탕에서도 보인다). 글자·점·배터리는 그 아래에 있다.
  if (f.dev) {
    const uint16_t magenta = rgb565(0xFF, 0x00, 0xFF);
    for (int x = 0; x < r.w; x += DEV_DASH) {
      gfx::fill(p, x, 0, (x + DEV_DASH <= r.w) ? DEV_DASH : r.w - x, DEV_BAND_H, ((x / DEV_DASH) % 2) ? BLACK : magenta);
    }
  }
  s.push(r.x, r.y, r.w, r.h, statusBuf, r.w);
}

void drawFrame(Screen& s, const Frame& f) { drawFrame(s, f, pixelart::mascot()); }

void drawFrame(Screen& s, const Frame& f, const Mascot& m) {
  for (int b = 0; b < BACKDROP_BANDS; b++) drawBackdropBand(s, b);
  for (int i = 0; i < ZONE_COUNT; i++) drawZone(s, f, (Zone)i);
  drawStage(s, f, m);
  drawStatus(s, f);
}

}  // namespace touchui
