#pragma once
#include <stdint.h>

#include "art_data.h"
#include "touch_layout.h"
#include "logic/link_state.h"
#include "logic/pixel_art.h"

// CoreS3 화면 한 장을 그린다. 하드웨어를 모른다.
//
// D57 그리는 방식: 버튼은 컴퓨터에서 미리 그려 둔 조각(art_data)을 그대로 화면에 붙이고, 보는 곳
// (상태 막대·캐릭터)만 메모리에서 합쳐 보낸다. 기기와 미리보기가 같은 조각·같은 코드를 쓰므로
// 미리보기 그림 = 기기 픽셀이다(D22 의 뜻을 잇는다).
//   이유: 반투명 타일의 흐림·그림자·부드러운 색 변화는 기기에서 실시간으로 만들 수 없다.
//   비용: 펌웨어가 조각만큼(약 0.6MB) 커진다. 모습을 바꾸려면 tools/gen_art.py 를 다시 돌린다.
//   탈출구: 사각형 채우기만으로 그리던 옛 방식은 git 의 ba7a857 에 있다.
namespace touchui {

// 그림을 받는 쪽. 기기에서는 LCD, 컴퓨터에서는 그림 파일·시험용 기록에 연결된다.
class Screen {
 public:
  virtual ~Screen() {}
  // 화면의 (x, y) 에 w x h 조각을 놓는다. px 는 RGB565 이고 한 줄이 stride 개다(stride >= w).
  virtual void push(int x, int y, int w, int h, const uint16_t* px, int stride) = 0;
};

// 지금 무슨 장면인가(캐릭터의 표정과 안내 말이 여기서 정해진다)
enum class Face : uint8_t {
  Hi,      // 연결되어 대기
  Plug,    // 컴퓨터에 꽂혀 있지 않음(또는 컴퓨터가 잠듦)
  Talk,    // 말하기(컴퓨터가 마이크를 열었거나, 방금 켜 달라고 했음)
  Ok,      // 확인(Enter)
  Nope,    // 취소(Esc)
  Up,
  Down,
  Next,    // 다음 창
  Back,    // 이전 창
  NoLink,  // 방금 누른 키가 나가지 않았다(`Key not sent`). 연결 상태는 상태 막대가 따로 말한다(D72)
  NoMic,   // 말하기를 켰는데 컴퓨터가 이 기기의 마이크를 열지 않음(D65 뒤로는 띄우지 않는다 — 미루어 듣는 중으로 보인다)
  Update,  // 컴퓨터가 "펌웨어를 바꾸겠다"고 했다: 확인을 눌러야 진행한다
  Allow,   // 컴퓨터가 기기를 알아봤지만 쓰기 시작하지 않는다: 컴퓨터 화면에서 "허용"을 눌러야 한다
  Pair,    // 선도 없고 블루투스로 짝을 맺은 컴퓨터도 없다: 컴퓨터의 블루투스 설정에서 연결한다(D59)
  PairCleared,  // 짝을 지웠다(확인+취소 3초). 곧 재시작한다
  Done,    // 컴퓨터의 Claude 가 일을 끝냈다고 알려 왔다(D63)
  Ask,     // 컴퓨터의 Claude 가 사람의 답(허락)을 기다린다고 알려 왔다(D63)
  PairFail,  // 짝을 지우려 했으나 지우지 못했다(`Pairing not cleared` — D72. 앞선 판은 NoLink 를 빌려 썼다)
};
const int FACE_COUNT = 18;
static_assert((int)Face::PairFail + 1 == FACE_COUNT, "FACE_COUNT 가 장면 수와 다르다");

struct FaceSpec {
  art::Hint       hint;   // 캐릭터 아래의 안내 말(사람이 할 일이 있을 때만. 없으면 HINT_NONE)
  pixelart::Eyes  eyes;
  pixelart::Mouth mouth;
};
const FaceSpec& specFor(Face f);

// 상태 막대(보는 곳의 맨 윗줄)에 보이는 것 — D54
struct Status {
  LinkState link = LinkState::Searching;  // 컴퓨터와의 연결
  int8_t    batteryPct = -1;              // 0~100, 모르면 -1
  bool      charging = false;
  bool      wireless = false;             // link 가 블루투스의 것인가(D59). false 면 USB
  bool operator==(const Status& o) const {
    return link == o.link && batteryPct == o.batteryPct && charging == o.charging && wireless == o.wireless;
  }
  bool operator!=(const Status& o) const { return !(*this == o); }
};

// 화면 한 장에 담기는 것 전부
struct Frame {
  Face    face;
  uint8_t pressed;    // 지금 눌려 있는 자리들(bit(Zone))
  bool    micLive;    // 듣는 중으로 보인다: 컴퓨터가 이 기기의 마이크를 열었거나(사실), 말하기를 눌렀다(미룸 — guess)
  bool    mouthOpen;  // 듣는 중에 목소리가 들어오고 있다(입을 벌리고 파형이 움직인다)
  Status  status{};
  uint8_t wave = 0;   // 파형의 몇 번째 모습인가(목소리가 들어오는 동안 돈다. 0 ~ WAVE_STEPS-1)
  int16_t clockMin = -1;  // 캐릭터 옆에 보일 시각(자정부터의 분, 0~1439). 보이지 않으면 -1 (D62)
  // micLive 가 사실이 아니라 "말하기를 눌렀으니 듣고 있을 것"이라는 미룸인가(D65). 상태 막대의 점이 빈 고리가 된다.
  bool    guess = false;
  // 개발판(DEV_BUILD=1)인가. 그러면 상태 막대 맨 위에 자홍·검정 띠를 늘 그린다(DEV_BAND_H) — 컴퓨터가 키를 치게 할 수
  // 있는 판이 올라가 있다는 것을 화면만 봐도 알 수 있게.
  bool    dev = false;
  bool operator==(const Frame& o) const {
    return face == o.face && pressed == o.pressed && micLive == o.micLive && mouthOpen == o.mouthOpen &&
           status == o.status && wave == o.wave && clockMin == o.clockMin && guess == o.guess && dev == o.dev;
  }
  bool operator!=(const Frame& o) const { return !(*this == o); }
};
const int WAVE_STEPS = 10;
const int DEV_BAND_H = 4;   // 개발판 표시 띠의 높이(화면 맨 위, 폭 전체)

// 다시 그릴 곳을 가리키는 비트: 버튼마다 하나(bit(Zone)) + 무대(캐릭터·파형·안내 말) + 상태 막대
using Dirty = uint16_t;
const Dirty DIRTY_STAGE  = 0x0080;
const Dirty DIRTY_STATUS = 0x0100;
// 모든 버튼 + 무대 + 상태 막대. 뜻 없는 비트를 켜 두면 Painter 가 "아직 남았다"에서 끝나지 않는다.
const Dirty DIRTY_ALL    = 0x01FF;
static_assert(DIRTY_ALL == (((1u << ZONE_COUNT) - 1) | DIRTY_STAGE | DIRTY_STATUS),
              "DIRTY_ALL 이 버튼·무대·상태 막대의 비트와 다르다");
// was 에서 now 로 바뀌면 어디를 다시 그려야 하는가
Dirty dirty(const Frame& was, const Frame& now);

// 이 화면에서 버튼 z 의 모습: 눌림 > 꺼짐(연결이 없다 / 물음에는 예·아니오만) > 듣는 중(말하기) > 평소
art::Look lookFor(const Frame& f, Zone z);
// 상태 막대의 낱말
art::Word wordFor(const Frame& f);

// 캐릭터 한 칸의 크기(픽셀)와 놓이는 자리(화면 좌표). 무대 안에 들어가게 정한다.
int  mascotScale(const Frame& f, const pixelart::Mascot& m);
Rect mascotRect(const Frame& f, const pixelart::Mascot& m);
// 파형이 놓이는 자리(듣는 중이고 안내 말이 없을 때만 그린다. 없으면 너비 0)
Rect waveRect(const Frame& f, const pixelart::Mascot& m);
// 시각이 놓이는 자리(쉬는 장면이고 시각을 알 때만. 없으면 너비 0) — D62
Rect clockRect(const Frame& f, const pixelart::Mascot& m);
// 상태 막대의 낱말이 놓이는 자리, 배터리 그림과 그 안의 숫자가 놓이는 자리(화면 좌표)
Rect statusWordRect(const Frame& f);
Rect batteryRect();
Rect batteryDigitsRect(int pct);

// 바탕(버튼 사이의 틈 포함)을 위에서부터 띠 하나씩 그린다. 켤 때, 그리고 화면이 다른 것으로 덮인 뒤에.
const int BACKDROP_BANDS = 4;
void drawBackdropBand(Screen& s, int band);
// 버튼 하나. 그 자리의 사각형만 보낸다.
void drawZone(Screen& s, const Frame& f, Zone z);
// 무대(캐릭터·파형·안내 말)와 상태 막대. 각각 STAGE_RECT · STATUS_RECT 만 보낸다.
void drawStage(Screen& s, const Frame& f, const pixelart::Mascot& m);
void drawStatus(Screen& s, const Frame& f);
// 화면 전체 = 바탕 + 모든 버튼 + 무대 + 상태 막대. 빈 곳을 남기지 않는다.
void drawFrame(Screen& s, const Frame& f);
void drawFrame(Screen& s, const Frame& f, const pixelart::Mascot& m);

// 달라진 곳을 한 번에 하나씩 그려 나간다.
//   기기는 한 틱(10ms)에 한 조각만 그린다 — 한꺼번에 그리면 그동안 터치를 읽지 못한다.
//   그리는 도중에 새 화면이 정해지면, 아직 못 그린 곳은 새 화면으로 그린다(옛 그림을 그리지 않는다).
//   순서: 바탕(처음에) → 버튼들(방금 누른 손가락의 반응이 가장 급하다) → 무대 → 상태 막대.
class Painter {
 public:
  // 새 화면이 정해졌다. dirtyMask = 지난 화면과 달라진 곳(TouchState::poll 이 준 값).
  void submit(const Frame& f, Dirty dirtyMask) {
    frame_ = f;
    pending_ |= (Dirty)(dirtyMask & DIRTY_ALL);
  }
  // 화면이 다른 것으로 덮였다(예: 개발판의 시안 그림): 바탕부터 전부 다시 그린다.
  void invalidate() {
    backdropBand_ = 0;
    pending_ = DIRTY_ALL;
  }
  // 아직 그릴 것이 남았는가
  bool busy() const { return backdropBand_ < BACKDROP_BANDS || pending_ != 0; }
  // 한 걸음 그린다. 그렸으면 true, 그릴 것이 없으면 false.
  bool step(Screen& s) { return step(s, pixelart::mascot()); }
  bool step(Screen& s, const pixelart::Mascot& m) {
    if (backdropBand_ < BACKDROP_BANDS) {
      drawBackdropBand(s, backdropBand_++);
      return true;
    }
    for (int z = ZONE_COUNT - 1; z >= 0; z--) {
      const Dirty b = (Dirty)(1u << z);
      if (!(pending_ & b)) continue;
      drawZone(s, frame_, (Zone)z);
      pending_ &= (Dirty)~b;
      return true;
    }
    if (pending_ & DIRTY_STAGE) {  // 무대가 먼저: 목소리에 맞춰 움직여야 한다
      drawStage(s, frame_, m);
      pending_ &= (Dirty)~DIRTY_STAGE;
      return true;
    }
    if (pending_ & DIRTY_STATUS) {
      drawStatus(s, frame_);
      pending_ &= (Dirty)~DIRTY_STATUS;
      return true;
    }
    return false;
  }
  const Frame& frame() const { return frame_; }
  Dirty pending() const { return pending_; }

 private:
  Frame frame_{};
  Dirty pending_ = 0;
  int   backdropBand_ = 0;  // 바탕을 어디까지 그렸는가(BACKDROP_BANDS 면 다 그렸다)
};

}  // namespace touchui
