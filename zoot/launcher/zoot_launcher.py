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
import zoot_controls as C  # noqa: E402
import zoot_theme as T  # noqa: E402
import zoot_update as U  # noqa: E402
from zoot_i18n import _  # noqa: E402

APP = 'ZooT-PC'
VERSION = '0.11.1'
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
# FPS modes, the same ones F7 cycles in the game: 60, 30, or the console's vsync raised to 240
# (Ocarina's built-in Dynamic FPS keeps the game at its speed at any rate).
FPS_MODES = [('60 FPS', '60'), ('30 FPS', '30'), ('Ilimitado', 'unlimited')]
SHARPNESS = [(f'{v}%', v) for v in range(0, 201, 25)]
MIN_RAM_GB, MIN_VRAM_GB = 16, 4
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
       ('graphics', 'Imagem e FPS', 'display'), ('controls', 'Controles', 'controls'),
       ('language', 'Idioma', 'language'),
       ('saves', 'Saves', 'saves'), ('help', 'Ajuda', 'help'), ('log', 'Registro', 'log')]

DEFAULTS = {
    'game': '', 'resolution': 3, 'filter': 15, 'aa': 1, 'aspect': 0, 'vsync': 2, 'mode': 1,
    'fullscreen': False, 'gpl': True, 'precache': True, 'volume': 100,
    'game_language': 17, 'launcher_language': 'pt', 'fps_mode': '60', 'sharpness': 25,
    'auto_update': True, 'last_update_check': 0,
}


def hardware():
    """The graphics cards (name, memory in bytes, from the display class in the registry, where
    the driver writes the real size; WMI caps it at 4 GB) and the installed RAM in bytes."""
    import ctypes
    import winreg
    gpus = []
    base = r'SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}'
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, base) as cls:
            for i in range(64):
                try:
                    sub = winreg.EnumKey(cls, i)
                except OSError:
                    break
                if not sub.isdigit():
                    continue
                try:
                    with winreg.OpenKey(cls, sub) as k:
                        name = winreg.QueryValueEx(k, 'DriverDesc')[0]
                        try:
                            mem = int(winreg.QueryValueEx(k, 'HardwareInformation.qwMemorySize')[0])
                        except OSError:
                            raw = winreg.QueryValueEx(k, 'HardwareInformation.MemorySize')[0]
                            mem = int.from_bytes(raw, 'little') if isinstance(raw, bytes) else int(raw)
                        if 'Basic Display' not in name and 'Remote' not in name:
                            gpus.append((name, mem))
                except OSError:
                    continue
    except OSError:
        pass

    class MemStatus(ctypes.Structure):
        _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong),
                    ('ullTotalPhys', ctypes.c_ulonglong), ('ullAvailPhys', ctypes.c_ulonglong),
                    ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                    ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong),
                    ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
    status = MemStatus()
    status.dwLength = ctypes.sizeof(MemStatus)
    ram = 0
    try:
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            ram = status.ullTotalPhys
    except (AttributeError, OSError):
        pass
    gpus.sort(key=lambda g: -g[1])
    return gpus, ram


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
        header = '[Controls]' if key.startswith('player_') else sections.get(key, '[Renderer]')
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
        U.cleanup(ZDIR / 'update')  # an earlier update finished: this is the new launcher
        self.rebuild()
        if self.settings.get('auto_update', True):
            self.after(2500, lambda: self.check_updates(False))

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
                              (_('Procurar atualização'), lambda: self.check_updates(True)),
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
        # Report, as in the SotC-PC launcher: a .zip to attach to a GitHub issue.
        T.Button(bar, _('Relatório'), self.save_report).pack(side='right', padx=(0, 10), pady=10)
        self.status = tk.Label(bar, text='', bg=T.SIDE, fg=T.MUTED, font=(T.BODY, 10),
                               anchor='w', justify='left')
        self.status.pack(side='left', fill='x', expand=True, padx=(24, 12))
        # A long status wraps instead of running under the buttons.
        self.status.bind('<Configure>', lambda e: self.status.configure(
            wraplength=max(120, e.width - 4)))
        self.content = tk.Frame(right, bg=T.NIGHT)
        self.content.pack(fill='both', expand=True)

        self.pages = {}
        self.mirrors = {}
        self.bind_all('<MouseWheel>', self.wheel)
        self.build_play()
        self.build_game()
        self.build_graphics()
        self.build_controls()
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

    def wheel(self, event):
        """The mouse wheel scrolls the current settings page (not an open list or the log)."""
        page = self.pages.get(self.current)
        widget_class = event.widget.winfo_class() if hasattr(event.widget, 'winfo_class') else ''
        if hasattr(page, 'scroll') and widget_class not in ('Text', 'Listbox', 'TCombobox') \
                and 'popdown' not in str(event.widget):
            page.scroll(int(-event.delta / 120))

    def page(self, name, title, scroll=True):
        """A page of the content area. Settings pages scroll with the mouse wheel, so new
        options never fall off the window; Play and Log fill the window instead."""
        if scroll:
            outer = tk.Frame(self.content, bg=T.NIGHT)
            canvas = tk.Canvas(outer, bg=T.NIGHT, highlightthickness=0, bd=0)
            canvas.pack(fill='both', expand=True)
            frame = tk.Frame(canvas, bg=T.NIGHT)
            window = canvas.create_window(0, 0, window=frame, anchor='nw')
            frame.bind('<Configure>',
                       lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
            canvas.bind('<Configure>', lambda e: canvas.itemconfigure(window, width=e.width))

            def scroll_by(units):
                if frame.winfo_height() > canvas.winfo_height():
                    canvas.yview_scroll(units, 'units')
            outer.scroll = scroll_by
            self.pages[name] = outer
        else:
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
        page = self.page('play', None, scroll=False)
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
        self.combo(side, 'fps_mode', _('Modo de FPS'), FPS_MODES, width=15)
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
        # (state, text): 'ok' green check, 'info' gold note, 'bad' red warning.
        items = [
            ('ok', _('Jogo encontrado')) if Path(self.settings['game']).exists()
            else ('bad', _('Jogo não encontrado')),
            ('ok', _('Núcleo do jogo (eden-cli) presente')) if CORE.exists()
            else ('bad', _('eden-cli.exe ausente')),
            ('ok', _('Shaders prontos')) if warm and (gfx or not self.settings['gpl'])
            else ('info', _('Shaders serão preparados na 1ª abertura')),
        ]
        if not hasattr(self, 'hw'):
            try:
                self.hw = hardware()
            except Exception:  # noqa: BLE001 - the check must never break the launcher
                self.hw = ([], 0)
        gpus, ram = self.hw
        gb = lambda b: b / 1024 ** 3
        if gpus:
            name, vram = gpus[0]
            size = f'{gb(vram):.0f} GB' if vram else '?'
            if vram and gb(vram) + 0.25 < MIN_VRAM_GB:
                items.append(('bad', _('Placa de vídeo: {name} · {size} (mínimo {min} GB)',
                                       name=name, size=size, min=MIN_VRAM_GB)))
            else:
                items.append(('ok', _('Placa de vídeo: {name} · {size}', name=name, size=size)))
        else:
            items.append(('bad', _('Placa de vídeo não encontrada')))
        if ram:
            if gb(ram) + 0.5 < MIN_RAM_GB:
                items.append(('bad', _('Memória RAM: {size} GB (mínimo {min} GB)',
                                       size=f'{gb(ram):.0f}', min=MIN_RAM_GB)))
            else:
                items.append(('ok', _('Memória RAM: {size} GB', size=f'{gb(ram):.0f}')))
        icons = {'ok': ('', T.GREEN, T.TEXT), 'info': ('', T.GOLD, T.SILVER),
                 'bad': ('', T.RED, T.RED)}
        for state, text in items:
            icon, icon_fg, text_fg = icons[state]
            row = tk.Frame(self.checks, bg=T.PANEL)
            row.pack(fill='x', pady=2)
            tk.Label(row, text=icon, font=(T.ICONS, 10), fg=icon_fg, bg=T.PANEL).pack(
                side='left', anchor='n', padx=(0, 8), pady=(2, 0))
            label = tk.Label(row, text=text, font=(T.BODY, 10), fg=text_fg, bg=T.PANEL,
                             justify='left', anchor='w')
            label.pack(side='left', fill='x', expand=True)
            # Wrap to the card's width, so a long card name never runs out of it.
            label.bind('<Configure>', lambda e, l=label: l.configure(
                wraplength=max(120, e.width - 4)))

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
                      fill=T.GOLD, width=w - 60)
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
        # The player's own cover for the game card and the loading screen (kept in user\zoot,
        # never shipped).
        box = self.card(page, _('Capa do jogo'))
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(0, 12))
        tk.Label(row, text=_('Uma imagem sua para o cartão do jogo e a tela de carregamento.'),
                 font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL, wraplength=420,
                 justify='left').pack(side='left')
        T.Button(row, _('Remover'), self.remove_cover, small=True).pack(side='right')
        T.Button(row, _('Escolher capa...'), self.choose_cover).pack(side='right', padx=8)
        box = self.card(page, _('Atualizações'))
        self.switch(box, 'auto_update', _('Procurar atualizações ao abrir'),
                    _('Consulta as versões do ZooT-PC no GitHub uma vez por dia.'))
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(2, 12))
        T.Button(row, _('Procurar atualização'), lambda: self.check_updates(True)).pack(
            side='left')
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
        self.combo(box, 'sharpness', _('Nitidez (FSR)'), SHARPNESS)
        self.combo(box, 'aa', _('Anti-serrilhado'), AA)
        self.combo(box, 'aspect', _('Proporção'), ASPECT)
        self.pad(box)
        box = self.card(page, 'FPS')
        self.combo(box, 'fps_mode', _('Modo de FPS'), FPS_MODES)
        tk.Label(box, text=_('No jogo: F7 alterna 60 → 30 → ilimitado na hora.'),
                 font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL).pack(anchor='w', padx=18,
                                                                pady=(0, 10))
        box = self.card(page, _('Tela'))
        self.switch(box, 'fullscreen', _('Tela cheia'))
        self.combo(box, 'vsync', 'VSync', VSYNC)
        self.combo(box, 'mode', _('Modo do console'), MODE)
        self.pad(box)

    # ---- controls -------------------------------------------------------------------------------
    def build_controls(self):
        """Player 1: the device (keyboard or a controller eden-cli finds), automatic mapping,
        vibration, and each console button with what it is bound to and a Change button. The
        bindings live in sdl2-config.ini ([Controls]), which the game reads when it starts."""
        page = self.page('controls', _('Controles'))
        self.bindings = C.read_bindings(INI)
        self.pads = []

        box = self.card(page, _('Dispositivo'))
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(0, 6))
        self.dev_var = tk.StringVar(value=_('Teclado'))
        self.dev_box = ttk.Combobox(row, textvariable=self.dev_var, state='readonly', width=30,
                                    style='Zoot.TCombobox', values=[_('Teclado')])
        self.dev_box.pack(side='left')
        T.Button(row, _('Mapear automaticamente'), self.automap).pack(side='right')
        T.Button(row, _('Procurar controles'), self.find_pads).pack(side='right', padx=8)
        row = tk.Frame(box, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(2, 10))
        self.pad_status = tk.Label(row, text='', font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL)
        self.pad_status.pack(side='left')
        vib = self.bindings.get('vibration_enabled', 'true') != 'false'
        strength = self.bindings.get('vibration_strength', '100')
        tk.Label(row, text=_('Vibração'), font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL).pack(
            side='right', padx=(10, 0))
        T.Switch(row, vib, lambda v: write_ini(
            {'player_0_vibration_enabled': 'true' if v else 'false'})).pack(side='right')
        sv = tk.StringVar(value=f'{strength}%')
        sbox = ttk.Combobox(row, values=[f'{v}%' for v in (25, 50, 75, 100)], textvariable=sv,
                            state='readonly', width=6, style='Zoot.TCombobox')
        sbox.pack(side='right', padx=(0, 12))
        sbox.bind('<<ComboboxSelected>>', lambda e: write_ini(
            {'player_0_vibration_strength': sv.get().rstrip('%')}))

        box = self.card(page, _('Botões'))
        grid = tk.Frame(box, bg=T.PANEL)
        grid.pack(fill='x', padx=18, pady=(0, 10))
        self.bind_labels = {}
        entries = [(k, n, False) for k, n in C.BUTTONS] + [(k, n, True) for k, n in C.STICKS]
        half = (len(entries) + 1) // 2
        for i, (key, name, stick) in enumerate(entries):
            col, r = divmod(i, half)
            cell = tk.Frame(grid, bg=T.PANEL)
            cell.grid(row=r, column=col, sticky='ew', padx=(0 if col == 0 else 24, 0), pady=1)
            grid.columnconfigure(col, weight=1)
            tk.Label(cell, text=_(name), font=(T.BODY_SEMI, 10), fg=T.GOLD, bg=T.PANEL,
                     width=6, anchor='w').pack(side='left')
            lbl = tk.Label(cell, text=C.describe(self.bindings.get(key), _), font=(T.BODY, 9),
                           fg=T.TEXT, bg=T.PANEL, anchor='w')
            lbl.pack(side='left', fill='x', expand=True)
            self.bind_labels[key] = lbl
            T.Button(cell, _('Mudar'), lambda k=key, n=name, s=stick: self.change_binding(k, n, s),
                     small=True).pack(side='right')
        tk.Label(page, text=_('Os controles valem na próxima abertura do jogo.'),
                 font=(T.BODY, 9), fg=T.MUTED, bg=T.NIGHT).pack(anchor='w', padx=30)
        self.find_pads(quiet=True)

    def current_pad(self):
        """The chosen controller (a dict from eden-cli) or None for the keyboard."""
        name = self.dev_var.get()
        return next((p for p in self.pads if p.get('name') == name), None)

    def find_pads(self, quiet=False):
        if not CORE.exists():
            return
        self.pad_status.config(text=_('Procurando controles...'))

        def work():
            pads = C.list_pads(CORE, BASE)

            def done():
                try:
                    self.pads = pads
                    self.dev_box.configure(values=[_('Teclado')] + [p['name'] for p in pads])
                    # Show the device the bindings already use.
                    guid = C.parse(self.bindings.get('button_a', '')).get('guid')
                    match = next((p for p in pads if C.parse(p['params']).get('guid') == guid),
                                 None)
                    if match:
                        self.dev_var.set(match['name'])
                    elif pads and not quiet:
                        self.dev_var.set(pads[0]['name'])
                    self.pad_status.config(
                        text=_('{n} controle(s) encontrado(s).', n=len(pads)) if pads
                        else _('Nenhum controle encontrado.'))
                except tk.TclError:
                    pass
            self.after(0, done)
        threading.Thread(target=work, daemon=True).start()

    def save_bindings(self, changes):
        self.bindings.update(changes)
        values = C.ini_values(changes)
        values.update({'player_0_connected': 'true', 'player_0_type': 0})
        write_ini(values)
        for key in changes:
            if key in self.bind_labels:
                self.bind_labels[key].config(text=C.describe(changes[key], _))

    def automap(self):
        pad = self.current_pad()
        if pad is None:
            self.save_bindings(C.default_keyboard())
            self.pad_status.config(text=_('Teclado: teclas padrão restauradas.'))
            return
        self.pad_status.config(text=_('Mapeando {name}...', name=pad['name']))

        def work():
            mapping = C.automap(CORE, BASE, pad['params'])

            def done():
                if mapping:
                    self.save_bindings({k: v for k, v in mapping.items()
                                        if k in self.bind_labels})
                    self.pad_status.config(text=_('Mapeado: {name}', name=pad['name']))
                else:
                    self.pad_status.config(text=_('Não foi possível mapear o controle.'))
            self.after(0, done)
        threading.Thread(target=work, daemon=True).start()

    def change_binding(self, key, name, stick):
        pad = self.current_pad()
        dlg = tk.Toplevel(self)
        dlg.title(APP)
        dlg.configure(bg=T.PANEL, highlightthickness=1, highlightbackground=T.GOLD)
        dlg.resizable(False, False)
        dlg.transient(self)
        w, h = 460, 190
        dlg.geometry(f'{w}x{h}+{self.winfo_rootx() + (self.winfo_width() - w) // 2}'
                     f'+{self.winfo_rooty() + (self.winfo_height() - h) // 2}')
        tk.Label(dlg, text=_(name), font=(T.HEAD_BOLD, 18), fg=T.GOLD, bg=T.PANEL).pack(
            pady=(20, 6))
        msg = tk.Label(dlg, text='', font=(T.BODY, 11), fg=T.TEXT, bg=T.PANEL, wraplength=420)
        msg.pack()
        hint = tk.Label(dlg, text=_('Esc cancela'), font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL)
        hint.pack(pady=(12, 0))
        dlg.grab_set()
        dlg.focus_force()

        if pad is None:  # keyboard: Tk captures the key itself
            directions = [_('cima'), _('baixo'), _('esquerda'), _('direita')] if stick else []
            keys = []

            def prompt():
                if stick:
                    msg.config(text=_('Aperte a tecla para {dir}', dir=directions[len(keys)]))
                else:
                    msg.config(text=_('Aperte a tecla para {name}', name=_(name)))
            prompt()

            def on_key(event):
                if event.keysym == 'Escape':
                    dlg.destroy()
                    return
                code = C.scancode_from_key(event)
                if code is None:
                    return
                if code in C.RESERVED:
                    hint.config(text=_('F1, F7 e F11 são atalhos do ZooT.'), fg=T.RED)
                    return
                if not stick:
                    self.save_bindings({key: C.key_param(code)})
                    dlg.destroy()
                    return
                keys.append(code)
                if len(keys) == 4:
                    self.save_bindings({key: C.stick_from_keys(*keys)})
                    dlg.destroy()
                else:
                    prompt()
            dlg.bind('<KeyPress>', on_key)
            return

        # Controller: eden-cli waits for the next button or stick on the pad.
        seconds = 10
        proc = C.capture_pad(CORE, BASE, stick, seconds)
        start = time.monotonic()
        text = (_('Mova o analógico para {name}', name=_(name)) if stick
                else _('Aperte o botão no controle para {name}', name=_(name)))

        def cancel(_e=None):
            try:
                proc.kill()
            except OSError:
                pass
            dlg.destroy()
        dlg.bind('<Escape>', cancel)
        dlg.protocol('WM_DELETE_WINDOW', cancel)

        def poll():
            if not dlg.winfo_exists():
                return
            if proc.poll() is None:
                left = max(0, seconds - int(time.monotonic() - start))
                msg.config(text=f'{text}  ({left}s)')
                dlg.after(150, poll)
                return
            params = C.read_capture(proc)
            if params:
                self.save_bindings({key: params})
                dlg.destroy()
            else:
                msg.config(text=_('Nada foi apertado.'))
                dlg.after(1500, lambda: dlg.winfo_exists() and dlg.destroy())
        poll()

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
        box = self.card(page, _('Atalhos no jogo'))
        for key, text in (('F1', _('Menu ZooT: vídeo, FPS, tela, volume e resolução na hora')),
                          ('F7', _('Modo de FPS: 60 → 30 → ilimitado')),
                          ('F11', _('Tela cheia / janela'))):
            row = tk.Frame(box, bg=T.PANEL)
            row.pack(fill='x', padx=18, pady=2)
            tk.Label(row, text=key, font=(T.BODY_SEMI, 10), fg=T.GOLD, bg=T.PANEL, width=5,
                     anchor='w').pack(side='left')
            tk.Label(row, text=text, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL).pack(side='left')
        self.pad(box)
        for title, text in I18N.HELP.get(I18N.LANG, I18N.HELP['pt']):
            box = self.card(page, title)
            tk.Label(box, text=text, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL, wraplength=700,
                     justify='left').pack(anchor='w', padx=18, pady=(0, 14))

    def build_log(self):
        page = self.page('log', _('Registro do jogo'), scroll=False)
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

    # ---- report ---------------------------------------------------------------------------------
    def save_report(self):
        """A .zip to attach to a GitHub issue: a summary of this PC, the launcher settings, the
        game's config and the last sessions' logs. Personal paths are hidden: the Windows user
        folder becomes %USERPROFILE% and the game shows only its file name."""
        import platform
        default = Path.home() / 'Desktop' / f'ZooT-relatorio-{time.strftime("%Y-%m-%d_%H-%M")}.zip'
        target = filedialog.asksaveasfilename(
            title=_('Salvar relatório'), defaultextension='.zip', filetypes=[('ZIP', '*.zip')],
            initialdir=str(default.parent if default.parent.is_dir() else Path.home()),
            initialfile=default.name)
        if not target:
            return
        home = str(Path.home())

        def private(text):
            return text.replace(home, '%USERPROFILE%').replace(home.replace('\\', '/'),
                                                                 '%USERPROFILE%')
        if not hasattr(self, 'hw'):
            try:
                self.hw = hardware()
            except Exception:  # noqa: BLE001
                self.hw = ([], 0)
        gpus, ram = self.hw
        game = Path(self.settings.get('game', ''))
        summary = '\n'.join([
            f'ZooT-PC launcher v{VERSION}',
            f'Windows {platform.version()} ({platform.machine()})',
            f'CPU: {platform.processor()}, {os.cpu_count()} threads',
            'GPU: ' + ('; '.join(f'{n} ({m / 1024 ** 3:.0f} GB)' for n, m in gpus) or 'not found'),
            f'RAM: {ram / 1024 ** 3:.0f} GB',
            f'Game file: {game.name or "not set"}',
            f'Shaders: {self.status.cget("text")}',
        ])
        settings = dict(self.settings, game=game.name)
        files = [(LOG, 'eden_log.txt'), (LOG.with_name('eden_log.txt.old.txt'), 'eden_log.old.txt'),
                 (ZDIR / 'launcher.log', 'launcher.log'), (INI, 'sdl2-config.ini')]
        try:
            with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('report.txt', summary + '\n')
                archive.writestr('launcher_settings.json',
                                 json.dumps(settings, indent=2, ensure_ascii=False))
                for path, name in files:
                    if path.is_file():
                        text = path.read_text(encoding='utf-8', errors='replace')
                        archive.writestr(name, private(text))
        except OSError as error:
            messagebox.showerror(APP, _('Não foi possível salvar o relatório: {e}', e=error))
            return
        messagebox.showinfo(APP, _('Relatório salvo em\n{path}\n\nAnexe este arquivo na issue do '
                                   'GitHub. Ele tem os registros das últimas sessões e um resumo '
                                   'do PC, sem dados pessoais.', path=target))
        try:
            os.startfile(Path(target).parent)
        except OSError:
            pass

    # ---- cover ----------------------------------------------------------------------------------
    def choose_cover(self):
        path = filedialog.askopenfilename(
            title=_('Escolher capa...'),
            filetypes=[(_('Imagens'), '*.png *.jpg *.jpeg *.webp *.bmp'), (_('Todos'), '*.*')])
        if not path:
            return
        src = Path(path)
        try:
            ZDIR.mkdir(parents=True, exist_ok=True)
            for old in ('capa.png', 'capa.jpg'):
                (ZDIR / old).unlink(missing_ok=True)
            if src.suffix.lower() in ('.png', '.jpg', '.jpeg'):
                # PNG and JPEG as they are (the loading screen reads both).
                shutil_target = ZDIR / ('capa.png' if src.suffix.lower() == '.png' else 'capa.jpg')
                shutil_target.write_bytes(src.read_bytes())
            else:
                Image.open(src).convert('RGB').save(ZDIR / 'capa.png')
        except OSError as e:
            messagebox.showerror(APP, str(e))
            return
        self.draw_hero()

    def remove_cover(self):
        for old in ('capa.png', 'capa.jpg'):
            (ZDIR / old).unlink(missing_ok=True)
        self.draw_hero()

    # ---- updates --------------------------------------------------------------------------------
    def check_updates(self, manual):
        """Looks for a newer ZooT-PC release on GitHub; automatic checks at most once a day."""
        now = time.time()
        if not manual and now - float(self.settings.get('last_update_check', 0)) < 86400:
            return
        self.settings['last_update_check'] = now
        save_settings(self.settings)
        if manual and hasattr(self, 'status'):
            self.status.config(text=_('Procurando atualização...'))

        def work():
            try:
                release = U.latest_release()
                error = None
            except Exception as e:  # noqa: BLE001 - offline, rate limit...
                release, error = None, e

            def done():
                if not self.winfo_exists():
                    return
                self.refresh_status()
                if release and U.is_newer(release['version'], VERSION):
                    self.offer_update(release)
                elif manual:
                    if error:
                        messagebox.showwarning(APP, _('Não foi possível procurar atualização:')
                                               + f'\n{error}')
                    else:
                        messagebox.showinfo(APP, _('Você já tem a versão mais recente '
                                                   '(v{v}).', v=VERSION))
            self.after(0, done)
        threading.Thread(target=work, daemon=True).start()

    def offer_update(self, release):
        notes = release['notes'].strip()
        if len(notes) > 900:
            notes = notes[:900].rsplit('\n', 1)[0] + '\n…'
        if not messagebox.askyesno(
                APP, _('Nova versão {v} disponível (você tem a v{cur}). Atualizar agora?',
                       v=release['version'], cur=VERSION) + f'\n\n{notes}'):
            return
        if self.proc and self.proc.poll() is None:
            messagebox.showinfo(APP, _('Feche o jogo antes de atualizar.'))
            return
        if not getattr(sys, 'frozen', False):
            messagebox.showinfo(APP, _('Atualização automática só funciona no ZooT.exe.')
                                + f"\n{release['page']}")
            return
        self.download_update(release)

    def download_update(self, release):
        work = ZDIR / 'update'
        dlg = tk.Toplevel(self)
        dlg.title(APP)
        dlg.configure(bg=T.PANEL, highlightthickness=1, highlightbackground=T.GOLD)
        dlg.resizable(False, False)
        dlg.transient(self)
        w, h = 460, 150
        dlg.geometry(f'{w}x{h}+{self.winfo_rootx() + (self.winfo_width() - w) // 2}'
                     f'+{self.winfo_rooty() + (self.winfo_height() - h) // 2}')
        tk.Label(dlg, text=_('Baixando {v}...', v=release['version']), font=(T.BODY_SEMI, 11),
                 fg=T.TEXT, bg=T.PANEL).pack(pady=(22, 10))
        bar = tk.Canvas(dlg, width=400, height=10, bg=T.CARD, highlightthickness=0)
        bar.pack()
        fill = bar.create_rectangle(0, 0, 0, 10, fill=T.GOLD, width=0)
        info = tk.Label(dlg, text='', font=(T.BODY, 9), fg=T.MUTED, bg=T.PANEL)
        info.pack(pady=(10, 0))
        dlg.grab_set()
        state = {'done': 0, 'total': release.get('size', 0), 'error': None, 'root': None}

        def work_thread():
            try:
                zip_path = U.download(release['url'], work / 'ZooT-PC-update.zip',
                                      lambda d, t: state.update(done=d, total=t or state['total']))
                state['root'] = U.prepare(zip_path, work)
            except Exception as e:  # noqa: BLE001
                state['error'] = e

        thread = threading.Thread(target=work_thread, daemon=True)
        thread.start()

        def poll():
            total = state['total'] or 1
            bar.coords(fill, 0, 0, 400 * min(1.0, state['done'] / total), 10)
            info.config(text=f"{state['done'] / 1048576:.1f} / {total / 1048576:.1f} MB")
            if thread.is_alive():
                dlg.after(150, poll)
                return
            if state['error'] or not state['root']:
                dlg.destroy()
                messagebox.showerror(APP, _('A atualização falhou:') + f"\n{state['error']}")
                return
            info.config(text=_('Instalando e reabrindo...'))
            U.apply_and_restart(state['root'], BASE, work, os.getpid())
            self.after(400, self.destroy)
        poll()

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
        # The welcome screen (no game chosen yet) has no status bar.
        if not hasattr(self, 'status'):
            return
        try:
            if not self.status.winfo_exists():
                return
        except tk.TclError:
            return
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
            'fsr_sharpening_slider': s['sharpness'],
            # FPS mode, as F7 sets it in the game.
            'fps_limit': 30 if s['fps_mode'] == '30' else 0,
            'nx2_frame_rate_limit': 240 if s['fps_mode'] == 'unlimited' else 60,
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
