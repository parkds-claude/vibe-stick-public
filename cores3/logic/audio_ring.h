#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// 마이크가 받은 소리를 USB 쪽에 넘기는 고리 버퍼. 하드웨어를 모른다(컴퓨터에서 시험).
//
// 쓰는 쪽(마이크 태스크)과 읽는 쪽(USB 인터럽트)이 하나씩이라 잠금 없이 쓴다:
// 쓰는 쪽은 head 만, 읽는 쪽은 tail 만 바꾼다.
//
// D38 소리는 한 방향으로만 넘긴다: 마이크 태스크 → 이 버퍼 → (USB 인터럽트가 꺼내) USB 보관함.
//   이유: USB 라이브러리(TinyUSB)의 보관함은 쓰는 곳과 읽는 곳이 각각 하나일 때만 안전하다(잠금이 없다).
//         마이크 태스크가 거기에 직접 쓰면, 컴퓨터가 마이크를 닫는 순간 라이브러리가 보관함을 비우는
//         것과 겹친다. 그래서 보관함에는 USB 쪽만 손대게 하고, 태스크 사이의 전달은 이 버퍼가 맡는다.
//   마이크의 시계와 컴퓨터의 시계가 조금 다른 것은 TinyUSB 가 맞춘다: 보관함을 절반쯤 찬 상태로
//   지키도록 1ms 에 보내는 표본 수를 15~17개로 조절하고, 컴퓨터가 그 속도를 따른다(USB 표준의
//   "비동기" 방식). 이 버퍼는 온 것을 그대로 넘길 뿐 속도를 맞추지 않는다.
//   비용: 복사가 한 번 더 있다(10ms 에 320바이트).
//   탈출구: 없음 — 이 구조를 바꾸려면 TinyUSB 보관함에 잠금을 넣어야 한다.
class AudioRing {
 public:
  static const uint32_t SIZE = 2048;  // 2의 거듭제곱. 16kHz 에서 0.128초

  // --- 쓰는 쪽(마이크 태스크) ---
  // 표본 n 개를 넣는다. 자리가 모자라면 하나도 넣지 않고 false(넘침으로 센다).
  bool push(const int16_t* s, size_t n) {
    const uint32_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) + n > SIZE) {
      overruns_.fetch_add((uint32_t)n, std::memory_order_relaxed);
      return false;
    }
    for (size_t i = 0; i < n; i++) buf_[(h + i) & (SIZE - 1)] = s[i];
    head_.store(h + (uint32_t)n, std::memory_order_release);
    return true;
  }

  // --- 읽는 쪽(USB 인터럽트) ---
  // 많아야 max 개를 꺼내 out 에 담고, 꺼낸 개수를 돌려준다(없으면 0).
  size_t pop(int16_t* out, size_t max) {
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t have = head_.load(std::memory_order_acquire) - t;
    const size_t take = have < max ? have : max;
    for (size_t i = 0; i < take; i++) out[i] = buf_[(t + i) & (SIZE - 1)];
    tail_.store(t + (uint32_t)take, std::memory_order_release);
    return take;
  }
  // 쌓여 있는 것을 버린다(컴퓨터가 마이크를 새로 열 때: 옛 소리가 나가지 않게).
  void discard() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

  // --- 어느 쪽에서나 ---
  uint32_t fill() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }
  uint32_t overruns() const { return overruns_.load(std::memory_order_relaxed); }

 private:
  int16_t               buf_[SIZE] = {};
  std::atomic<uint32_t> head_{0};  // 다음에 쓸 자리(쓰는 쪽만 바꾼다)
  std::atomic<uint32_t> tail_{0};  // 다음에 읽을 자리(읽는 쪽만 바꾼다)
  std::atomic<uint32_t> overruns_{0};
};

// 컴퓨터의 "입력 음량" 조절값(데시벨)을 곱셈 값으로 바꾼다. 256 이 1배다.
//   db: -24 ~ +24. 범위 밖은 끝값으로 자른다.
inline int32_t gainQ8FromDb(int db) {
  // 10^(db/20) * 256 을 1dB 단위 표로 둔다(기기에서 실수 계산을 하지 않으려고).
  static const uint16_t TABLE[49] = {
      16,   18,   20,   23,   26,   29,   32,   36,   41,   46,   51,   57,   64,
      72,   81,   91,   102,  114,  128,  144,  162,  181,  203,  228,  256,  287,
      322,  362,  406,  455,  511,  573,  643,  722,  810,  908,  1019, 1144, 1283,
      1440, 1615, 1812, 2033, 2282, 2560, 2872, 3223, 3616, 4057};
  if (db < -24) db = -24;
  if (db > 24) db = 24;
  return TABLE[db + 24];
}

// 표본에 음량을 곱한다(넘치면 끝값에서 자른다). gainQ8 = 256 이면 그대로, 0 이면 소리 없음.
inline void applyGain(int16_t* s, size_t n, int32_t gainQ8) {
  if (gainQ8 == 256) return;
  for (size_t i = 0; i < n; i++) {
    int32_t v = ((int32_t)s[i] * gainQ8) >> 8;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    s[i] = (int16_t)v;
  }
}
