# -*- mode: python ; coding: utf-8 -*-
# Build ONEDIR (bukan onefile):
#   - tanpa ekstraksi DLL ke %TEMP% saat start -> tidak rawan diblokir AV/Defender
#   - semua DLL terlihat di folder -> mudah didiagnosis bila ada yang kurang
#   - upx=False: kompresi UPX pada DLL Qt/shiboken memicu error
#     "procedure could not be found"
import os

_SHB = os.path.join(os.environ["LOCALAPPDATA"],
                    "Programs", "Python", "Python39",
                    "Lib", "site-packages", "shiboken6")

a = Analysis(
    ['run.py'],
    pathex=[],
    binaries=[
        (os.path.join(_SHB, 'vcruntime140.dll'), '.'),
        (os.path.join(_SHB, 'vcruntime140_1.dll'), '.'),
        (os.path.join(_SHB, 'msvcp140.dll'), '.'),
    ],
    datas=[],
    hiddenimports=['mido', 'mido.backends', 'mido.backends.rtmidi', 'rtmidi'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='DMX512Controller',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name='DMX512Controller',
)
