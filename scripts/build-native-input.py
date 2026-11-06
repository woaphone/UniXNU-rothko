#!/usr/bin/env python3
"""Build and install the yunluo input objects into the display archive the kernel force-loads.

Members added (or replaced) in build/native-legacy-display/libNativeLegacyDisplay.late.a:
  NativeLegacyDisplay.late.o   rebuilt display driver (calls native_input_probe weakly)
  InputProbe.o                 MTK SPI0 + Novatek NT36523 + keypad, board threads
  HidInputProbe.o              boot-argument gated IOHIDFamily/IOHIDEventDriver load + HID device
  NativeHidInput.late.o        owned IOHIDDevice subclass (built by build-hid-input-yunluo.py)
  hid-original-bindings.o      absolute bindings to the unchanged IOHIDDevice
  KeyStoreProbe.o              ephemeral-storage-gated board registration
  NativeKeyStoreEvents.late.o  notification/state-only service, no key operations

Nothing is deleted: the archive is updated in place with `ar r`, exactly as the display objects are.
"""
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-input'
OUT.mkdir(parents=True, exist_ok=True)
ARCHIVE = ROOT / 'build/native-legacy-display/libNativeLegacyDisplay.late.a'
HID = ROOT / 'build/native-hid-input'
DISPLAY = ROOT / 'build/native-legacy-display'


def run(cmd, **kw):
    return subprocess.check_output([str(x) for x in cmd], timeout=900, **kw)


def recipe():
    """The compiler invocation XNU itself used for an IOKit C++ translation unit."""
    record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/'
                         'IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
    return record


def compile_cpp(source, obj, includes=()):
    record = recipe()
    command, index, skip = [], 0, False
    args = record['arguments']
    while index < len(args):
        value = args[index]
        if value == record['file']:
            command.append(str(source))
        elif value == '-o':
            command += ['-o', str(obj)]
            index += 1
        elif value == '-Xclang' and args[index + 1].startswith('-W'):
            index += 1
        elif not value.startswith('-W'):
            command.append(value)
        index += 1
    command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                '-Wno-c99-designator', '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                '-Wno-unnecessary-virtual-specifier', '-Wno-unknown-warning-option',
                '-I', str(ROOT / '.cache/xnu-work/osfmk'), '-I', str(ROOT)]
    for path in includes:
        command += ['-I', str(path)]
    (OUT / (obj.stem + '.compile.json')).write_text(json.dumps(command, indent=2) + '\n')
    result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    (OUT / (obj.stem + '.compile.log')).write_text(result.stdout)
    if result.returncode:
        raise SystemExit(result.stdout[-4000:])


# K70U iPadOS 17 original-family ABI, derived from the retained prelinked image.
for name in ('OriginalLegacyFramebufferABI.h', 'original-bindings.S'):
    shutil.copy2(ROOT / 'kits/rothko' / name, DISPLAY / name)
run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-c', DISPLAY / 'original-bindings.S', '-o', DISPLAY / 'original-bindings.o'])

# 1. Display driver, rebuilt from the edited shared source (its weak native_input_probe call).
print(run(['python3', ROOT / 'scripts/build-arm-driver.py', 'NativeLegacyDisplay'], text=True), end='')
display_obj = DISPLAY / 'NativeLegacyDisplay.o'
if not display_obj.exists():
    raise SystemExit('Display object missing')
# The display class registers itself from its own __mod_init_func and the platform depends on that:
# fb199's first attempt renamed the section the way the HID object needs and the boot log answered
# "NativeLegacyDisplay: metaclass missing (C++ bootstrap did not register it)", which leaves the
# compositor without a framebuffer. The display object is therefore installed unchanged (the previous
# qualified archive recorded identical compiled/late hashes for exactly this reason).
(DISPLAY / 'NativeLegacyDisplay.late.o').write_bytes(display_obj.read_bytes())

# The board's canvas properties and publication hooks are part of this driver.
compile_cpp(ROOT / 'kits/sheng/LegacyDisplayProbe.cpp', DISPLAY / 'LegacyDisplayProbe.o')

# 2. Owned input sources.
compile_cpp(ROOT / 'kits/rothko/InputProbe.cpp', OUT / 'InputProbe.o')
compile_cpp(ROOT / 'drivers/MtkCpuFreq/MtkCpuFreq.cpp', OUT / 'MtkCpuFreq.o')
compile_cpp(ROOT / 'kits/yunluo/HidInputProbe.cpp', OUT / 'HidInputProbe.o')
compile_cpp(ROOT / 'kits/yunluo/KeyStoreProbe.cpp', OUT / 'KeyStoreProbe.o')
keyevents = OUT / 'NativeKeyStoreEvents.o'
keyevents_late = OUT / 'NativeKeyStoreEvents.late.o'
compile_cpp(ROOT / 'drivers/NativeKeyStoreEvents/NativeKeyStoreEvents.cpp', keyevents)
sys.path.insert(0, str(ROOT / 'kits/NativePrelink'))
from native_prelink import macho


def defer_keystore_initializer(data):
    data = bytearray(data)
    parsed = macho(data)
    if parsed['kind'] != 1:
        raise SystemExit('Expected an ARM64 Mach-O object')
    renamed = globalized = 0
    for group in parsed['segments']:
        for section in group['sections']:
            if section['name'] == '__mod_init_func':
                offset = section['command_offset']
                data[offset:offset + 16] = b'__mod_init_defer'.ljust(16, b'\0')
                renamed += 1
    command = next(p for c, p, _ in parsed['commands'] if c == 2)
    syms, count, strings, _ = struct.unpack_from('<4I', data, command + 8)
    for index in range(count):
        offset = syms + 16 * index
        string_index, kind = struct.unpack_from('<IB', data, offset)
        at = strings + string_index
        if data[at:data.index(b'\0', at)] == b'__GLOBAL__sub_I_NativeKeyStoreEvents.cpp':
            data[offset + 4] = kind | 1
            globalized += 1
    if renamed != 1 or globalized != 1:
        raise SystemExit('Expected exactly one owned keystore initializer')
    return bytes(data)


keyevents_late.write_bytes(defer_keystore_initializer(keyevents.read_bytes()))


def text_bytes(path):
    data = path.read_bytes()
    parsed = macho(data)
    section = next(s for g in parsed['segments'] for s in g['sections'] if s['name'] == '__text')
    return data[section['fileoff']:section['fileoff'] + section['size']]


if text_bytes(keyevents) != text_bytes(keyevents_late):
    raise SystemExit('Keystore late-initializer staging changed executable instructions')

# 3. Install into the archive the kernel link force-loads.
for member in (DISPLAY / 'original-bindings.o', DISPLAY / 'NativeLegacyDisplay.late.o', DISPLAY / 'LegacyDisplayProbe.o', OUT / 'InputProbe.o', OUT / 'MtkCpuFreq.o', OUT / 'HidInputProbe.o',
               HID / 'NativeHidInput.late.o', HID / 'hid-original-bindings.o',
               OUT / 'KeyStoreProbe.o', keyevents_late):
    if not member.exists():
        raise SystemExit(f'Missing archive member {member}')
    run(['xcrun', 'ar', 'r', ARCHIVE, member])
members = run(['llvm-ar', 't', ARCHIVE], text=True).split()
for required in ('NativeLegacyDisplay.late.o', 'InputProbe.o', 'MtkCpuFreq.o', 'HidInputProbe.o',
                 'NativeHidInput.late.o', 'hid-original-bindings.o', 'original-bindings.o',
                 'LegacyDisplayProbe.o', 'KeyStoreProbe.o', 'NativeKeyStoreEvents.late.o'):
    if required not in members:
        raise SystemExit(f'{required} not in archive: {members}')
(OUT / 'build-report.json').write_text(json.dumps({'archive': str(ARCHIVE), 'members': members}, indent=2) + '\n')

# 4. Refresh the archive's own report: the kernel link refuses an archive whose recorded hash or
#    source hashes do not match, which is exactly what keeps a stale object out of the kernel.
import hashlib


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


report_path = DISPLAY / 'build-report.json'
report = json.loads(report_path.read_text())
sources = ['drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp',
           'kits/sheng/LegacyDisplayProbe.cpp',
           'kits/rothko/InputProbe.cpp', 'kits/rothko/GoodixConfig.h',
           'kits/yunluo/HidInputProbe.cpp',
           'drivers/NativeHidInput/NativeHidInput.cpp',
           'drivers/NativeHidInput/HidReportState.h', 'kits/yunluo/NvtYunluoCore.h',
           'kits/yunluo/BoardKeyState.h', 'kits/yunluo/KeyStoreProbe.cpp',
           'drivers/NativeKeyStoreEvents/NativeKeyStoreEvents.cpp']
for name in sources:
    report['source_sha256'][name] = sha(ROOT / name)
report['archive_sha256'] = sha(ARCHIVE)
report['compiled_object_sha256'] = sha(display_obj)
report['late_object_sha256'] = sha(DISPLAY / 'NativeLegacyDisplay.late.o')
report['input_objects'] = {
    'InputProbe.o': sha(OUT / 'InputProbe.o'),
    'MtkCpuFreq.o': sha(OUT / 'MtkCpuFreq.o'),
    'HidInputProbe.o': sha(OUT / 'HidInputProbe.o'),
    'NativeHidInput.late.o': sha(HID / 'NativeHidInput.late.o'),
    'hid-original-bindings.o': sha(HID / 'hid-original-bindings.o'),
    'KeyStoreProbe.o': sha(OUT / 'KeyStoreProbe.o'),
    'NativeKeyStoreEvents.late.o': sha(keyevents_late),
}
report['input_abi'] = json.loads((HID / 'abi-audit.json').read_text())
report_path.write_text(json.dumps(report, indent=2) + '\n')
print('PASS: input objects installed into', ARCHIVE.name, '->', ' '.join(members))
