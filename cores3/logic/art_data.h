#pragma once
#include <stdint.h>

// 컴퓨터에서 미리 그려 둔 화면 조각(D57).
// 내용(art_data.cpp)은 tools/gen_art.py 가 만든다 — 모습을 바꾸려면 그 스크립트를 고쳐 다시 돌린다.
namespace art {

struct Image {  // 색 그림: RGB565, 왼쪽 위부터 한 줄씩
  int16_t         w, h;
  const uint16_t* px;
};
struct Mask {   // "덮는 정도"(0~255) 그림: 색은 찍을 때 정한다 → 같은 그림을 여러 색으로 쓴다
  int16_t        w, h;
  const uint8_t* a;
};

// 버튼의 모습
enum Look : uint8_t { LOOK_NORMAL, LOOK_PRESSED, LOOK_OFF, LOOK_LIVE, LOOK_COUNT };

extern const Image WALL;                // 바탕 전체(240x320)
extern const Image VIEW_LIVE;           // 듣는 중의 보는 곳(240x88)
extern const Image KEY[7][LOOK_COUNT];  // [Zone 번호][Look]. LOOK_LIVE 는 말하기에만 있다(없으면 px 가 nullptr)

// 상태 막대의 낱말. WORD_BT = 블루투스로 쓰는 중, WORD_PAIR = 짝을 맺은 컴퓨터가 없다(D59)
enum Word : uint8_t { WORD_READY, WORD_WAIT, WORD_OFF, WORD_LIVE, WORD_BT, WORD_PAIR, WORD_COUNT };
extern const Mask WORD[WORD_COUNT];
// 사람이 할 일이 있을 때 캐릭터 아래에 뜨는 말.
// HINT_NOTSENT = 방금 누른 키가 나가지 않았다(`Key not sent`), HINT_NOTCLEARED = 짝을 지우지 못했다(D72).
// `Not connected` 는 상태 막대의 낱말(WORD_OFF)로만 쓴다 — 말 하나는 한 가지 뜻으로.
enum Hint : uint8_t {
  HINT_CABLE, HINT_ALLOW, HINT_MIC, HINT_UPDATE, HINT_NOTSENT, HINT_PAIR, HINT_CLEARED, HINT_DONE, HINT_ASK,
  HINT_NOTCLEARED,
  HINT_COUNT, HINT_NONE = 255
};
extern const Mask HINT[HINT_COUNT];
extern const Mask DIGIT[10];            // 배터리 안의 숫자
extern const Mask BIGDIGIT[10];         // 시계의 큰 숫자(D62)
extern const Mask BIGCOLON;
extern const Mask DOT, RING, HALF;      // 연결 점: 채움 · 빈 고리 · 반원
extern const Mask BAT_BODY, BAT_NUB;    // 배터리의 몸통과 꼭지

// 그림을 만들 때 쓴 자리 표: 0 = 보는 곳, 1~7 = Zone 차례. touch_layout.h 와 같아야 한다(시험이 견준다).
extern const int16_t LAYOUT[8][4];

}  // namespace art
