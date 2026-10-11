#!/usr/bin/env python3
"""CoreS3 화면의 조각 그림을 만든다(D56·D57).

    python3 cores3/tools/gen_art.py            cores3/logic/art_data.cpp 를 다시 만든다
    python3 cores3/tools/gen_art.py --check    지금 있는 art_data.cpp 가 이 스크립트의 결과와 같은지만 본다

부드러운 색 변화·반투명 타일·그림자는 기기에서 계산하지 않는다. 여기서 4배 크기로 그려 넓이 평균으로
줄이고 기기 화면의 색 깊이(RGB565)로 맞춘 조각을 펌웨어에 넣는다. 기기와 미리보기는 같은 조각을 쓴다.

필요한 것(저장소에 넣지 않는다 — cores3/art/README.md):
    cores3/art/fonts/Pretendard-SemiBold.otf   글꼴(SIL OFL 1.1)
    cores3/art/thiings/<이름>.png              (선택) 타일에 넣을 그림. 없으면 선 그림으로 대신한다
    cores3/art/logo/logo.svg                   (선택) 그림이 없을 때 말하기 타일에 쓸 자기 로고. 없으면 마이크 선 그림
    PIL(Pillow), rsvg-convert(로고가 SVG 일 때)
"""
import os
import random
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "..", "art")
OUT = os.path.join(HERE, "..", "logic", "art_data.cpp")
FONT = os.path.join(ART, "fonts", "Pretendard-SemiBold.otf")
FONT_LABEL = os.path.join(ART, "fonts", "Pretendard-Medium.otf")   # 타일의 이름(없으면 위 글꼴로)
LOGO = os.path.join(ART, "logo", "logo.svg")

SS = 4          # 몇 배로 크게 그렸다가 줄이는가
W, H = 240, 320

# 자리 표: 0 = 보는 곳, 1~7 = Zone 순서(Talk, Up, Down, Nope, Back, Next, Ok). touch_layout.h 와 같아야 한다
# (art_data.cpp 에 함께 적어 두고 시험이 견준다).
VIEW = (0, 0, 240, 88)
ZONES = {
    "talk": (0, 230, 118, 90), "up": (0, 92, 77, 40), "down": (81, 92, 78, 40), "nope": (163, 92, 77, 40),
    "back": (0, 136, 118, 90), "next": (122, 136, 118, 90), "ok": (122, 230, 118, 90),
}
ORDER = ("talk", "up", "down", "nope", "back", "next", "ok")
LOOKS = ("normal", "pressed", "off", "live")  # art_data.h 의 Look 과 같은 차례

# ---- 모습(7판, 사용자 지시): 윈도우폰의 타일 메뉴 구성 + thiings 의 입체 그림 + 고급스러운 회색 톤 ----
WALL = [("#E3E5EA", 0.0), ("#D9DCE2", 0.55), ("#DCD7E2", 1.0)]         # 바탕: 밝고 차분한 회색, 아래로 갈수록 옅은 보랏빛
BLOBS = [((40, 30), 120, "#F6F7FA", 0.6), ((225, 180), 120, "#EDE9F3", 0.5), ((40, 320), 130, "#D0D3DC", 0.45)]
INK, INK_SOFT = "#1C1C1E", (128, 131, 140, 255)
RED = "#FF453A"
GLYPH_OFF = (170, 173, 180, 255)
TILE = (255, 255, 255, 140)            # 타일: 반투명 흰 유리
TILE_EDGE = (255, 255, 255, 235)
TILE_PRESSED = (168, 172, 182, 245)    # 눌린 순간: 분명히 어둡게
TILE_OFF = (255, 255, 255, 80)
# D64 듣는 중: 빨간 타일(받아쓰는 중이라는 것이 한눈에 보여야 한다는 요구에서).
#   앞선 판은 검은 타일이었다(회색 화면에서 가장 센 대비). 대비는 셌지만 "녹음 중"이라는 뜻으로 읽히지 않았다.
#   빨강은 이 화면에서 이 한 가지 사실(맥이 이 기기의 마이크로 듣고 있다)에만 넓게 쓴다.
#   되돌리려면 TILE_LIVE 를 (22, 22, 25, 255) 로, LIVE_MARK 를 RED 로(그리고 touch_draw.cpp 의 듣는 중 색).
TILE_LIVE = (255, 59, 48, 255)
TILE_LIVE_EDGE = (255, 255, 255, 150)  # 위쪽 가장자리의 밝은 선(유리 타일과 같은 결)
LIVE_MARK = (255, 255, 255, 255)       # 말하기 타일의 "녹음 중" 점(빨간 바탕이라 흰 점)
RADIUS, RADIUS_SMALL = 20, 15
INSET = 3                               # 타일을 자리 안쪽으로 들이는 만큼(타일 사이가 넉넉해진다)
# 화면의 글자는 모두 영어다(화면의 글은 영어로 통일하기로 했다).
LABELS = {"talk": "Talk", "ok": "OK", "back": "Back", "next": "Next"}   # 큰 타일 왼쪽 아래의 이름(윈도우폰 타일처럼)
LABEL_PX = 11
# 타일의 그림: thiings.co 의 입체 그림(개인 용도 무료 — cores3/art/README.md). 파일이 없으면 선 그림으로 대신한다.
# (파일 이름, 색을 어떻게 다루나) — "graphite" 는 밝기만 남겨 회색의 입체로 바꾼다. 버튼에는 색을 쓰지 않기로 했다
# (차분하게 보이도록): 색은 상태(연결 점·배터리·녹음 점)와 캐릭터에만 남긴다.
THIINGS = os.path.join(ART, "thiings")
ICONS = {"talk": ("retro-microphone", "keep"), "ok": ("check-mark", "graphite"), "nope": ("x-mark", "graphite"),
         "back": ("left-arrow", "graphite"), "next": ("right-arrow", "graphite"),
         "up": ("up-arrow", "graphite"), "down": ("arrow-down", "graphite")}
ICON_H = {"talk": 54, "ok": 42, "back": 34, "next": 34, "up": 21, "down": 21, "nope": 19}   # 그림의 높이(픽셀)

# 말 하나는 한 가지 뜻으로만 쓴다(D72 — 시험이 본다). `Not connected` 는 "지금 연결이 없다"는 상태 막대의 낱말뿐이고,
# "방금 누른 키가 나가지 않았다"는 `Key not sent`, "짝을 지우지 못했다"는 `Pairing not cleared` 다.
# 새 말은 끝에 덧붙인다(번호가 art_data.h 의 차례와 같아야 한다).
WORDS = ("Ready", "Waiting", "Not connected", "Listening", "Bluetooth", "Pairing")   # art_data.h 의 Word 차례
HINTS = ("No computer found", "Allow on your Mac", "Mic didn't open", "Update firmware? Hold OK", "Key not sent",
         "Pair in Bluetooth settings", "Pairing cleared", "Claude is done", "Claude needs you",
         "Pairing not cleared")   # Hint 차례
TEXT_PX, TEXT_BOX_H, TEXT_BASE = 13, 18, 14                            # 낱말: 글자 크기, 상자 높이, 밑줄 자리
DIGIT_PX, DIGIT_BOX_H, DIGIT_BASE = 11, 11, 10                         # 배터리 안의 숫자
BIG_PX, BIG_BOX_H, BIG_BASE = 30, 26, 24                               # 시계의 큰 숫자(D62)


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def vgrad(size, stops):
    w, h = size
    col = Image.new("RGB", (1, h))
    px = col.load()
    for y in range(h):
        t = y / max(1, h - 1)
        for (c0, p0), (c1, p1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                k = (t - p0) / max(1e-6, p1 - p0)
                a, b = rgb(c0), rgb(c1)
                px[0, y] = tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))
                break
    return col.resize((w, h))


def load_logo():
    """자기 로고(art/logo/logo.svg)를 "덮는 정도" 그림으로. 타일 그림이 없고 이 파일이 있을 때만 말하기 타일에 쓴다
    (파일이 없으면 돌려주는 별 모양은 쓰이지 않는다 — 그때는 마이크 선 그림을 그린다)."""
    if os.path.exists(LOGO):
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, "logo.png")
            subprocess.run(["rsvg-convert", "-w", "1024", "-h", "1024", LOGO, "-o", png], check=True)
            return Image.open(png).convert("RGBA").split()[3].copy()
    import math
    m = Image.new("L", (1024, 1024), 0)
    d = ImageDraw.Draw(m)
    for i in range(12):
        a = math.pi * i / 6
        d.line([(512, 512), (512 + 470 * math.cos(a), 512 + 470 * math.sin(a))], fill=255, width=70)
    return m


_icon_cache = {}


def load_icon(zone):
    """그 타일의 입체 그림(투명 바탕, 여백을 잘라 낸 것). 파일이 없으면 None."""
    if zone in _icon_cache:
        return _icon_cache[zone]
    name, tone = ICONS[zone]
    path = os.path.join(THIINGS, name + ".png")
    im = None
    if os.path.exists(path):
        im = Image.open(path).convert("RGBA")
        im = im.crop(im.split()[3].point(lambda a: 255 if a > 8 else 0).getbbox())
        if tone == "graphite":  # 밝기만 남겨 짙은 회색의 입체로(그림자 = 먹색, 밝은 면 = 은회색)
            a = im.split()[3]
            g = ImageOps.autocontrast(ImageOps.grayscale(im.convert("RGB")), cutoff=1)
            im = ImageOps.colorize(g, black="#3D4048", white="#E4E6EB", mid="#868A94").convert("RGBA")
            im.putalpha(a)
    _icon_cache[zone] = im
    return im


class Art:
    """화면 한 장(4배 크기). 바탕 위에 타일과 그림을 얹는다."""

    def __init__(self):
        base = vgrad((W * SS, H * SS), WALL).convert("RGBA")
        for (cx, cy), r, color, alpha in BLOBS:  # 바탕의 은은한 빛 덩어리
            blob = Image.new("L", base.size, 0)
            ImageDraw.Draw(blob).ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], fill=int(255 * alpha))
            blob = blob.filter(ImageFilter.GaussianBlur(r * SS * 0.45))
            base.paste(Image.new("RGBA", base.size, rgb(color) + (255,)), (0, 0), blob)
        self.img = base
        self.d = ImageDraw.Draw(self.img, "RGBA")
        self.font_label = ImageFont.truetype(FONT_LABEL if os.path.exists(FONT_LABEL) else FONT, LABEL_PX * SS)

    def _put(self, layer):
        self.img = Image.alpha_composite(self.img, layer)
        self.d = ImageDraw.Draw(self.img, "RGBA")

    def tile(self, b, r, fill, edge=None, shadow=True, inset=INSET):
        """타일 하나: 옅은 그림자 위에, 뒤의 바탕이 흐리게 비치는 유리판. 위쪽 가장자리에 밝은 선."""
        x, y, w, h = b
        box = [(x + inset) * SS, (y + inset) * SS, (x + w - inset) * SS - 1, (y + h - inset) * SS - 1]
        if shadow:  # 그림자는 자리 안에서만(자리 밖은 잘려 나간다)
            sh = Image.new("L", self.img.size, 0)
            ImageDraw.Draw(sh).rounded_rectangle([box[0], box[1] + SS, box[2], box[3] + SS], radius=r * SS, fill=38)
            clip = Image.new("L", self.img.size, 0)
            ImageDraw.Draw(clip).rectangle([x * SS, y * SS, (x + w) * SS - 1, (y + h) * SS - 1], fill=255)
            sh = ImageChops.multiply(sh.filter(ImageFilter.GaussianBlur(1.6 * SS)), clip)
            self.img.paste(Image.new("RGBA", self.img.size, (30, 32, 44, 255)), (0, 0), sh)
        mask = Image.new("L", self.img.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle(box, radius=r * SS, fill=255)
        if fill[3] < 255:
            self.img.paste(self.img.filter(ImageFilter.GaussianBlur(10 * SS)), (0, 0), mask)
        layer = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).rounded_rectangle(box, radius=r * SS, fill=fill)
        self._put(layer)
        if edge:
            layer = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
            ImageDraw.Draw(layer).rounded_rectangle(box, radius=r * SS, outline=edge, width=SS)
            fade = Image.new("L", self.img.size, 0)
            fade.paste(vgrad((w * SS, h * SS), [("#FFFFFF", 0.0), ("#404040", 0.5), ("#000000", 1.0)]).convert("L"), (x * SS, y * SS))
            layer.putalpha(ImageChops.multiply(layer.split()[3], fade))
            self._put(layer)

    def icon(self, im, cx, cy, height, opacity=1.0, grey=False):
        """입체 그림을 높이 height 로 맞춰 (cx, cy) 가운데에 놓는다."""
        w = max(1, round(im.width * height * SS / im.height))
        t = im.resize((w, int(height * SS)), Image.LANCZOS)
        if grey:  # 꺼진 모습: 색을 빼고 옅게
            a = t.split()[3]
            t = ImageOps.colorize(ImageOps.grayscale(t.convert("RGB")), black="#8E9199", white="#E6E8EC").convert("RGBA")
            t.putalpha(a)
        if opacity < 1.0:
            t.putalpha(t.split()[3].point(lambda a: int(a * opacity)))
        layer = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        layer.paste(t, (int(cx * SS - t.width / 2), int(cy * SS - t.height / 2)))
        self._put(layer)

    def logo(self, mask, cx, cy, size, color):
        m = mask.resize((int(size * SS), int(size * SS)), Image.LANCZOS)
        self.img.paste(Image.new("RGBA", m.size, color), (int(cx * SS - m.size[0] / 2), int(cy * SS - m.size[1] / 2)), m)
        self.d = ImageDraw.Draw(self.img, "RGBA")

    def line(self, pts, w, color):
        p = [(x * SS, y * SS) for x, y in pts]
        self.d.line(p, fill=color, width=int(w * SS), joint="curve")
        for x, y in p:
            self.d.ellipse([x - w * SS / 2, y - w * SS / 2, x + w * SS / 2, y + w * SS / 2], fill=color)

    def dot(self, cx, cy, r, color):
        self.d.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], fill=color)

    def label(self, zone, color):
        x, y, w, h = ZONES[zone]
        self.d.text(((x + 13) * SS, (y + h - 12) * SS), LABELS[zone], font=self.font_label, fill=color, anchor="ls")

    def view_tile(self, live=False):
        """보는 곳(맨 위의 넓은 타일): 평소에는 유리판, 듣는 중에는 빨간 판(D64)."""
        if live:
            self.tile(VIEW, RADIUS, TILE_LIVE, TILE_LIVE_EDGE)
        else:
            self.tile(VIEW, RADIUS, TILE, TILE_EDGE)

    def done(self):
        """줄이고 RGB565 로. 잔 무늬를 섞어 부드러운 색 변화에 띠가 지지 않게 한다(자리마다 늘 같은 무늬)."""
        small = self.img.convert("RGB").resize((W, H), Image.BOX)
        px = small.load()
        rnd = random.Random(7)
        out = []
        for y in range(H):
            for x in range(W):
                r, g, b = px[x, y]
                n = rnd.random() - 0.5
                r5 = max(0, min(31, int(r / 255 * 31 + 0.5 + n)))
                g6 = max(0, min(63, int(g / 255 * 63 + 0.5 + n)))
                b5 = max(0, min(31, int(b / 255 * 31 + 0.5 + n)))
                out.append((r5 << 11) | (g6 << 5) | b5)
        return out


def fallback_glyph(a, z, cx, cy, col, big):
    """thiings 그림이 없을 때의 선 그림."""
    t = 4.4 if big else 2.8
    if z == "up":
        a.line([(cx - 9, cy + 4.5), (cx, cy - 4.5), (cx + 9, cy + 4.5)], t, col)
    elif z == "down":
        a.line([(cx - 9, cy - 4.5), (cx, cy + 4.5), (cx + 9, cy - 4.5)], t, col)
    elif z == "nope":
        a.line([(cx - 6.5, cy - 6.5), (cx + 6.5, cy + 6.5)], t, col)
        a.line([(cx + 6.5, cy - 6.5), (cx - 6.5, cy + 6.5)], t, col)
    elif z in ("back", "next"):
        s = -1 if z == "back" else 1
        for dx in (-6, 6):
            a.line([(cx + dx - s * 5.5, cy - 13), (cx + dx + s * 6.5, cy), (cx + dx - s * 5.5, cy + 13)], t, col)
    elif z == "ok":
        a.line([(cx - 17, cy + 1), (cx - 6, cy + 12), (cx + 18, cy - 11)], 5.2, col)
    elif z == "talk":
        # 마이크: 몸통(알약) + 받침(U) + 기둥 + 발. 공개판의 말하기 타일이 이것이다 — 입체 그림(thiings)은 재배포할 수
        # 없고, 남의 로고는 넣지 않는다.
        a.line([(cx, cy - 16), (cx, cy - 1)], 13, col)
        a.line([(cx - 13, cy - 4), (cx - 13, cy + 1), (cx - 9.5, cy + 8), (cx, cy + 11.5), (cx + 9.5, cy + 8),
                (cx + 13, cy + 1), (cx + 13, cy - 4)], 3.6, col)
        a.line([(cx, cy + 11.5), (cx, cy + 19)], 3.6, col)
        a.line([(cx - 8, cy + 19), (cx + 8, cy + 19)], 3.6, col)


def draw_keys(a, look, logo):
    """일곱 타일을 모두 look 의 모습으로 그린다(look = live 는 말하기에만 뜻이 있다)."""
    off, pressed, live = look == "off", look == "pressed", look == "live"
    for z in ORDER:
        x, y, w, h = ZONES[z]
        big = z in LABELS
        hot = live and z == "talk"   # 듣는 중의 말하기 타일
        fill = TILE_LIVE if hot else TILE_PRESSED if pressed else TILE_OFF if off else TILE
        edge = TILE_LIVE_EDGE if hot else None if (pressed or off) else TILE_EDGE
        a.tile(ZONES[z], RADIUS if big else RADIUS_SMALL, fill, edge, shadow=not off)
        cx, cy = x + w / 2, y + h / 2 - (8 if big else 0)
        im = load_icon(z)
        scale = 0.93 if pressed else 1.0  # 눌리면 그림이 살짝 들어간다
        if im is not None:
            a.icon(im, cx, cy, ICON_H[z] * scale, opacity=0.45 if off else 1.0, grey=off)
        elif z == "talk" and os.path.exists(LOGO):
            # 입체 그림은 없고 자기 로고(art/logo/logo.svg)를 둔 경우에만 그것을 쓴다. 둘 다 없으면 아래의 선 그림(마이크).
            a.logo(logo, cx, cy, 46 * scale, GLYPH_OFF if off else ((255, 255, 255, 255) if hot else rgb("#D97757") + (255,)))
        else:
            # 선 그림의 색: 확인만 초록, 나머지는 먹색. 취소(✕)를 빨갛게 칠하지 않는다 — 이 화면에서 빨강은 "듣는 중"
            # 하나만 뜻한다(D64. 시험 test_draw_listening_is_red 가 본다).
            col = (GLYPH_OFF if off else (255, 255, 255, 255) if hot
                   else (48, 209, 88, 255) if z == "ok" else INK)
            fallback_glyph(a, z, cx, cy, col, big)
        if big:
            a.label(z, GLYPH_OFF if off else ((255, 255, 255, 240) if hot else INK_SOFT))
        if hot:  # 녹음 중을 뜻하는 점
            a.dot(x + w - 19, y + 19, 4.5, LIVE_MARK)


def crop(px, box):
    x, y, w, h = box
    return [px[(y + r) * W + x + c] for r in range(h) for c in range(w)]


def text_mask(s, px, box_h, base):
    """글자 한 줄의 "덮는 정도" 그림. 높이는 box_h 로 같고(밑줄 자리가 같다) 너비는 글자에 딱 맞다."""
    f = ImageFont.truetype(FONT, px * SS)
    w = int(f.getlength(s) + SS) // SS + 1
    im = Image.new("L", (w * SS, box_h * SS), 0)
    ImageDraw.Draw(im).text((0, base * SS), s, font=f, fill=255, anchor="ls")
    return im.resize((w, box_h), Image.BOX)


def shape_mask(w, h, draw):
    im = Image.new("L", (w * SS, h * SS), 0)
    draw(ImageDraw.Draw(im))
    return im.resize((w, h), Image.BOX)


def build():
    logo = load_logo()
    images, masks = {}, {}
    wall = Art()
    wall.view_tile()          # 평소의 보는 곳(유리 타일)은 바탕에 함께 그려 둔다
    images["WALL"] = (W, H, wall.done())
    live = Art()
    live.view_tile(live=True)
    images["VIEW_LIVE"] = (VIEW[2], VIEW[3], crop(live.done(), VIEW))
    for look in LOOKS:
        a = Art()
        draw_keys(a, look, logo)
        px = a.done()
        for z in ORDER:
            if look == "live" and z != "talk":
                continue
            x, y, w, h = ZONES[z]
            images[f"KEY_{z}_{look}".upper()] = (w, h, crop(px, ZONES[z]))
    for i, s in enumerate(WORDS):
        masks[f"WORD_{i}"] = text_mask(s, TEXT_PX, TEXT_BOX_H, TEXT_BASE)
    for i, s in enumerate(HINTS):
        masks[f"HINT_{i}"] = text_mask(s, TEXT_PX, TEXT_BOX_H, TEXT_BASE)
    for i in range(10):
        masks[f"DIGIT_{i}"] = text_mask(str(i), DIGIT_PX, DIGIT_BOX_H, DIGIT_BASE)
        masks[f"BIGDIGIT_{i}"] = text_mask(str(i), BIG_PX, BIG_BOX_H, BIG_BASE)
    masks["BIGCOLON"] = text_mask(":", BIG_PX, BIG_BOX_H, BIG_BASE)
    e = lambda d, box, **kw: d.ellipse([v * SS for v in box], **kw)
    masks["DOT"] = shape_mask(9, 9, lambda d: e(d, (0, 0, 9, 9), fill=255))
    masks["RING"] = shape_mask(9, 9, lambda d: e(d, (0, 0, 9, 9), outline=255, width=int(2.2 * SS)))
    def half(d):
        e(d, (0, 0, 9, 9), outline=255, width=2 * SS)
        d.pieslice([0, 0, 9 * SS, 9 * SS], 90, 270, fill=255)
    masks["HALF"] = shape_mask(9, 9, half)
    masks["BAT_BODY"] = shape_mask(30, 15, lambda d: d.rounded_rectangle([0, 0, 30 * SS - 1, 15 * SS - 1], radius=4.6 * SS, fill=255))
    masks["BAT_NUB"] = shape_mask(2, 6, lambda d: d.rounded_rectangle([0, 0, 2 * SS - 1, 6 * SS - 1], radius=1 * SS, fill=255))
    return images, masks


def emit(images, masks):
    out = ['// 이 파일은 cores3/tools/gen_art.py 가 만든다. 손으로 고치지 않는다(고칠 것은 그 스크립트에 있다).',
           '// 내용: 미리 그려 둔 화면 조각(RGB565)과 낱말·숫자·모양의 "덮는 정도" 그림. 설계서 D56·D57.',
           '#include "art_data.h"', '', 'namespace art {', '', 'namespace {']
    for name, (w, h, px) in images.items():
        out.append(f"const uint16_t PX_{name}[{w * h}] = {{")
        for i in range(0, len(px), 16):
            out.append("  " + ",".join(f"0x{v:04X}" for v in px[i:i + 16]) + ",")
        out.append("};")
    for name, im in masks.items():
        data = list(im.tobytes())
        out.append(f"const uint8_t A_{name}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            out.append("  " + ",".join(str(v) for v in data[i:i + 24]) + ",")
        out.append("};")
    out += ["}  // namespace", ""]
    img = lambda n: f"{{{images[n][0]}, {images[n][1]}, PX_{n}}}"
    msk = lambda n: f"{{{masks[n].size[0]}, {masks[n].size[1]}, A_{n}}}"
    out.append(f"const Image WALL = {img('WALL')};")
    out.append(f"const Image VIEW_LIVE = {img('VIEW_LIVE')};")
    out.append("const Image KEY[7][LOOK_COUNT] = {")
    for z in ORDER:
        row = [img(f"KEY_{z}_{look}".upper()) if f"KEY_{z}_{look}".upper() in images else "{0, 0, nullptr}" for look in LOOKS]
        out.append("    {" + ", ".join(row) + "},")
    out.append("};")
    out.append("const Mask WORD[WORD_COUNT] = {" + ", ".join(msk(f"WORD_{i}") for i in range(len(WORDS))) + "};")
    out.append("const Mask HINT[HINT_COUNT] = {" + ", ".join(msk(f"HINT_{i}") for i in range(len(HINTS))) + "};")
    out.append("const Mask DIGIT[10] = {" + ", ".join(msk(f"DIGIT_{i}") for i in range(10)) + "};")
    out.append("const Mask BIGDIGIT[10] = {" + ", ".join(msk(f"BIGDIGIT_{i}") for i in range(10)) + "};")
    out.append(f"const Mask BIGCOLON = {msk('BIGCOLON')};")
    for n in ("DOT", "RING", "HALF", "BAT_BODY", "BAT_NUB"):
        out.append(f"const Mask {n} = {msk(n)};")
    rows = [VIEW] + [ZONES[z] for z in ORDER]
    out.append("const int16_t LAYOUT[8][4] = {" + ", ".join("{%d, %d, %d, %d}" % r for r in rows) + "};")
    out += ["", "}  // namespace art", ""]
    return "\n".join(out)


# 저장소의 art_data.cpp 를 만든 그림 도구의 판. 판이 다르면 글자·그림자의 끝자리가 달라져, 손대지 않은 조각까지
# 모두 미세하게 달라진다(10.2.0 과 12.2.0 사이에서 확인 — 저장소의 결과는 12.2.0 으로 만들었다).
PIL_EXPECTED = "12.2"


if __name__ == "__main__":
    import PIL
    if not os.path.exists(FONT):
        sys.exit(f"글꼴이 없다: {FONT}\n받는 법은 cores3/art/README.md")
    same_tool = PIL.__version__.startswith(PIL_EXPECTED + ".") or PIL.__version__ == PIL_EXPECTED
    if not same_tool and "--any-pil" not in sys.argv:
        sys.exit(f"이 컴퓨터의 그림 도구(PIL {PIL.__version__})는 기준({PIL_EXPECTED}.x)과 판이 다르다.\n"
                 "여기서 만들면 손대지 않은 조각까지 모두 달라지고, --check 도 '다르다'고 잘못 알린다.\n"
                 "기준 판의 Pillow 로 돌릴 것(cores3/art/README.md). 그래도 여기서 하려면 --any-pil 을 붙인다.")
    missing = [ICONS[z][0] + ".png" for z in ORDER if load_icon(z) is None]
    if missing and "--check" not in sys.argv and "--lines" not in sys.argv:
        sys.exit(f"타일의 입체 그림이 없다: {', '.join(missing)} (cores3/art/thiings/)\n"
                 "없는 채로 만들면 그 타일은 선 그림으로 바뀐다. 그렇게 하려면 --lines 를 붙인다.")
    text = emit(*build())
    if "--check" in sys.argv:
        same = os.path.exists(OUT) and open(OUT, encoding="utf-8").read() == text
        print("art_data.cpp 는 gen_art.py 의 결과와 " + ("같다" if same else "다르다 — gen_art.py 를 다시 돌릴 것"))
        sys.exit(0 if same else 1)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    have = [z for z in ORDER if load_icon(z) is not None]
    print(f"저장: {os.path.normpath(OUT)} ({len(text) // 1024}KB, thiings 그림 {len(have)}/7 — 없는 것은 선 그림)")
