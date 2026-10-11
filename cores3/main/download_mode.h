#pragma once
#include "esp_private/usb_phy.h"

#ifdef __cplusplus
extern "C" {
#endif

// 기기를 "내려받기 모드"(새 펌웨어를 받는 상태)로 재시작한다. 돌아오지 않는다.
//
// 이 기기는 USB 를 키보드·마이크로 쓰는 동안에는 평소의 자동 굽기 통로(USB-Serial-JTAG)가 닫혀 있다.
// 그래서 재시작하기 전에 USB 를 그 통로로 되돌리고, 컴퓨터가 장치를 새로 알아보게 한 뒤 재시작한다.
// 재시작하면 컴퓨터에는 "USB JTAG/serial debug unit" 으로 보이고, tools/flash.sh 가 거기에 굽는다.
//
// otg_phy: USB 를 켤 때 받은 손잡이(usb_new_phy). 여기서 놓아준다. USB 를 켜기 전이면 NULL.
// 부르기 전에: 컴퓨터에서 장치를 떼고(tud_disconnect) USB 를 처리하는 태스크를 세워 둘 것.
// 그 태스크 안에서 부르면 안 된다(loop 에서 부른다).
void download_mode_restart(usb_phy_handle_t otg_phy);

#ifdef __cplusplus
}
#endif
