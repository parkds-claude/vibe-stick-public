#pragma once
#include <stdint.h>

// CoreS3 화면에서 손가락으로 누르는 자리. 하드웨어를 모른다(컴퓨터에서 시험).
//
// D55 배치(D33 을 바꿈): 왼손에 세로로 쥐고 엄지로 누른다. USB 선은 위로 빠진다(240x320).
//   +------------------------+
//   |  보는 곳(상태·캐릭터)   |   누르는 자리가 아니다
//   +-------+-------+--------+
//   |   ▲   |   ▼   |  취소  |   작게: 덜 쓰는 셋
//   +-------+-------+--------+
//   |     «     |     »      |   크게: 창 넘기기
//   +-----------+------------+
//   |  말하기   |    확인    |   크게: 가장 자주 쓰는 둘, 손에 가장 가까운 줄
//   +-----------+------------+
//   이유: 사용자가 정한 쓰임 — 가장 많이 쓰는 것은 말하기·창 넘기기·확인이고 왼손 엄지로 누른다.
//         세로로 쥐면 선이 손에 닿지 않고, 엄지가 화면 폭(31mm) 전체에 닿으며, 보는 곳이 엄지에 가리지 않는다.
//         (옛 D33 은 가장 자주 누르는 말하기가 손에서 가장 먼 왼쪽 위에 있었다.)
//   비용: 작은 버튼의 높이가 5.1mm 다. 확인(Enter)이 말하기 바로 옆이다
//         → 버튼 사이 틈(GAP)과 누름 인정 범위 줄이기(HIT_INSET)로 경계에 죽은 띠를 둔다.
//   탈출구: 아래 ZONE_RECTS 의 숫자와 tools/gen_art.py 의 같은 표(둘은 시험이 견준다).
namespace touchui {

const int W = 240;
const int H = 320;
const int GAP = 4;        // 버튼 사이 틈(픽셀). 1픽셀 = 약 0.13mm
const int HIT_INSET = 3;  // 버튼 테두리에서 이만큼 안쪽부터 누름으로 친다 → 경계의 죽은 띠 = GAP + 2*HIT_INSET

struct Rect {
  int16_t x, y, w, h;
  constexpr bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  constexpr Rect inset(int d) const {
    return {(int16_t)(x + d), (int16_t)(y + d), (int16_t)(w - 2 * d), (int16_t)(h - 2 * d)};
  }
};

// 누르는 자리. 번호가 곧 눌림 비트(bit())의 자리다.
enum class Zone : uint8_t { Talk, Up, Down, Nope, Back, Next, Ok };
const int ZONE_COUNT = 7;

constexpr Rect ZONE_RECTS[ZONE_COUNT] = {
    /* Talk */ {0, 230, 118, 90},
    /* Up   */ {0, 92, 77, 40},
    /* Down */ {81, 92, 78, 40},
    /* Nope */ {163, 92, 77, 40},
    /* Back */ {0, 136, 118, 90},
    /* Next */ {122, 136, 118, 90},
    /* Ok   */ {122, 230, 118, 90},
};
// 보는 곳: 상태 막대(위 STATUS_H 줄)와 무대(캐릭터·파형·안내 말). 누르는 자리가 아니다.
constexpr Rect VIEW_RECT = {0, 0, 240, 88};
const int STATUS_H = 28;
constexpr Rect STATUS_RECT = {0, 0, 240, STATUS_H};
constexpr Rect STAGE_RECT = {0, STATUS_H, 240, 88 - STATUS_H};
// D71 보는 곳을 톡 치면 "화면만 바로잡기"(키는 나가지 않는다 — logic/touch_state.h 의 flipGuess).
//   누르는 자리(Zone)가 아니다: 눌린 모습도, 누름 소리도, 키도 없다. 톡으로 치는 곳만 여기서 정한다.
//   가장자리 4픽셀은 뺀다(쥔 손의 살, 위로 빠지는 USB 선을 만지는 손가락). 아래쪽은 작은 버튼(▲▼✕)의
//   누름 인정 범위(y 95~)와 15픽셀(약 2mm) 떨어뜨렸다 — 버튼끼리의 죽은 띠(10픽셀)보다 넓게: 그 버튼을
//   누르려다 조금 위에 닿은 손가락이 표시를 뒤집으면, 바로잡으려던 어긋남을 도리어 만든다.
constexpr Rect VIEW_TAP_RECT = {4, 4, 232, 76};
constexpr bool viewTapAt(int x, int y) { return VIEW_TAP_RECT.contains(x, y); }
// 크게 쓰는 넷(말하기·«·»·확인)과 작게 쓰는 셋
constexpr bool isBigZone(Zone z) { return z == Zone::Talk || z == Zone::Back || z == Zone::Next || z == Zone::Ok; }

constexpr const Rect& zoneRect(Zone z) { return ZONE_RECTS[(int)z]; }
constexpr uint8_t bit(Zone z) { return (uint8_t)(1u << (int)z); }

// (x, y) 를 누르면 어느 자리인가. 버튼 사이 틈이나 테두리 근처면 false(아무 자리도 아님).
inline bool zoneAt(int x, int y, Zone& out) {
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (ZONE_RECTS[i].inset(HIT_INSET).contains(x, y)) {
      out = (Zone)i;
      return true;
    }
  }
  return false;
}

}  // namespace touchui
