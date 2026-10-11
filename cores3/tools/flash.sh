#!/bin/sh
# vibe-stick CoreS3 판을 빌드해 기기에 올린다.
#   cores3/tools/flash.sh          평소 판
#   cores3/tools/flash.sh --dev    개발판(안전장치 D42, 시험용 사건 주입 D43)
#
# 기기가 우리 펌웨어로 돌고 있으면 로그 포트로 명령을 보내 스스로 내려받기 모드에 들어가게 한다.
#   - 개발판은 곧바로 들어간다.
#   - 평소 판은 화면에 `Update firmware?` 를 띄운다 → 1.5초 뒤부터 20초 안에 화면의 ✓ 를 "2초 꾹" 눌렀다 떼야
#     들어간다(D47 · D75 — 톡 친 ✓ 는 받지 않는다: 습관처럼 누른 ✓ 로 펌웨어가 바뀌지 않게).
#     손가락이 화면에 닿아 있으면 받지 않는다(D51). 거절하거나 시간이 지나면 10초 동안은 다시 묻지 않는다.
# 다른 펌웨어(UIFlow2 등)이거나 아무 반응이 없으면: 기기의 RST 버튼을 3초 눌러 초록 불을 켠 뒤 다시 실행.
# 처음 올리기 전에는 반드시 backup.py 로 통째 백업부터(개발 기록(공개판에는 없음)).
set -e
cd "$(dirname "$0")/.."
# shellcheck disable=SC1090
. "$HOME/esp/esp-idf-5.5.4/export.sh" >/dev/null 2>&1

BUILD=build
DEV=0
KIND="평소 판"
case "$1" in
  "") ;;
  --dev) BUILD=build-dev; DEV=1; KIND="개발판" ;;
  # 잘못 친 인자(--Dev, --dve)를 조용히 "평소 판"으로 받아들이지 않는다.
  *) echo "모르는 인자: $1  (쓰는 법: flash.sh 또는 flash.sh --dev)"; exit 1 ;;
esac
if [ $# -gt 1 ]; then echo "인자는 하나까지만 받는다(쓰는 법: flash.sh 또는 flash.sh --dev)"; exit 1; fi

# 빌드가 실패하면 여기서 멈춘다(실패한 채 넘어가면 예전에 만든 펌웨어가 올라간다).
# 어느 판인지는 늘 분명히 넘긴다(-DVIBE_DEV=0 또는 1). 넘기지 않으면 빌드 폴더가 기억해 둔 값을
# 그대로 쓴다 — build/ 에서 한 번이라도 `idf.py -DVIBE_DEV=1 build` 를 했다면, 평소 판을 올린다면서
# 개발판(컴퓨터가 키를 치게 할 수 있고, 화면 확인 없이 다시 구워지는 판)이 올라간다.
# 기본 설정(sdkconfig.defaults)이 지금의 설정(sdkconfig — git 에 넣지 않는, 기기마다 만들어지는 파일)보다 새것이면
# 다시 만들게 치운다. ESP-IDF 는 이미 있는 sdkconfig 의 값을 기본 설정보다 앞세우므로, 치우지 않으면 기본 설정에
# 더한 것이 조용히 빠진다(맥북에 남아 있던 옛 sdkconfig 에 블루투스가 꺼져 있어 빌드가 깨졌다).
# 파일의 시각이 아니라 내용으로 본다: 기본 설정의 "CONFIG_…=값" 줄이 지금의 설정에 그대로 들어 있는가.
# (시각으로 보면, 그 사이에 idf.py 를 직접 돌려 sdkconfig 가 새로 쓰였을 때 놓친다 — 독립 검토.)
if [ -f sdkconfig ]; then
  MISSING=$(grep -E '^CONFIG_[A-Za-z0-9_]+=' sdkconfig.defaults | while IFS= read -r line; do
    grep -qxF -- "$line" sdkconfig || printf '%s\n' "$line"
  done)
  if [ -n "$MISSING" ]; then
    echo "기본 설정에 있는데 지금 설정에는 없는 값이 있다 → sdkconfig 를 다시 만든다(옛 것은 sdkconfig.old 로):"
    printf '%s\n' "$MISSING" | sed 's/^/  /'
    mv -f sdkconfig sdkconfig.old
  fi
fi
# 빌드 기록은 이름을 미리 알 수 없는 임시 파일에 쓴다(본인만 읽는다). 정해진 이름(/tmp/vibe-build.log)에 쓰면
# 누가 그 이름으로 미리 링크를 걸어 둔 다른 파일을 덮어쓸 수 있다(보안 점검). 성공하면 지운다.
LOG=$(mktemp "${TMPDIR:-/tmp}/vibe-$BUILD.XXXXXX")
if ! idf.py -B "$BUILD" "-DVIBE_DEV=$DEV" build >"$LOG" 2>&1; then
  tail -30 "$LOG"
  echo "빌드 실패 — 올리지 않는다. 전체 기록: $LOG"
  exit 1
fi
tail -2 "$LOG"
rm -f "$LOG"

# 만든 것이 정말 그 판인지 확인하고 올린다(컴파일 명령에 DEV_BUILD=1 이 들어 있는가).
if grep -q -- '-DDEV_BUILD=1' "$BUILD/compile_commands.json"; then BUILT=1; else BUILT=0; fi
if [ "$BUILT" != "$DEV" ]; then
  echo "$KIND 을 만들려 했는데 만들어진 것은 다른 판이다(DEV_BUILD=$BUILT) — 올리지 않는다."
  echo "$BUILD 폴더를 지우고 다시 실행한다."
  exit 1
fi

# 어느 기기에 올릴지 적어 둔 것이 있어야 한다. 기기를 내려받기 모드로 보내기 전에 본다
# (보낸 뒤에 알면, 기기는 이미 화면이 꺼진 채 기다리고 있다).
python tools/vibectl.py wanted >/dev/null
STATE=$(python tools/vibectl.py state)
case "$STATE" in
  app)      PORT=$(python tools/vibectl.py boot) ;;
  download) PORT=$(python tools/vibectl.py port download) ;;
  uiflow)   echo "UIFlow2 가 돌고 있다. esptool 을 쏘면 USB 에서 사라진다 → RST 3초(초록 불) 뒤 다시 실행."; exit 1 ;;
  ambiguous) echo "ESP32 보드가 여러 대 보여 어느 것인지 알 수 없다. 다른 보드를 뽑거나 VIBE_MAC=<MAC 12자리> 를 준다."; exit 1 ;;
  *)        echo "기기($(python tools/vibectl.py wanted))가 보이지 않는다. USB 선(데이터가 되는 선)을 확인하고, RST 3초(초록 불) 뒤 다시 실행."; exit 1 ;;
esac
echo "내려받기 모드: $PORT"
# 굽기 전의 마지막 확인: 이 보드가 정말 우리 기기인가(MAC). 옆에 꽂힌 다른 ESP32-S3 보드에 굽지 않는다.
MAC=$(python tools/vibectl.py confirm "$PORT")
echo "기기 확인: MAC $MAC"

# 끝나면 watchdog_reset 으로 내보낸다(이 기기는 hard_reset 으로는 내려받기 모드에 남는 일이 있었다).
(cd "$BUILD" && python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
    --before no_reset --after watchdog_reset write_flash "@flash_args")

if PORT=$(python tools/vibectl.py wait app 20); then
  # 올라간 것이 그 판인지 기기에게 묻는다(vibe:status 의 dev=).
  SAID=$(python tools/vibectl.py cmd status 2>/dev/null || true)
  case "$SAID" in
    *"dev=$DEV "*) echo "올림 완료($KIND, 기기가 dev=$DEV 라고 답함). 기기 포트: $PORT"
                   python tools/vibectl.py time >/dev/null 2>&1 || true ;;  # 기기의 시계를 맞춰 둔다(D62)
    *"dev="*)      echo "올렸지만 기기가 다른 판이라고 답한다: $SAID"; exit 1 ;;
    *)             echo "올렸지만 어느 판인지 기기의 답을 받지 못했다(tools/vibectl.py cmd status 로 확인). 기기 포트: $PORT"; exit 1 ;;
  esac
else
  echo "올렸지만 기기가 USB 에 다시 나타나지 않았다(지금 상태: $(python tools/vibectl.py state))."
  exit 1
fi
