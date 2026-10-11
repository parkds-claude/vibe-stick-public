#pragma once
#include <stddef.h>
#include <string.h>

// D67 기기가 스스로 적어 두는 짧은 기록: 가장 최근 N 바이트만 남는 고리. 하드웨어를 모른다(컴퓨터에서 시험 가능).
//   까닭: 기록은 USB 의 로그 포트로만 나가는데, 선을 뽑은 동안(블루투스로 쓸 때)이나 아무도 포트를 열지 않은
//         동안의 일은 어디에도 남지 않았다. 사용자가 선을 뽑고 블루투스로 써 보다 "Not connected" 가
//         떴는데(맥은 연결됐다고 했다) 무슨 일이 있었는지 알 길이 없었다.
//   쓰는 법: 기기는 모든 기록 줄을 여기에도 넣고, 컴퓨터가 `vibe:hist` 를 보내면 통째로 내준다.
//   비용: 메모리 N 바이트. 여러 태스크가 넣으므로 넣고 꺼내는 쪽에서 잠가야 한다(이 틀은 잠그지 않는다).
//   탈출구: 넣지 않으면 된다(usb_dev.cpp 의 histAdd).
namespace touchui {

template <size_t N>
class HistRing {
 public:
  // 글 s(n 바이트)를 끝에 붙인다. 자리가 모자라면 가장 오래된 것부터 밀려난다.
  void add(const char* s, size_t n) {
    if (n == 0) return;
    if (len_ + n > N) wrapped_ = true;  // 오래된 것이 밀려난다(꼭 맞게 찬 것은 밀려난 것이 아니다)
    if (n > N) {
      s += n - N;
      n = N;
    }
    for (size_t i = 0; i < n; i++) {
      buf_[head_] = s[i];
      if (++head_ == N) head_ = 0;
    }
    len_ = (len_ + n > N) ? N : len_ + n;
  }

  size_t size() const { return len_; }

  // 오래된 것부터 차례로 out 에 옮긴다(cap 바이트까지). 밀려나 앞이 잘린 첫 줄은 빼고 온전한 줄부터 준다.
  // 반환: 옮긴 바이트 수. (잠근 채 부르는 일이라 나눗셈 없이 돈다.)
  size_t copy(char* out, size_t cap) const {
    size_t at = (head_ >= len_) ? head_ - len_ : head_ + N - len_;  // 가장 오래된 글자의 자리
    size_t left = len_;
    if (wrapped_) {  // 첫 줄바꿈 뒤부터가 온전한 줄이다
      while (left > 0) {
        const char c = buf_[at];
        if (++at == N) at = 0;
        --left;
        if (c == '\n') break;
      }
    }
    size_t n = 0;
    while (left > 0 && n < cap) {
      out[n++] = buf_[at];
      if (++at == N) at = 0;
      --left;
    }
    return n;
  }

 private:
  char   buf_[N] = {};
  size_t head_ = 0;  // 다음에 쓸 자리
  size_t len_ = 0;   // 들어 있는 바이트 수
  bool   wrapped_ = false;  // 오래된 것이 밀려난 적이 있다(첫 줄이 잘려 있을 수 있다)
};

}  // namespace touchui
