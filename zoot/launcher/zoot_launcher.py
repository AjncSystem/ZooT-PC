# SPDX-License-Identifier: GPL-3.0-or-later
"""ZooT-PC launcher, laid out like the SotC-PC launcher: a side bar with the port's name and its
pages, the page on the right and a bottom bar with the status and PLAY. Picks the game once, shows
the player's own cover (user/zoot/capa.png, never shipped), writes the options to eden-cli's
sdl2-config.ini and starts the game.

Layout of a package (and of the dev build folder):
    ZooT.exe              this launcher, frozen
    eden-cli.exe          the game runner
    nx2_switches.txt
    user/                 portable data: config, saves, shader caches, logs
    user/zoot/            launcher settings, the player's own capa.png / icon.png, backups
"""
import json
import os
import subprocess
import sys
import threading
import time
import tkinter as tk
import zipfile
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk

if not getattr(sys, 'frozen', False):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import zoot_i18n as I18N  # noqa: E402
import zoot_theme as T  # noqa: E402
from zoot_i18n import _  # noqa: E402

APP = 'ZooT-PC'
VERSION = '0.10'
TITLE_ID = '0400486024DEC000'
GAME_EXTS = ('.nup', '.nx', '.nsp', '.xci')

if getattr(sys, 'frozen', False):
    BASE = Path(sys.executable).resolve().parent
    BUNDLE = Path(getattr(sys, '_MEIPASS', BASE))
else:
    # Dev runs: the build folder of this checkout (<ZooT-PC>\build\bin).
    BASE = Path(os.environ.get('ZOOT_BASE',
                               Path(__file__).resolve().parents[3] / 'build' / 'bin'))
    BUNDLE = Path(__file__).resolve().parent

CORE = BASE / 'eden-cli.exe'
USER = BASE / 'user'
ZDIR = USER / 'zoot'
SETTINGS = ZDIR / 'launcher.json'
INI = USER / 'config' / 'sdl2-config.ini'
LOG = USER / 'log' / 'eden_log.txt'
SAVES = USER / 'nand' / 'user' / 'save'
SHIELD = BUNDLE / 'assets' / 'zoot-256.png'
NO_WINDOW = 0x08000000  # CREATE_NO_WINDOW

RESOLUTIONS = [('0,5x (540p)', 1), ('0,75x', 2), ('1x (1080p)', 3), ('1,25x', 4), ('1,5x', 5),
               ('2x (4K)', 6), ('3x', 7), ('4x', 8)]
FILTERS = [('Bilinear', 1), ('Bicúbico', 2), ('ScaleForce', 5), ('AMD FSR 1', 6),
           ('AMD FSR 3', 15), ('Lanczos', 4), ('Área', 7)]
AA = [('Nenhum', 0), ('FXAA', 1), ('SMAA', 2)]
ASPECT = [('16:9', 0), ('21:9', 2), ('16:10', 3), ('4:3', 1), ('Esticar', 4)]
VSYNC = [('Desligado', 0), ('Mailbox', 1), ('Ligado (FIFO)', 2), ('FIFO relaxado', 3)]
MODE = [('TV (dock)', 1), ('Portátil', 0)]
# The console language (language_index) in each language's own name, and the region it needs.
GAME_LANGUAGES = [
    ('Português (Brasil)', 17), ('English (US)', 1), ('English (UK)', 12),
    ('Español (España)', 5), ('Español (Latinoamérica)', 14), ('Français', 2),
    ('Français (Canada)', 13), ('Deutsch', 3), ('Italiano', 4), ('Nederlands', 8),
    ('Português (Portugal)', 9), ('Русский', 10), ('Polski', 18), ('日本語', 0), ('한국어', 7),
    ('简体中文', 15), ('繁體中文', 16), ('ไทย', 19)]
REGION_OF = {0: 0, 7: 5, 15: 4, 6: 4, 16: 6, 11: 6,
             2: 2, 3: 2, 4: 2, 5: 2, 8: 2, 9: 2, 10: 2, 12: 2, 18: 2}  # others: USA (1)

NAV = [('play', 'Jogar', 'play'), ('game', 'Jogo', 'gameplay'),
       ('graphics', 'Imagem e FPS', 'display'), ('language', 'Idioma', 'language'),
       ('saves', 'Saves', 'saves'), ('help', 'Ajuda', 'help'), ('log', 'Registro', 'log')]

DEFAULTS = {
    'game': '', 'resolution': 3, 'filter': 15, 'aa': 1, 'aspect': 0, 'vsync': 2, 'mode': 1,
    'fullscreen': False, 'gpl': True, 'precache': True, 'volume': 100,
    'game_language': 17, 'launcher_language': 'pt',
}


def load_settings():
    data = dict(DEFAULTS)
    try:
        data.update(json.loads(SETTINGS.read_text(encoding='utf-8-sig')))
    except (OSError, ValueError):
        pass
    return data


def save_settings(data):
    ZDIR.mkdir(parents=True, exist_ok=True)
    tmp = SETTINGS.with_suffix('.tmp')
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')
    tmp.replace(SETTINGS)


def write_ini(values):
    """Sets keys in sdl2-config.ini, marking each as not default. Keys missing from the file go
    to [System] (languages), [Audio] (volume) or [Renderer]."""
    try:
        lines = INI.read_text(encoding='utf-8-sig').splitlines()
    except OSError:
        lines = ['[Renderer]']
    seen = set()
    out = []
    for line in lines:
        key = line.split('=', 1)[0]
        if key in values:
            out.append(f'{key}={values[key]}')
            seen.add(key)
        elif key.endswith('\\default') and key[:-8] in values:
            out.append(f'{key[:-8]}\\default=false')
        else:
            out.append(line)
    sections = {'volume': '[Audio]', 'language_index': '[System]', 'region_index': '[System]'}
    for key in (k for k in values if k not in seen):
        header = sections.get(key, '[Renderer]')
        if header not in out:
            out.append(header)
        i = out.index(header) + 1
        out[i:i] = [f'{key}={values[key]}', f'{key}\\default=false']
    INI.parent.mkdir(parents=True, exist_ok=True)
    INI.write_text('\n'.join(out) + '\n', encoding='utf-8')


SKIP_DIRS = {'windows', 'program files', 'program files (x86)', 'programdata', 'appdata',
             '$recycle.bin', 'system volume information', 'msys64', 'node_modules', '.git',
             '.cache', 'build', 'cache', 'shader', 'nand', 'sdmc'}


def find_games(max_depth=3, max_dirs=6000):
    """Game files near the package and on every drive, three folders deep at most,
    Ocarina of Time first."""
    roots = [BASE.parent, BASE.parent.parent, Path.home() / 'Downloads', Path.home() / 'Desktop']
    roots += [Path(f'{d}:\\') for d in 'CDEFGHIJK' if Path(f'{d}:\\').exists()]
    found, seen, visited = [], set(), 0
    for root in roots:
        stack = [(root, 0)]
        while stack and visited < max_dirs:
            folder, depth = stack.pop()
            try:
                key = folder.resolve()
            except OSError:
                continue
            if key in seen:
                continue
            seen.add(key)
            visited += 1
            try:
                entries = list(os.scandir(folder))
            except OSError:
                continue
            for e in entries:
                try:
                    if e.is_file() and Path(e.name).suffix.lower() in GAME_EXTS:
                        p = Path(e.path)
                        if p not in found:
                            found.append(p)
                    elif e.is_dir() and depth < max_depth and e.name.lower() not in SKIP_DIRS:
                        stack.append((Path(e.path), depth + 1))
                except OSError:
                    continue
    found.sort(key=lambda f: (TITLE_ID.lower() not in f.name.lower()
                              and 'ocarina' not in f.name.lower(), f.name))
    return found


def export_title(game):
    """The game's own title (from the player's copy), through eden-cli, cached in user/zoot."""
    cached = ZDIR / 'game-title.txt'
    try:
        return cached.read_text(encoding='utf-8').strip() or None
    except OSError:
        pass
    if not CORE.exists():
        return None
    ZDIR.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run([str(CORE), '--zoot-export-art', str(game), str(ZDIR)], cwd=BASE,
                       timeout=60, creationflags=NO_WINDOW, capture_output=True)
        return cached.read_text(encoding='utf-8').strip() or None
    except (OSError, subprocess.SubprocessError):
        return None


def cover_path():
    """The player's own cover first, then the game file's icon when it had one."""
    return next((p for p in (ZDIR / 'capa.png', ZDIR / 'capa.jpg', ZDIR / 'game-icon.png',
                             ZDIR / 'game-icon.jpg') if p.exists()), None)


def set_app_id():
    """Its own taskbar identity, so Windows shows the ZooT icon and does not group the window
    with python.exe (dev runs) or other programs. Must run before the first window."""
    try:
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('AjncSystem.ZooT-PC')
    except (AttributeError, OSError):
        pass


class Launcher(tk.Tk):
    W, H = 1140, 740

    def __init__(self):
        set_app_id()
        # Portable: the user folder lives next to ZooT.exe and is made on the first run.
        for folder in (USER, ZDIR):
            try:
                folder.mkdir(parents=True, exist_ok=True)
            except OSError:
                pass
        super().__init__()
        self.report_callback_exception = self.report_error
        self.settings = load_settings()
        I18N.LANG = self.settings.get('launcher_language', 'pt')
        T.load_fonts(self, I18N.LANG)
        self.title(APP)
        self.configure(bg=T.NIGHT)
        self.geometry(f'{self.W}x{self.H}')
        self.minsize(1040, 700)
        self.images = []
        self.proc = None
        self.game_title = None
        self.style = T.apply(self)
        self.set_icon()
        self.rebuild()

    # ---- errors and icon ------------------------------------------------------------------------
    def report_error(self, exc, value, tb):
        """Errors in handlers: written to user/zoot/launcher.log and shown (pythonw has no
        console)."""
        import traceback
        text = ''.join(traceback.format_exception(exc, value, tb))
        try:
            ZDIR.mkdir(parents=True, exist_ok=True)
            with open(ZDIR / 'launcher.log', 'a', encoding='utf-8') as f:
                f.write(time.strftime('%Y-%m-%d %H:%M:%S ') + text + '\n')
        except OSError:
            pass
        messagebox.showerror(APP, f"{_('Erro no lançador:')}\n{value}\n\n"
                                  f"{_('Detalhes em user\\zoot\\launcher.log')}")

    def set_icon(self):
        ico = BUNDLE / 'assets' / 'zoot.ico'
        if ico.exists() and not (ZDIR / 'icon.png').exists():
            try:
                self.iconbitmap(default=str(ico))
            except tk.TclError:
                pass
        for path in (ZDIR / 'icon.png', SHIELD):
            if path.exists():
                try:
                    img = ImageTk.PhotoImage(Image.open(path).convert('RGBA').resize((64, 64)))
                    self.iconphoto(True, img)
                    self.window_icon = img
                    return
                except OSError:
                    continue

    def keep(self, img):
        self.images.append(img)
        return img

    def rebuild(self):
        for w in self.winfo_children():
            w.destroy()
        self.images = []
        if self.settings['game'] and Path(self.settings['game']).exists():
            self.build()
            self.show('play')
        else:
            self.show_setup()

    # ---- first run ------------------------------------------------------------------------------
    def show_setup(self):
        for w in self.winfo_children():
            w.destroy()
        wrap = tk.Frame(self, bg=T.NIGHT)
        wrap.place(relx=0.5, rely=0.5, anchor='center')
        if SHIELD.exists():
            img = self.keep(ImageTk.PhotoImage(Image.open(SHIELD).resize((150, 150), Image.LANCZOS)))
            tk.Label(wrap, image=img, bg=T.NIGHT).pack(pady=(0, 12))
        tk.Label(wrap, text=APP, font=(T.HEAD_BOLD, 34), fg=T.GOLD, bg=T.NIGHT).pack()
        tk.Label(wrap, text=_('Bem-vindo! Escolha o arquivo do seu jogo para começar.'),
                 font=(T.BODY, 13), fg=T.TEXT, bg=T.NIGHT).pack(pady=(6, 2))
        tk.Label(wrap, text=_('Formatos aceitos: .nup, .nx, .nsp, .xci'), font=(T.BODY, 10),
                 fg=T.MUTED, bg=T.NIGHT).pack(pady=(0, 18))
        self.found = tk.Listbox(wrap, width=90, height=7, bg=T.PANEL, fg=T.TEXT,
                                selectbackground=T.GOLD_DARK, highlightthickness=1,
                                highlightbackground=T.BORDER, relief='flat', font=(T.BODY, 10))
        self.found.pack(pady=(0, 12))
        self.found.bind('<Double-Button-1>', lambda e: self.pick_found())
        self.found_paths = []
        row = tk.Frame(wrap, bg=T.NIGHT)
        row.pack()
        T.Button(row, _('Usar o selecionado'), self.pick_found, primary=True).pack(side='left',
                                                                                   padx=6)
        T.Button(row, _('Procurar arquivo...'), self.browse).pack(side='left', padx=6)
        self.setup_status = tk.Label(wrap, text=_('Procurando jogos...'), font=(T.BODY, 10),
                                     fg=T.MUTED, bg=T.NIGHT)
        self.setup_status.pack(pady=(14, 0))
        threading.Thread(target=self.search_games, daemon=True).start()

    def search_games(self):
        games = find_games()

        def show():
            try:
                self.found_paths = games
                for g in games:
                    self.found.insert('end', str(g))
                if games:
                    self.found.selection_set(0)
                self.setup_status.config(text=_('{n} arquivo(s) encontrado(s).', n=len(games))
                                         if games else
                                         _('Nenhum jogo encontrado: use "Procurar arquivo...".'))
            except tk.TclError:
                pass  # the screen was left meanwhile
        self.after(0, show)

    def pick_found(self):
        sel = self.found.curselection()
        if sel:
            self.use_game(self.found_paths[sel[0]])

    def browse(self):
        path = filedialog.askopenfilename(
            title=_('Arquivo do jogo'),
            filetypes=[(_('Jogos'), ' '.join('*' + e for e in GAME_EXTS)), (_('Todos'), '*.*')])
        if path:
            self.use_game(Path(path))

    def use_game(self, path):
        self.settings['game'] = str(path)
        save_settings(self.settings)
        for f in ('game-icon.jpg', 'game-icon.png', 'game-title.txt'):
            try:
                (ZDIR / f).unlink()
            except OSError:
                pass
        self.rebuild()

    # ---- main window: side bar, pages, bottom bar ------------------------------------------------
    def build(self):
        side = tk.Frame(self, bg=T.SIDE, width=230)
        side.pack(side='left', fill='y')
        side.pack_propagate(False)
        head = tk.Frame(side, bg=T.SIDE)
        head.pack(anchor='w', padx=20, pady=(24, 0))
        if SHIELD.exists():
            img = self.keep(ImageTk.PhotoImage(Image.open(SHIELD).resize((40, 40), Image.LANCZOS)))
            tk.Label(head, image=img, bg=T.SIDE).pack(side='left', padx=(0, 8))
        tk.Label(head, text='ZOOT-PC', bg=T.SIDE, fg=T.GOLD, font=(T.HEAD_BOLD, 19)).pack(
            side='left')
        tk.Frame(side, bg=T.GOLD, height=1, width=46).pack(anchor='w', padx=22, pady=(12, 8))
        tk.Label(side, text=f"{_('port nativo · Windows')}  ·  v{VERSION}", bg=T.SIDE,
                 fg=T.MUTED, font=(T.BODY, 9), wraplength=190, justify='left').pack(
            anchor='w', padx=22, pady=(0, 18))

        self.nav, self.current = {}, None
        w, h = 200, 38
        self.pill_on = self.keep(T.pill(w, h, T.CARD, T.BORDER, T.SIDE))
        self.pill_hover = self.keep(T.pill(w, h, T.PANEL, None, T.SIDE))
        self.accent = self.keep(T.pill(3, 18, T.GOLD, None, T.CARD))
        for name, title, icon in NAV:
            c = tk.Canvas(side, width=w, height=h, bg=T.SIDE, highlightthickness=0, bd=0,
                          cursor='hand2')
            c.pack(padx=14, pady=(0, 2))
            items = {'pill': c.create_image(0, 0, anchor='nw', state='hidden'),
                     'bar': c.create_image(1, h // 2, anchor='w', image=self.accent,
                                           state='hidden'),
                     'icon': c.create_text(22, h // 2, text=T.ICON[icon], font=(T.ICONS, 12)),
                     'label': c.create_text(46, h // 2, text=_(title), anchor='w',
                                            font=(T.BODY_SEMI, 10))}
            c.bind('<Button-1>', lambda _e, n=name: self.show(n))
            c.bind('<Enter>', lambda _e, n=name: self.nav_paint(n, hover=True))
            c.bind('<Leave>', lambda _e, n=name: self.nav_paint(n))
            self.nav[name] = (c, items)
            self.nav_paint(name)
        tk.Frame(side, bg=T.SIDE).pack(fill='both', expand=True)
        for text, command in ((_('Registro do jogo'), lambda: self.show('log')),
                              (_('Trocar jogo'), self.show_setup),
                              (_('Configurar de novo'), self.reset_setup)):
            link = tk.Label(side, text=text, bg=T.SIDE, fg=T.MUTED,
                            font=(T.BODY, 9, 'underline'), cursor='hand2')
            link.pack(anchor='w', padx=22, pady=(0, 6))
            link.bind('<Button-1>', lambda _e, c=command: c())
        tk.Frame(side, bg=T.SIDE, height=12).pack()
        tk.Frame(self, bg=T.BORDER, width=1).pack(side='left', fill='y')

        right = tk.Frame(self, bg=T.NIGHT)
        right.pack(side='left', fill='both', expand=True)
        bar = tk.Frame(right, bg=T.SIDE, height=76)
        bar.pack(fill='x', side='bottom')
        bar.pack_propagate(False)
        tk.Frame(right, bg=T.BORDER, height=1).pack(fill='x', side='bottom')
        self.play_btn = T.Button(bar, f"▶  {_('JOGAR')}", self.play, primary=True, big=True)
        self.play_btn.pack(side='right', padx=(10, 24), pady=10)
        # Stop, as in the SotC-PC launcher: closes the running game from the launcher.
        self.stop_btn = T.Button(bar, f"■  {_('Parar')}", self.stop)
        self.stop_btn.pack(side='right', pady=10)
        self.status = tk.Label(bar, text='', bg=T.SIDE, fg=T.MUTED, font=(T.BODY, 10),
                               anchor='w', justify='left')
        self.status.pack(side='left', fill='x', expand=True, padx=(24, 12))
        self.content = tk.Frame(right, bg=T.NIGHT)
        self.content.pack(fill='both', expand=True)

        self.pages = {}
        self.mirrors = {}
        self.build_play()
        self.build_game()
        self.build_graphics()
        self.build_language()
        self.build_saves()
        self.build_help()
        self.build_log()
        self.refresh_status()
        threading.Thread(target=self.load_title, daemon=True).start()

    def nav_paint(self, name, hover=False):
        c, items = self.nav[name]
        on = name == self.current
        pill = self.pill_on if on else self.pill_hover if hover else ''
        c.itemconfigure(items['pill'], image=pill, state='normal' if pill else 'hidden')
        c.itemconfigure(items['bar'], state='normal' if on else 'hidden')
        c.itemconfigure(items['icon'], fill=T.GOLD if on else T.TEXT if hover else T.MUTED)
        c.itemconfigure(items['label'], fill=T.TEXT_STRONG if on else T.TEXT if hover else T.SILVER)

    def show(self, name):
        for page in self.pages.values():
            page.pack_forget()
        self.pages[name].pack(fill='both', expand=True)
        self.current = name
        for n in self.nav:
            self.nav_paint(n)
        if name == 'log':
            self.load_log()
        elif name == 'saves':
            self.load_saves()

    def page(self, name, title):
        frame = tk.Frame(self.content, bg=T.NIGHT)
        self.pages[name] = frame
        if title:
            tk.Label(frame, text=title.upper(), font=(T.HEAD_BOLD, 17), fg=T.GOLD,
                     bg=T.NIGHT).pack(anchor='w', padx=30, pady=(24, 4))
            tk.Frame(frame, bg=T.BORDER, height=1).pack(fill='x', padx=30, pady=(4, 10))
        return frame

    def card(self, parent, title=None):
        box = tk.Frame(parent, bg=T.PANEL, highlightthickness=1, highlightbackground=T.BORDER)
        box.pack(fill='x', padx=30, pady=(6, 8))
        if title:
            tk.Label(box, text=title, font=(T.BODY_SEMI, 11), fg=T.SILVER, bg=T.PANEL).pack(
                anchor='w', padx=18, pady=(12, 4))
        return box

    def combo(self, parent, key, label, choices, on_change=None, width=22):
        s = self.settings
        row = tk.Frame(parent, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=4)
        tk.Label(row, text=label, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL, anchor='w').pack(
            side='left')
        names = [_(n) for n, _v in choices]
        values = [v for _n, v in choices]
        current = names[values.index(s[key])] if s[key] in values else names[0]
        var = tk.StringVar(value=current)
        box = ttk.Combobox(row, values=names, textvariable=var, state='readonly', width=width,
                           style='Zoot.TCombobox')
        box.pack(side='right')

        def changed(_e):
            self.set_opt(key, values[names.index(var.get())])
            if on_change:
                on_change()
        box.bind('<<ComboboxSelected>>', changed)
        # The same option on another page follows this one.
        self.mirrors.setdefault(key, []).append(
            lambda: var.set(names[values.index(self.settings[key])])
            if self.settings[key] in values else None)

    def switch(self, parent, key, label, hint=''):
        row = tk.Frame(parent, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=4)
        tk.Label(row, text=label, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL, anchor='w').pack(
            side='left')
        sw = T.Switch(row, self.settings[key], lambda v: self.set_opt(key, v))
        sw.pack(side='right')
        self.mirrors.setdefault(key, []).append(lambda: sw.set(self.settings[key]))
        if hint:
            tk.Label(parent, text=hint, font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL,
                     wraplength=640, justify='left').pack(anchor='w', padx=18, pady=(0, 4))

    def pad(self, box):
        tk.Frame(box, bg=T.PANEL, height=10).pack()

    # ---- pages ----------------------------------------------------------------------------------
    def build_play(self):
        """The cover on the left, and on the right, as in the SotC-PC launcher, the quick
        settings and the ready check."""
        page = self.page('play', None)
        side = tk.Frame(page, bg=T.PANEL, highlightthickness=1, highlightbackground=T.BORDER,
                        width=360)
        side.pack(side='right', fill='y', padx=(0, 30), pady=26)
        side.pack_propagate(False)
        left = tk.Frame(page, bg=T.NIGHT)
        left.pack(side='left', fill='both', expand=True, padx=(30, 18), pady=26)
        # Ready check under the cover.
        check_box = tk.Frame(left, bg=T.PANEL, highlightthickness=1,
                             highlightbackground=T.BORDER)
        check_box.pack(side='bottom', fill='x', pady=(14, 0))
        tk.Label(check_box, text=_('Verificação').upper(), font=(T.HEAD_BOLD, 12), fg=T.GOLD,
                 bg=T.PANEL).pack(anchor='w', padx=18, pady=(12, 4))
        self.checks = tk.Frame(check_box, bg=T.PANEL)
        self.checks.pack(fill='x', padx=18, pady=(0, 12))
        self.hero = tk.Canvas(left, bg=T.NIGHT, highlightthickness=0)
        self.hero.pack(side='top', fill='both', expand=True)
        self.hero.bind('<Configure>', lambda e: self.draw_hero())

        tk.Label(side, text=_('Ajustes rápidos').upper(), font=(T.HEAD_BOLD, 12), fg=T.GOLD,
                 bg=T.PANEL).pack(anchor='w', padx=18, pady=(16, 6))
        self.combo(side, 'resolution', _('Resolução'), RESOLUTIONS, width=15)
        self.combo(side, 'filter', _('Filtro de escala'), FILTERS, width=15)
        self.combo(side, 'vsync', 'VSync', VSYNC, width=15)
        self.combo(side, 'mode', _('Modo do console'), MODE, width=15)
        self.combo(side, 'game_language', _('Idioma do jogo'), GAME_LANGUAGES, width=19)
        self.switch(side, 'fullscreen', _('Tela cheia'))
        self.switch(side, 'gpl', _('Shaders sem travadas (GPL)'))
        self.switch(side, 'precache', _('Pré-compilar antes de jogar'))
        self.combo(side, 'aa', _('Anti-serrilhado'), AA, width=15)
        self.combo(side, 'aspect', _('Proporção'), ASPECT, width=15)

    def refresh_checks(self):
        if not hasattr(self, 'checks'):
            return
        for w in self.checks.winfo_children():
            w.destroy()
        title_dir = USER / 'cache' / 'shader' / TITLE_ID.lower()
        warm = (title_dir / 'warm-compute.done').exists()
        gfx = (title_dir / 'warm-graphics.done').exists()
        items = [
            (Path(self.settings['game']).exists(), _('Jogo encontrado'), _('Jogo não encontrado')),
            (CORE.exists(), _('Núcleo do jogo (eden-cli) presente'), _('eden-cli.exe ausente')),
            (warm and (gfx or not self.settings['gpl']), _('Shaders prontos'),
             _('Shaders serão preparados na 1ª abertura')),
        ]
        for ok, good, bad in items:
            row = tk.Frame(self.checks, bg=T.PANEL)
            row.pack(fill='x', pady=2)
            tk.Label(row, text='' if ok else '', font=(T.ICONS, 10),
                     fg=T.GREEN if ok else T.GOLD, bg=T.PANEL).pack(side='left', padx=(0, 8))
            tk.Label(row, text=good if ok else bad, font=(T.BODY, 10),
                     fg=T.TEXT if ok else T.SILVER, bg=T.PANEL, wraplength=600,
                     justify='left').pack(side='left')

    def draw_hero(self):
        c = self.hero
        w, h = max(c.winfo_width(), 200), max(c.winfo_height(), 200)
        c.delete('all')
        tkimg = ImageTk.PhotoImage(T.hero_card((w, h), cover_path(), SHIELD))
        self.hero_img = tkimg
        c.create_image(0, 0, image=tkimg, anchor='nw')
        name = self.game_title or Path(self.settings['game']).stem
        size = 24 if w > 700 else 20
        c.create_text(30, h - 46, text=name, anchor='sw', font=(T.HEAD_BOLD, size),
                      fill=T.TEXT, width=w - 60)
        c.create_text(30, h - 28, text=Path(self.settings['game']).name, anchor='w',
                      font=(T.BODY, 9), fill=T.MUTED)

    def load_title(self):
        title = export_title(self.settings['game'])

        def done():
            self.game_title = title
            try:
                self.draw_hero()
            except tk.TclError:
                pass
        self.after(0, done)

    def build_game(self):
        page = self.page('game', _('Jogo'))
        box = self.card(page, _('Arquivo'))
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(0, 12))
        tk.Label(row, text=self.settings['game'], font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL,
                 wraplength=560, justify='left').pack(side='left')
        T.Button(row, _('Trocar jogo'), self.show_setup, small=True).pack(side='right')
        box = self.card(page, _('Shaders'))
        self.switch(box, 'gpl', _('Shaders sem travadas (GPL)'),
                    _('Monta os shaders em partes e liga na hora: bem menos engasgo na 1ª vez.'))
        self.switch(box, 'precache', _('Pré-compilar antes de jogar'),
                    _('Na 1ª abertura (ou driver novo) prepara os shaders do jogo com tela de '
                      'carregamento. Esc pula.'))
        self.pad(box)
        box = self.card(page, _('Áudio'))
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(4, 14))
        tk.Label(row, text=_('Volume'), font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL).pack(
            side='left')
        ttk.Scale(row, from_=0, to=100, value=self.settings['volume'],
                  style='Zoot.Horizontal.TScale',
                  command=lambda v: self.set_opt('volume', int(float(v)))).pack(
            side='right', fill='x', expand=True, padx=(24, 0))

    def build_graphics(self):
        page = self.page('graphics', _('Imagem e FPS'))
        box = self.card(page, _('Imagem'))
        self.combo(box, 'resolution', _('Resolução'), RESOLUTIONS)
        self.combo(box, 'filter', _('Filtro de escala'), FILTERS)
        self.combo(box, 'aa', _('Anti-serrilhado'), AA)
        self.combo(box, 'aspect', _('Proporção'), ASPECT)
        self.pad(box)
        box = self.card(page, _('Tela'))
        self.switch(box, 'fullscreen', _('Tela cheia'))
        self.combo(box, 'vsync', 'VSync', VSYNC)
        self.combo(box, 'mode', _('Modo do console'), MODE)
        self.pad(box)

    def build_language(self):
        page = self.page('language', _('Idioma'))
        box = self.card(page, _('Idioma do jogo'))
        self.combo(box, 'game_language', _('Idioma do jogo'), GAME_LANGUAGES, width=26)
        tk.Label(box, text=_('O jogo usa o idioma do console. A região é ajustada sozinha.') +
                 '\n' + _('Se o jogo não tiver o idioma escolhido, ele usa o inglês.'),
                 font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL, justify='left').pack(
            anchor='w', padx=18, pady=(2, 12))
        box = self.card(page, _('Idioma do lançador'))
        self.combo(box, 'launcher_language', _('Idioma do lançador'), I18N.LANGUAGES,
                   on_change=self.change_launcher_language, width=26)
        self.pad(box)

    def change_launcher_language(self):
        I18N.LANG = self.settings['launcher_language']
        T.load_fonts(self, I18N.LANG)
        self.style = T.apply(self)
        self.after(10, lambda: (self.rebuild(), self.show('language')))

    def build_saves(self):
        page = self.page('saves', _('Saves'))
        box = self.card(page, _('Pastas de save'))
        self.saves_list = tk.Frame(box, bg=T.PANEL)
        self.saves_list.pack(fill='x', padx=18, pady=(0, 8))
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(4, 14))
        T.Button(row, _('Abrir pasta'), self.open_saves, small=True).pack(side='left')
        T.Button(row, _('Fazer backup'), self.backup_saves, small=True).pack(side='left',
                                                                              padx=8)

    def save_dirs(self):
        if not SAVES.exists():
            return []
        return sorted({p for p in SAVES.rglob(TITLE_ID) if p.is_dir()} |
                      {p for p in SAVES.rglob(TITLE_ID.lower()) if p.is_dir()})

    def load_saves(self):
        for w in self.saves_list.winfo_children():
            w.destroy()
        slots = []
        for d in self.save_dirs():
            for slot in sorted(d.glob('slot_*')):
                files = list(slot.glob('*'))
                if files:
                    stamp = max(f.stat().st_mtime for f in files)
                    slots.append((slot.name, stamp))
        if not slots:
            tk.Label(self.saves_list, text=_('Nenhum save ainda: jogue uma vez.'),
                     font=(T.BODY, 10), fg=T.MUTED, bg=T.PANEL).pack(anchor='w')
            return
        for name, stamp in slots:
            row = tk.Frame(self.saves_list, bg=T.PANEL)
            row.pack(fill='x', pady=1)
            tk.Label(row, text=T.ICON['saves'], font=(T.ICONS, 11), fg=T.GOLD, bg=T.PANEL).pack(
                side='left', padx=(0, 10))
            tk.Label(row, text=name.replace('slot_', 'Slot '), font=(T.BODY_SEMI, 10),
                     fg=T.TEXT, bg=T.PANEL).pack(side='left')
            tk.Label(row, text=time.strftime('%d/%m/%Y %H:%M', time.localtime(stamp)),
                     font=(T.BODY, 10), fg=T.MUTED, bg=T.PANEL).pack(side='right')

    def backup_saves(self):
        dirs = self.save_dirs()
        if not dirs:
            messagebox.showinfo(APP, _('Nenhum save ainda: jogue uma vez.'))
            return
        out = ZDIR / 'backups'
        out.mkdir(parents=True, exist_ok=True)
        zpath = out / time.strftime('saves-%Y%m%d-%H%M%S.zip')
        with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
            for d in dirs:
                for f in d.rglob('*'):
                    if f.is_file():
                        z.write(f, f.relative_to(SAVES))
        messagebox.showinfo(APP, f"{_('Backup salvo em:')}\n{zpath}")

    def open_saves(self):
        dirs = self.save_dirs()
        target = dirs[0] if dirs else SAVES
        target.mkdir(parents=True, exist_ok=True)
        os.startfile(target)

    def build_help(self):
        page = self.page('help', _('Ajuda'))
        for title, text in I18N.HELP.get(I18N.LANG, I18N.HELP['pt']):
            box = self.card(page, title)
            tk.Label(box, text=text, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL, wraplength=700,
                     justify='left').pack(anchor='w', padx=18, pady=(0, 14))

    def build_log(self):
        page = self.page('log', _('Registro do jogo'))
        row = tk.Frame(page, bg=T.NIGHT)
        row.pack(fill='x', padx=30, pady=(0, 8))
        T.Button(row, _('Atualizar'), self.load_log, small=True).pack(side='left')
        T.Button(row, _('Abrir arquivo'), self.open_log, small=True).pack(side='left', padx=8)
        self.log_text = tk.Text(page, bg=T.PANEL, fg=T.TEXT, insertbackground=T.TEXT,
                                font=('Consolas', 9), relief='flat', highlightthickness=1,
                                highlightbackground=T.BORDER, wrap='none')
        self.log_text.pack(fill='both', expand=True, padx=30, pady=(0, 20))

    def load_log(self):
        self.log_text.config(state='normal')
        self.log_text.delete('1.0', 'end')
        try:
            lines = LOG.read_text(encoding='utf-8', errors='replace').splitlines()[-400:]
            self.log_text.insert('end', '\n'.join(lines))
            self.log_text.see('end')
        except OSError:
            self.log_text.insert('end', _('Ainda não há registro: jogue uma vez.'))
        self.log_text.config(state='disabled')

    def open_log(self):
        if LOG.exists():
            os.startfile(LOG)
        else:
            messagebox.showinfo(APP, _('Ainda não há registro: jogue uma vez.'))

    def reset_setup(self):
        self.settings['game'] = ''
        save_settings(self.settings)
        self.rebuild()

    # ---- options and status ----------------------------------------------------------------------
    def set_opt(self, key, value):
        self.settings[key] = value
        save_settings(self.settings)
        for mirror in self.mirrors.get(key, []):
            try:
                mirror()
            except tk.TclError:
                pass
        if key in ('gpl', 'precache'):
            self.refresh_status()

    def refresh_status(self):
        title_dir = USER / 'cache' / 'shader' / TITLE_ID.lower()
        warm = (title_dir / 'warm-compute.done').exists()
        gfx = (title_dir / 'warm-graphics.done').exists()
        if not CORE.exists():
            text = _('eden-cli.exe não encontrado em {path}', path=BASE)
        elif self.proc and self.proc.poll() is None:
            text = _('Jogo em execução...')
        elif warm and (gfx or not self.settings['gpl']):
            text = _('Shaders prontos.')
        else:
            text = _('1ª abertura: os shaders serão preparados (alguns minutos).')
        self.status.config(text=text)
        self.refresh_checks()

    # ---- play -----------------------------------------------------------------------------------
    def apply_ini(self):
        s = self.settings
        lang = s['game_language']
        write_ini({
            'resolution_setup': s['resolution'], 'scaling_filter': s['filter'],
            'anti_aliasing': s['aa'], 'aspect_ratio': s['aspect'], 'use_vsync': s['vsync'],
            'use_docked_mode': s['mode'], 'fullscreen_mode': 0,
            'use_graphics_pipeline_library': 'true' if s['gpl'] else 'false',
            'precache_shaders': 2 if s['precache'] else 0,
            'use_disk_shader_cache': 'true', 'volume': s['volume'],
            'language_index': lang, 'region_index': REGION_OF.get(lang, 1),
        })

    def play(self):
        if self.proc and self.proc.poll() is None:
            return
        if not CORE.exists():
            messagebox.showerror(APP, _('eden-cli.exe não encontrado em {path}', path=BASE))
            return
        self.apply_ini()
        env = dict(os.environ)
        env['ZOOT_PORT_VERSION'] = VERSION
        env['ZOOT_LANG'] = I18N.LANG  # the loading screen speaks the launcher's language
        if self.settings['gpl']:
            env['EDEN_NX2_GPL_OPTIMIZE'] = '1'
        args = [str(CORE), '-g', self.settings['game']]
        if self.settings['fullscreen']:
            args.append('-f')
        try:
            # Explicit handles: under pythonw the launcher has no console, and inheriting its
            # missing standard handles can make the start fail.
            self.proc = subprocess.Popen(args, cwd=BASE, env=env, stdin=subprocess.DEVNULL,
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                         creationflags=NO_WINDOW)
        except OSError as e:
            messagebox.showerror(APP, f"{_('Não foi possível iniciar o jogo:')}\n{e}")
            return
        # The launcher stays on screen while the game runs (the player asked for it).
        self.play_btn.config(text=f"●  {_('Jogando...')}")
        self.refresh_status()
        self.after(1000, self.watch)

    def stop(self):
        """Closes the running game (asks first: progress since the last save is lost)."""
        if not self.proc or self.proc.poll() is not None:
            return
        if not messagebox.askyesno(APP, _('Fechar o jogo agora? O progresso desde o último save '
                                          'será perdido.')):
            return
        self.stopping = True
        try:
            self.proc.terminate()
        except OSError:
            pass

    def watch(self):
        code = self.proc.poll()
        if code is None:
            self.after(1000, self.watch)
            return
        self.play_btn.config(text=f"▶  {_('JOGAR')}")
        self.refresh_status()
        code &= 0xFFFFFFFF
        stopped, self.stopping = getattr(self, 'stopping', False), False
        if not stopped and code not in (0, 0xC000013A):
            tail = ''
            try:
                tail = '\n'.join(LOG.read_text(encoding='utf-8', errors='replace')
                                 .splitlines()[-12:])
            except OSError:
                pass
            messagebox.showwarning(APP, f"{_('O jogo fechou com o código {code}.', code=hex(code))}"
                                        f"\n\n{_('Últimas linhas do registro:')}\n{tail[-1500:]}")


def main():
    app = Launcher()
    if '--run' in sys.argv:  # start the game straight away (shortcuts)
        app.after(300, app.play)
    app.mainloop()


if __name__ == '__main__':
    main()
