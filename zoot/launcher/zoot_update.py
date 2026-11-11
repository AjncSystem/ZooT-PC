# SPDX-License-Identifier: GPL-3.0-or-later
"""ZooT-PC updates: the latest release of AjncSystem/ZooT-PC on GitHub, its zip downloaded into
user\\zoot\\update, and a small script that waits for the launcher to close, copies the new
files over the old ones (never the user folder: config, saves, shader caches, covers) and opens
ZooT.exe again. Portable: nothing is written outside the ZooT-PC folder."""
import json
import os
import shutil
import subprocess
import urllib.request
import zipfile
from pathlib import Path

REPO = 'AjncSystem/ZooT-PC'
API = f'https://api.github.com/repos/{REPO}/releases/latest'
AGENT = 'ZooT-PC-launcher'  # generic: no personal data in the request
NO_WINDOW = 0x08000000


def version_tuple(text):
    text = (text or '').strip().lstrip('vV')
    out = []
    for part in text.split('.'):
        digits = ''.join(ch for ch in part if ch.isdigit())
        out.append(int(digits) if digits else 0)
    return tuple(out)


def latest_release(timeout=10):
    """{'version', 'notes', 'url', 'size', 'page'} of the newest release, or None."""
    req = urllib.request.Request(API, headers={'User-Agent': AGENT,
                                               'Accept': 'application/vnd.github+json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = json.loads(r.read().decode('utf-8'))
    if data.get('draft') or data.get('prerelease'):
        return None
    asset = next((a for a in data.get('assets', [])
                  if a.get('name', '').lower().endswith('.zip')), None)
    if not asset:
        return None
    return {'version': data.get('tag_name', ''), 'notes': data.get('body') or '',
            'url': asset['browser_download_url'], 'size': asset.get('size', 0),
            'page': data.get('html_url', '')}


def is_newer(remote, local):
    return version_tuple(remote) > version_tuple(local)


def download(url, target, progress=None, chunk=1 << 16):
    """Downloads `url` to `target`, calling progress(done, total)."""
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_suffix('.part')
    req = urllib.request.Request(url, headers={'User-Agent': AGENT})
    with urllib.request.urlopen(req, timeout=30) as r, open(tmp, 'wb') as f:
        total = int(r.headers.get('Content-Length') or 0)
        done = 0
        while True:
            block = r.read(chunk)
            if not block:
                break
            f.write(block)
            done += len(block)
            if progress:
                progress(done, total)
    tmp.replace(target)
    return target


def prepare(zip_path, work):
    """Extracts the release zip into work\\new and returns the folder holding ZooT.exe."""
    new = Path(work) / 'new'
    if new.exists():
        shutil.rmtree(new, ignore_errors=True)
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():  # nothing may escape the folder
            if name.startswith('/') or '..' in Path(name).parts:
                raise ValueError(f'unsafe path in the update: {name}')
        z.extractall(new)
    root = next((p.parent for p in new.rglob('ZooT.exe')), None)
    if root is None or not (root / 'eden-cli.exe').exists():
        raise ValueError('the update has no ZooT.exe / eden-cli.exe')
    return root


def apply_and_restart(source, base, work, pid):
    """Writes and starts the script that swaps the files once this launcher (pid) exits.

    The script runs in one hidden console shared by everything it starts (CREATE_NO_WINDOW,
    not DETACHED_PROCESS): a detached cmd made every tasklist/find open a console of its own,
    and with Windows Terminal as the default terminal 0.11 stopped in an empty "find" window.
    Waiting is Wait-Process with a time limit, so the update never waits forever."""
    script = Path(work) / 'apply.cmd'
    log = Path(work) / 'apply.log'
    # Only cmd built-ins, PowerShell, taskkill and robocopy: no ping/find/timeout (ping could
    # hang, timeout fails without a keyboard). Wait-Process has a time limit.
    wait = (f'Wait-Process -Id {pid} -Timeout 60 -ErrorAction SilentlyContinue; '
            f'Stop-Process -Id {pid} -Force -ErrorAction SilentlyContinue; '
            'Start-Sleep -Milliseconds 1500')
    text = (
        '@echo off\r\n'
        'chcp 65001 >nul\r\n'  # the paths below are UTF-8 (folders like "Área de Trabalho")
        f'echo waiting for {pid} > "{log}"\r\n'
        f'powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "{wait}" '
        '>nul 2>&1\r\n'
        # Everything but the user folder; robocopy returns < 8 on success.
        f'robocopy "{source}" "{base}" /E /XD "{Path(source) / "user"}" /R:10 /W:1 '
        f'/NFL /NDL /NJH /NJS /NP >> "{log}" 2>&1\r\n'
        f'echo robocopy %ERRORLEVEL% >> "{log}"\r\n'
        f'start "" "{Path(base) / "ZooT.exe"}"\r\n')
    script.write_bytes(text.encode('utf-8'))  # bytes: \r\n stays \r\n
    subprocess.Popen(['cmd', '/d', '/c', str(script)], cwd=str(base),
                     creationflags=NO_WINDOW | 0x00000200,  # CREATE_NEW_PROCESS_GROUP
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, close_fds=True)


def cleanup(work):
    """Removes an update folder left by an earlier update (the launcher is the new one now)."""
    if Path(work).exists():
        shutil.rmtree(work, ignore_errors=True)
