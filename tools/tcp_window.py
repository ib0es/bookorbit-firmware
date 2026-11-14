"""Build TCP-window tuning against the installed Arduino SDK.

Only the five lwIP objects using TCP_WND / DEFAULT_TCP_RECVMBOX_SIZE are
rebuilt. All other archive members and SDK packages stay intact.
No ABI-affecting options (including window scaling) are changed.
"""

import hashlib
import io
import json
import math
import re
import shlex
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

LWIP_REV = 'fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e'
LWIP_SHA256 = '99edee6321fc4e77d279a22313013085ca0c4d90b82ca6f1f79bde6d8aae7780'
SOURCES = ('core/tcp.c', 'core/tcp_in.c', 'core/tcp_out.c', 'core/init.c', 'api/api_msg.c')


def prepare_source(root: Path) -> Path:
    source = root / '.pio/lwip-window/source'
    stamp = source / '.revision'
    if stamp.exists() and stamp.read_text() == LWIP_REV:
        return source
    url = f'https://codeload.github.com/espressif/esp-lwip/tar.gz/{LWIP_REV}'
    print(f'[TCP window] Downloading pinned Espressif lwIP {LWIP_REV[:12]}')
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != LWIP_SHA256:
        raise RuntimeError('lwIP source archive checksum mismatch')
    source.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            relative = Path(*Path(member.name).parts[1:])
            target = source / relative
            if not target.resolve().is_relative_to(source.resolve()):
                raise RuntimeError('Invalid lwIP archive path')
            if member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                stream = archive.extractfile(member)
                if stream is None:
                    raise RuntimeError('Cannot read lwIP archive member')
                target.write_bytes(stream.read())
    stamp.write_text(LWIP_REV)
    return source


def build_archive(root: Path, sdk: Path, build: Path, cc: str, ar: str, memory: str, window: int) -> Path:
    if not 5760 < window <= 65535:
        raise RuntimeError('Experimental TCP window must be 5761..65535 bytes; scaling is intentionally unchanged')
    versions = (sdk / 'versions.txt').read_text()
    if 'esp-idf: v5.5.5 b774170ff46' not in versions:
        raise RuntimeError('TCP window tuning requires the pinned ESP-IDF 5.5.5 b774170ff46 SDK')
    source = prepare_source(root)
    installed = sdk / 'include/lwip/lwip/src/include'
    for header in (source / 'src/include').rglob('*.h'):
        counterpart = installed / header.relative_to(source / 'src/include')
        if counterpart.exists() and counterpart.read_bytes() != header.read_bytes():
            raise RuntimeError(f'Installed SDK lwIP headers differ from pinned source: {counterpart.name}')
    # This token audit catches future source edits that would require replacing
    # further archive members. altcp TLS is not compiled into this SDK archive.
    affected = re.compile(r'\b(?:TCP_WND|TCP_WND_MAX|TCP_WND_UPDATE_THRESHOLD|DEFAULT_TCP_RECVMBOX_SIZE)\b')
    archive = sdk / 'lib/liblwip.a'
    members = subprocess.check_output([ar, 't', str(archive)], text=True).splitlines()
    for path in (source / 'src').rglob('*.c'):
        if affected.search(path.read_text()) and path.name + '.obj' in members:
            if path.relative_to(source / 'src').as_posix() not in SOURCES:
                raise RuntimeError(f'Unaccounted TCP-window dependency: {path}')
    for name in SOURCES:
        if members.count(Path(name).name + '.obj') != 1:
            raise RuntimeError(f'Expected one archive member for {name}')
    config = sdk / memory / 'include/sdkconfig.h'
    config_text = config.read_text()
    if re.search(r'^#define CONFIG_LWIP_WND_SCALE\s+1', config_text, re.M):
        raise RuntimeError('Unexpected window scaling: revalidate SDK ABI before rebuilding')
    mailbox = math.ceil(window / 1436) + 2
    build.mkdir(parents=True, exist_ok=True)
    result = build / 'liblwip.a'
    signature = hashlib.sha256()
    for path in (Path(__file__), archive, config, sdk / 'flags/c_flags', sdk / 'flags/includes'):
        signature.update(path.read_bytes())
    signature.update(f'{cc}:{memory}:{window}:{mailbox}'.encode())
    for name in SOURCES:
        signature.update((source / 'src' / name).read_bytes())
    fingerprint = signature.hexdigest()
    manifest = build / 'manifest.json'
    if result.exists() and manifest.exists() and json.loads(manifest.read_text()).get('fingerprint') == fingerprint:
        return result
    overlay = build / 'tcp_window_config.h'
    overlay.write_text(
        f'#include "{config.as_posix()}"\n'
        '#undef CONFIG_LWIP_TCP_WND_DEFAULT\n'
        f'#define CONFIG_LWIP_TCP_WND_DEFAULT {window}\n'
        '#undef CONFIG_LWIP_TCP_RECVMBOX_SIZE\n'
        f'#define CONFIG_LWIP_TCP_RECVMBOX_SIZE {mailbox}\n'
    )
    flags = shlex.split((sdk / 'flags/c_flags').read_text())
    flags += [
        '-Os',
        '-DESP_PLATFORM',
        '-D_GNU_SOURCE',
        '-include',
        str(overlay),
        '-I' + str(config.parent),
        '-iprefix',
        str(sdk / 'include') + '/',
        '@' + str(sdk / 'flags/includes'),
    ]
    objects = []
    for name in SOURCES:
        original = source / 'src' / name
        path = original
        if name == 'core/tcp.c':
            # A strong symbol lets firmware confirm the rebuilt archive is linked.
            path = build / 'tcp_window.c'
            path.write_text(original.read_text() + '\nunsigned bookorbit_tcp_window_bytes(void) { return TCP_WND; }\n')
        obj = build / (original.name + '.obj')
        subprocess.run([cc, *flags, '-c', str(path), '-o', str(obj)], check=True)
        objects.append(str(obj))
    temporary = build / 'liblwip.pending.a'
    shutil.copyfile(archive, temporary)
    subprocess.run([ar, 'r', str(temporary), *objects], check=True)
    temporary.replace(result)
    manifest.write_text(
        json.dumps(
            {
                'fingerprint': fingerprint,
                'idf': 'v5.5.5 b774170ff46',
                'lwip': LWIP_REV,
                'receive_window': window,
                'receive_mailbox': mailbox,
                'objects': list(SOURCES),
            },
            indent=2,
        )
        + '\n'
    )
    print(f'[TCP window] Rebuilt lwIP: receive window {window}, mailbox {mailbox}')
    return result
