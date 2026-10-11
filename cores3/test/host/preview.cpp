// CoreS3 화면의 모든 장면을 기기와 똑같은 그리기 코드·같은 조각 그림으로 그려 한 장의 그림(PPM)으로 내보낸다.
// 사용: cores3/test/host/preview.sh [결과.png]
//   preview <결과.ppm>            모든 장면을 한 장에
//   preview --one <결과.ppm>      대기 장면 한 장만 3배로(배치를 크게 볼 때)
#include <cstdio>
#include <string>
#include <vector>

#include "touch_draw.h"

using namespace touchui;

struct Buf : Screen {
  std::vector<uint16_t> px;
  Buf() : px(W * H, 0) {}
  void push(int x, int y, int w, int h, const uint16_t* p, int stride) override {
    for (int r = 0; r < h; r++) {
      for (int c = 0; c < w; c++) {
        if (x + c >= 0 && y + r >= 0 && x + c < W && y + r < H) px[(y + r) * W + x + c] = p[r * stride + c];
      }
    }
  }
};

static void savePpm(const char* path, const std::vector<uint8_t>& img, int w, int h) {
  FILE* fp = std::fopen(path, "wb");
  if (!fp) return;
  std::fprintf(fp, "P6\n%d %d\n255\n", w, h);
  std::fwrite(img.data(), 1, img.size(), fp);
  std::fclose(fp);
}

// RGB565 → 화면에서 보이는 색(아래 비트를 채워 넣는다)
static void put(uint8_t* p, uint16_t c) {
  const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, bl = c & 0x1F;
  p[0] = (uint8_t)((r << 3) | (r >> 2));
  p[1] = (uint8_t)((g << 2) | (g >> 4));
  p[2] = (uint8_t)((bl << 3) | (bl >> 2));
}

// 화면 한 장을 S 배로 키워 큰 그림의 (ox, oy) 에 붙인다.
static void paste(std::vector<uint8_t>& img, int imgW, const Buf& b, int ox, int oy, int S) {
  for (int y = 0; y < H * S; y++) {
    for (int x = 0; x < W * S; x++) {
      put(&img[((oy + y) * imgW + ox + x) * 3], b.px[(y / S) * W + (x / S)]);
    }
  }
}

// 화면 한 장: 장면 + 상태 막대(연결·배터리)
static Frame shot(Face face, uint8_t pressed, bool live, bool mouth, LinkState link = LinkState::Ready,
                  int pct = 87, bool charging = false) {
  Frame f{face, pressed, live, mouth};
  f.status.link = link;
  f.status.batteryPct = (int8_t)pct;
  f.status.charging = charging;
  f.wave = (uint8_t)(mouth ? 2 : 0);
  return f;
}
// 블루투스로 쓰는 화면(D59). guess: 듣는 중이 사실이 아니라 미룸이다(빈 고리 — D65)
static Frame shotBt(Face face, uint8_t pressed, bool live, bool mouth, bool guess = false) {
  Frame f = shot(face, pressed, live, mouth);
  f.status.wireless = true;
  f.guess = guess;
  return f;
}

// 개발판(DEV_BUILD=1)의 대기 화면
static Frame shotDev() {
  Frame f = shot(Face::Hi, 0, false, false);
  f.dev = true;
  return f;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::string(argv[1]) == "--one") {
    const int S = 3;
    Buf b;
    drawFrame(b, shot(Face::Hi, 0, false, false));
    std::vector<uint8_t> img(W * S * H * S * 3);
    paste(img, W * S, b, 0, 0, S);
    savePpm(argv[2], img, W * S, H * S);
    return 0;
  }
  if (argc > 3 && std::string(argv[1]) == "--raw") {  // 장면 하나를 기기 픽셀 그대로(개발판의 vibectl.py show 용이 아니라 견주기용)
    Buf b;
    drawFrame(b, std::string(argv[2]) == "listen" ? shot(Face::Talk, 0, true, true) : shot(Face::Hi, 0, false, false));
    std::vector<uint8_t> img(W * H * 3);
    paste(img, W, b, 0, 0, 1);
    savePpm(argv[3], img, W, H);
    return 0;
  }
  const char* out = argc > 1 ? argv[1] : "preview.ppm";
  // 실제로 나올 수 있는 화면들
  const Frame shots[] = {
      shot(Face::Hi, 0, false, false),                    // 대기
      shot(Face::Talk, bit(Zone::Talk), false, false),    // 말하기를 누르는 순간
      shot(Face::Talk, 0, false, false),                  // 말하기를 켰다(컴퓨터가 마이크를 열기 전)
      shot(Face::Talk, 0, true, false),                   // 컴퓨터가 듣는 중, 조용함
      shot(Face::Talk, 0, true, true),                    // 듣는 중, 목소리가 들어옴
      shot(Face::Ok, bit(Zone::Ok), false, false, LinkState::Ready, 100),  // 확인을 누르는 순간
      shot(Face::Nope, bit(Zone::Nope), false, false),
      shot(Face::Up, bit(Zone::Up), false, false),
      shot(Face::Down, bit(Zone::Down), false, false),
      shot(Face::Next, bit(Zone::Next), false, false),
      shot(Face::Back, bit(Zone::Back), false, false, LinkState::Ready, 42, true),  // 충전 중
      shot(Face::Ok, 0, true, false),                     // 듣는 중에 확인을 눌렀다
      shot(Face::Plug, 0, false, false, LinkState::Searching, 64, true),   // 컴퓨터가 잠듦(전원은 들어옴)
      shot(Face::NoLink, bit(Zone::Talk), false, false, LinkState::Searching, 12),  // 선이 뽑힌 채 누름, 배터리 부족
      shot(Face::Allow, 0, false, false, LinkState::Connecting, 100),     // 컴퓨터 화면에서 "허용"을 눌러야 한다
      shot(Face::Update, 0, false, false, LinkState::Ready, 100, true),   // 펌웨어를 바꾸겠다는 요청: 확인을 눌러야 진행
      shot(Face::Hi, 0, false, false, LinkState::Ready, -1),              // 배터리 값을 아직 모름
      shotBt(Face::Hi, 0, false, false),                  // 블루투스로 대기
      shotBt(Face::Talk, 0, true, true, true),            // 블루투스: 말하기를 눌러 미루어 듣는 중(빈 고리)
      shotBt(Face::NoLink, bit(Zone::Talk), false, false),  // 연결은 있는데 키만 나가지 않았다(`Key not sent` — D72)
      shotBt(Face::PairFail, 0, false, false),            // 짝을 지우지 못했다(`Pairing not cleared` — D72)
      shotBt(Face::PairCleared, 0, false, false),         // 짝을 지웠다
      shot(Face::Pair, 0, false, false, LinkState::Pairing, 70),  // 선도 짝도 없다
      shotDev(),                                          // 개발판: 맨 위에 자홍·검정 띠가 늘 보인다
  };
  const int n = sizeof(shots) / sizeof(shots[0]);
  const int cols = 6, rows = (n + cols - 1) / cols, S = 2, G = 16;
  const int SW = G + cols * (W * S + G), SH = G + rows * (H * S + G);
  std::vector<uint8_t> img(SW * SH * 3, 0x2A);
  for (int i = 0; i < n; i++) {
    Buf b;
    drawFrame(b, shots[i]);
    paste(img, SW, b, G + (i % cols) * (W * S + G), G + (i / cols) * (H * S + G), S);
  }
  savePpm(out, img, SW, SH);
  return 0;
}
