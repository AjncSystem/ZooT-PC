# SPDX-License-Identifier: GPL-3.0-or-later
"""ZooT-PC launcher look: night blue, polished silver and old gold, the colours of the ZooT
shield. Fonts: Cinzel (titles) and Inter (text), SIL Open Font License, in assets/fonts.
Rounded buttons, switches and the game card are drawn by Pillow at 4x and scaled down."""
import ctypes
import sys
import tkinter as tk
from pathlib import Path
from tkinter import ttk

from PIL import Image, ImageDraw, ImageFilter, ImageTk

if getattr(sys, 'frozen', False):
    ASSETS = Path(getattr(sys, '_MEIPASS', Path(sys.executable).parent)) / 'assets'
else:
    ASSETS = Path(__file__).resolve().parent / 'assets'

NIGHT = '#0a1120'
SIDE = '#070c18'
TEXT_STRONG = '#f6f8fc'
PANEL = '#101a2e'
CARD = '#15223b'
BORDER = '#283a5e'
TEXT = '#e9edf4'
MUTED = '#8c98af'
SILVER = '#c3ccda'
GOLD = '#dcb44e'
GOLD_LIGHT = '#f2d98d'
GOLD_DARK = '#8a6a1e'
GREEN = '#4c9d62'
RED = '#ef5350'

HEAD = HEAD_BOLD = 'Georgia'
BODY = BODY_SEMI = 'Segoe UI'
ICONS = 'Segoe MDL2 Assets'
SS = 4

# Segoe Fluent Icons (Windows 11) / Segoe MDL2 Assets (Windows 10), as in the SotC-PC launcher.
ICON = {'play': '', 'display': '', 'gameplay': '', 'language': '',
        'saves': '', 'help': '', 'log': '', 'folder': '',
        'controls': ''}


def pill(w, h, fill, outline, bg):
    """A rounded card for the side bar (the current page, the hovered one, the gold accent)."""
    big = Image.new('RGBA', (w * SS, h * SS), rgba(bg))
    ImageDraw.Draw(big).rounded_rectangle(
        [0, 0, w * SS - 1, h * SS - 1], min(8, h // 2) * SS, fill=rgba(fill),
        outline=rgba(outline) if outline else None, width=SS)
    return ImageTk.PhotoImage(big.resize((w, h), Image.LANCZOS))


FONTS_LOADED = False


def load_fonts(root, lang='pt'):
    """Picks the fonts for the launcher language. Cinzel and Inter are Latin (Inter also has
    Cyrillic): Japanese and Korean use Windows' own UI fonts, Russian titles use Georgia."""
    global HEAD, HEAD_BOLD, BODY, BODY_SEMI, ICONS, FONTS_LOADED
    _load_bundled(root)
    import tkinter.font as tkfont
    families = set(tkfont.families(root))
    pick = lambda *names: next((n for n in names if n in families), names[-1])
    if lang == 'ja':
        BODY = BODY_SEMI = pick('Yu Gothic UI', 'Meiryo UI', 'Meiryo', 'MS UI Gothic')
        HEAD = HEAD_BOLD = pick('Yu Gothic UI Semibold', 'Yu Gothic UI', 'Meiryo', 'MS UI Gothic')
    elif lang == 'ko':
        BODY = BODY_SEMI = pick('Malgun Gothic', 'Gulim')
        HEAD = HEAD_BOLD = pick('Malgun Gothic Semilight', 'Malgun Gothic', 'Gulim')
    elif lang == 'ru':
        HEAD = HEAD_BOLD = pick('Georgia', 'Times New Roman')
    ICONS = pick('Segoe Fluent Icons', 'Segoe MDL2 Assets')


def _load_bundled(root):
    global HEAD, HEAD_BOLD, BODY, BODY_SEMI, ICONS, FONTS_LOADED
    import tkinter.font as tkfont
    if not FONTS_LOADED:
        for path in sorted((ASSETS / 'fonts').glob('*.ttf')):
            try:
                ctypes.windll.gdi32.AddFontResourceExW(str(path), 0x10, 0)  # FR_PRIVATE
            except (AttributeError, OSError):
                pass
        FONTS_LOADED = True
    families = set(tkfont.families(root))
    pick = lambda *names: next((n for n in names if n in families), names[-1])
    HEAD = pick('Cinzel SemiBold', 'Georgia')
    HEAD_BOLD = pick('Cinzel Bold', 'Cinzel SemiBold', 'Georgia')
    BODY = pick('Inter', 'Segoe UI')
    BODY_SEMI = pick('Inter SemiBold', 'Inter', 'Segoe UI Semibold')
    ICONS = pick('Segoe Fluent Icons', 'Segoe MDL2 Assets')


def rgba(color, alpha=255):
    c = color.lstrip('#')
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(4))


def vgrad(size, top, bottom):
    img = Image.new('RGBA', size)
    d = ImageDraw.Draw(img)
    for y in range(size[1]):
        d.line([(0, y), (size[0], y)], fill=mix(top, bottom, y / max(1, size[1] - 1)))
    return img


def rounded_mask(size, radius):
    big = Image.new('L', (size[0] * SS, size[1] * SS), 0)
    ImageDraw.Draw(big).rounded_rectangle([0, 0, big.width - 1, big.height - 1],
                                          radius * SS, fill=255)
    return big.resize(size, Image.LANCZOS)


def button_image(size, primary, hover, bg):
    w, h = size
    img = Image.new('RGBA', size, rgba(bg))
    if primary:
        top, bottom = (rgba(GOLD_LIGHT), rgba(GOLD)) if hover else (rgba(GOLD), rgba(GOLD_DARK))
    else:
        top, bottom = (rgba('#2c3f66'), rgba('#1c2b48')) if hover else \
            (rgba('#22324f'), rgba('#172440'))
    fill = vgrad(size, top, bottom)
    img.paste(fill, (0, 0), rounded_mask(size, h // 2 if h < 40 else 12))
    border = Image.new('L', (w * SS, h * SS), 0)
    r = (h // 2 if h < 40 else 12) * SS
    ImageDraw.Draw(border).rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], r,
                                             outline=255, width=2 * SS)
    edge = rgba(GOLD_LIGHT if primary else ('#4a5f8a' if hover else BORDER))
    img.paste(Image.new('RGBA', size, edge), (0, 0), border.resize(size, Image.LANCZOS))
    return img


class Button(tk.Label):
    """A rounded gold (primary) or blue button drawn as an image, with a hover state."""

    def __init__(self, parent, text, command, primary=False, big=False, small=False):
        bg = parent.cget('bg')
        font = (HEAD_BOLD, 18) if big else ((BODY_SEMI, 9) if small else (BODY_SEMI, 11))
        w = 230 if big else (90 if small else 200)
        h = 56 if big else (30 if small else 38)
        self.images = {k: ImageTk.PhotoImage(button_image((w, h), primary, k, bg))
                       for k in (False, True)}
        super().__init__(parent, image=self.images[False], text=text, compound='center',
                         font=font, fg=NIGHT if primary else TEXT, bg=bg, cursor='hand2', bd=0)
        self.command = command
        self.bind('<Enter>', lambda e: self.config(image=self.images[True]))
        self.bind('<Leave>', lambda e: self.config(image=self.images[False]))
        self.bind('<Button-1>', lambda e: self.command())


class Switch(tk.Label):
    """An on/off switch: green track with a gold knob when on, dark when off."""

    def __init__(self, parent, value, on_change):
        bg = parent.cget('bg')
        self.value = bool(value)
        self.on_change = on_change
        self.images = {v: ImageTk.PhotoImage(self.draw(v, bg)) for v in (False, True)}
        super().__init__(parent, image=self.images[self.value], bg=bg, cursor='hand2', bd=0)
        self.bind('<Button-1>', self.toggle)

    @staticmethod
    def draw(on, bg):
        w, h = 46, 24
        big = Image.new('RGBA', (w * SS, h * SS), rgba(bg))
        d = ImageDraw.Draw(big)
        d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], h * SS // 2,
                            fill=rgba(GREEN if on else '#26324a'),
                            outline=rgba(GOLD if on else BORDER), width=SS * 2)
        r = (h - 8) * SS // 2
        cx = (w - h // 2) * SS if on else (h // 2) * SS
        cy = h * SS // 2
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgba(GOLD_LIGHT if on else SILVER))
        return big.resize((w, h), Image.LANCZOS)

    def toggle(self, _=None):
        self.value = not self.value
        self.config(image=self.images[self.value])
        self.on_change(self.value)

    def set(self, value):
        """Shows `value` without calling on_change (another page changed the option)."""
        self.value = bool(value)
        self.config(image=self.images[self.value])


def hero_card(size, art, fallback):
    """The game card: the player's own game art over a blurred, darkened copy of itself, in a
    rounded frame with a silver edge and a dark fade at the bottom for the title. Without art,
    the ZooT shield on night blue."""
    w, h = size
    card = Image.new('RGBA', size, rgba(NIGHT))
    src = None
    if art:
        try:
            src = Image.open(art).convert('RGBA')
        except OSError:
            src = None
    base = vgrad(size, rgba('#1b2c4c'), rgba('#0b1426'))
    if src is not None and src.width / src.height > 1.2:
        # A wide picture (a cover or a screenshot): it fills the whole card.
        scale = max(w / src.width, h / src.height)
        full = src.resize((int(src.width * scale) + 1, int(src.height * scale) + 1),
                          Image.LANCZOS)
        left = (full.width - w) // 2
        top = max(0, int((full.height - h) * 0.35))
        base = full.crop((left, top, left + w, top + h))
    elif src is not None:
        # blurred background filling the card
        scale = max(w / src.width, h / src.height)
        bg = src.resize((int(src.width * scale) + 1, int(src.height * scale) + 1), Image.LANCZOS)
        bg = bg.crop(((bg.width - w) // 2, (bg.height - h) // 2,
                      (bg.width - w) // 2 + w, (bg.height - h) // 2 + h))
        bg = bg.filter(ImageFilter.GaussianBlur(28))
        base = Image.blend(bg, Image.new('RGBA', size, rgba(NIGHT)), 0.45)
        # the art itself, square, centred in the upper part
        side = int(min(h * 0.68, w * 0.5))
        fg = src.resize((side, side), Image.LANCZOS)
        shadow = Image.new('RGBA', size, (0, 0, 0, 0))
        sm = Image.new('L', size, 0)
        sm.paste(rounded_mask((side, side), 18), ((w - side) // 2 + 6, int(h * 0.07) + 10))
        shadow.paste(Image.new('RGBA', size, (0, 0, 0, 170)), (0, 0), sm)
        base = Image.alpha_composite(base, shadow.filter(ImageFilter.GaussianBlur(12)))
        base.paste(fg, ((w - side) // 2, int(h * 0.07)), rounded_mask((side, side), 18))
    else:
        try:
            shield = Image.open(fallback).convert('RGBA')
            side = int(min(h * 0.62, w * 0.5))
            shield = shield.resize((side, side), Image.LANCZOS)
            base.alpha_composite(shield, ((w - side) // 2, int(h * 0.07)))
        except OSError:
            pass
    # bottom fade for the title
    fade = Image.new('RGBA', size, (0, 0, 0, 0))
    fd = ImageDraw.Draw(fade)
    start = int(h * 0.62)
    for y in range(start, h):
        a = int(230 * ((y - start) / max(1, h - start)) ** 1.2)
        fd.line([(0, y), (w, y)], fill=rgba(NIGHT, a))
    base = Image.alpha_composite(base, fade)
    card.paste(base, (0, 0), rounded_mask(size, 16))
    # silver edge
    edge = Image.new('L', (w * SS, h * SS), 0)
    ImageDraw.Draw(edge).rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], 16 * SS,
                                           outline=255, width=2 * SS)
    card.paste(Image.new('RGBA', size, rgba(BORDER)), (0, 0), edge.resize(size, Image.LANCZOS))
    return card


def apply(root):
    style = ttk.Style(root)
    style.theme_use('clam')
    style.configure('Zoot.TCombobox', fieldbackground=CARD, background=CARD, foreground=TEXT,
                    arrowcolor=GOLD, bordercolor=BORDER, lightcolor=CARD, darkcolor=CARD,
                    selectbackground=CARD, selectforeground=TEXT, padding=4)
    style.map('Zoot.TCombobox', fieldbackground=[('readonly', CARD)],
              foreground=[('readonly', TEXT)], bordercolor=[('focus', GOLD)])
    root.option_add('*TCombobox*Listbox.background', CARD)
    root.option_add('*TCombobox*Listbox.foreground', TEXT)
    root.option_add('*TCombobox*Listbox.selectBackground', GOLD_DARK)
    root.option_add('*TCombobox*Listbox.font', (BODY, 10))
    style.configure('Zoot.Horizontal.TScale', background=PANEL, troughcolor=CARD,
                    bordercolor=BORDER, lightcolor=GOLD, darkcolor=GOLD_DARK)
    return style
