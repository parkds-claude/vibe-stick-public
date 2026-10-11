#!/usr/bin/env python3
"""조이스틱을 꽂은 채 리셋되어 "다운로드 모드"에 빠진 기기를 풀어 준다.

왜 필요한가:
  조이스틱 통신선(SDA)이 기기의 부팅 설정 핀(G0)과 같은 선이다. 조이스틱과 통신하는 도중에
  기기가 리셋되면(펌웨어를 올릴 때, 시리얼 도구가 리셋을 걸 때) 조이스틱이 그 선을 낮게
  붙잡은 채로 남고, 기기는 리셋할 때마다 "펌웨어 내려받기 대기"로 들어간다. 이때 화면에는
  마지막 그림이 그대로 남아 멈춘 것처럼 보인다(실물에서 겪음).

하는 일:
  클럭선(SCL=G26)을 몇 번 흔들어 조이스틱이 선을 놓게 하고(I2C 의 표준 "bus clear"),
  G0 가 풀렸으면 기기를 다시 시작한다. 플래시에는 아무것도 쓰지 않는다.

사용: PlatformIO 의 파이썬으로(esptool 이 ~/.platformio/packages/tool-esptoolpy 에 있어야 한다 — 한 번이라도 빌드했으면 있다)
  ~/.platformio/penv/bin/python tools/unstick.py PORT      (공식 설치·VS Code 확장)
  (Homebrew 로 깐 PlatformIO 는 /opt/homebrew/opt/platformio/libexec/bin/python)

이것으로도 안 풀리면: 조이스틱을 뽑고 전원 버튼으로 껐다 켠다.
"""
import os
import sys
import time

sys.path.insert(0, os.path.expanduser("~/.platformio/packages/tool-esptoolpy"))
from esptool.cmds import detect_chip  # noqa: E402

GPIO_OUT_W1TS, GPIO_OUT_W1TC = 0x3FF44008, 0x3FF4400C
GPIO_EN_W1TS, GPIO_EN_W1TC = 0x3FF44024, 0x3FF44028
GPIO_IN = 0x3FF4403C
PIN0, PIN26 = 0x3FF44088, 0x3FF44088 + 26 * 4       # 출력 방식(오픈 드레인)
FUNC0, FUNC26 = 0x3FF44530, 0x3FF44530 + 26 * 4     # 출력 신호 선택
MUX0, MUX26 = 0x3FF49044, 0x3FF49028                # 핀 기능
RTC_PAD_G26, RTC_PAD_G0 = 0x3FF48488, 0x3FF48498    # 풀업·풀다운(이 두 핀은 RTC 쪽에서 정한다)
RUE, RDE = 1 << 27, 1 << 28
SDA, SCL = 1 << 0, 1 << 26
MUX_GPIO_INPUT = (2 << 12) | (2 << 10) | (1 << 9)   # GPIO 기능, 입력 켬


def main():
    port = sys.argv[1]
    esp = detect_chip(port=port, baud=115200, connect_mode="default_reset")

    def sda():
        return esp.read_reg(GPIO_IN) & 1

    if sda():
        print("G0 가 이미 풀려 있다 → 기기를 다시 시작한다")
        esp.hard_reset()
        return 0

    print("G0(조이스틱 SDA)가 낮게 붙잡혀 있다 → 클럭선을 흔든다")
    for reg in (RTC_PAD_G26, RTC_PAD_G0):            # 두 선에 약한 풀업
        esp.write_reg(reg, (esp.read_reg(reg) | RUE) & ~RDE)
    for mux, pin, func in ((MUX26, PIN26, FUNC26), (MUX0, PIN0, FUNC0)):
        esp.write_reg(mux, MUX_GPIO_INPUT)
        esp.write_reg(pin, 0x4)                       # 오픈 드레인: 0 이면 낮춤, 1 이면 놓음
        esp.write_reg(func, 0x100)
    esp.write_reg(GPIO_OUT_W1TS, SDA | SCL)
    esp.write_reg(GPIO_EN_W1TS, SCL)

    freed = None
    for i in range(1, 28):
        esp.write_reg(GPIO_OUT_W1TC, SCL)
        time.sleep(0.002)
        esp.write_reg(GPIO_OUT_W1TS, SCL)
        time.sleep(0.002)
        if sda() and freed is None:
            freed = i
            # 한 바이트(9클럭)를 마저 채워 조이스틱이 확실히 놓게 한다
            for _ in range(9):
                esp.write_reg(GPIO_OUT_W1TC, SCL)
                time.sleep(0.002)
                esp.write_reg(GPIO_OUT_W1TS, SCL)
                time.sleep(0.002)
            break
    print("풀린 클럭 수:", freed)

    # STOP 조건(SCL 이 높은 동안 SDA 를 낮음→높음)으로 통신을 끝낸다
    esp.write_reg(GPIO_OUT_W1TC, SCL)
    esp.write_reg(GPIO_EN_W1TS, SDA)
    esp.write_reg(GPIO_OUT_W1TC, SDA)
    time.sleep(0.002)
    esp.write_reg(GPIO_OUT_W1TS, SCL)
    time.sleep(0.002)
    esp.write_reg(GPIO_OUT_W1TS, SDA)
    time.sleep(0.002)
    esp.write_reg(GPIO_EN_W1TC, SDA | SCL)            # 두 선에서 손을 뗀다
    time.sleep(0.05)

    if sda():
        print("G0 가 풀렸다 → 기기를 다시 시작한다")
        esp.hard_reset()
        return 0
    print("G0 가 여전히 낮다 → 조이스틱을 뽑고 전원 버튼으로 껐다 켜야 한다")
    return 1


if __name__ == "__main__":
    sys.exit(main())
