#include "boot_guard.h"

#include "esp_attr.h"
#include "esp_system.h"

// 재시작해도 지워지지 않는 자리(전원을 껐다 켜면 쓰레기 값이 든다 → 표식으로 가린다)
#define GUARD_MAGIC 0x56494245u  // "VIBE"
static RTC_NOINIT_ATTR uint32_t s_magic;
static RTC_NOINIT_ATTR uint32_t s_crashes;

bool boot_guard_tripped(uint32_t limit) {
  const esp_reset_reason_t why = esp_reset_reason();
  const bool crashed = (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT);
  if (s_magic != GUARD_MAGIC || !crashed) {
    s_magic = GUARD_MAGIC;
    s_crashes = 0;
    return false;
  }
  if (++s_crashes >= limit) {
    s_crashes = 0;  // 내려받기 모드에서 새 펌웨어를 받으면 처음부터 다시 센다
    return true;
  }
  return false;
}

void boot_guard_healthy(void) { s_crashes = 0; }
