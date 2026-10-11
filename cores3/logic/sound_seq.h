#pragma once
#include <stdint.h>

#include "logic/clock.h"

// D63 소리를 내는 순서의 판정. 하드웨어를 모른다(스피커와 마이크는 Out 뒤에 있다).
//   스피커와 마이크는 같은 소리 통로를 쓴다 → 소리 하나 = open(마이크를 내린다) … close(마이크를 올린다).
//   지키는 것:
//     - open 과 close 는 늘 짝이다(close 가 빠지면 마이크가 죽은 채로 남는다).
//     - 컴퓨터가 듣는 중에는 시작하지 않는다. 내는 도중에 컴퓨터가 듣기 시작하면 그 자리에서 끊고 돌려준다.
//     - 내는 도중에 청한 덜 중요한 소리(누름 소리)는 버린다 — 알림음이 끝난 뒤 뒤늦게 "톡" 하지 않게.
namespace soundseq {

struct Tone {
  uint16_t hz;
  uint16_t ms;
};

class Out {
 public:
  virtual ~Out() {}
  virtual void open() = 0;                        // 통로를 스피커에게
  virtual void tone(uint16_t hz, uint16_t ms) = 0;
  virtual void close() = 0;                       // 통로를 마이크에게
};

class Player {
 public:
  // tailMs: 마지막 음이 스피커에서 다 나갈 때까지의 여유
  explicit Player(uint32_t tailMs) : tailMs_(tailMs) {}

  // 소리를 청한다. kind 가 클수록 중요하다. seq 는 poll 이 끝낼 때까지 살아 있어야 한다(상수 표).
  void request(int kind, const Tone* seq, int len) {
    if (!seq || len <= 0 || kind < 0) return;
    if (kind <= (playing_ ? kind_ : pendKind_)) return;  // 내는 중이면 그보다 중요한 것만, 아니면 청해 둔 것보다 중요한 것만
    pendKind_ = kind;
    pendSeq_ = seq;
    pendLen_ = len;
  }

  // 매 틱 부른다. micLive: 컴퓨터가 듣는 중인가.
  void poll(uint32_t nowMs, bool micLive, Out& out) {
    if (playing_) {
      if (micLive) {  // 컴퓨터가 듣기 시작했다: 바로 비켜 준다. 청해 둔 것도 버린다.
        stop(out);
        clearPending();
        return;
      }
      if (!reachedMs(nowMs, nextMs_)) return;
      if (++pos_ < len_) {
        play(nowMs, out);
        return;
      }
      stop(out);
      return;  // 청해 둔 것이 있으면 다음 틱에 시작한다(마이크를 한 번 올렸다가)
    }
    if (pendKind_ < 0) return;
    kind_ = pendKind_;
    seq_ = pendSeq_;
    len_ = pendLen_;
    clearPending();
    if (micLive) return;  // 듣는 중: 내지 않고 버린다
    pos_ = 0;
    playing_ = true;
    out.open();
    play(nowMs, out);
  }

  bool playing() const { return playing_; }

 private:
  void play(uint32_t nowMs, Out& out) {
    out.tone(seq_[pos_].hz, seq_[pos_].ms);
    nextMs_ = nowMs + seq_[pos_].ms + (pos_ + 1 == len_ ? tailMs_ : 0);
  }
  void stop(Out& out) {
    out.close();
    playing_ = false;
    kind_ = -1;
  }
  void clearPending() {
    pendKind_ = -1;
    pendSeq_ = nullptr;
    pendLen_ = 0;
  }

  uint32_t    tailMs_;
  int         pendKind_ = -1;
  const Tone* pendSeq_ = nullptr;
  int         pendLen_ = 0;
  bool        playing_ = false;
  int         kind_ = -1;
  const Tone* seq_ = nullptr;
  int         len_ = 0, pos_ = 0;
  uint32_t    nextMs_ = 0;
};

}  // namespace soundseq
