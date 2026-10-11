#!/usr/bin/env python3
"""desc_dump 가 찍은 USB 설명자를 읽어 형식을 검사한다. 기기가 필요 없다.

    desc_dump | check_desc.py

검사하는 것: 전체 길이, 인터페이스 번호와 개수, 기능 묶음(IAD), 끝점 번호·방향·종류·크기,
ESP32-S3 의 제약(컴퓨터로 보내는 끝점은 1~4번 넷까지), 마이크 형식(한 채널·16비트·비동기),
소리 제어 묶음의 길이, 키보드 보고 설명자의 길이, 문자열.
설명자 말고 하나 더: 컴퓨터가 마이크 음량을 "전체"와 "채널 1"에 정했을 때 소리에 실제로 곱하는 값(D46).
"""
import sys

passed = failed = 0


def check(cond, text):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print("  실패:", text)


lines = [ln.split() for ln in sys.stdin.read().splitlines() if ln.strip()]
raw = {ln[0]: bytes(int(x, 16) for x in ln[1:]) for ln in lines if ln[0] in ("device", "config", "hidreport")}
strings = {int(ln[1]): " ".join(ln[2:]) for ln in lines if ln[0] == "string"}
nums = {ln[0]: int(ln[1]) for ln in lines if ln[0] in ("ep_audio_size", "audio_fifo", "sample_rate")}
volumes = {ln[1]: tuple(int(x) for x in ln[2:]) for ln in lines if ln[0] == "volume"}

# ---- 장치 설명자 ----
dev = raw["device"]
check(len(dev) == 18 and dev[1] == 1, "장치 설명자는 18바이트")
check(dev[4:7] == bytes([0xEF, 0x02, 0x01]), "여러 기능이 한 몸인 장치(EF/02/01)여야 맥이 기능 묶음(IAD)을 읽는다")
check(dev[7] == 64, "0번 끝점 크기 64")
vid, pid = dev[8] | dev[9] << 8, dev[10] | dev[11] << 8
check((vid, pid) == (0x303A, 0x4015), f"식별값 303A:4015 (지금 {vid:04X}:{pid:04X})")
check(dev[14] and dev[15] and dev[16], "제조사·제품·일련번호 문자열 번호가 있다")
check(dev[17] == 1, "구성은 하나")

# ---- 구성 설명자를 조각으로 나눈다 ----
cfg = raw["config"]
total = cfg[2] | cfg[3] << 8
check(cfg[0] == 9 and cfg[1] == 2, "구성 설명자 머리")
parts, i = [], 0
while i < len(cfg):
    n = cfg[i]
    if n < 2 or i + n > len(cfg):
        check(False, f"{i}번째에서 조각 길이가 이상하다({n})")
        break
    parts.append(cfg[i:i + n])
    i += n
check(i == total == len(cfg), f"조각 길이의 합({i})이 적힌 전체 길이({total})와 같다")
check(cfg[7] & 0x80, "전원 속성의 맨 윗비트는 1 이어야 한다")
check(cfg[8] * 2 <= 500, "요청 전류 500mA 이하")

ifaces = [p for p in parts if p[1] == 4]
eps = [p for p in parts if p[1] == 5]
iads = [p for p in parts if p[1] == 0x0B]
numbers = sorted({p[2] for p in ifaces})
check(numbers == list(range(cfg[4])), f"인터페이스 번호가 0부터 빠짐없이 {cfg[4]}개 (지금 {numbers})")
check(cfg[4] == 5, "인터페이스 5개(소리 제어, 소리 흐름, 키보드, 로그 2)")

# ---- 기능 묶음(IAD): 소리 2개, 로그 2개 ----
check(len(iads) == 2, f"기능 묶음 표시가 둘(소리, 로그) — 지금 {len(iads)}")
if len(iads) == 2:
    check(iads[0][2] == 0 and iads[0][3] == 2 and iads[0][4] == 1, "소리 묶음: 0번부터 2개, 종류 Audio")
    check(iads[1][2] == 3 and iads[1][3] == 2 and iads[1][4] == 2, "로그 묶음: 3번부터 2개, 종류 CDC")

# ---- 인터페이스의 종류 ----
by_num = {}
for p in ifaces:
    by_num.setdefault(p[2], []).append(p)


def first(n):
    """n 번 인터페이스의 첫 설명자(없으면 빈 바이트 — 아래 비교가 실패로 떨어진다)"""
    return by_num.get(n, [bytes(9)])[0]


check(first(0)[5:7] == bytes([1, 1]), "0번 = 소리 제어")
stream = by_num.get(1, [])
check(bool(stream) and all(p[5:7] == bytes([1, 2]) for p in stream), "1번 = 소리 흐름")
alts = sorted(p[3] for p in stream)
check(alts == [0, 1], f"소리 흐름은 닫힘(0)·열림(1) 두 가지 (지금 {alts})")
check([p[4] for p in sorted(stream, key=lambda p: p[3])] == [0, 1], "닫힘에는 끝점 0개, 열림에는 1개")
check(first(2)[5:8] == bytes([3, 1, 1]), "2번 = 키보드(부트 규약)")
check(first(3)[5] == 2 and first(4)[5] == 0x0A, "3·4번 = 로그 포트(CDC 제어, 데이터)")

# ---- 끝점 ----
addr = [p[2] for p in eps]
check(len(addr) == len(set(addr)), f"끝점 주소가 겹치지 않는다 {[hex(a) for a in addr]}")
ins = sorted(a & 0x7F for a in addr if a & 0x80)
check(ins == [1, 2, 3, 4], f"컴퓨터로 보내는 끝점은 1~4번 넷(ESP32-S3 의 한도) — 지금 {ins}")
outs = [a for a in addr if not a & 0x80]
check(len(outs) == 1 and 1 <= outs[0] <= 6, "받는 끝점은 하나(로그 포트)")
ep = {p[2]: p for p in eps}
audio_ep = ep.get(0x81)
check(audio_ep is not None, "소리 끝점 0x81")
if audio_ep:
    size = audio_ep[4] | audio_ep[5] << 8
    check(audio_ep[3] & 0x03 == 1, "소리 끝점은 등시(isochronous)")
    check(audio_ep[3] & 0x0C == 0x04, "소리 끝점은 비동기(컴퓨터가 기기의 속도를 따른다)")
    check(audio_ep[6] == 1, "1ms 마다")
    per_ms = nums["sample_rate"] // 1000 * 2
    check(size == per_ms + 2 == nums["ep_audio_size"], f"묶음 크기 = (1ms 표본 + 1개) x 2바이트 = {per_ms + 2} (지금 {size})")
    check(nums["audio_fifo"] >= 4 * size, "보관함은 묶음의 4배 이상(흐름 조절의 조건)")
hid_ep = ep.get(0x82)
check(hid_ep is not None and hid_ep[3] & 3 == 3 and (hid_ep[4] | hid_ep[5] << 8) >= 8, "키보드 끝점 0x82: 인터럽트, 8바이트 이상")
check(0x83 in ep and ep[0x83][3] & 3 == 3, "로그 알림 끝점 0x83: 인터럽트")
check(0x84 in ep and ep[0x84][3] & 3 == 2 and 0x04 in ep and ep[0x04][3] & 3 == 2, "로그 데이터 끝점 0x84/0x04: 벌크")

# ---- 소리 제어 묶음 ----
cs = [p for p in parts if p[1] == 0x24]  # 종류별 인터페이스 설명자
ac_header = next((p for p in cs if p[2] == 1 and len(p) == 9), None)
check(ac_header is not None and ac_header[3:5] == bytes([0x00, 0x02]), "소리 규격 2.0")
if ac_header:
    want = ac_header[6] | ac_header[7] << 8
    # 머리 다음부터 소리 흐름 인터페이스가 나오기 전까지의 조각 길이 합(머리 자신 포함)
    start = parts.index(ac_header)
    got = 0
    for p in parts[start:]:
        if p[1] == 4:
            break
        got += len(p)
    check(want == got, f"소리 제어 묶음의 길이({want})가 실제({got})와 같다")
units = {p[3]: p for p in cs if p[2] in (2, 3, 6, 0x0A) and len(p) > 4}
check(set(units) == {1, 2, 3, 4}, f"부품 번호 1(마이크)·2(음량)·3(USB 로 나감)·4(시계) — 지금 {sorted(units)}")
fmt = next((p for p in cs if p[2] == 2 and len(p) == 6), None)
check(fmt is not None and fmt[4] == 2 and fmt[5] == 16, "형식: 표본 2바이트, 16비트")
as_general = next((p for p in cs if p[2] == 1 and len(p) == 16), None)
check(as_general is not None and as_general[10] == 1, "한 채널")

# ---- 키보드 ----
hid_desc = next((p for p in parts if p[1] == 0x21), None)
check(hid_desc is not None, "HID 설명자가 있다")
if hid_desc:
    rlen = hid_desc[7] | hid_desc[8] << 8
    rep = raw["hidreport"][:rlen]
    check(rep[:4] == bytes([0x05, 0x01, 0x09, 0x06]), "보고 설명자가 '키보드'로 시작한다")
    check(rep[-1] == 0xC0, f"보고 설명자가 적힌 길이({rlen})에서 끝난다")
    check(0x85 not in rep[4:8], "보고 번호를 쓰지 않는다(8바이트 표준 보고)")

# ---- 문자열 ----
check(strings.get(0) == "lang=0409", "언어 표")
check(strings.get(dev[15]) == "vibe-stick", f"제품 이름 (지금 {strings.get(dev[15])!r})")
check(strings.get(dev[16]) == "A1B2C3D4E5F6", "일련번호가 넣어 준 값 그대로")
ac_str = first(0)[8]
check(strings.get(ac_str) == "vibe-stick mic", f"마이크 이름 (지금 {strings.get(ac_str)!r})")
check(all(len(s) <= 32 for s in strings.values()), "문자열은 32자 이하")

# ---- 입력 음량 (D46): 소리에 곱하는 값 = 전체 + 채널 1, 범위(-24 ~ +24dB)를 넘으면 끝값 ----
# desc_dump 가 컴퓨터의 요청(SET·GET)을 흉내 내어 찍은 줄:
#   (요청을 받아들였는가, 소리에 곱하는 값, 전체, 채널 1, 컴퓨터가 되물어 받은 전체, 되물어 받은 채널 1) — 데시벨
VOLUME_CASES = [
    ("initial",      (1, 0, 0, 0, 0, 0),          "컴퓨터가 정하지 않았으면 0dB(그대로)"),
    ("channel_only", (1, -10, 0, -10, 0, -10),    "채널 1 만 -10 → -10"),
    ("master_only",  (1, -6, -6, 0, -6, 0),       "전체만 -6 → -6"),
    ("both",         (1, -16, -6, -10, -6, -10),  "전체 -6 + 채널 -10 → -16"),
    ("review",       (1, -24, 0, -24, 0, -24),    "전체 0 을 쓴 뒤 채널 -24 → -24(되물어 받는 값과 실제가 같다)"),
    ("sum_below",    (1, -24, -20, -20, -20, -20), "합 -40 은 아래 끝값 -24 로"),
    ("sum_above",    (1, 24, 20, 20, 20, 20),     "합 +40 은 위 끝값 +24 로"),
    ("cancel",       (1, 0, 24, -24, 24, -24),    "전체 +24 + 채널 -24 → 0"),
    ("set_below",    (1, -24, -24, 0, -24, 0),    "범위 밖(-100)을 쓰면 끝값 -24 로 적힌다"),
    ("bad_channel",  (0, -24, -24, 0, -24, 0),    "없는 채널(2)은 거절하고 값은 그대로"),
    ("reset",        (1, 0, 0, 0, 0, 0),          "둘 다 0 으로 되돌리면 0"),
]
for name, want, text in VOLUME_CASES:
    got = volumes.get(name)
    check(got == want, f"음량 — {text} (지금 {got})")

print(f"USB 설명자 검사: 통과 {passed}, 실패 {failed}")
sys.exit(1 if failed else 0)
