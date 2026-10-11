#pragma once
// StickC 판의 src/ble_hid.cpp 가 진단 줄을 찍는 곳(Serial.printf)을 이 프로젝트의 "기기 속 기록"(D67)으로 잇는다.
// 그 파일은 -DDEBUG_LINK=1 로 빌드할 때만 진단 줄을 찍고(src/debug_trace.h), 찍는 곳은 블루투스 태스크다
// → 여기로 들어온 글은 고리에만 넣는다(USB 로 곧바로 내보내지 않는다 — usb_dev.cpp 의 vibeNote).
struct VibeSerial {
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};
extern VibeSerial Serial;
