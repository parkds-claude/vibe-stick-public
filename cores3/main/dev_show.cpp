#include "dev_show.h"

#include <M5Unified.h>

#include "config.h"

#if DEV_BUILD

namespace {

uint8_t row[320 * 2];  // 받고 있는 한 줄(가로로 가장 긴 320점)
size_t  rowBytes = sizeof(row);
int     rows = 240;
size_t  rowHave = 0;
int     rowY = 0;
bool    on = false;

}  // namespace

namespace devshow {

void begin(int rotation) {
  M5.Display.setRotation(rotation & 3);
  rowBytes = (size_t)M5.Display.width() * 2;
  rows = M5.Display.height();
  on = true;
  rowHave = 0;
  rowY = 0;
}

bool active() { return on; }

size_t remaining() {
  if (!on || rowY >= rows) return 0;
  return (size_t)(rows - rowY) * rowBytes - rowHave;
}

void feed(const uint8_t* data, size_t n) {
  while (n > 0 && rowY < rows) {
    size_t take = rowBytes - rowHave;
    if (take > n) take = n;
    memcpy(row + rowHave, data, take);
    rowHave += take;
    data += take;
    n -= take;
    if (rowHave == rowBytes) {
      // 받은 바이트는 화면이 받는 순서(큰 쪽 먼저) 그대로다 → 뒤집지 않고 보낸다.
      M5.Display.pushImage(0, rowY, (int)(rowBytes / 2), 1, (const lgfx::swap565_t*)row);
      rowHave = 0;
      ++rowY;
    }
  }
}

void end() {
  on = false;
  M5.Display.setRotation(SCREEN_ROTATION);  // 평소 화면의 방향으로
}

}  // namespace devshow

#else  // 평소 판: 빈 껍데기(부르는 곳도 #if DEV_BUILD 안에 있다)

namespace devshow {
void   begin(int) {}
bool   active() { return false; }
size_t remaining() { return 0; }
void   feed(const uint8_t*, size_t) {}
void   end() {}
}  // namespace devshow

#endif
