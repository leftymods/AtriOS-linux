#!/usr/bin/env python3
"""Select public OTA runtime from a built board rootfs, excluding device state.

The default selection is the reviewed HTTPS client/runtime plus boot services,
public update configuration, normalized vendor fish scripts and the signed
upstream regulatory database pair. A board may add reviewed paths through a
JSON manifest {"schema": 1, "paths": ["usr/lib/vendor/helper"]}.
"""
import argparse
from collections import deque
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import shlex
import tempfile

RUNTIME_FILES = (
    'usr/bin/atrios',
    *('usr/lib/atrios/' + name for name in (
        'atrios-runtime', 'atri-aec', 'atriai', 'bt-agent', 'bt-control',
        'bt-volume-sync', 'hardware', 'light', 'micprobe', 'ntp', 'ota',
        'rotaryd', 'rtk-bt-bridge', 'rtk-bt-init', 'screend', 'supervise',
        'wifi-scan', 'wireless', 'ymusic', 'console.sh',
        'libonnxruntime.so', 'LICENSE.onnxruntime')),
    'usr/share/atrios/models/gtcrn/gtcrn_simple.onnx',
    'usr/share/atrios/models/gtcrn/LICENSE',
    'etc/atrios/ota-public.pem', 'etc/atrios/updates.json',
    'etc/atrios/hardware.json', 'etc/os-release',
    'etc/atrios-console', 'etc/fish/config.fish',
)
FORBIDDEN_TOP = {'root', 'home', 'var', 'run', 'dev', 'proc', 'sys', 'tmp', 'boot', '.git'}
FORBIDDEN_FILES = {'etc/passwd', 'etc/shadow', 'etc/group', 'etc/gshadow',
                   'etc/machine-id', 'usr/lib/atrios/atrios-api'}
PRIVATE_FIELDS = {'token', 'password', 'passwd', 'secret', 'api_key', 'private_key', 'client_secret'}
MAX_FILES = 50000
MAX_FILE_SIZE = 64 * 1024 * 1024


def public_path(value):
    if not isinstance(value, str) or not value or '\\' in value or '\x00' in value:
        raise ValueError('Invalid runtime path')
    path = PurePosixPath(value)
    if path.is_absolute() or '..' in path.parts or str(path) != value or not path.parts:
        raise ValueError('Runtime path must be canonical and relative')
    lower = value.lower()
    if path.parts[0] in FORBIDDEN_TOP or value in FORBIDDEN_FILES or lower.startswith('etc/ssh/'):
        raise ValueError('Private, legacy or user-owned runtime path is forbidden: ' + value)
    if value == 'etc/atrios' or (value.startswith('etc/atrios/') and value not in ('etc/atrios/ota-public.pem', 'etc/atrios/updates.json', 'etc/atrios/hardware.json')):
        raise ValueError('Only public OTA configuration may be staged')
    if any(part.lower() in ('__pycache__', '.git') for part in path.parts):
        raise ValueError('Cache or repository metadata is not a runtime payload')
    name = path.name.lower()
    if name.endswith(('.key', '.p12', '.pfx')) or any(word in name for word in ('token', 'password', 'secret', 'private')):
        raise ValueError('Private credential path is forbidden: ' + value)
    return value


def resolve_in_rootfs(root, name):
    # Absolute symlinks inside an offline root are chroot-relative, never host
    # filesystem paths. Resolve one component at a time without Path.resolve().
    pending, components, links = deque(PurePosixPath(name).parts), [], 0
    while pending:
        part = pending.popleft()
        if part in ('', '.'):
            continue
        if part == '..':
            if not components:
                raise ValueError('Runtime symlink escapes built rootfs: ' + name)
            components.pop()
            continue
        path = root.joinpath(*components, part)
        try:
            path.lstat()
        except FileNotFoundError:
            if links:
                raise ValueError('Broken runtime symlink inside built rootfs: ' + name) from None
            raise
        if path.is_symlink():
            links += 1
            if links > 40:
                raise ValueError('Too many runtime symlinks: ' + name)
            target = PurePosixPath(os.readlink(path))
            if target.is_absolute():
                components = []
                parts = target.parts[1:]
            else:
                parts = target.parts
            pending.extendleft(reversed(parts))
        else:
            components.append(part)
    resolved_name = '/'.join(components)
    public_path(resolved_name)
    return root.joinpath(*components)


def source_file(root, name):
    path = resolve_in_rootfs(root, name)
    if not path.is_file() or path.stat().st_size > MAX_FILE_SIZE:
        raise ValueError('Runtime source is not a bounded regular file: ' + name)
    return path


def reject_private_data(path, logical_name):
    data = path.read_bytes()
    if re.search(rb'(?m)^-----BEGIN [A-Z0-9 -]*PRIVATE KEY(?: BLOCK)?-----', data):
        raise ValueError('Private signing/client key cannot be staged')
    json_file = PurePosixPath(logical_name).suffix == '.json'
    if json_file or data.lstrip().startswith((b'{', b'[')):
        try:
            value = json.loads(data)
        except (ValueError, UnicodeError):
            if json_file:
                raise ValueError('Public JSON configuration is invalid') from None
            return
        def visit(item):
            if isinstance(item, dict):
                if any(str(key).lower() in PRIVATE_FIELDS for key in item):
                    raise ValueError('Credential fields cannot be staged')
                for child in item.values(): visit(child)
            elif isinstance(item, list):
                for child in item: visit(child)
        visit(value)


def manifest_paths(path):
    data = json.loads(Path(path).read_text())
    if not isinstance(data, dict) or set(data) != {'schema', 'paths'} or data['schema'] != 1:
        raise ValueError('Runtime manifest requires schema 1 and paths')
    if not isinstance(data['paths'], list) or len(data['paths']) > MAX_FILES:
        raise ValueError('Runtime manifest paths must be a bounded list')
    return [public_path(name) for name in data['paths']]


def stage_runtime(source, destination, board, version, runtime_manifest=None):
    source, destination = Path(source), Path(destination)
    if source.is_symlink() or not source.is_dir():
        raise ValueError('Built rootfs must be a real directory')
    source = source.resolve()
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError('Runtime destination already exists')
    if destination.resolve().is_relative_to(source):
        raise ValueError('Runtime destination must be outside the built rootfs')
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', board) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+){0,3}', version):
        raise ValueError('Invalid board/version')
    if source_file(source, 'etc/atrios-board').read_text().strip() != board:
        raise ValueError('Built rootfs belongs to another board')
    if source_file(source, 'etc/atrios-version').read_text().strip() != version:
        raise ValueError('Built rootfs version does not match release tag')
    os_release = source_file(source, 'etc/os-release').read_text()
    versions = [shlex.split(line.split('=', 1)[1]) for line in os_release.splitlines() if line.startswith('VERSION_ID=')]
    if versions != [[version]]:
        raise ValueError('Built rootfs OS branding does not match release tag')
    plan = {}
    def add(name):
        public_path(name)
        path = source_file(source, name)
        if name in plan and plan[name] != path:
            raise ValueError('Duplicate runtime destination')
        reject_private_data(path, name)
        plan[name] = path
        if len(plan) > MAX_FILES: raise ValueError('Too many runtime files')
    def add_tree(name, *, fish_only=False):
        public_path(name)
        directory = resolve_in_rootfs(source, name)
        if not directory.is_dir(): raise ValueError('Runtime directory does not exist: ' + name)
        for parent, dirs, files in os.walk(directory, followlinks=False):
            if any((Path(parent) / item).is_symlink() for item in dirs):
                raise ValueError('Runtime directory symlinks are forbidden')
            dirs[:] = [item for item in dirs if item not in ('__pycache__', '.git')]
            for item in files:
                relative = (PurePosixPath(name) / Path(parent).relative_to(directory).as_posix() / item).as_posix()
                if not fish_only or relative.endswith('.fish'): add(relative)
    for name in RUNTIME_FILES:
        try:
            source_file(source, name)
        except FileNotFoundError:
            continue
        add(name)
    for required in ('usr/lib/atrios/ota', 'etc/atrios/ota-public.pem', 'etc/os-release'):
        if required not in plan: raise ValueError('Required OTA runtime is missing: ' + required)
    for name, fish in [('etc/init.d', False), ('usr/share/fish', True)]:
        try:
            directory = resolve_in_rootfs(source, name)
        except FileNotFoundError:
            continue
        if directory.is_dir(): add_tree(name, fish_only=fish)
    pair = ['lib/firmware/regulatory.db-upstream', 'lib/firmware/regulatory.db.p7s-upstream']
    for name in pair:
        try:
            source_file(source, name)
        except FileNotFoundError:
            raise ValueError('A complete upstream database/signature pair is required') from None
        add(name)
    if runtime_manifest:
        for name in manifest_paths(runtime_manifest):
            if resolve_in_rootfs(source, name).is_dir(): add_tree(name)
            else: add(name)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.ota-runtime-', dir=destination.parent) as temporary:
        staging = Path(temporary) / 'runtime'
        staging.mkdir()
        for name, path in sorted(plan.items()):
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        os.replace(staging, destination)
    return sorted(plan)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'destination'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('board', 'version'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--runtime-manifest', type=Path)
    args = parser.parse_args()
    try:
        files = stage_runtime(**vars(args))
    except (ValueError, OSError, json.JSONDecodeError) as error:
        parser.exit(1, 'OTA runtime staging failed: ' + str(error) + '\n')
    print('Staged public OTA runtime files:', len(files))


if __name__ == '__main__':
    main()
