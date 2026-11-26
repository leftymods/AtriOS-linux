#!/usr/bin/env python3
"""Resolve and prepare isolated kernel build trees for a complete board profile.

Existing cache/kernel and explicit local sources are never modified. A local
source is snapshotted; Git metadata and previous AtriOS backup copies are not
build inputs. Internal links are rewritten relative to the snapshot. The known
repository drivers symlink may be materialized; other escaping links fail.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile


SPEC = importlib.util.spec_from_file_location('atrios_kernel_source', Path(__file__).with_name('kernel-source.py'))
SOURCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOURCE)
SKIP = {'.git', '.atrios-source-backups'}


def safe_component(value, label):
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.+-]*', value):
        raise ValueError('Invalid ' + label)
    return value


def inside(path, root):
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        raise ValueError('Kernel source link escapes its snapshot: ' + str(path)) from None


def source_entries(root, approved_drivers=None):
    root = root.resolve(strict=True)
    stack = [(root, Path(), frozenset())]
    while stack:
        directory, prefix, ancestors = stack.pop()
        resolved = directory.resolve(strict=True)
        if resolved in ancestors:
            raise ValueError('Recursive kernel source directory link')
        chain = ancestors | {resolved}
        for path in sorted(directory.iterdir(), key=lambda p: p.name):
            if path.name in SKIP:
                continue
            relative = prefix / path.name
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                target = path.resolve(strict=True)
                if (relative == Path('drivers/atrios') and approved_drivers is not None
                        and target == approved_drivers.resolve()):
                    # Old local trees often link to the repository drivers. Copy
                    # their content rather than leaving a writable external link.
                    yield relative, 'dir', path, None
                    stack.append((target, relative, chain))
                else:
                    target_relative = inside(target, root)
                    yield relative, 'link', path, target_relative.as_posix()
            elif stat.S_ISDIR(mode):
                yield relative, 'dir', path, None
                stack.append((path, relative, chain))
            elif stat.S_ISREG(mode):
                yield relative, 'file', path, stat.S_IMODE(mode)
            else:
                raise ValueError('Special kernel source entry is forbidden: ' + str(relative))


def tree_digest(root, approved_drivers=None, drivers_only=False):
    digest = hashlib.sha256()
    entries = sorted(source_entries(root, approved_drivers), key=lambda entry: entry[0].as_posix())
    for relative, kind, path, extra in entries:
        if drivers_only and (kind != 'file' or not (path.suffix in {'.c', '.h', '.S', '.inc'} or path.name in {'Kconfig', 'Makefile', 'Kbuild'})):
            continue
        if kind == 'file':
            content = hashlib.sha256()
            with path.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    content.update(chunk)
            record = [relative.as_posix(), kind, extra, content.hexdigest()]
        elif kind == 'link':
            record = [relative.as_posix(), kind, extra]
        else:
            record = [relative.as_posix(), kind]
        digest.update(json.dumps(record, separators=(',', ':'), ensure_ascii=True).encode() + b'\n')
    return digest.hexdigest()


def snapshot_source(source, destination, approved_drivers=None):
    destination.mkdir(mode=stat.S_IMODE(source.stat().st_mode) | stat.S_IRWXU)
    for relative, kind, path, extra in source_entries(source, approved_drivers):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if kind == 'dir':
            target.mkdir(mode=stat.S_IMODE(path.stat().st_mode) | stat.S_IRWXU, exist_ok=True)
        elif kind == 'link':
            target.symlink_to(os.path.relpath(destination / extra, target.parent))
        else:
            shutil.copy2(path, target)


def file_identity(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError('Missing kernel profile input: ' + str(path))
    return {'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def project_path(root, value):
    if not value or '\n' in value or '\r' in value or '\0' in value:
        raise ValueError('Invalid kernel profile path')
    path = Path(value).expanduser()
    return (root / path if not path.is_absolute() else path).resolve()


def resolve_profile(root, env=None):
    env = os.environ if env is None else env
    root = Path(root).resolve(strict=True)
    board = env.get('BOARD', '')
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', board):
        raise ValueError('Invalid kernel board identifier')
    arch = env.get('KERNEL_ARCH') or env.get('ARCH', '')
    if arch not in ('arm64', 'arm'):
        raise ValueError('Set KERNEL_ARCH to arm64 or arm for this board')
    family = env.get('BOARDFAMILY', '')
    subdir = None if env.get('KERNEL_DTS_SUBDIR_UNSET') == '1' else env.get('KERNEL_DTS_SUBDIR')
    if subdir is None:
        if family != 'amlogic-meson64':
            raise ValueError('KERNEL_DTS_SUBDIR must be explicitly configured for this family')
        subdir = 'amlogic'
    if subdir and (subdir.startswith('/') or '\\' in subdir or any(not re.fullmatch(r'[A-Za-z0-9_-]+', part) for part in subdir.split('/'))):
        raise ValueError('Invalid KERNEL_DTS_SUBDIR')
    dtb_config = env.get('KERNEL_DTB_CONFIG') or ('CONFIG_ARCH_MESON' if family == 'amlogic-meson64' else 'y')
    if dtb_config != 'y' and not re.fullmatch(r'CONFIG_[A-Z0-9_]+', dtb_config):
        raise ValueError('Invalid KERNEL_DTB_CONFIG')
    image = safe_component(env.get('KERNEL_IMAGE_TARGET') or ('Image' if arch == 'arm64' else 'zImage'), 'KERNEL_IMAGE_TARGET')
    source_dir = env.get('UBOOT_SOURCE_DIR', '')
    config = project_path(root, env.get('KERNEL_CONFIG_PATH') or str(Path(source_dir) / env.get('KERNEL_DEFCONFIG', '')))
    dts = project_path(root, env.get('KERNEL_DTS_PATH') or str(Path(source_dir) / 'dts' / (env.get('DTS_NAME', '') + '.dts')))
    logical_dts = safe_component(env.get('DTS_NAME', ''), 'DTS name')
    if dts.stem != logical_dts or dts.suffix != '.dts':
        raise ValueError('KERNEL_DTS_PATH basename must match DTS_NAME + .dts')
    patch_dir = project_path(root, env.get('KERNEL_PATCH_DIR') or str(Path(source_dir) / 'patches/linux-7.0'))
    drivers = project_path(root, env.get('KERNEL_DRIVERS_DIR') or 'drivers/atrios')
    if not drivers.is_dir():
        raise ValueError('Missing kernel driver sources: ' + str(drivers))
    patches = [file_identity(path) for path in sorted(patch_dir.glob('*.patch'))] if patch_dir.is_dir() else []
    local = env.get('KERNEL_SOURCE_DIR')
    if local:
        source = project_path(root, local)
        if not (source / 'Makefile').is_file() or (source / 'Makefile').is_symlink():
            raise ValueError('Invalid KERNEL_SOURCE_DIR: ' + str(source))
        if (root / 'cache/kernel-profiles').is_relative_to(source):
            raise ValueError('Kernel profile cache must not be inside KERNEL_SOURCE_DIR')
        origin = {'kind': 'local', 'path': str(source), 'tree_sha256': tree_digest(source, drivers)}
    else:
        url, sha = env.get('KERNEL_ARCHIVE_URL', ''), env.get('KERNEL_ARCHIVE_SHA256', '')
        SOURCE.validate_pin(url, sha)
        origin = {'kind': 'archive', 'url': url, 'sha256': sha.lower()}
    identity = {'schema': 1, 'board': board, 'source': origin, 'arch': arch, 'dts_subdir': subdir,
                'dtb_config': dtb_config, 'image_target': image, 'cross_compile': env.get('CROSS_COMPILE', ''),
                'config': file_identity(config), 'dts': file_identity(dts), 'patches': patches,
                'drivers': {'path': str(drivers), 'sha256': tree_digest(drivers, drivers_only=True)}}
    fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    build = root / 'cache/kernel-profiles' / board / fingerprint
    return {'identity': identity, 'fingerprint': fingerprint, 'build_dir': str(build),
            'config': str(config), 'dts': str(dts), 'patch_dir': str(patch_dir), 'drivers': str(drivers)}


def prepare_profile(profile):
    destination = Path(profile['build_dir'])
    for parent in destination.parents:
        if parent.is_symlink():
            raise ValueError('Kernel profile cache must not follow a directory symlink')
    marker = destination / '.atrios-kernel-profile.json'
    if destination.is_symlink():
        raise ValueError('Kernel profile cache must not be a symlink')
    if destination.exists():
        if (not destination.is_dir() or not marker.is_file() or marker.is_symlink()
                or json.loads(marker.read_text()) != profile or not (destination / 'Makefile').is_file()
                or (destination / 'Makefile').is_symlink()):
            raise ValueError('Existing kernel profile cache does not match its selected inputs; preserved unchanged')
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    origin = profile['identity']['source']
    with tempfile.TemporaryDirectory(prefix='.kernel-profile-', dir=destination.parent) as temporary:
        staged = Path(temporary) / 'source'
        if origin['kind'] == 'local':
            snapshot_source(Path(origin['path']), staged, Path(profile['drivers']))
            if tree_digest(staged) != origin['tree_sha256']:
                raise ValueError('Local kernel source changed while taking its isolated snapshot')
            # A read-only source is a valid input; only the private copy is writable.
            for relative, kind, path, _ in source_entries(staged):
                if kind != 'link':
                    path.chmod(stat.S_IMODE(path.stat().st_mode) | stat.S_IWUSR | (stat.S_IXUSR if kind == 'dir' else 0))
        else:
            SOURCE.fetch_kernel(origin['url'], origin['sha256'], staged)
        (staged / '.atrios-kernel-profile.json').write_text(json.dumps(profile, sort_keys=True) + '\n')
        if destination.exists() or destination.is_symlink():
            raise ValueError('Kernel profile cache appeared during preparation; refusing replacement')
        os.rename(staged, destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    try:
        profile = resolve_profile(args.project_root)
        if args.prepare:
            prepare_profile(profile)
        identity = profile['identity']
        for value in [profile['build_dir'], identity['arch'], identity['dts_subdir'], identity['dtb_config'],
                      identity['image_target'], profile['config'], profile['dts'], profile['patch_dir'],
                      profile['drivers'], profile['fingerprint']]:
            print(value)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, 'Kernel profile preparation failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
