#include "sound.h"

#include <M5Unified.h>

#include "config.h"
#include "mic.h"
#include "sound_seq.h"

namespace {

using soundseq::Tone;
const Tone CLICK[] = {{1900, 14}};                        // 톡
const Tone DONE[] = {{880, 90}, {1320, 150}};             // 끝났다: 올라가는 두 음
const Tone ASK[] = {{1320, 90}, {990, 90}, {1320, 150}};  // 답을 기다린다: 부르는 세 음
const uint32_t TAIL_MS = 30;                              // 마지막 음이 다 나갈 여유

// 통로를 넘기고 돌려받는 일(순서의 판정은 logic/sound_seq.h)
struct SpeakerOut : soundseq::Out {
  void open() override {
    mic::pause();  // 통로를 스피커에게 넘긴다
    M5.Speaker.begin();
    M5.Speaker.setVolume(SOUND_VOLUME);
  }
  void tone(uint16_t hz, uint16_t ms) override { M5.Speaker.tone(hz, ms); }
  void close() override {
    M5.Speaker.end();
    mic::resume();  // 통로를 마이크에게 돌려준다
  }
};

SpeakerOut       out;
soundseq::Player player(TAIL_MS);

}  // namespace

namespace sound {

void play(Kind k) {
  if (!SOUND_ON) return;
  switch (k) {
    case Kind::Click: player.request((int)k, CLICK, sizeof(CLICK) / sizeof(CLICK[0])); break;
    case Kind::Done:  player.request((int)k, DONE, sizeof(DONE) / sizeof(DONE[0]));    break;
    case Kind::Ask:   player.request((int)k, ASK, sizeof(ASK) / sizeof(ASK[0]));       break;
  }
}

void poll(uint32_t nowMs, bool micLive) {
  if (SOUND_ON) player.poll(nowMs, micLive, out);
}

}  // namespace sound
