#include "download_mode.h"

#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/clk_gate_ll.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/periph_defs.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

// ESP32-S3 의 USB 데이터 핀
#define USB_DM_GPIO 19
#define USB_DP_GPIO 20

// D41 내려받기 모드로 갈 때는 USB 를 USB-Serial-JTAG 로 되돌린 뒤 재시작한다.
//   순서는 arduino-esp32 의 usb_switch_to_cdc_jtag()(cores/esp32/esp32-hal-tinyusb.c)를 따랐다.
//   ESP32-S3 보드 대부분이 이 방식으로 "USB 키보드 등을 쓰다가 자동으로 굽기"를 한다.
//   이유: "내려받기 모드로 재시작"만 하면 USB 가 OTG 쪽에 묶인 채라, 이 기기에서는 재시작한 뒤
//         컴퓨터에 아무것도 보이지 않는다(UIFlow2 에서 겪은 일 — 개발 기록(공개판에는 없음)).
//   비용: 숨은(private) 저수준 헤더를 쓴다. ESP-IDF 판이 바뀌면 다시 확인해야 한다(지금 5.5.4).
//   탈출구: 이 길이 막히면 기기의 RST 버튼을 3초 누른다(초록 불) — 언제나 되는 길이다.
void download_mode_restart(usb_phy_handle_t otg_phy) {
  // 1) USB-OTG 를 끈다. (부르는 쪽이 먼저 컴퓨터에서 장치를 떼고 USB 태스크를 세워 두었다.)
  if (otg_phy) usb_del_phy(otg_phy);
  periph_ll_reset(PERIPH_USB_MODULE);
  periph_ll_disable_clk_set_rst(PERIPH_USB_MODULE);

  // 2) USB-Serial-JTAG 의 시계를 켠다. 지금 빌드 설정에서는 늘 켜져 있지만(sdkconfig.defaults 의
  //    CONFIG_USJ_ENABLE_USB_SERIAL_JTAG), 설정이 바뀌어 꺼져 있어도 이 길이 막히지 않게 한다.
  PERIPH_RCC_ATOMIC() { usb_serial_jtag_ll_enable_bus_clock(true); }

  // 3) USB 통로를 기본값(USB-Serial-JTAG)으로 되돌린다.
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                      RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);  // 외부 PHY 아님
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);

  // 4) 데이터 선 둘을 잠깐 0 으로 눌러 컴퓨터가 "뽑혔다"고 알게 한다(그래야 새로 알아본다).
  const gpio_config_t io = {
      .pin_bit_mask = (1ULL << USB_DM_GPIO) | (1ULL << USB_DP_GPIO),
      .mode = GPIO_MODE_OUTPUT_OD,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);
  gpio_set_level(USB_DM_GPIO, 0);
  gpio_set_level(USB_DP_GPIO, 0);
  vTaskDelay(pdMS_TO_TICKS(50));

  // 5) USB-Serial-JTAG 를 선에 잇고, 컴퓨터가 새 장치로 알아볼 때(버스 리셋)까지 최대 1초 기다린다.
  const usb_serial_jtag_pull_override_vals_t pull = {.dp_pu = 1, .dm_pu = 0, .dp_pd = 0, .dm_pd = 0};
  usb_serial_jtag_ll_phy_enable_pull_override(&pull);
  usb_serial_jtag_ll_phy_disable_pull_override();
  usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  for (int i = 0; i < 100; i++) {
    if (usb_serial_jtag_ll_get_intraw_mask() & USB_SERIAL_JTAG_INTR_BUS_RESET) break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 6) "다음 시작은 내려받기 모드"라고 적어 두고 재시작한다.
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
  for (;;) {
  }
}
