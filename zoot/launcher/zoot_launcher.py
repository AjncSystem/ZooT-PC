# SPDX-License-Identifier: GPL-3.0-or-later
"""ZooT-PC launcher: picks the game once, shows the player's own game art (read from their copy at
runtime, never shipped), sets the options in eden-cli's sdl2-config.ini and starts the game.

Layout of a package (and of the dev build folder):
    ZooT.exe              this launcher, frozen
    eden-cli.exe          the game runner
    nx2_switches.txt
    user/                 portable data: config, saves, shader caches, logs
    user/zoot/            launcher settings, the extracted game art, the player's own icon.png
"""
import json
import os
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageDraw, ImageFilter, ImageTk

if not getattr(sys, 'frozen', False):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import zoot_theme as T  # noqa: E402

APP = 'ZooT-PC'
VERSION = '0.1.0'
TITLE_ID = '0400486024DEC000'
GAME_EXTS = ('.nup', '.nx', '.nsp', '.xci')

if getattr(sys, 'frozen', False):
    BASE = Path(sys.executable).resolve().parent
    BUNDLE = Path(getattr(sys, '_MEIPASS', BASE))
else:
    BASE = Path(os.environ.get('ZOOT_BASE', r'Z:\build\bin'))
    BUNDLE = Path(__file__).resolve().parent

CORE = BASE / 'eden-cli.exe'
USER = BASE / 'user'
ZDIR = USER / 'zoot'
SETTINGS = ZDIR / 'launcher.json'
INI = USER / 'config' / 'sdl2-config.ini'
LOG = USER / 'log' / 'eden_log.txt'
SAVES = USER / 'nand' / 'user' / 'save'
NO_WINDOW = 0x08000000  # CREATE_NO_WINDOW

# ---- options: (key in launcher.json, ini key, label, choices [(label, ini value)], default) ------
RESOLUTIONS = [('0,5x (540p)', 1), ('0,75x', 2), ('1x (1080p)', 3), ('1,25x', 4), ('1,5x', 5),
               ('2x (4K)', 6), ('3x', 7), ('4x', 8)]
FILTERS = [('Bilinear', 1), ('Bicúbico', 2), ('ScaleForce', 5), ('AMD FSR 1', 6),
           ('AMD FSR 3', 15), ('Lanczos', 4), ('Área', 7)]
AA = [('Nenhum', 0), ('FXAA', 1), ('SMAA', 2)]
ASPECT = [('16:9', 0), ('21:9', 2), ('16:10', 3), ('4:3', 1), ('Esticar', 4)]
VSYNC = [('Desligado', 0), ('Mailbox', 1), ('Ligado (FIFO)', 2), ('FIFO relaxado', 3)]
MODE = [('TV (dock)', 1), ('Portátil', 0)]

DEFAULTS = {
    'game': '', 'resolution': 3, 'filter': 15, 'aa': 1, 'aspect': 0, 'vsync': 2, 'mode': 1,
    'fullscreen': False, 'gpl': True, 'precache': True, 'volume': 100,
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
    to the [Renderer] section (eden-cli reads any key from its own section on load)."""
    try:
        lines = INI.read_text(encoding='utf-8').splitlines()
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
    missing = [k for k in values if k not in seen]
    if missing:
        sections = {'volume': '[Audio]'}
        for key in missing:
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


def export_art(game):
    """Asks eden-cli for the game's own icon and title (from the player's copy) into user/zoot."""
    ZDIR.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run([str(CORE), '--zoot-export-art', str(game), str(ZDIR)], cwd=BASE,
                       timeout=60, creationflags=NO_WINDOW, capture_output=True)
    except (OSError, subprocess.SubprocessError):
        return None, None
    art = next((p for p in (ZDIR / 'game-icon.jpg', ZDIR / 'game-icon.png') if p.exists()), None)
    title = None
    try:
        title = (ZDIR / 'game-title.txt').read_text(encoding='utf-8').strip() or None
    except OSError:
        pass
    return art, title


class Launcher(tk.Tk):
    W, H = 1100, 750

    def __init__(self):
        super().__init__()
        T.load_fonts(self)
        self.title(APP)
        self.configure(bg=T.NIGHT)
        self.geometry(f'{self.W}x{self.H}')
        self.minsize(1000, 720)
        self.settings = load_settings()
        self.images = []
        self.proc = None
        self.style = T.apply(self)
        self.set_icon()
        self.body = tk.Frame(self, bg=T.NIGHT)
        self.body.pack(fill='both', expand=True)
        if self.settings['game'] and Path(self.settings['game']).exists():
            self.show_main()
        else:
            self.show_setup()

    # ---- window icon: the player's own icon.png, else the ZooT shield ------------------------
    def set_icon(self):
        for path in (ZDIR / 'icon.png', BUNDLE / 'assets' / 'zoot-256.png'):
            if path.exists():
                try:
                    img = ImageTk.PhotoImage(Image.open(path).convert('RGBA').resize((64, 64)))
                    self.iconphoto(True, img)
                    self.images.append(img)
                    return
                except OSError:
                    continue

    def clear(self):
        for w in self.body.winfo_children():
            w.destroy()

    # ---- first run -----------------------------------------------------------------------------
    def show_setup(self):
        self.clear()
        wrap = tk.Frame(self.body, bg=T.NIGHT)
        wrap.place(relx=0.5, rely=0.5, anchor='center')
        shield = BUNDLE / 'assets' / 'zoot-256.png'
        if shield.exists():
            img = ImageTk.PhotoImage(Image.open(shield).resize((150, 150), Image.LANCZOS))
            self.images.append(img)
            tk.Label(wrap, image=img, bg=T.NIGHT).pack(pady=(0, 12))
        tk.Label(wrap, text=APP, font=(T.HEAD_BOLD, 34), fg=T.GOLD, bg=T.NIGHT).pack()
        tk.Label(wrap, text='Bem-vindo! Escolha o arquivo do seu jogo para começar.',
                 font=(T.BODY, 13), fg=T.TEXT, bg=T.NIGHT).pack(pady=(6, 2))
        tk.Label(wrap, text='Formatos aceitos: .nup, .nx, .nsp, .xci', font=(T.BODY, 10),
                 fg=T.MUTED, bg=T.NIGHT).pack(pady=(0, 18))
        self.found = tk.Listbox(wrap, width=80, height=6, bg=T.PANEL, fg=T.TEXT,
                                selectbackground=T.GOLD_DARK, highlightthickness=1,
                                highlightbackground=T.BORDER, relief='flat', font=(T.BODY, 10))
        self.found.pack(pady=(0, 12))
        self.found.bind('<Double-Button-1>', lambda e: self.pick_found())
        row = tk.Frame(wrap, bg=T.NIGHT)
        row.pack()
        T.Button(row, 'Usar o selecionado', self.pick_found, primary=True).pack(side='left',
                                                                                padx=6)
        T.Button(row, 'Procurar arquivo...', self.browse).pack(side='left', padx=6)
        self.setup_status = tk.Label(wrap, text='Procurando jogos...', font=(T.BODY, 10),
                                     fg=T.MUTED, bg=T.NIGHT)
        self.setup_status.pack(pady=(14, 0))
        threading.Thread(target=self.search_games, daemon=True).start()

    def search_games(self):
        games = find_games()

        def show():
            if not self.winfo_exists() or not hasattr(self, 'found'):
                return
            self.found_paths = games
            for g in games:
                self.found.insert('end', str(g))
            if games:
                self.found.selection_set(0)
            self.setup_status.config(text=f'{len(games)} arquivo(s) encontrado(s).'
                                     if games else 'Nenhum jogo encontrado: use "Procurar".')
        self.after(0, show)

    def pick_found(self):
        sel = self.found.curselection()
        if sel:
            self.use_game(self.found_paths[sel[0]])

    def browse(self):
        path = filedialog.askopenfilename(
            title='Arquivo do jogo', filetypes=[('Jogos', ' '.join('*' + e for e in GAME_EXTS)),
                                                ('Todos', '*.*')])
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
        self.show_main()

    # ---- main screen ---------------------------------------------------------------------------
    def show_main(self):
        self.clear()
        top = tk.Frame(self.body, bg=T.NIGHT)
        top.pack(fill='x', padx=28, pady=(20, 0))
        shield = BUNDLE / 'assets' / 'zoot-256.png'
        if shield.exists():
            img = ImageTk.PhotoImage(Image.open(shield).resize((44, 44), Image.LANCZOS))
            self.images.append(img)
            tk.Label(top, image=img, bg=T.NIGHT).pack(side='left')
        tk.Label(top, text=APP, font=(T.HEAD_BOLD, 22), fg=T.GOLD, bg=T.NIGHT).pack(
            side='left', padx=(10, 8))
        tk.Label(top, text=f'v{VERSION}', font=(T.BODY, 10), fg=T.MUTED, bg=T.NIGHT).pack(
            side='left', pady=(10, 0))
        T.Button(top, 'Trocar jogo', self.show_setup, small=True).pack(side='right')
        T.Button(top, 'Log', self.open_log, small=True).pack(side='right', padx=6)
        T.Button(top, 'Saves', self.open_saves, small=True).pack(side='right')
        tk.Frame(self.body, bg=T.BORDER, height=1).pack(fill='x', padx=28, pady=(14, 0))

        main = tk.Frame(self.body, bg=T.NIGHT)
        main.pack(fill='both', expand=True, padx=28, pady=20)
        left = tk.Frame(main, bg=T.NIGHT)
        left.pack(side='left', fill='both', expand=True)
        right = tk.Frame(main, bg=T.PANEL, highlightthickness=1, highlightbackground=T.BORDER)
        right.pack(side='right', fill='y', padx=(20, 0))

        self.hero = tk.Canvas(left, width=560, height=380, bg=T.NIGHT, highlightthickness=0)
        self.hero.pack(fill='both', expand=True)
        self.hero.bind('<Configure>', lambda e: self.draw_hero())
        bottom = tk.Frame(left, bg=T.NIGHT)
        bottom.pack(fill='x', pady=(16, 0))
        self.play_btn = T.Button(bottom, '▶  JOGAR', self.play, primary=True, big=True)
        self.play_btn.pack(side='left')
        self.status = tk.Label(bottom, text='', font=(T.BODY, 10), fg=T.MUTED, bg=T.NIGHT,
                               justify='left')
        self.status.pack(side='left', padx=18)

        self.build_options(right)
        self.art, self.game_title = None, None
        self.update_status()
        threading.Thread(target=self.load_art, daemon=True).start()

    def load_art(self):
        # The player's own cover first (user/zoot/capa.png|jpg), then the game file's icon.
        art = next((p for p in (ZDIR / 'capa.png', ZDIR / 'capa.jpg', ZDIR / 'game-icon.jpg',
                                ZDIR / 'game-icon.png') if p.exists()), None)
        title = None
        try:
            title = (ZDIR / 'game-title.txt').read_text(encoding='utf-8').strip() or None
        except OSError:
            pass
        if title is None and CORE.exists():
            exported, title = export_art(self.settings['game'])
            art = art or exported
        self.after(0, lambda: self.set_art(art, title))

    def set_art(self, art, title):
        self.art, self.game_title = art, title
        if self.winfo_exists():
            self.draw_hero()

    def draw_hero(self):
        c = self.hero
        w, h = max(c.winfo_width(), 200), max(c.winfo_height(), 200)
        c.delete('all')
        img = T.hero_card((w, h), self.art, BUNDLE / 'assets' / 'zoot-256.png')
        tkimg = ImageTk.PhotoImage(img)
        self.images.append(tkimg)
        self.images = self.images[-12:]
        c.create_image(0, 0, image=tkimg, anchor='nw')
        name = self.game_title or Path(self.settings['game']).stem
        c.create_text(28, h - 58, text=name, anchor='w', font=(T.HEAD_BOLD, 22), fill=T.TEXT)
        c.create_text(28, h - 28, text=Path(self.settings['game']).name, anchor='w',
                      font=(T.BODY, 9), fill=T.MUTED)

    def build_options(self, parent):
        s = self.settings
        tk.Label(parent, text='OPÇÕES', font=(T.HEAD_BOLD, 13), fg=T.GOLD, bg=T.PANEL).pack(
            anchor='w', padx=18, pady=(14, 6))
        self.vars = {}

        def section(text):
            tk.Label(parent, text=text, font=(T.BODY_SEMI, 10), fg=T.SILVER, bg=T.PANEL).pack(
                anchor='w', padx=18, pady=(10, 2))

        def combo(key, label, choices):
            row = tk.Frame(parent, bg=T.PANEL)
            row.pack(fill='x', padx=18, pady=3)
            tk.Label(row, text=label, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL, width=16,
                     anchor='w').pack(side='left')
            names = [n for n, _ in choices]
            current = next((n for n, v in choices if v == s[key]), names[0])
            var = tk.StringVar(value=current)
            box = ttk.Combobox(row, values=names, textvariable=var, state='readonly', width=16,
                               style='Zoot.TCombobox')
            box.pack(side='right')
            box.bind('<<ComboboxSelected>>',
                     lambda e: self.set_opt(key, dict(choices)[var.get()]))

        def switch(key, label, hint=''):
            row = tk.Frame(parent, bg=T.PANEL)
            row.pack(fill='x', padx=18, pady=3)
            tk.Label(row, text=label, font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL,
                     anchor='w').pack(side='left')
            T.Switch(row, s[key], lambda v: self.set_opt(key, v)).pack(side='right')
            if hint:
                tk.Label(parent, text=hint, font=(T.BODY, 8), fg=T.MUTED, bg=T.PANEL,
                         wraplength=290, justify='left').pack(anchor='w', padx=18)

        section('Imagem')
        combo('resolution', 'Resolução', RESOLUTIONS)
        combo('filter', 'Filtro de escala', FILTERS)
        combo('aa', 'Anti-serrilhado', AA)
        combo('aspect', 'Proporção', ASPECT)
        section('Tela')
        switch('fullscreen', 'Tela cheia')
        combo('vsync', 'VSync', VSYNC)
        combo('mode', 'Modo do console', MODE)
        section('Shaders')
        switch('gpl', 'Shaders sem travadas (GPL)',
               'Monta os shaders em partes e liga na hora: bem menos engasgo na 1ª vez.')
        switch('precache', 'Pré-compilar antes de jogar',
               'Na 1ª abertura (ou driver novo) prepara os shaders do jogo com tela de '
               'carregamento. Esc pula.')
        section('Áudio')
        row = tk.Frame(parent, bg=T.PANEL)
        row.pack(fill='x', padx=18, pady=(3, 16))
        tk.Label(row, text='Volume', font=(T.BODY, 10), fg=T.TEXT, bg=T.PANEL).pack(side='left')
        vol = ttk.Scale(row, from_=0, to=100, value=s['volume'], style='Zoot.Horizontal.TScale',
                        command=lambda v: self.set_opt('volume', int(float(v)), quiet=True))
        vol.pack(side='right', fill='x', expand=True, padx=(18, 0))

    def set_opt(self, key, value, quiet=False):
        self.settings[key] = value
        save_settings(self.settings)

    def update_status(self):
        title_dir = USER / 'cache' / 'shader' / TITLE_ID.lower()
        warm = (title_dir / 'warm-compute.done').exists()
        gfx = (title_dir / 'warm-graphics.done').exists()
        parts = []
        if not CORE.exists():
            parts.append(f'eden-cli.exe não encontrado em {BASE}')
        elif warm and (gfx or not self.settings['gpl']):
            parts.append('Shaders prontos.')
        else:
            parts.append('1ª abertura: os shaders serão preparados (alguns minutos).')
        self.status.config(text='\n'.join(parts))

    # ---- play ----------------------------------------------------------------------------------
    def apply_ini(self):
        s = self.settings
        write_ini({
            'resolution_setup': s['resolution'], 'scaling_filter': s['filter'],
            'anti_aliasing': s['aa'], 'aspect_ratio': s['aspect'], 'use_vsync': s['vsync'],
            'use_docked_mode': s['mode'], 'fullscreen_mode': 0,
            'use_graphics_pipeline_library': 'true' if s['gpl'] else 'false',
            'precache_shaders': 2 if s['precache'] else 0,
            'use_disk_shader_cache': 'true', 'volume': s['volume'],
        })

    def play(self):
        if self.proc and self.proc.poll() is None:
            return
        if not CORE.exists():
            messagebox.showerror(APP, f'eden-cli.exe não encontrado em:\n{BASE}')
            return
        self.apply_ini()
        env = dict(os.environ)
        if self.settings['gpl']:
            env['EDEN_NX2_GPL_OPTIMIZE'] = '1'
        args = [str(CORE), '-g', self.settings['game']]
        if self.settings['fullscreen']:
            args.append('-f')
        try:
            self.proc = subprocess.Popen(args, cwd=BASE, env=env)
        except OSError as e:
            messagebox.showerror(APP, f'Não foi possível iniciar o jogo:\n{e}')
            return
        self.withdraw()
        self.after(1000, self.watch)

    def watch(self):
        code = self.proc.poll()
        if code is None:
            self.after(1000, self.watch)
            return
        self.deiconify()
        self.update_status()
        if code not in (0, None) and code & 0xFFFFFFFF not in (0, 0xC000013A):
            tail = ''
            try:
                tail = '\n'.join(LOG.read_text(encoding='utf-8', errors='replace')
                                 .splitlines()[-12:])
            except OSError:
                pass
            messagebox.showwarning(APP, f'O jogo fechou com o código {code & 0xFFFFFFFF:#x}.'
                                        f'\n\nÚltimas linhas do log:\n{tail[-1500:]}')

    def open_log(self):
        if LOG.exists():
            os.startfile(LOG)
        else:
            messagebox.showinfo(APP, 'Ainda não há log: jogue uma vez.')

    def open_saves(self):
        SAVES.mkdir(parents=True, exist_ok=True)
        os.startfile(SAVES)


def main():
    if '--run' in sys.argv:  # start the game straight away (shortcuts)
        app = Launcher()
        app.after(300, app.play)
        app.mainloop()
        return
    Launcher().mainloop()


if __name__ == '__main__':
    main()
