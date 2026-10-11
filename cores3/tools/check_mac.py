#!/usr/bin/env python3
"""맥에서 기기를 확인한다(설계서 3장의 C1~C4). 사람 손 없이 돈다.

    check_mac.py list        C1  맥이 기기를 USB 장치·마이크·키보드로 잡았는가
    check_mac.py ramp        C2  (개발판) 기기가 1씩 올라가는 숫자를 보내게 하고, 맥이 받은 숫자가
                                 하나도 빠지거나 겹치지 않았는지 센다 — USB 로 가는 길의 검증
    check_mac.py mic         C2  기기 마이크로 녹음해 살아 있는지 보고(맥북 마이크와 같은 소리를 들었는가),
                                 맥이 음소거가 아니면 한국어 문장을 읽어 주고 받아쓰기까지 해 본다
                             C4  녹음을 시작하고 끝낼 때 기기가 "마이크 열림/닫힘"을 아는가
    check_mac.py keys        C3  (개발판) 시험용 창을 띄우고 일곱 동작을 시켜, 맥이 받은 키를 대조한다
    check_mac.py all         위 넷을 차례로

mic 은 맥이 음소거가 아닐 때만 스피커로 한국어 문장을 한 번 읽는다(음소거를 풀지 않는다).
keys 는 3초쯤 작은 창을 맨 앞에 띄운다. 그동안 맥을 만지면 시험이 멈춘다(키가 다른 곳에 들어가지 않게).

필요한 것: ffmpeg, (받아쓰기) whisper-cli 와 ~/Models/whisper/ggml-*.bin, (keys) swift.
"""
import contextlib
import difflib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proc  # noqa: E402
import vibectl  # noqa: E402

MIC_NAME = os.environ.get("VIBE_MIC_NAME", "vibe-stick mic")  # 도구 자체를 시험할 때 다른 마이크로 바꿔 본다
SENTENCE = "지금부터 마이크 시험을 시작합니다. 오늘은 날씨가 참 좋습니다."
VOICE = "Yuna"

results = []


def report(code, ok, text):
    results.append((code, ok))
    print(f"[{'통과' if ok else '실패'}] {code} {text}", flush=True)


def run(cmd, timeout=60):
    # 부른 프로그램이 멈춰도(시간 초과) 예외로 죽지 않고 "실패한 결과"를 받는다 — 그 항목만 실패로 적고
    # 나머지 확인과 끝의 집계를 마친다(proc.py, backup.py 의 esptool 과 같은 방식).
    return proc.run_text(cmd, timeout=timeout)


# ---------- C1 ----------

def check_list():
    usb = run(["system_profiler", "SPUSBHostDataType"]).stdout
    report("C1-usb", "vibe-stick" in usb, "USB 장치 목록에 vibe-stick")
    try:
        audio = json.loads(run(["system_profiler", "SPAudioDataType", "-json"]).stdout or "{}")
    except ValueError:
        audio = {}  # 시간 초과로 끊긴 출력 등: 목록을 받지 못한 것으로 본다(아래에서 "없음"으로 적힌다)
    found = None
    for group in audio.get("SPAudioDataType", []):
        for item in group.get("_items", []):
            if item.get("_name") == MIC_NAME:
                found = item
    detail = ""
    if found:
        detail = (f" (입력 채널 {found.get('coreaudio_device_input', '?')}, "
                  f"{found.get('coreaudio_device_srate', '?')}Hz, "
                  f"기본 입력: {found.get('coreaudio_default_audio_input_device', '아니오')})")
    report("C1-mic", found is not None, f"소리 입력 목록에 '{MIC_NAME}'{detail}")
    hid = run(["hidutil", "list"]).stdout
    kb = [ln for ln in hid.splitlines() if "0x303a" in ln.lower() and "0x4015" in ln.lower()]
    report("C1-key", len(kb) > 0, f"HID 목록에 이 기기({len(kb)}줄)")
    return found is not None


# ---------- C2, C4 ----------

def avf_index(name):
    out = run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""]).stderr
    audio = out.split("AVFoundation audio devices:")[-1]
    for line in audio.splitlines():
        m = re.search(r"\[(\d+)\] (.+)$", line)
        if m and m.group(2).strip() == name:
            return m.group(1)
    return None


def rms(samples):
    return (sum(s * s for s in samples) / max(1, len(samples))) ** 0.5


def whisper_model():
    for size in ("small", "medium", "large-v3", "base"):
        p = os.path.expanduser(f"~/Models/whisper/ggml-{size}.bin")
        if os.path.exists(p):
            return p
    return None


def norm(s):
    return re.sub(r"[^0-9A-Za-z가-힣]", "", s)


def record(idx, seconds, wav):
    """avfoundation 의 idx 번 입력을 seconds 초 녹음하는 프로세스를 띄운다(16kHz 한 채널)."""
    return subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation",
                             "-i", f":{idx}", "-t", str(seconds), "-ac", "1", "-ar", "16000", wav])


def read_wav(path):
    with wave.open(path) as w:
        n = w.getnframes()
        return struct.unpack(f"<{n}h", w.readframes(n)), w.getframerate()


def envelope(data, rate, frame_ms=50):
    """frame_ms 마다의 소리 크기(RMS)"""
    step = rate * frame_ms // 1000
    return [rms(data[i:i + step]) for i in range(0, len(data) - step, step)]


def best_correlation(a, b, max_lag=20):
    """두 크기 흐름이 가장 잘 겹치는 어긋남에서의 상관계수(-1~1)"""
    def corr(x, y):
        n = min(len(x), len(y))
        if n < 20:
            return 0.0
        x, y = x[:n], y[:n]
        mx, my = sum(x) / n, sum(y) / n
        sx = sum((v - mx) ** 2 for v in x) ** 0.5
        sy = sum((v - my) ** 2 for v in y) ** 0.5
        if sx == 0 or sy == 0:
            return 0.0
        return sum((x[i] - mx) * (y[i] - my) for i in range(n)) / (sx * sy)
    return max(max(corr(a[k:], b), corr(a, b[k:])) for k in range(max_lag))


class DeviceLog:
    """시험하는 동안 기기 로그를 모은다(마이크 열림·닫힘, 명령의 답)."""

    def __init__(self):
        self.items = []
        self._stop = threading.Event()
        self._ser = None
        self._t = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        try:
            with vibectl.open_app() as ser:
                self._ser = ser
                while not self._stop.is_set():
                    chunk = ser.read(256)
                    if chunk:
                        # 받는 자리에서 제어 문자를 걸러 둔다: 이 글은 뒤에서 그대로 찍힌다(기기의 글은 믿을 수 없는 입력이다).
                        self.items.append((time.time(), vibectl.printable(chunk.decode("utf-8", "replace"))))
        except BaseException as e:  # noqa: BLE001
            self.items.append((time.time(), f"<로그 포트 오류: {e}>"))

    def __enter__(self):
        self._t.start()
        time.sleep(1.0)
        return self

    def __exit__(self, *a):
        self._stop.set()
        self._t.join(timeout=2)

    def send(self, cmd):
        if self._ser:
            self._ser.write(f"vibe:{cmd}\n".encode())

    def times(self, needle):
        return [ts for ts, c in self.items if needle in c]

    def text(self):
        return "".join(c for _, c in self.items)


def output_muted():
    out = run(["osascript", "-e", "output muted of (get volume settings)"]).stdout.strip()
    return out != "false"


@contextlib.contextmanager
def scratch(prefix):
    """녹음·임시 파일을 둘 폴더(본인만 볼 수 있다). 끝나면 폴더째 지운다 — 도중에 멈춰도.

    녹음에는 방의 소리(대화)가 그대로 담긴다. 예전에는 지우지 않아 이틀 전의 녹음이 임시 폴더에 남아 있었다
    (보안 점검). 들어 봐야 할 때는 VIBE_KEEP=1 을 붙여 돌린다(남기고 자리를 알린다 — 본 뒤 직접 지울 것).
    """
    d = tempfile.mkdtemp(prefix=prefix)
    try:
        yield d
    finally:
        if os.environ.get("VIBE_KEEP") == "1":
            print(f"      (VIBE_KEEP=1 — 남김: {d})")
        else:
            shutil.rmtree(d, ignore_errors=True)


def check_ramp():
    with scratch("vibe-ramp-") as tmp:
        _check_ramp(tmp)


def _check_ramp(tmp):
    idx = avf_index(MIC_NAME)
    if idx is None:
        report("C2-ramp", False, f"녹음 도구가 '{MIC_NAME}' 를 찾지 못했다")
        return
    wav = os.path.join(tmp, "ramp.wav")
    with DeviceLog() as log:
        log.send("ramp 1")
        time.sleep(0.5)
        if "vibe:ramp 1" not in log.text():
            report("C2-ramp", False, "기기가 시험 신호 명령을 받지 않았다(개발판이 아니다?)")
            return
        seconds = 12
        rec = record(idx, seconds, wav)
        try:
            rec.wait(timeout=seconds + 15)
        except subprocess.TimeoutExpired:
            rec.kill()  # 녹음 도구가 멈췄다: 끝내 둔다(쓰다 만 파일은 아래에서 "없다/짧다"로 드러난다)
            rec.wait()
        finally:
            # 무슨 일이 있어도 시험 신호는 끈다. 켜진 채 남으면 다음 마이크 시험이 그 신호를 "소리"로 알고 통과한다.
            log.send("ramp 0")
            time.sleep(0.5)
    if not os.path.exists(wav):
        report("C2-ramp", False, "녹음 파일이 없다")
        return
    data, rate = read_wav(wav)
    # 처음 0.5초는 뺀다: 맥이 마이크를 여는 순간에 채워 넣는 조용한 소리(0)와 시작 구간.
    body = data[rate // 2:]
    breaks, good = 0, 0
    first_bad = None
    for i in range(1, len(body)):
        d = body[i] - body[i - 1]
        if d == 1 or d == -15999:
            good += 1
        else:
            breaks += 1
            if first_bad is None:
                first_bad = (i, body[i - 1], body[i])
    print(f"      받은 표본 {len(body)}개({len(body) / rate:.1f}초, {rate}Hz), 이어진 곳 {good}, 어긋난 곳 {breaks}"
          + (f", 첫 어긋남: {first_bad[0]}번째 {first_bad[1]} → {first_bad[2]}" if first_bad else ""))
    report("C2-ramp-data", len(body) > 8 * rate, f"시험 신호를 {len(body) / rate:.1f}초 받았다")
    report("C2-ramp-exact", breaks == 0 and good > 0,
           "받은 숫자가 하나도 빠지거나 겹치지 않았다" if breaks == 0 else f"어긋난 곳이 {breaks}군데 있다")


def check_mic():
    with scratch("vibe-mic-") as tmp:
        _check_mic(tmp)


def _check_mic(tmp):
    idx = avf_index(MIC_NAME)
    if idx is None:
        report("C2-open", False, f"녹음 도구가 '{MIC_NAME}' 를 찾지 못했다")
        return
    builtin = None
    for name in ("MacBook Pro 마이크", "MacBook Pro Microphone", "MacBook Air 마이크"):
        builtin = builtin or avf_index(name)
    wav, wav2 = os.path.join(tmp, "device.wav"), os.path.join(tmp, "macbook.wav")
    seconds = 11
    muted = output_muted()
    with DeviceLog() as log:
        t_start = time.time()
        rec = record(idx, seconds, wav)
        rec2 = record(builtin, seconds, wav2) if builtin and builtin != idx else None
        spoke = False
        if not muted:
            time.sleep(4.5)  # 녹음이 실제로 시작되기까지 2초쯤 걸린다. 그 뒤 조용한 구간을 둔다
            spoke = run(["say", "-v", VOICE, SENTENCE]).returncode == 0
        rec.wait(timeout=seconds + 15)
        t_end = time.time()
        if rec2:
            rec2.wait(timeout=10)
        time.sleep(2.0)  # 닫힘 로그가 올 시간
        opened, closed = log.times("[mic] open"), log.times("[mic] closed")
        errors = [c for _, c in log.items if "오류" in c]

    ok = os.path.exists(wav) and os.path.getsize(wav) > 16000
    report("C2-open", ok, "기기 마이크로 녹음" + ("" if ok else " — 파일이 없다"))
    if not ok:
        return
    data, rate = read_wav(wav)
    env = sorted(envelope(data, rate))
    quiet, loud = env[len(env) // 10], env[len(env) * 9 // 10]
    peak = max(abs(v) for v in data)
    clipped = sum(1 for v in data if abs(v) >= 32700) / max(1, len(data))
    print(f"      길이 {len(data) / rate:.1f}초, 조용한 때 RMS {quiet:.0f}, 큰 때 RMS {loud:.0f}, "
          f"최댓값 {peak}, 찢어진 비율 {clipped * 100:.2f}%")
    # 마이크가 죽어 있으면(0 만 오면) 조용한 때의 크기도 0 이다.
    report("C2-live", quiet >= 2,
           f"마이크가 살아 있다(조용한 때에도 RMS {quiet:.0f} — 0 이면 죽은 것)")
    report("C2-clip", clipped < 0.001, "소리가 찢어지지 않는다")
    if rec2 and os.path.exists(wav2):
        data2, rate2 = read_wav(wav2)
        c = best_correlation(envelope(data, rate), envelope(data2, rate2))
        verdict = "같은 소리를 들었다" if c >= 0.5 else "방이 조용해 판정하기 어렵다(실패는 아니다)"
        print(f"      맥북 마이크와의 겹침 {c:.2f} — {verdict}")
        if c >= 0.5:
            report("C2-same", True, "기기 마이크와 맥북 마이크가 같은 소리를 들었다")

    if spoke:
        report("C2-level", loud > 3 * max(quiet, 1), f"말소리가 조용한 때의 {loud / max(quiet, 1):.1f}배(3배 이상)")
        model = whisper_model()
        if model and run(["which", "whisper-cli"]).returncode == 0:
            out = run(["whisper-cli", "-m", model, "-l", "ko", "-nt", "-np", "-f", wav], timeout=180).stdout
            heard = " ".join(out.split())
            sim = difflib.SequenceMatcher(None, norm(SENTENCE), norm(heard)).ratio()
            print(f"      읽어 준 문장: {SENTENCE}\n      받아쓴 문장: {heard}")
            report("C2-stt", sim >= 0.8, f"받아쓰기가 읽어 준 문장과 {sim * 100:.0f}% 같다(80% 이상)")
        else:
            print("      (whisper-cli 나 모델이 없어 받아쓰기는 건너뜀)")
    else:
        print("      (맥이 음소거라 문장을 읽어 주는 시험은 건너뜀 — 음소거를 풀고 다시 돌리면 한다)")

    report("C4-open", bool(opened), "녹음을 시작하자 기기가 '마이크 열림'을 알았다"
           + (f"(녹음 명령 뒤 {opened[0] - t_start:.2f}초)" if opened else ""))
    report("C4-close", bool(closed) and closed[-1] > t_end - 1.5,
           "녹음이 끝나자 기기가 '마이크 닫힘'을 알았다"
           + (f"(녹음 끝 {closed[-1] - t_end:+.2f}초)" if closed else ""))
    for e in errors:
        print("      " + e.strip())


# ---------- C3 ----------

SWIFT = r'''
import Cocoa
setvbuf(stdout, nil, _IONBF, 0)
class KeyView: NSView {
  override var acceptsFirstResponder: Bool { true }
  func show(_ tag: String, _ e: NSEvent) {
    let f = e.modifierFlags.intersection(.deviceIndependentFlagsMask)
    var m: [String] = []
    if f.contains(.command) { m.append("cmd") }
    if f.contains(.shift) { m.append("shift") }
    if f.contains(.control) { m.append("ctrl") }
    if f.contains(.option) { m.append("opt") }
    print("\(tag) \(e.keyCode) \(m.joined(separator: "+"))")
  }
  override func keyDown(with e: NSEvent) { show("key", e) }
  override func performKeyEquivalent(with e: NSEvent) -> Bool { show("key", e); return true }
}
class Delegate: NSObject, NSApplicationDelegate {
  func applicationDidBecomeActive(_ n: Notification) { print("active") }
  func applicationDidResignActive(_ n: Notification) { print("lost") }
}
let app = NSApplication.shared
let d = Delegate()
app.delegate = d
app.setActivationPolicy(.regular)
let w = NSWindow(contentRect: NSRect(x: 200, y: 400, width: 360, height: 120),
                 styleMask: [.titled], backing: .buffered, defer: false)
w.title = "vibe-stick 키 시험 (잠시만 그대로 두세요)"
let v = KeyView(frame: w.contentView!.bounds)
w.contentView = v
w.makeKeyAndOrderFront(nil)
w.makeFirstResponder(v)
app.activate(ignoringOtherApps: true)
DispatchQueue.main.asyncAfter(deadline: .now() + 25) { exit(0) }
app.run()
'''

# 사건 이름 → 맥이 받아야 할 (키 코드, 수정키). 키 코드는 맥의 가상 키 번호다.
# 다른 곳에 새더라도 해가 적은 키부터 보낸다(화살표 → Esc → Enter → ⌘ 조합).
EXPECT = [
    ("up", "126", ""),
    ("down", "125", ""),
    ("nope", "53", ""),          # Escape
    ("ok", "36", ""),            # Return
    ("talk", "2", "cmd"),        # ⌘D
    # Back·Next 는 짧게 = 같은 창 안의 옆 패널(F17·F18), 길게 = 다른 창(⌘`) 이다(D58). 로그 포트로 넣는 사건은
    # 누르고 있는 손가락이 없어 늘 "짧게"가 된다 → 여기서 볼 수 있는 것은 F18·F17 이다(길게는 손으로 본다).
    # (독립 검토: 예전 기대값 ⌘` 은 D58 뒤로 늘 실패하게 돼 있었다. 고친 뒤 기기에서 돌려 보지는 못했다.)
    ("next", "79", ""),          # F18
    ("back", "64", ""),          # F17
]


def check_keys():
    with vibectl.open_app() as ser:
        time.sleep(0.3)
        ser.write(b"vibe:status\n")
        if "dev=1" not in vibectl.pump(ser, 1.0, out=open(os.devnull, "w")):
            report("C3", False, "개발판이 아니다(시험용 사건을 받지 않는다) — tools/flash.sh --dev 로 올린 뒤 다시")
            return
    with scratch("vibe-keys-") as tmp:
        _check_keys(tmp)


def _check_keys(tmp):
    src = os.path.join(tmp, "keywin.swift")
    with open(src, "w") as f:
        f.write(SWIFT)
    app = subprocess.Popen(["swift", src], stdout=subprocess.PIPE, text=True)
    lines = []
    threading.Thread(target=lambda: [lines.append(ln.strip()) for ln in app.stdout], daemon=True).start()
    try:
        end = time.time() + 40  # swift 가 스크립트를 번역하는 시간
        while "active" not in lines and time.time() < end:
            time.sleep(0.1)
        if "active" not in lines:
            report("C3", False, "시험용 창이 맨 앞으로 오지 못했다")
            return
        time.sleep(0.5)
        with vibectl.open_app() as ser:
            time.sleep(0.3)
            for name, code, mods in EXPECT:
                if "lost" in lines:
                    report("C3", False, "시험 도중 다른 창이 앞으로 왔다 — 키가 새지 않게 멈춤")
                    return
                if app.poll() is not None:  # 시험용 창이 닫혔거나 죽었다: 이제 키는 엉뚱한 프로그램으로 간다
                    report("C3", False, "시험용 창이 사라졌다 — 키가 새지 않게 멈춤")
                    return
                before = len(lines)
                ser.write(f"vibe:ev {name}\n".encode())
                vibectl.pump(ser, 0.7, out=open(os.devnull, "w"))
                if "lost" in lines:  # 키를 보내는 사이에 초점을 잃었다: 더 보내지 않는다
                    report("C3", False, f"'{name}' 을 보내는 사이에 다른 창이 앞으로 왔다 — 멈춤(이 키는 다른 곳에 들어갔을 수 있다)")
                    return
                got = [ln for ln in lines[before:] if ln.startswith("key ")]
                want = f"key {code} {mods}".strip()
                report(f"C3-{name}", want in [g.strip() for g in got],
                       f"'{name}' → 맥이 받은 키: {got if got else '없음'} (기대: {want})")
    finally:
        app.kill()


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    if what not in ("list", "ramp", "mic", "keys", "all"):
        sys.exit(__doc__)
    if what in ("list", "all"):
        check_list()
    if what in ("ramp", "all"):
        check_ramp()
    if what in ("mic", "all"):
        check_mic()
    if what in ("keys", "all"):
        check_keys()
    bad = [c for c, ok in results if not ok]
    print(f"\n통과 {len(results) - len(bad)}, 실패 {len(bad)}" + (f" — {', '.join(bad)}" if bad else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
