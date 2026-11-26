#!/usr/bin/env python3
"""Stage repository drivers/config/DTS, preserving replaced kernel-tree files."""
import argparse
import hashlib
import re
from pathlib import Path
import shutil
import sys


def check_destination(kernel, target):
    if not target.resolve().is_relative_to(kernel.resolve()):
        raise RuntimeError(f'Kernel-tree destination escapes its private source root: {target}')


def preserve_and_write(kernel, target, content):
    check_destination(kernel, target)
    if target.is_symlink():
        raise RuntimeError(f'Refusing to overwrite kernel-tree symlink: {target}')
    if target.exists():
        old = target.read_bytes()
        if old == content:
            return
        digest = hashlib.sha256(old).hexdigest()[:16]
        backup = kernel / '.atrios-source-backups' / target.relative_to(kernel)
        backup = backup.with_name(backup.name + '.' + digest)
        check_destination(kernel, backup)
        if backup.is_symlink():
            raise RuntimeError(f'Refusing kernel backup symlink: {backup}')
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(target, backup)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(content)


def add_rule(kernel, relative, rule, matches):
    target = kernel / relative
    check_destination(kernel, target)
    lines = target.read_text().splitlines()
    found = False
    updated = []
    for line in lines:
        if matches(line):
            if found:
                continue
            found = True
        updated.append(line)
    if not found:
        updated.append(rule)
    preserve_and_write(kernel, target, ('\n'.join(updated) + '\n').encode())


def integrate(kernel, drivers, dts, config, *, arch='arm64', dts_subdir='amlogic', dtb_config='CONFIG_ARCH_MESON'):
    if arch not in ('arm64', 'arm'):
        raise ValueError('Unsupported kernel architecture: ' + arch)
    if dts_subdir and (dts_subdir.startswith('/') or '\\' in dts_subdir or
                       any(not re.fullmatch(r'[A-Za-z0-9_-]+', p) for p in dts_subdir.split('/'))):
        raise ValueError('Invalid kernel DTS subdirectory')
    if dtb_config != 'y' and not re.fullmatch(r'CONFIG_[A-Z0-9_]+', dtb_config):
        raise ValueError('Invalid kernel DTB configuration')
    kernel = kernel.resolve()
    drivers = drivers.resolve()
    destination = kernel / 'drivers/atrios'
    if destination.is_symlink():
        if destination.resolve() != drivers:
            raise RuntimeError(f'Refusing foreign drivers symlink: {destination}')
        # The existing repository symlink always exposes current source files.
    else:
        for source in drivers.rglob('*'):
            if source.is_file() and (source.suffix in {'.c', '.h', '.S', '.inc'} or source.name in {'Kconfig', 'Makefile', 'Kbuild'}):
                preserve_and_write(kernel, destination / source.relative_to(drivers), source.read_bytes())
    add_rule(kernel, 'drivers/Kconfig', 'source "drivers/atrios/Kconfig"',
             lambda line: line.strip() == 'source "drivers/atrios/Kconfig"')
    add_rule(kernel, 'drivers/Makefile', 'obj-y += atrios/',
             lambda line: line.strip().startswith('obj-') and 'atrios/' in line.split('#')[0].split())
    dtb = dts.with_suffix('.dtb').name
    relative = Path('arch') / arch / 'boot/dts' / dts_subdir
    condition = 'y' if dtb_config == 'y' else '$(' + dtb_config + ')'
    add_rule(kernel, relative / 'Makefile', f'dtb-{condition} += {dtb}',
             lambda line: dtb in line.split('#')[0].split())
    preserve_and_write(kernel, kernel / relative / dts.name, dts.read_bytes())
    preserve_and_write(kernel, kernel / '.config', config.read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kernel', type=Path)
    parser.add_argument('drivers', type=Path)
    parser.add_argument('dts', type=Path)
    parser.add_argument('config', type=Path)
    parser.add_argument('--arch', default='arm64')
    parser.add_argument('--dts-subdir', default='amlogic')
    parser.add_argument('--dtb-config', default='CONFIG_ARCH_MESON')
    args = parser.parse_args()
    integrate(args.kernel, args.drivers, args.dts, args.config, arch=args.arch,
              dts_subdir=args.dts_subdir, dtb_config=args.dtb_config)
