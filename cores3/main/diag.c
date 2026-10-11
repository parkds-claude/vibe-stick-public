#include "diag.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_system.h"

// 재시작해도 지워지지 않는 자리(전원을 껐다 켜면 쓰레기 값 → magic 으로 가린다)
RTC_NOINIT_ATTR vibe_diag_t g_vibe_diag;

void diag_boot(void) {
  uint32_t boots = (g_vibe_diag.magic == DIAG_MAGIC) ? g_vibe_diag.boots : 0;
  // 지난번에 물러난 까닭은 다음 켜짐에서도 읽을 수 있게 남겨 둘 필요가 없다:
  // 물러나면 곧바로 내려받기 모드라 그 사이에 이 함수가 불리지 않는다.
  memset(&g_vibe_diag, 0, sizeof(g_vibe_diag));
  g_vibe_diag.magic = DIAG_MAGIC;
  g_vibe_diag.boots = boots + 1;
  g_vibe_diag.stage = DIAG_STAGE_START;
  g_vibe_diag.reset_reason = (uint32_t)esp_reset_reason();
}
