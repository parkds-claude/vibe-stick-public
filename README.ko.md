# vibe-stick

키보드 없이, 말로 바이브 코딩하는 버튼. *(English: [README.md](README.md))*

M5StickC Plus + Mini JoyC HAT 을 **블루투스 키보드**로 만들어, 맥의 VS Code 안 Claude Code 를 말로 조작한다.
음성 인식은 Claude Code 확장에 들어 있는 받아쓰기(⌘D)를 그대로 쓴다. **컴퓨터에 설치할 것은 없다.**

![화면 모음](docs/design/screens.png)
*모든 화면(기기와 같은 픽셀). 기본 마스코트는 Claude Code 캐릭터의 비공식 팬아트로 MIT 예외 — [마스코트](#마스코트) 참고.*

## ⚠️ 먼저 읽을 것 — 위험 안내

**펌웨어는 이 저장소(https://github.com/parkds-claude/vibe-stick-public)의 소스로 직접 빌드하고, 다른 곳에서 받은 바이너리는 굽지 않는다** — 고친 펌웨어는 만든 사람이 원하는 것을 치는 키보드다. 배포판(Release)과 빌드된 펌웨어는 없다. 보안에 대한 안내와 문제를 알리는 길: [SECURITY.md](SECURITY.md).

이 기기는 **키보드**다. 짝 맺은 컴퓨터에 Enter·Esc·⌘D 를 보낸다. 그리고 Enter 는 Claude 의 "이 파일 고쳐도 돼?" 창을 **승인**한다.

- **짝 맺기는 숫자 확인이 없다(Just Works).** PAIR ME 화면인 동안 근처의 아무 컴퓨터나 연결을 시도할 수 있고, **먼저 맺는 쪽이 임자**다 — 그러면 앞+옆 3초로 초기화하기 전까지 기기는 그 컴퓨터 것이다. 짝 맺는 순간 옆에서 엿듣는 장치가 있으면 원리상 키를 알아낼 수 있다(보이는 것은 Enter·Esc·⌘D·화살표). 짝은 집·사무실처럼 믿을 수 있는 곳에서 맺고, 맺은 뒤에는 기기가 다른 컴퓨터를 받지 않는다(한 번에 한 컴퓨터).
- **기기를 잃어버리면** 주운 사람이 짝 맺은 컴퓨터 근처에서 버튼을 눌러 Enter 를 보낼 수 있고, **기기를 든 누구나 짝 초기화를 할 수 있다**(PIN 없음). 잃어버렸으면 **컴퓨터의 Bluetooth 설정에서 `vibe-stick` 을 바로 지운다.**
- 기기는 컴퓨터 화면을 모른다. **앞 버튼을 누르기 전에 컴퓨터 화면을 본다.** 편집기(파일)를 클릭한 상태면 앞 버튼·조이스틱이 **파일 안에 글자로 들어간다.** 옆 버튼(Esc)을 빠르게 두 번 누르면 Claude Code 의 *되감기(Rewind)* 창이 열리고 거기서 Enter 는 **파일을 되돌린다** — 기기가 1초에 한 번으로 막고 있지만, "Rewind" 가 보이면 옆 버튼으로 닫는다. 더 자세한 주의는 [docs/사용법.md](docs/사용법.md) "조심할 것 세 가지".
- 블루투스 이름 `vibe-stick` 과 "키보드"라는 사실은 광고로 늘 보인다.

## 두 가지 판

| | 조이스틱 판 (`src/`) | 화면 판 (`cores3/`) |
|---|---|---|
| 기기 | M5StickC Plus + Mini JoyC HAT | M5Stack CoreS3 (터치 화면) |
| 연결 | 블루투스 키보드 | 선 하나로 **USB 키보드 + USB 마이크**, 선을 뽑으면 블루투스 키보드 |
| 말을 듣는 곳 | 컴퓨터의 마이크 | 기기의 마이크(USB 로 들어간다 — 컴퓨터의 소리 입력으로 골라야 한다) |
| 조작 | 조이스틱 + 버튼 둘 | 터치 타일 일곱 |
| 빌드 도구 | PlatformIO | ESP-IDF 5.5 |
| 사용법 | [docs/사용법.md](docs/사용법.md) | [docs/사용법-cores3.md](docs/사용법-cores3.md) |

![화면 판의 화면](docs/design/cores3-screens.png)
*화면 판의 모든 장면(기기 픽셀 그대로). 아이콘은 빌드 스크립트가 그린 선 그림이다. 캐릭터는 위와 같은 비공식 Claude Code 팬 아트다(MIT 의 예외 — [마스코트](#마스코트) 참고). 자체 캐릭터로 빌드하려면 `cd cores3 && idf.py -B build -DMASCOT=Cat reconfigure` 뒤 `tools/flash.sh`.*

두 판 모두 키보드다. **굽기 전에 [SECURITY.md](SECURITY.md) 를 읽고**, 펌웨어는 직접 빌드하고, 남이 만든 바이너리는 굽지 말 것.

## 조작

| 조작 | 동작 | 화면 |
|---|---|---|
| 조이스틱 **누르기** | 말하기 켜고 끄기(⌘D). 딸깍 한 번이면 된다 | TALK |
| **앞 큰 버튼** | Enter — 보내기 · 허락 · 선택 확정 | OK! |
| **옆 버튼** | Esc — 취소 · 거부 (1초에 한 번) | NOPE |
| 조이스틱 **오른쪽 / 왼쪽** | ⌘` / ⇧⌘` — 다음 / 이전 VS Code 창 | NEXT / BACK |
| 조이스틱 **위 / 아래** | ↑ ↓ — 선택지 고르기 | UP / DOWN |
| 앞 + 옆 **3초** | 짝 정보 지우고 다시 시작 | BYE! |

자세한 쓰는 법·짝 맺기·문제 해결: [docs/사용법.md](docs/사용법.md)

## 전제와 한계 (솔직하게)

- **맥 + VS Code + Claude Code 확장의 기본 단축키 전용**이다. ⌘D(받아쓰기)와 ⌘`(같은 앱의 다음 창)를 보낸다. 단축키를 바꿨으면 그대로는 안 맞는다. Windows 는 Ctrl+D(말하기)만 보내고 창 이동은 없다(`src/config.h` 의 `HOST`). Linux 는 아직 없다.
- **기기는 녹음이 켜져 있는지 모른다.** TALK 는 "내가 ⌘D 를 보낸 횟수"로 추측한 것이다. 컴퓨터가 15초 조용하면 스스로 끄거나, 다른 창에서 ⌘D 가 다른 일을 하면 화면과 실제가 어긋난다. **컴퓨터 화면의 마이크 표시가 기준**이다.
- 잘못 받아써진 글은 기기로 지울 수 없다(키보드로 지우거나 말로 고쳐 달라고 한다).
- 배터리는 120mAh 로 작다. 30초 뒤 화면이 어두워질 뿐 절전은 없다(수명 실측 전). 책상에서는 USB 를 꽂아 둔다.
- 조이스틱 통신선이 ESP32 의 부팅 핀(G0)과 같다. **조이스틱을 꽂은 채 시리얼 도구로 리셋하지 않는다**(다운로드 모드로 빠져 멈춘 것처럼 보임 → `tools/unstick.py`, 안 되면 조이스틱 뽑고 전원 껐다 켜기).

## 준비물과 굽기

| 기기 | 공식 명칭 · SKU | 문서 | 구매 (M5Stack 공식 상점, 2026-10 확인) |
|---|---|---|---|
| 본체 | **M5StickC PLUS** (ESP32-PICO-D4, 4MB 플래시, 1.14" LCD, 120mAh) · K016-P | [docs](https://docs.m5stack.com/en/core/m5stickc_plus) | [$19.95 — EOL, 남은 재고만](https://shop.m5stack.com/products/m5stickc-plus-esp32-pico-mini-iot-development-kit) |
| 조이스틱 | **M5StickC Mini JoyC HAT** (STM32F030, I2C 0x54, 조이스틱 1개 + 버튼 + RGB LED) · U156 | [docs](https://docs.m5stack.com/en/products/sku/U156) | [$8.50](https://shop.m5stack.com/products/m5stickc-mini-joyc-hat-stm32f030) |
| 케이블 | USB-C (데이터 되는 것) | | |

- 본체는 **M5StickC PLUS(1세대)** 로 만들고 확인했다. 후속 **PLUS2**(ESP32-PICO-V3-02, 8MB, K016-P2)도 단종·품절이고 **이 펌웨어로 시험하지 않았다**(전원 관리 칩이 달라 `M5Unified` 가 받아 주더라도 보장 못 함). 그다음 **M5StickS3** 는 칩(ESP32-S3)이 달라 그대로는 안 된다. 중고·국내 유통(디바이스마트·엘레파츠 등)·알리익스프레스에서 "M5StickC Plus" 로 검색하면 아직 구할 수 있다.
- 조이스틱은 조이스틱 **두 개**짜리 "JoyC HAT"(U079, I2C 0x38)과 다른 제품이다. 꼭 **Mini** JoyC(U156) 여야 한다.
- [PlatformIO](https://platformio.org/) (VS Code 확장 또는 `pip install platformio`). 라이브러리는 빌드 때 자동으로 받는다.

```
pio run -e stickc                                   # 빌드
pio run -e stickc -t upload --upload-port <PORT>    # 굽기 (<PORT> 는 /dev/cu.usbserial-… 또는 COMx)
pio run -e stickc-debug -t upload --upload-port <PORT>   # 진단 로그가 켜진 빌드
```

올린 뒤: 기기 화면에 **PAIR ME** → 맥 시스템 설정 → Bluetooth → `vibe-stick` 연결 → **HI!** 가 뜨면 끝. 다음부터는 켜면 스스로 붙는다.

## 구조

`src/logic/` 은 기기와 무관한 순수 코드이고 컴퓨터에서 시험한다. 하드웨어와 닿는 층(`src/*.cpp`)은 얇게 두고 시험하지 않는다(기기 실물로 확인).

| 파일 | 맡는 일 |
|---|---|
| `src/logic/joy_filter.*` | 조이스틱 읽기 값 다듬기: 실패 버티기, 누름 떨림 거르기, 방향 보정 |
| `src/logic/input_logic.*` | 버튼·조이스틱 → 사건 (말하기 방식, 방향 판정, 취소 간격 등) |
| `src/logic/keymap.*` | 사건 → 키 |
| `src/logic/dispatch.*` | 사건 처리: 키 전송 순서, 연결 없을 때 버리기, 연결 상태가 바뀔 때 키 떼기 |
| `src/logic/link_tracker.h` | 블루투스 사건(연결·암호화·끊김) → 연결 상태 |
| `src/logic/scene.h`, `pixel_art.*`, `screen_draw.*` | 화면: 장면, 글자·마스코트 점 그림, "사각형 채우기"만으로 그리기 |
| `src/logic/screen_state.h` | 지금 어떤 장면을 보일 것인가 |
| `src/logic/debounce.h`, `clock.h`, `link_state.h` | 공용 도우미: 떨림 거르기, 시간 비교, 연결 상태 정의 |
| `src/ble_hid.*` | 블루투스 키보드 (NimBLE) |
| `src/joyc.*` | 조이스틱 읽기 (I2C) |
| `src/ui.*` | 화면을 LCD 에 옮기기, 밝기 |
| `src/config.h` | 설정(맥/윈도우, 말하기 방식, 조이스틱 방향) |

설계 결정은 코드 주석에 `D번호` 로 이유·비용·되돌리는 법과 함께 적혀 있다.

## 개발

```
test/host/run.sh                 # 호스트 시험 세 묶음: 조작·연결 / 화면 / loop 순서 통합 (기기 불필요)
test/host/preview.sh             # 화면 미리보기 그림 → docs/design/screens.png 를 덮어쓴다 (기기와 같은 픽셀)
test/host/compare_refactor.sh    # 구조 정리 전후의 동작이 같은지 무작위 입력으로 대조
```

시험은 시나리오 **122개**(조작·연결 80, 화면 37, loop 통합 5)이고, 안에서 반복하며 확인하는 CHECK 는 8천여 건이다.

로그 보기(기기를 재시작시키지 않고 연다 — 조이스틱을 꽂은 채 `--reset` 은 쓰지 않는다):

```
~/.platformio/penv/bin/python tools/serial_capture.py <PORT> 20     # pyserial 이 있는 파이썬이면 아무거나
```

## 마스코트

기본 마스코트는 Claude Code 의 캐릭터를 작성자가 개인용으로 픽셀로 옮긴 **비공식 팬아트**다. **MIT 라이선스에 포함되지 않는다** — 권리는 Anthropic 에 있고, 승인을 받은 것이 아니며, 요청이 있으면 제거한다(그때까지 이 저장소의 git 이력에는 남는다). **상업적으로 쓰거나 판매·배포하는 기기에 넣지 말 것** — `-DMASCOT_ID=Cat` 등 자체 캐릭터로 굽는다. 이 프로젝트를 위해 새로 그린 다섯(MIT)이 함께 들어 있어 빌드 때 고를 수 있다.

| 이름 | 빌드 값 | 모습 |
|---|---|---|
| 클로드 (기본) | `Claude` | 비공식 팬아트 |
| 까망이 | `Cat` | 검은 고양이 |
| 스틱이 | `Stick` | 오락실 조이스틱 |
| 키캡이 | `Keycap` | 키보드에서 빠져나온 키 |
| 삐삐 | `Robot` | 안테나 달린 로봇 |
| 새싹이 | `Sprout` | 머리에 새싹 난 찹쌀떡 |

바꾸려면 `src/logic/pixel_art.h` 의 `MASCOT_ID` 를 고치거나 `PLATFORMIO_BUILD_FLAGS="-DMASCOT_ID=Cat" pio run …` 처럼 붙인다. 굽기 전에 `test/host/preview.sh` 로 그림부터 본다. 그림 데이터는 `src/logic/pixel_art.cpp` 의 글자 격자라 새 마스코트를 그려 넣기도 쉽다. 다섯 모습: [docs/design/mascots.png](docs/design/mascots.png)

## 비공식 프로젝트

Anthropic(Claude·Claude Code), M5Stack, Apple, Microsoft(VS Code) 어느 곳과도 관계없고 승인·후원을 받지 않은 개인 취미 프로젝트다. 상표는 각 회사의 것이다. 외부 구성 요소와 마스코트 예외는 [NOTICE](NOTICE), 코드와 자체 캐릭터 다섯의 라이선스는 [MIT](LICENSE).
