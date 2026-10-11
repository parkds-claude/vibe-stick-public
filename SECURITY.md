# Security

vibe-stick turns a small M5Stack device into a **keyboard** for the computer it is plugged into or paired with. A keyboard can approve prompts and run commands, so please read this before you build or use it. *(한국어 요약은 아래에 있습니다.)*

**The only official source is https://github.com/parkds-claude/vibe-stick-public.** There are no releases, no prebuilt firmware and no signed tags. Treat any other repository, archive or `.bin` carrying this name as untrusted.

## What the firmware is designed to guarantee

This section is about the **CoreS3 build (`cores3/`), normal build** (`flash.sh` without `--dev`; `vibe:status` reports `dev=0`; no magenta/black band at the top of the screen — that band is a reminder for a development build you forgot about, not proof: hostile firmware simply would not draw it). The StickC build is covered separately below.

- **Keys are sent only when a person presses the screen or shakes the device** (a hard shake sends Esc, and only while the screen is lit). Nothing the computer sends (log-port commands, USB control requests, HID output reports, a Bluetooth peer) makes the device type.
- **Firmware is replaced only after a deliberate gesture on the device** (CoreS3: hold the OK tile for about two seconds while `Update firmware? Hold OK` is shown; a tap is ignored). The prompt disappears when the USB link is lost. The reset button (hold RST) always works for someone holding the device.
- **Presses that the user could not have meant are dropped**: right after power-on, right after the link changes, the touch that wakes a dark screen, and presses spanning the firmware prompt.
- **Microphone samples (audio) leave the device only over USB audio and only while the computer has opened the microphone.** The screen is red with a filled dot while that is the case.
- **Sound-level statistics are a separate matter, and a weaker promise.** Whenever the screen shows *Listening* — including the "guess" state (hollow ring) where you pressed Talk but the computer did not open this microphone — the device writes loudness statistics (peaks and counts, never audio) into its event history: every 5 seconds during the first minute of a listening period, then once a minute, plus one reading when listening starts and stops. Any program on the computer can read that history over the log port. `vibe:status` itself reports a level only while the microphone is open.
- The development-only commands (`vibe:ev` key injection, raw image upload, test signals) are compiled out of the normal build.

## Known limitations — please do not rely on more than this

- **Bluetooth pairing is "Just Works"** (no passkey or numeric comparison), and legacy pairing is accepted. While the device has no bond (first use, or after you clear it), the first computer to connect becomes its peer, and pairing can in theory be intercepted. Pair in a place you trust. Once bonded, a second peer is not kept: it can complete pairing, but the new bond is deleted and the link dropped immediately, and no keys are sent to it.
- **No Secure Boot, no flash or NVS encryption.** Anyone holding the device can reflash it or read the Bluetooth bond key from flash. If you lose the device, remove it from your computer's Bluetooth device list.
- **The USB log port is unauthenticated.** Any program running as you on the same computer can show a notification, raise the firmware prompt (it still needs the hold gesture), set the clock, or read the device's event history (button events, link changes, and sound levels recorded while it was listening). A machine that already runs hostile code is outside what this device can defend.
- **The development build is unsafe by design**: it lets the computer inject key events and enters download mode without asking. Never leave it on a device you use. A development build draws a **magenta/black band across the top of the screen** whenever its normal screen is shown (not while a test image sent by the computer covers it); `vibe:status` reports `dev=1`.
- **Dependencies are pinned, not all of them by hash.** ESP-IDF components are locked by hash (`cores3/dependencies.lock`); CI pins its actions by commit and the ESP-IDF image by digest. PlatformIO packages (StickC build) and `pip` sub-dependencies are pinned by version only.
- **USB VID/PID are development placeholders** (Espressif's VID with a TinyUSB-style PID). Do not ship hardware with them.
- **The Claude Code hook** (`cores3/tools/claude_note.py`) runs a script from your clone every time Claude finishes a reply. Install it only from a copy you have read and trust, and pin that copy.
- The default mascot and (optional) third-party icons have their own terms — see `NOTICE`.

## The StickC build (`src/`) is different

The StickC build is a Bluetooth keyboard only: no USB command port (its serial console only prints), no microphone, no on-screen firmware prompt. So the points above about USB commands, the microphone and the hold-to-update gesture **do not apply to it**. Anyone who can plug it into a computer can reflash it with standard tools, without any confirmation on the device. Its Bluetooth pairing has the same "Just Works" limitation, and clearing the bond is a button hold with no PIN. See "Read first — risks" in the README.

## Using it safely

- **Build the firmware yourself from this source. Do not flash binaries from anyone else** — a modified build is a keyboard that can type anything. This project publishes no prebuilt firmware.
- Before the first flash, back up the device's original contents (`cores3/tools/backup.py`) and keep the file private; it may contain Wi-Fi credentials from the previous firmware.
- Do not plug the device into computers you do not own.
- If `Update firmware?` appears when you did not ask for it, press ✕. Do not hold OK.

## If you send a pull request

Changes to how keys are sent, to the flashing tools, to CI, or to pinned dependencies are read line by line. Pull requests that modify the generated art file (`cores3/logic/art_data.cpp`, ~2 MB of numbers) are not accepted — change the generator script instead. Workflows for outside contributors run only after approval.

## Reporting a vulnerability

Please use GitHub's **private vulnerability reporting**: the repository's *Security* tab → *Report a vulnerability*. Do not open a public issue for something exploitable. This is a personal project maintained on a best-effort basis; only the latest `main` is supported.

---

## 한국어 요약

- 이 기기는 **키보드**다. 평소 판(`dev=0`)에서는 사람이 화면을 누를 때(와 세게 흔들 때 — Esc)만 키가 나가고, 컴퓨터가 기기에게 키를 치게 할 수 없다. 펌웨어는 화면의 OK 를 2초쯤 **꾹** 눌러야 바뀐다(톡은 받지 않는다).
- 위의 약속은 **화면 판(`cores3/`)의 평소 판**에 대한 것이다. 조이스틱 판(`src/`)은 블루투스 키보드뿐이라 USB·마이크·펌웨어 확인에 대한 약속이 해당하지 않는다 — 컴퓨터에 꽂을 수 있는 사람은 확인 없이 다시 구울 수 있다.
- 한계: 블루투스 짝 맺기에 숫자 확인이 없다(짝이 없는 동안에는 먼저 붙는 컴퓨터가 짝이 된다 — 믿을 수 있는 곳에서 맺을 것). 기기 안의 것은 암호화돼 있지 않다(잃어버리면 컴퓨터의 블루투스 목록에서 지울 것). 로그 포트에는 인증이 없다(같은 컴퓨터의 프로그램이 알림·물음을 띄우고 기록을 읽을 수 있다). **화면이 "듣는 중"인 동안에는 — 컴퓨터가 마이크를 열지 않은 "어림"(빈 고리) 때에도 — 소리의 크기(소리 자체가 아니라 크기의 통계)가 기기 속 기록에 남고, 그 기록은 같은 컴퓨터의 프로그램이 읽을 수 있다.** 개발판은 일부러 안전하지 않다 — 올려 둔 채 쓰지 말 것(개발판은 평소 화면 맨 위에 자홍·검정 띠가 보인다 — 잊어버린 개발판을 알아채라는 표시이지, 띠가 없다고 안전하다는 증명은 아니다).
- **정식 저장소는 https://github.com/parkds-claude/vibe-stick-public 하나뿐이다.** 배포판(Release)·빌드된 펌웨어·서명된 꼬리표는 없다.
- **직접 빌드한 펌웨어만 올릴 것.** 남이 만든 `.bin` 을 받지 말 것. 이 프로젝트는 빌드된 펌웨어를 배포하지 않는다.
- 취약점은 공개 이슈가 아니라 GitHub 의 **비공개 취약점 신고**(Security 탭 → Report a vulnerability)로 알려 주세요.
