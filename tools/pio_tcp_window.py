"""Post-framework PlatformIO hook for all device environments."""

import sys
from pathlib import Path

Import('env')  # type: ignore[name-defined]  # noqa: F821
env = globals()['env']

root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'tools'))
from tcp_window import build_archive  # noqa: E402

platform = env.PioPlatform()
chip = env.BoardConfig().get('build.mcu')
window_opt = env.GetProjectOption('custom_tcp_window', None)

if chip in ('esp32c3', 'esp32s3') and window_opt:
    sdk = Path(platform.get_package_dir(f'framework-arduinoespressif32-{chip}-libs'))
    window = int(window_opt)
    memory = 'dio_qspi' if chip == 'esp32c3' else 'dio_opi'
    build = Path(env.subst('$BUILD_DIR')) / 'tcp-window'
    archive = build_archive(
        root,
        sdk,
        build,
        env.subst('$CC'),
        env.subst('$AR'),
        memory,
        window,
    )
    libraries = env.Flatten(env['LIBS'])
    if not any(str(lib) in ('lwip', '-llwip') for lib in libraries):
        raise RuntimeError('Cannot find SDK lwIP library in linker inputs')
    # Arduino repeats its SDK library group; replace every occurrence.
    env.Replace(LIBS=[env.File(str(archive)) if str(lib) in ('lwip', '-llwip') else lib for lib in libraries])
else:
    print(f'[pio_tcp_window] Skipping TCP window tuning for {chip} (custom_tcp_window={window_opt})')
