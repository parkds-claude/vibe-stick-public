#pragma once
// StickC 판의 src/ble_hid.cpp 를 고치지 않고 이 프로젝트(ESP-IDF)에서 빌드하기 위한 가장 작은 대역(D59).
// 그 파일이 Arduino 에서 쓰는 것은 millis 와 delay 둘뿐이다. 이 머리글은 그 파일을 빌드할 때만 보인다
// (main/CMakeLists.txt) — 다른 라이브러리가 "Arduino 환경이구나" 하고 오해하지 않게.
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// 진단 줄(Serial.printf)은 기기 속 기록으로 간다(D67).
#ifdef __cplusplus
#include "../vibe_serial.h"
#endif
