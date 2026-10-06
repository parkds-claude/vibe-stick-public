// 모든 장면을 기기와 똑같은 그리기 코드로 그려 한 장의 그림(PPM)으로 내보낸다.
// 사용: test/host/preview.sh [결과.png]
//   preview <결과.ppm>                 이 빌드의 마스코트로 모든 장면
//   preview --mascots <결과.ppm>       마스코트 다섯(줄) x 장면 여덟(칸)
//   preview --each <폴더> [마스코트]   장면 하나씩. 마스코트는 이름(cat 등)이나 번호, 없으면 이 빌드의 것
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "logic/screen_draw.h"

using namespace screen;

struct Buf : Canvas {
  std::vector<uint16_t> px;
  Buf() : px(W * H, 0) {}
  void fillRect(int x, int y, int w, int h, uint16_t color) override {
    for (int yy = y; yy < y + h; yy++) {
      for (int xx = x; xx < x + w; xx++) {
        if (xx >= 0 && yy >= 0 && xx < W && yy < H) px[yy * W + xx] = color;
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

// 이름("cat")이나 번호("2")로 마스코트를 고른다. 모르는 값이면 이 빌드의 것.
static const pixelart::Mascot& pickMascot(const char* arg) {
  if (!arg) return pixelart::mascot();
  for (int i = 0; i < pixelart::MASCOT_COUNT; i++) {
    if (std::strcmp(pixelart::mascotAt(i).name, arg) == 0) return pixelart::mascotAt(i);
  }
  return (arg[0] >= '0' && arg[0] <= '9') ? pixelart::mascotAt(std::atoi(arg)) : pixelart::mascot();
}

// --each <폴더> [마스코트]: 장면 하나씩 1배 크기 PPM 으로 저장한다(홍보 그림·영상 만들기용).
//   파일 이름: scene_<번호>_<장면 이름>.ppm, 그리고 ready_nostick.ppm, ready_lowbatt.ppm
static int saveEach(const char* dir, const pixelart::Mascot& mascot) {
  const char* names[SCENE_COUNT] = {"ready", "pairing", "searching", "connecting", "talk",
                                    "ok",    "nope",    "up",        "down",       "next",
                                    "back",  "nolink",  "bye",       "oops"};
  const LinkState links[SCENE_COUNT] = {
      LinkState::Ready,   LinkState::Pairing, LinkState::Searching, LinkState::Connecting,
      LinkState::Ready,   LinkState::Ready,   LinkState::Ready,     LinkState::Ready,
      LinkState::Ready,   LinkState::Ready,   LinkState::Ready,     LinkState::Searching,
      LinkState::Pairing, LinkState::Searching};
  auto dump = [&](const char* name, Scene s, LinkState l, int batt, bool noStick) {
    Buf b;
    Frame f{};
    f.scene = s;
    f.link = l;
    f.batteryPct = batt;
    f.noStick = noStick;
    drawFrame(b, f, mascot);
    std::vector<uint8_t> img(W * H * 3);
    for (int i = 0; i < W * H; i++) put(&img[i * 3], b.px[i]);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    savePpm(path, img, W, H);
  };
  for (int i = 0; i < SCENE_COUNT; i++) {
    char name[64];
    std::snprintf(name, sizeof(name), "scene_%02d_%s", i, names[i]);
    dump(name, (Scene)i, links[i], 72, false);
  }
  dump("ready_nostick", Scene::Ready, LinkState::Ready, 72, true);
  dump("ready_lowbatt", Scene::Ready, LinkState::Ready, 8, false);
  return 0;
}

// 화면 한 장을 S 배로 키워 큰 그림의 (ox, oy) 에 붙인다.
static void paste(std::vector<uint8_t>& img, int imgW, const Buf& b, int ox, int oy, int S) {
  for (int y = 0; y < H * S; y++) {
    for (int x = 0; x < W * S; x++) {
      put(&img[((oy + y) * imgW + ox + x) * 3], b.px[(y / S) * W + (x / S)]);
    }
  }
}

// --mascots <결과.ppm>: 마스코트마다 한 줄씩, 표정이 다른 장면 여덟
static int saveMascots(const char* out) {
  const Scene scenes[] = {Scene::Ready, Scene::Talk,    Scene::Ok,        Scene::Nope,
                          Scene::Next,  Scene::Down,    Scene::Pairing,   Scene::Searching};
  const LinkState links[] = {LinkState::Ready, LinkState::Ready,   LinkState::Ready,   LinkState::Ready,
                             LinkState::Ready, LinkState::Ready,   LinkState::Pairing, LinkState::Searching};
  const int cols = sizeof(scenes) / sizeof(scenes[0]), rows = pixelart::MASCOT_COUNT, S = 2, G = 16;
  const int SW = G + cols * (W * S + G), SH = G + rows * (H * S + G);
  std::vector<uint8_t> img(SW * SH * 3, 0x2A);
  for (int r = 0; r < rows; r++) {
    for (int k = 0; k < cols; k++) {
      Buf b;
      Frame f{};
      f.scene = scenes[k];
      f.link = links[k];
      f.batteryPct = 72;
      drawFrame(b, f, pixelart::mascotAt(r));
      paste(img, SW, b, G + k * (W * S + G), G + r * (H * S + G), S);
    }
  }
  savePpm(out, img, SW, SH);
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::string(argv[1]) == "--each") {
    return saveEach(argv[2], pickMascot(argc > 3 ? argv[3] : nullptr));
  }
  if (argc > 2 && std::string(argv[1]) == "--mascots") return saveMascots(argv[2]);
  const char* out = argc > 1 ? argv[1] : "preview.ppm";
  // (장면, 연결 상태, 조이스틱 없음) — 실제로 나올 수 있는 조합
  struct Shot {
    Scene     scene;
    LinkState link;
    bool      noStick;
  };
  const Shot shots[] = {
      {Scene::Ready, LinkState::Ready, false},      {Scene::Talk, LinkState::Ready, false},
      {Scene::Ok, LinkState::Ready, false},         {Scene::Nope, LinkState::Ready, false},
      {Scene::Up, LinkState::Ready, false},         {Scene::Down, LinkState::Ready, false},
      {Scene::Next, LinkState::Ready, false},       {Scene::Back, LinkState::Ready, false},
      {Scene::Pairing, LinkState::Pairing, false},  {Scene::Searching, LinkState::Searching, false},
      {Scene::Connecting, LinkState::Connecting, false},
      {Scene::NoLink, LinkState::Searching, false}, {Scene::Bye, LinkState::Pairing, false},
      {Scene::Oops, LinkState::Searching, false},   {Scene::Ready, LinkState::Ready, true},
      {Scene::Ready, LinkState::Ready, false},
  };
  const int n = sizeof(shots) / sizeof(shots[0]);
  const int cols = 8, rows = (n + cols - 1) / cols, S = 2, G = 16;
  const int SW = G + cols * (W * S + G), SH = G + rows * (H * S + G);
  std::vector<uint8_t> img(SW * SH * 3, 0x2A);
  for (int i = 0; i < n; i++) {
    Buf b;
    Frame f{};
    f.scene = shots[i].scene;
    f.link = shots[i].link;
    f.batteryPct = (i == n - 1) ? 10 : 72;  // 마지막 칸: 배터리 부족
    f.noStick = shots[i].noStick;
    drawFrame(b, f);
    paste(img, SW, b, G + (i % cols) * (W * S + G), G + (i / cols) * (H * S + G), S);
  }
  savePpm(out, img, SW, SH);
  return 0;
}
