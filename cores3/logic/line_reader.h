#pragma once
#include <stddef.h>
#include <stdint.h>

// 로그 포트로 들어오는 바이트를 "명령 한 줄"로 모은다. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//
// usb_dev 의 한 틱은 이렇게 쓴다:
//   pumpLines(USB 에서 읽는 것, reader, 한 틱의 한도, 줄마다 부를 것);
//
// 받아들일 수 없는 줄은 줄바꿈까지 통째로 버린다.
//   이유: 예전에는 길이를 넘기면 모은 것만 비우고 이어서 받았다. 그러면 x 48개 + "vibe:boot" 의
//         뒷부분이 새 명령 vibe:boot 로 읽힌다. 줄 안의 0 바이트도 같은 종류다 — C 문자열은 거기서
//         끝나므로 "vibe:boot<0>junk" 가 vibe:boot 로 보인다(코덱스 검토).
//         명령은 "한 줄 전체"가 맞을 때만 명령이어야 한다: 줄의 일부가 다른 명령으로 읽히면 안 된다.
//   비용: 넘친 줄은 버렸다는 표시 없이 사라진다(보낸 쪽은 답이 없는 것으로 안다).
//   탈출구: N 을 키운다(LineReader<N> 은 N-1 자까지 받는다).
template <size_t N>
class LineReader {
 public:
  // 바이트 하나를 넣는다. 그 바이트가 줄바꿈(\n·\r)이고 받아들일 줄이 모였으면 true — 그 줄은
  // line() 으로 읽는다. 빈 줄, 넘친 줄, 0 바이트가 든 줄에서는 true 를 주지 않는다.
  bool feed(uint8_t c) {
    if (c == '\n' || c == '\r') {
      const bool ok = !skip_ && len_ > 0;
      buf_[ok ? len_ : 0] = '\0';
      len_ = 0;
      skip_ = false;  // 버리는 것은 이 줄까지다
      return ok;
    }
    if (skip_) return false;
    if (c == 0 || len_ + 1 >= N) {
      skip_ = true;  // 이 줄은 받을 수 없다: 줄바꿈이 올 때까지 버린다
      return false;
    }
    buf_[len_++] = (char)c;
    return false;
  }

  // 방금 받아들인 줄(0 으로 끝난다). feed() 가 true 를 준 직후부터 다음 feed() 전까지만 뜻이 있다.
  const char* line() const { return buf_; }

 private:
  char   buf_[N] = {};
  size_t len_ = 0;
  bool   skip_ = false;  // 지금 줄을 버리는 중이다
};

// src 에서 바이트를 읽어 reader 에 넣고, 줄이 하나 될 때마다 onLine(const char*) 을 부른다.
// src.read() 는 다음 바이트(0~255)를, 없으면 -1 을 준다. 읽은 바이트 수를 돌려준다.
//
// 한 번에 budget 바이트까지만 읽는다.
//   이유: 온 것이 있는 한 계속 읽으면, 컴퓨터가 쉬지 않고 보내는 동안 loop 가 여기서 빠져나오지
//         못한다 — 터치·화면·안전장치가 그만큼 밀린다(코덱스 검토).
//   비용: 한도를 넘는 만큼은 다음 틱으로 밀린다(읽다 만 줄은 reader 가 들고 있어 깨지지 않는다).
//   탈출구: budget 을 키운다(config.h 의 LOG_READ_BUDGET).
template <size_t N, class Source, class Handler>
int pumpLines(Source& src, LineReader<N>& reader, int budget, Handler&& onLine) {
  int n = 0;
  while (n < budget) {
    const int c = src.read();
    if (c < 0) break;
    ++n;
    if (reader.feed((uint8_t)c)) onLine(reader.line());
  }
  return n;
}
