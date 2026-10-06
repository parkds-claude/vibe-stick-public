#include "joy_filter.h"

// -128 을 뒤집어도 넘치지 않게
static inline int8_t negate8(int8_t v) { return v == -128 ? 127 : (int8_t)-v; }

void JoyFilter::feed(bool readOk, int8_t rawX, int8_t rawY, bool btn) {
  if (!readOk) {
    // 아직 값이 없을 때(처음·reset 직후)도 센다: 한도까지는 "아직 모름", 넘으면 "없음"(D31).
    if (misses_ <= MAX_MISSES && ++misses_ > MAX_MISSES) forget();
    return;
  }
  misses_ = 0;
  valid_ = true;
  x_ = swapXY_ ? rawY : rawX;
  y_ = swapXY_ ? rawX : rawY;
  if (invertY_) y_ = negate8(y_);
  if (invertX_) x_ = negate8(x_);
  btn_.update(btn);
}

void JoyFilter::fill(RawInput& out) const {
  out.joyOk = valid_;
  out.x = valid_ ? x_ : 0;
  out.y = valid_ ? y_ : 0;
  out.joyBtn = valid_ && btn_.stable;
}

void JoyFilter::forget() {
  valid_ = false;
  x_ = 0;
  y_ = 0;
  btn_ = Debounced();
}

void JoyFilter::reset() {
  forget();
  misses_ = 0;
}
