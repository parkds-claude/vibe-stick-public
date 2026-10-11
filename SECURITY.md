# Security

vibe-stick turns an M5StickC Plus with the Mini JoyC HAT into a **Bluetooth keyboard** for the computer it is paired with. A keyboard can approve prompts and run commands, so please read this before you build or use it. *(한국어 요약은 아래에 있습니다.)*

**The only official source is https://github.com/parkds-claude/vibe-stick-public.** There are no releases, no prebuilt firmware and no signed tags. Treat any other repository, archive or `.bin` carrying this name as untrusted.

The touch-screen build for the M5Stack CoreS3 is a separate project with its own security notes: https://github.com/parkds-claude/vibe-stick-cores3. Nothing on this page is a promise about it.

## What this device is

It is a Bluetooth keyboard only: no USB command port (its serial console only prints), no on-screen firmware prompt, and the firmware never turns on the microphone built into the M5StickC Plus. As built by default it sends Enter, Esc, ⌘D, ⌘\` / ⇧⌘\` (next / previous window) and the up / down arrow keys to the paired computer, and Enter approves Claude's "may I edit this file?" prompt. See "Read first — risks" in the README.

## Known limitations — please do not rely on more than this

- **Bluetooth pairing is "Just Works"** (no passkey or numeric comparison), and legacy pairing is accepted. While the screen shows PAIR ME, any nearby computer can try to pair, and the first one wins; pairing can in theory be intercepted. Pair in a place you trust. Afterwards the device keeps only that one computer: a second one can complete pairing, but its bond is deleted and the link dropped at once, and no keys are sent to it.
- **Clearing the pairing is a button hold with no PIN.** Anyone holding the device can do it.
- **Anyone who can plug it into a computer can reflash it** with standard tools, without any confirmation on the device.
- **No Secure Boot, no flash or NVS encryption.** Anyone holding the device can reflash it or read the Bluetooth bond key from flash. If you lose the device, remove `vibe-stick` from your computer's Bluetooth device list.
- **Dependencies are pinned by version at most, never by hash.** `platformio.ini` pins the PlatformIO platform and the two libraries it names to exact versions, and CI installs an exact PlatformIO release; what those pull in (M5GFX, the framework and tool packages, PlatformIO's own Python dependencies) is resolved within version ranges at build time. CI pins its actions by commit.
- The default mascot has its own terms — see `NOTICE`.

## Using it safely

- **Build the firmware yourself from this source. Do not flash binaries from anyone else** — a modified build is a keyboard that can type anything. This project publishes no prebuilt firmware.
- Look at the computer's screen before you press the front button.
- Do not pair it with, or plug it into, computers you do not own.

## If you send a pull request

Changes to how keys are sent, to the tools, to CI, or to pinned dependencies are read line by line. Workflows for outside contributors run only after approval.

## Reporting a vulnerability

Please use GitHub's **private vulnerability reporting**: the repository's *Security* tab → *Report a vulnerability*. Do not open a public issue for something exploitable. This is a personal project maintained on a best-effort basis; only the latest `main` is supported.

---

## 한국어 요약

- 이 기기는 **블루투스 키보드**다. 기본 설정으로 구우면 짝 맺은 컴퓨터에 Enter·Esc·⌘D·⌘\`/⇧⌘\`(다음·이전 창)·위/아래 화살표를 보내고, Enter 는 Claude 의 "이 파일 고쳐도 돼?" 창을 승인한다. USB 명령 포트와 펌웨어 확인 화면은 없고, 본체에 들어 있는 마이크는 펌웨어가 켜지 않는다.
- 한계: 블루투스 짝 맺기에 숫자 확인이 없고 옛 방식 짝 맺기도 받는다(PAIR ME 화면인 동안 먼저 맺는 컴퓨터가 임자다 — 믿을 수 있는 곳에서 맺을 것). 짝 초기화는 PIN 없이 버튼을 길게 누르는 것이라 기기를 든 누구나 할 수 있다. 컴퓨터에 꽂을 수 있는 사람은 기기의 확인 없이 다시 구울 수 있다. 기기 안의 것은 암호화돼 있지 않다(잃어버리면 컴퓨터의 블루투스 목록에서 `vibe-stick` 을 지울 것).
- **정식 저장소는 https://github.com/parkds-claude/vibe-stick-public 하나뿐이다.** 배포판(Release)·빌드된 펌웨어·서명된 꼬리표는 없다. 화면이 달린 CoreS3 판은 다른 저장소(<https://github.com/parkds-claude/vibe-stick-cores3>)에 있고 보안 안내도 거기에 따로 있다.
- **직접 빌드한 펌웨어만 올릴 것.** 남이 만든 `.bin` 을 받지 말 것. 이 프로젝트는 빌드된 펌웨어를 배포하지 않는다.
- 취약점은 공개 이슈가 아니라 GitHub 의 **비공개 취약점 신고**(Security 탭 → Report a vulnerability)로 알려 주세요.
