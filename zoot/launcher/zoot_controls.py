# SPDX-License-Identifier: GPL-3.0-or-later
"""Controls for the ZooT-PC launcher: the player 1 bindings in eden-cli's sdl2-config.ini, in
the input format of Lichii itself (ParamPackage strings), how to show them, the keyboard defaults
of eden-cli and the Windows virtual key -> SDL scancode table for capturing keys in Tk.
Controllers are listed, mapped and captured by eden-cli (--zoot-pad-*), so their bindings are
exactly what the game reads."""
import json
import subprocess
from pathlib import Path

# (ini key without "player_0_", label). Sticks last.
BUTTONS = [
    ('button_a', 'A'), ('button_b', 'B'), ('button_x', 'X'), ('button_y', 'Y'),
    ('button_l', 'L'), ('button_r', 'R'), ('button_zl', 'ZL'), ('button_zr', 'ZR'),
    ('button_plus', '+'), ('button_minus', '−'), ('button_home', 'Home'),
    ('button_screenshot', 'Captura'), ('button_dup', '↑'), ('button_ddown', '↓'),
    ('button_dleft', '←'), ('button_dright', '→'), ('button_lstick', 'L3'),
    ('button_rstick', 'R3'),
]
# Short labels (the binding beside them reads "Left stick" / "Right stick").
STICKS = [('lstick', 'L ◉'), ('rstick', 'R ◉')]

# eden-cli's keyboard defaults (src/yuzu_cmd/sdl_config.cpp), as SDL scancodes.
DEFAULT_KEYS = {
    'button_a': 4, 'button_b': 22, 'button_x': 29, 'button_y': 27, 'button_lstick': 23,
    'button_rstick': 10, 'button_l': 9, 'button_r': 11, 'button_zl': 20, 'button_zr': 26,
    'button_plus': 16, 'button_minus': 17, 'button_dleft': 30, 'button_dup': 31,
    'button_dright': 5,
}
DEFAULT_STICKS = {'lstick': (82, 81, 80, 79, 7), 'rstick': (12, 14, 13, 15, 0)}

# SDL scancodes by name, for showing bindings.
SCANCODE_NAMES = {40: 'Enter', 41: 'Esc', 42: 'Backspace', 43: 'Tab', 44: 'Espaço', 45: '-',
                  46: '=', 47: '[', 48: ']', 49: '\\', 51: ';', 52: "'", 53: '`', 54: ',',
                  55: '.', 56: '/', 57: 'Caps Lock', 73: 'Insert', 74: 'Home', 75: 'Page Up',
                  76: 'Delete', 77: 'End', 78: 'Page Down', 79: '→', 80: '←', 81: '↓', 82: '↑',
                  224: 'Ctrl', 225: 'Shift', 226: 'Alt', 228: 'Ctrl dir.', 229: 'Shift dir.',
                  230: 'Alt dir.', 98: 'Num 0'}
SCANCODE_NAMES.update({4 + i: chr(ord('A') + i) for i in range(26)})
SCANCODE_NAMES.update({30 + i: str(i + 1) for i in range(9)})
SCANCODE_NAMES[39] = '0'
SCANCODE_NAMES.update({58 + i: f'F{i + 1}' for i in range(12)})
SCANCODE_NAMES.update({89 + i: f'Num {i + 1}' for i in range(9)})

# Windows virtual key (Tk's event.keycode on Windows) -> SDL scancode.
VK_TO_SCANCODE = {0x0D: 40, 0x1B: 41, 0x08: 42, 0x09: 43, 0x20: 44, 0xBD: 45, 0xBB: 46,
                  0xDB: 47, 0xDD: 48, 0xDC: 49, 0xBA: 51, 0xDE: 52, 0xC0: 53, 0xBC: 54,
                  0xBE: 55, 0xBF: 56, 0x14: 57, 0x2D: 73, 0x24: 74, 0x21: 75, 0x2E: 76,
                  0x23: 77, 0x22: 78, 0x27: 79, 0x25: 80, 0x28: 81, 0x26: 82, 0x60: 98}
VK_TO_SCANCODE.update({0x41 + i: 4 + i for i in range(26)})
VK_TO_SCANCODE.update({0x31 + i: 30 + i for i in range(9)})
VK_TO_SCANCODE[0x30] = 39
VK_TO_SCANCODE.update({0x70 + i: 58 + i for i in range(12)})
VK_TO_SCANCODE.update({0x61 + i: 89 + i for i in range(9)})
# Keys the game window keeps for ZooT: F1 menu, F7 FPS, F11 fullscreen.
RESERVED = {58, 64, 65, 67, 68}  # F1, F7, F8, F10, F11

# XInput controllers seen through SDL's joystick API.
XBOX_BUTTONS = {0: 'A', 1: 'B', 2: 'X', 3: 'Y', 4: 'LB', 5: 'RB', 6: 'Back', 7: 'Start',
                8: 'L3', 9: 'R3', 10: 'Guide'}
XBOX_AXES = {0: 'L ↔', 1: 'L ↕', 2: 'LT', 3: 'R ↔', 4: 'R ↕', 5: 'RT'}


def scancode_from_key(event):
    """The SDL scancode of a Tk key event, telling left and right modifiers apart."""
    sym = getattr(event, 'keysym', '')
    side = {'Shift_L': 225, 'Shift_R': 229, 'Control_L': 224, 'Control_R': 228,
            'Alt_L': 226, 'Alt_R': 230}
    if sym in side:
        return side[sym]
    return VK_TO_SCANCODE.get(getattr(event, 'keycode', -1))


# ---- ParamPackage strings ----------------------------------------------------------------------
def parse(text):
    """'engine:sdl,button:1' -> {'engine': 'sdl', 'button': '1'} (nested values unescaped)."""
    out = {}
    text = (text or '').strip().strip('"')
    if not text or text == '[empty]':
        return out
    for part in text.split(','):
        if ':' in part:
            k, v = part.split(':', 1)
            out[k] = v.replace('$0', ':').replace('$1', ',').replace('$2', '$')
    return out


def escape(value):
    return str(value).replace('$', '$2').replace(',', '$1').replace(':', '$0')


def key_param(code):
    return f'engine:keyboard,code:{code},toggle:0'


def stick_from_keys(up, down, left, right, modifier=0):
    parts = [f'{name}:{escape(key_param(code))}' for name, code in
             (('up', up), ('down', down), ('left', left), ('right', right))]
    parts.append(f'modifier:{escape(key_param(modifier))}')
    return 'engine:analog_from_button,' + ','.join(parts) + ',modifier_scale:0.500000'


# ---- mouse -------------------------------------------------------------------------------------
# Tk mouse button -> Lichii's MouseButton (input_common/drivers/mouse.h): left, right, middle
# (wheel), back, forward.
TK_MOUSE_BUTTONS = {1: 0, 3: 1, 2: 2, 4: 3, 5: 4}
MOUSE_BUTTON_NAMES = {0: 'Mouse esquerdo', 1: 'Mouse direito', 2: 'Mouse meio',
                      3: 'Mouse lateral 1', 4: 'Mouse lateral 2'}


def mouse_param(button):
    return f'engine:mouse,button:{button},port:0,pad:0'


def mouse_stick(invert_y=False):
    """The right stick on the mouse camera (mouse panning moves this stick)."""
    return ('engine:mouse,axis_x:0,axis_y:1,threshold:0.500000,range:1.000000,'
            'deadzone:0.000000,invert_x:+,invert_y:' + ('-' if invert_y else '+'))


def is_mouse_stick(text):
    p = parse(text)
    return p.get('engine') == 'mouse' and 'axis_x' in p


# Keyboard and mouse: WASD walks (C walks slowly), the mouse turns the camera, left click is the
# sword (B), right click targets (ZL), Space is the action button (A).
def keyboard_mouse(invert_y=False):
    keys = {
        'button_a': 44, 'button_x': 30, 'button_y': 31, 'button_l': 32, 'button_r': 225,
        'button_zr': 9, 'button_plus': 40, 'button_minus': 43, 'button_home': 0,
        'button_screenshot': 0, 'button_dup': 82, 'button_ddown': 81, 'button_dleft': 80,
        'button_dright': 79, 'button_lstick': 224,
    }
    out = {key: key_param(code) for key, code in keys.items()}
    out['button_b'] = mouse_param(0)
    out['button_zl'] = mouse_param(1)
    out['button_rstick'] = mouse_param(2)
    out['lstick'] = stick_from_keys(26, 22, 4, 7, 6)
    out['rstick'] = mouse_stick(invert_y)
    return out


# ---- controller + keyboard/mouse together (engine:combo in eden-cli) ---------------------------
BINDING_KEYS = [k for k, _ in BUTTONS] + [k for k, _ in STICKS]


def combo(pad, kbm):
    """One console input fed by the controller (a) and the keyboard/mouse (b) at once."""
    if pad and kbm:
        return f'engine:combo,a:{escape(pad)},b:{escape(kbm)}'
    return pad or kbm or ''


def is_kbm(text):
    return parse(text).get('engine') in ('keyboard', 'mouse', 'analog_from_button')


def split_layers(bindings):
    """{'button_a': ini value} -> (controller layer, keyboard/mouse layer)."""
    pad, kbm = {}, {}
    for key in BINDING_KEYS:
        value = bindings.get(key, '')
        p = parse(value)
        if p.get('engine') == 'combo':
            if p.get('a'):
                pad[key] = p['a']
            if p.get('b'):
                kbm[key] = p['b']
        elif p.get('engine') and is_kbm(value):
            kbm[key] = value
        elif p.get('engine'):
            pad[key] = value
    return pad, kbm


def kbm_layout(mouse_camera=True, invert_y=False):
    """The keyboard and mouse layout; without the mouse camera, I J K L turn the camera."""
    out = keyboard_mouse(invert_y)
    if not mouse_camera:
        out['rstick'] = stick_from_keys(12, 14, 13, 15, 0)
    return out


def default_keyboard():
    out = {key: key_param(DEFAULT_KEYS.get(key, 0)) for key, _ in BUTTONS}
    for key, keys in DEFAULT_STICKS.items():
        out[key] = stick_from_keys(*keys)
    return out


def key_name(code):
    try:
        code = int(code)
    except (TypeError, ValueError):
        return '?'
    return SCANCODE_NAMES.get(code, f'#{code}') if code else '—'


def describe(text, tr=lambda s: s):
    """A binding as the player reads it: 'Tecla A', 'Botão B', 'LT', 'Direcional ↑'..."""
    p = parse(text)
    engine = p.get('engine')
    if not engine:
        return '—'
    if engine == 'keyboard':
        name = key_name(p.get('code'))
        return '—' if name == '—' else f"{tr('Tecla')} {tr(name)}"
    if engine == 'mouse':
        if 'axis_x' in p:
            return tr('Mouse (câmera)')
        return tr(MOUSE_BUTTON_NAMES.get(int(p.get('button', 0)), 'Mouse'))
    if engine == 'analog_from_button':
        keys = [key_name(parse(p.get(d, '')).get('code')) for d in ('up', 'left', 'down', 'right')]
        return f"{tr('Teclas')} {'/'.join(keys)}"
    xbox = True  # SDL's joystick numbering for XInput pads; other pads show numbers too
    if 'axis_x' in p:
        axes = (p.get('axis_x'), p.get('axis_y'))
        if xbox and axes == ('0', '1'):
            return tr('Analógico esquerdo')
        if xbox and axes == ('3', '4'):
            return tr('Analógico direito')
        return f"{tr('Analógico')} {axes[0]}/{axes[1]}"
    if 'hat' in p:
        arrows = {'up': '↑', 'down': '↓', 'left': '←', 'right': '→'}
        return f"{tr('Direcional')} {arrows.get(p.get('direction'), p.get('direction', ''))}"
    if 'axis' in p:
        axis = int(p.get('axis', -1))
        name = XBOX_AXES.get(axis, f"{tr('Eixo')} {axis}")
        return name if axis in (2, 5) else f"{name} {p.get('invert', '+')}"
    if 'button' in p:
        b = int(p.get('button', -1))
        return f"{tr('Botão')} {XBOX_BUTTONS.get(b, str(b))}"
    return engine


# ---- the ini -----------------------------------------------------------------------------------
def read_bindings(ini):
    out = {}
    try:
        lines = Path(ini).read_text(encoding='utf-8-sig').splitlines()
    except OSError:
        return out
    for line in lines:
        if line.startswith('player_0_') and '=' in line and '\\default' not in line:
            key, value = line.split('=', 1)
            out[key[len('player_0_'):]] = value.strip().strip('"')
    return out


def ini_values(bindings):
    """{'button_a': 'engine:...'} -> the sdl2-config.ini keys and quoted values."""
    return {f'player_0_{k}': f'"{v}"' for k, v in bindings.items()}


# ---- eden-cli helpers --------------------------------------------------------------------------
def run_core(core, args, cwd, timeout):
    """Runs eden-cli with a --zoot-pad-* command and returns its JSON answer (or None)."""
    try:
        r = subprocess.run([str(core), *args], cwd=cwd, capture_output=True, timeout=timeout,
                           creationflags=0x08000000)
    except (OSError, subprocess.SubprocessError):
        return None
    for line in reversed(r.stdout.decode('utf-8', 'replace').splitlines()):
        line = line.strip()
        if line.startswith('{') or line.startswith('['):
            try:
                return json.loads(line)
            except ValueError:
                return None
    return None


def list_pads(core, cwd):
    return run_core(core, ['--zoot-pad-list'], cwd, 30) or []


def automap(core, cwd, device_params):
    return run_core(core, ['--zoot-pad-automap', device_params], cwd, 30) or {}


def capture_pad(core, cwd, stick, seconds=10):
    """Starts the capture; the caller polls the returned process (so it can be cancelled)."""
    return subprocess.Popen([str(core), '--zoot-pad-capture', 'stick' if stick else 'button',
                             str(seconds)], cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                            creationflags=0x08000000)


def read_capture(proc):
    out = proc.stdout.read().decode('utf-8', 'replace') if proc.stdout else ''
    for line in reversed(out.splitlines()):
        if line.strip().startswith('{'):
            try:
                return json.loads(line).get('params') or ''
            except ValueError:
                return ''
    return ''
