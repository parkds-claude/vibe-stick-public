# StickC 판과 함께 쓰는 순수 로직(src/logic)은 복사하지 않고 그 자리의 파일을 그대로 빌드한다.
set(SHARED "${CMAKE_CURRENT_LIST_DIR}/../../src")

idf_component_register(
    SRCS
        "main.cpp"
        "ui.cpp"
        "mic.cpp"
        "usb_dev.cpp"
        "usb_desc.c"
        "download_mode.c"
        "boot_guard.c"
        "diag.c"
        "dev_show.cpp"
        "sensors.cpp"
        "sound.cpp"
        "../logic/touch_draw.cpp"
        "../logic/art_data.cpp"
        "${SHARED}/logic/input_logic.cpp"
        "${SHARED}/logic/keymap.cpp"
        "${SHARED}/logic/dispatch.cpp"
        "${SHARED}/logic/pixel_art.cpp"
        # 블루투스 키보드: StickC 판의 것을 고치지 않고 그대로 쓴다(D59)
        "${SHARED}/ble_hid.cpp"
    INCLUDE_DIRS
        "."
        "../logic"
        "${SHARED}"
    PRIV_REQUIRES
        usb esp_timer hal soc esp_driver_gpio bt nvs_flash
)

# src/ble_hid.cpp 는 <Arduino.h> 를 찾는다 → 그 파일을 빌드할 때만 우리의 작은 대역(shim/Arduino.h)을 보여 준다.
# 블루투스 부품은 <Arduino.h> 가 보이면 Arduino 의 글자열(String)까지 쓰려 든다 → 이 파일에서도 "없다"고 분명히 알린다
# (부품 자신은 그 머리글을 보지 못하므로 같은 값이다).
set_source_files_properties("${SHARED}/ble_hid.cpp" PROPERTIES
    COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/shim;-DNIMBLE_CPP_ARDUINO_STRING_AVAILABLE=0;-DDEBUG_LINK=1")
# -DDEBUG_LINK=1: 그 파일의 진단 줄(연결됨·끊김과 그 까닭·암호화·짝의 수)을 켠다. 찍히는 곳은 기기 속 기록이다
# (D67, main/vibe_serial.h). 선을 뽑고 블루투스로 쓰는 동안의 일을 나중에 읽으려고 켠다.

# M5Unified 헤더가 내는 경고 하나를 끈다(우리 코드의 경고가 아니다).
target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-literal-suffix>")

# TinyUSB 라이브러리가 우리 설정 파일(tusb_config.h)을 읽게 한다.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} BEFORE PUBLIC "${CMAKE_CURRENT_LIST_DIR}")

# 개발판: idf.py -DVIBE_DEV=1 build  (안전장치 D42, 시험용 사건 주입 D43 — config.h)
if(VIBE_DEV)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEV_BUILD=1)
endif()
# 다른 마스코트: idf.py -DMASCOT=Cat build  (src/logic/pixel_art.h, D26)
if(MASCOT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MASCOT_ID=${MASCOT})
endif()
