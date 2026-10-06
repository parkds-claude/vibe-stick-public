#pragma once

// 접점 떨림 거르기: 같은 값이 두 번 연속 들어와야 바뀐 것으로 인정한다.
// (버튼은 10ms 틱마다, 조이스틱 누름은 읽을 때마다 한 번씩 update 를 부른다.)
struct Debounced {
  bool stable = false;
  bool last   = false;
  bool update(bool raw) {
    if (raw != stable && raw == last) stable = raw;
    last = raw;
    return stable;
  }
};
