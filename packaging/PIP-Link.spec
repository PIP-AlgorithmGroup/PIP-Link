# -*- mode: python ; coding: utf-8 -*-
# 运行方式：在项目根目录执行 pyinstaller packaging/PIP-Link.spec

import os
ROOT = os.path.abspath(os.path.join(os.path.dirname(SPEC), '..'))

block_cipher = None

a = Analysis(
    [os.path.join(ROOT, 'main.py')],
    pathex=[ROOT],
    binaries=[],
    datas=[
        (os.path.join(ROOT, 'LICENSE'), '.'),
        (os.path.join(ROOT, 'README.md'), '.'),
        (os.path.join(ROOT, 'config.json'), '.'),
        (os.path.join(ROOT, 'imgui.ini'), '.'),
        (os.path.join(ROOT, 'assets'), 'assets'),
    ],
    hiddenimports=[
        'pygame',
        'cv2',
        'numpy',
        'pynput',
        'pynput.keyboard',
        'pynput.mouse',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'matplotlib',  # 排除不需要的大型库
        'PIL',
        'scipy',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='PIP-Link',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=os.path.join(ROOT, 'assets/icon.ico'),
    version=os.path.join(ROOT, 'packaging/version_info.txt'),
    contents_directory='.',
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='PIP-Link',
    contents_directory='.',
)