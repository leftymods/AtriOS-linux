#!/usr/bin/env python3
"""Build deterministic, RSA-signed OTA bundles accepted by the on-device installer.

Rootfs sources must contain only directories and regular files. The build/source
symlinks directly under the selected kernel's modules directory are deliberately
excluded; all other links, special files and forbidden runtime paths are errors.
Kernel modules and firmware are emitted under logical lib/, including when the
input tree uses usr/lib/, so previously installed OTA clients update active files.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import tarfile
import tempfile

MAX_DOWNLOAD = 512 * 1024 * 1024
MAX_EXPANDED = 2 * 1024 * 1024 * 1024
MAX_MEMBERS = 50000
MAX_KERNEL = 128 * 1024 * 1024
MAX_DTB = 16 * 1024 * 1024


class BoundedWriter:
    def __init__(self, stream, limit):
        self.stream, self.limit, self.total = stream, limit, 0

    def write(self, data):
        if self.total + len(data) > self.limit:
            raise ValueError('Archive exceeds size limit')
        count = self.stream.write(data)
        self.total += count
        return count

    def flush(self):
        return self.stream.flush()


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            result.update(chunk)
    return result.hexdigest()


def run_openssl(arguments):
    result = subprocess.run(['openssl', *map(str, arguments)], stdin=subprocess.DEVNULL,
                            capture_output=True, timeout=30)
    if result.returncode:
        raise ValueError('OpenSSL failed: signing key must be an unencrypted RSA private key')
    return result.stdout


def safe_name(name):
    path = PurePosixPath(name)
    if not name or name.startswith('/') or '\\' in name or any(x in ('', '.', '..') for x in name.split('/')):
        raise ValueError('Unsafe payload path: ' + name)
    if len(name.encode('utf-8')) > 255:
        raise ValueError('Payload path is too long: ' + name)
    return path


def rootfs_name(name):
    path = safe_name(name)
    if path.parts[0] in {'proc', 'sys', 'dev', 'run', 'tmp', 'boot'} or name.startswith(('var/run/', 'var/lib/atrios/ota-')):
        raise ValueError('Forbidden rootfs destination: ' + name)
    return path


def canonical_rootfs_name(name):
    parts = safe_name(name).parts
    if parts[:3] in (('usr', 'lib', 'modules'), ('usr', 'lib', 'firmware')):
        return PurePosixPath('lib', *parts[2:]).as_posix()
    return name


def module_build_link(name, release):
    return name in {f'lib/modules/{release}/build', f'lib/modules/{release}/source',
                    f'usr/lib/modules/{release}/build', f'usr/lib/modules/{release}/source'}


def collect_tree(source, release, prefix=''):
    if source.is_symlink() or not source.is_dir():
        raise ValueError('Rootfs/modules input must be a real directory: ' + str(source))
    result = {}
    pending = [source]
    members = 0
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                members += 1
                if members > MAX_MEMBERS:
                    raise ValueError('Too many rootfs entries')
                path = Path(entry.path)
                name = canonical_rootfs_name(prefix + path.relative_to(source).as_posix())
                mode = entry.stat(follow_symlinks=False).st_mode
                if stat.S_ISLNK(mode) and module_build_link(name, release):
                    continue
                if not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
                    raise ValueError('Links and special rootfs files are forbidden: ' + name)
                rootfs_name(name)
                if stat.S_ISREG(mode):
                    if name in result:
                        raise ValueError('Duplicate canonical rootfs destination: ' + name)
                    result[name] = path
                else:
                    pending.append(path)
    return result


def snapshot(source, destination, limit, private_key):
    if source.is_symlink():
        raise ValueError('Symlink input is forbidden: ' + str(source))
    if source.resolve() == private_key.resolve():
        raise ValueError('Private signing key cannot be a payload')
    fd = os.open(source, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    try:
        with os.fdopen(fd, 'rb') as src:
            before = os.fstat(src.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
                raise ValueError('Input is not a bounded regular file: ' + str(source))
            head = src.read(4096)
            if re.search(br'-----BEGIN [A-Z ]*PRIVATE KEY-----', head):
                raise ValueError('Private key material cannot be included in an OTA payload')
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open('xb') as out:
                out.write(head)
                copied = len(head)
                while True:
                    chunk = src.read(65536)
                    if not chunk:
                        break
                    copied += len(chunk)
                    if copied > limit:
                        raise ValueError('Input grew beyond size limit: ' + str(source))
                    out.write(chunk)
            after = os.fstat(src.fileno())
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError('Input changed during packaging: ' + str(source))
            os.chmod(destination, stat.S_IMODE(before.st_mode) & 0o777)
    except BaseException:
        destination.unlink(missing_ok=True)
        raise


def validate_kernel(path):
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(64)
    if size < 64 or header[56:60] != b'ARM\x64':
        raise ValueError('Kernel is not an uncompressed ARM64 Linux Image')
    image_size = struct.unpack_from('<Q', header, 16)[0]
    # image_size is the in-memory footprint, including BSS; it can exceed file size.
    if image_size and not 64 <= image_size <= 512 * 1024 * 1024:
        raise ValueError('Kernel header declares an invalid memory footprint')


def validate_dtb(path):
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(40)
    if len(header) != 40:
        raise ValueError('Truncated DTB header')
    magic, total, structures, strings, reserve, version, compatible, _, string_size, structure_size = struct.unpack('>10I', header)
    if magic != 0xd00dfeed or total != size or version < 17 or compatible > 17:
        raise ValueError('Invalid DTB header')
    if not (40 <= reserve <= size - 16 and 40 <= structures <= size and 40 <= strings <= size
            and structures + structure_size <= size and strings + string_size <= size):
        raise ValueError('DTB blocks exceed file bounds')

    if reserve % 8 or structures % 4:
        raise ValueError('Unaligned DTB block')
    data = path.read_bytes()  # bounded by MAX_DTB in snapshot()
    cursor = reserve
    while cursor + 16 <= size:
        address, length = struct.unpack_from('>QQ', data, cursor)
        cursor += 16
        if address == length == 0:
            break
    else:
        raise ValueError('Unterminated DTB memory reservation map')
    cursor, end, depth, roots, finished = structures, structures + structure_size, 0, 0, False
    while cursor + 4 <= end:
        token = struct.unpack_from('>I', data, cursor)[0]
        cursor += 4
        if token == 1:  # FDT_BEGIN_NODE
            if depth == 0:
                roots += 1
            terminator = data.find(b'\0', cursor, end)
            if terminator < 0:
                raise ValueError('Unterminated DTB node name')
            cursor = (terminator + 4) & ~3
            depth += 1
        elif token == 2:  # FDT_END_NODE
            depth -= 1
            if depth < 0:
                raise ValueError('Unbalanced DTB node')
        elif token == 3:  # FDT_PROP
            if depth == 0 or cursor + 8 > end:
                raise ValueError('Malformed DTB property')
            length, name_offset = struct.unpack_from('>II', data, cursor)
            cursor += 8
            if name_offset >= string_size or data.find(b'\0', strings + name_offset, strings + string_size) < 0:
                raise ValueError('Invalid DTB property string')
            cursor = (cursor + length + 3) & ~3
        elif token == 4:  # FDT_NOP
            continue
        elif token == 9:  # FDT_END
            if depth != 0 or roots != 1:
                raise ValueError('Incomplete DTB tree')
            finished = True
            break
        else:
            raise ValueError('Invalid DTB structure token')
        if cursor > end:
            raise ValueError('DTB structure exceeds its block')
    if not finished:
        raise ValueError('Missing DTB END token')


def archive(destination, files):
    with destination.open('xb') as raw:
        compressed = BoundedWriter(raw, MAX_DOWNLOAD)
        with gzip.GzipFile(filename='', mode='wb', fileobj=compressed, mtime=0, compresslevel=9) as gz:
            expanded = BoundedWriter(gz, MAX_EXPANDED)
            with tarfile.open(fileobj=expanded, mode='w|', format=tarfile.USTAR_FORMAT) as tf:
                for name, source in sorted(files.items()):
                    safe_name(name)
                    info = tarfile.TarInfo(name)
                    info.size = source.stat().st_size
                    info.uid = info.gid = info.mtime = 0
                    info.uname = info.gname = ''
                    info.mode = source.stat().st_mode & 0o777
                    with source.open('rb') as stream:
                        tf.addfile(info, stream)
        raw.flush()
        os.fsync(raw.fileno())


def build_bundle(*, board, version, kernel_release, kernel, dtb, signing_key, output_dir,
                 rootfs=None, modules=None):
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', board):
        raise ValueError('Invalid board identifier')
    if len(version) > 64 or not re.fullmatch(r'v?[0-9]+(?:\.[0-9]+){0,3}', version):
        raise ValueError('Invalid version')
    if not re.fullmatch(r'[A-Za-z0-9._+-]{1,128}', kernel_release):
        raise ValueError('Invalid kernel release')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]*\.dtb', dtb.name):
        raise ValueError('Invalid DTB filename')
    if signing_key.is_symlink() or not signing_key.is_file():
        raise ValueError('An explicit regular RSA signing key is required')
    # Validate RSA before creating output; do not print or copy private material.
    run_openssl(['rsa', '-in', signing_key, '-check', '-noout'])
    output_dir = output_dir.resolve()
    sources = {}
    for source in (rootfs, modules):
        if source is not None and output_dir.is_relative_to(source.resolve()):
            raise ValueError('Output directory must be outside rootfs/modules inputs')
    if rootfs is not None:
        sources.update(collect_tree(rootfs, kernel_release))
    if modules is not None:
        module_files = collect_tree(modules, kernel_release, f'lib/modules/{kernel_release}/')
        if set(sources) & set(module_files):
            raise ValueError('Duplicate rootfs/module destinations')
        sources.update(module_files)
    if len(sources) > MAX_MEMBERS:
        raise ValueError('Too many rootfs files')
    # Carry the full modules_install result, including dependency indices.
    module_prefixes = [f'lib/modules/{kernel_release}/', f'usr/lib/modules/{kernel_release}/']
    selected = [prefix for prefix in module_prefixes if prefix + 'modules.dep' in sources]
    if len(selected) != 1:
        raise ValueError('Exactly one matching complete kernel modules tree is required')
    prefix = selected[0]
    for index in ('modules.dep', 'modules.alias', 'modules.builtin', 'modules.order'):
        if prefix + index not in sources:
            raise ValueError('Incomplete modules tree: missing ' + index)
    dependency_index = sources[prefix + 'modules.dep']
    if dependency_index.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('modules.dep exceeds size limit')
    for line in dependency_index.read_text().splitlines():
        for module in line.replace(':', ' ').split():
            safe_name(module)
            if prefix + module not in sources:
                raise ValueError('modules.dep references missing module: ' + module)
    output_dir.mkdir(parents=True, exist_ok=True)
    filename = f'AtriOS-{board}-ota.tar.gz'
    with tempfile.TemporaryDirectory(prefix='.ota-build-', dir=output_dir) as temporary:
        work = Path(temporary)
        snapshots = {}
        total = 0
        for index, (name, source) in enumerate(sorted(sources.items())):
            target = work / 'rootfs-files' / str(index)
            snapshot(source, target, MAX_EXPANDED - total, signing_key)
            total += target.stat().st_size
            snapshots[name] = target
        rootfs_archive = work / 'rootfs.tar.gz'
        archive(rootfs_archive, snapshots)
        image = work / 'Image'; snapshot(kernel, image, MAX_KERNEL, signing_key); validate_kernel(image)
        tree = work / dtb.name; snapshot(dtb, tree, MAX_DTB, signing_key); validate_dtb(tree)
        payloads = {'kernel': ('payload/Image', image), 'dtb': ('payload/' + dtb.name, tree),
                    'rootfs': ('payload/rootfs.tar.gz', rootfs_archive)}
        manifest = {'schema': 1, 'device_id': board, 'version': version,
                    'kernel_release': kernel_release, 'dtb_name': dtb.name,
                    'files': {kind: {'path': name, 'sha256': digest(path)}
                              for kind, (name, path) in payloads.items()}}
        manifest_path = work / 'manifest.json'
        manifest_path.write_bytes((json.dumps(manifest, sort_keys=True, separators=(',', ':')) + '\n').encode())
        signature = work / 'manifest.sig'
        run_openssl(['dgst', '-sha256', '-sign', signing_key, '-sigopt', 'rsa_padding_mode:pkcs1', '-out', signature, manifest_path])
        for path in (image, tree, rootfs_archive, manifest_path, signature):
            os.chmod(path, 0o644)
        bundle = work / filename
        archive(bundle, {**{name: path for name, path in payloads.values()},
                         'manifest.json': manifest_path, 'manifest.sig': signature})
        checksum = work / (filename + '.sha256')
        checksum.write_text(digest(bundle) + '  ' + filename + '\n', encoding='ascii')
        with checksum.open('rb') as stream:
            os.fsync(stream.fileno())
        # Each public file appears by rename, never as a partially written download.
        os.replace(bundle, output_dir / filename)
        os.replace(checksum, output_dir / checksum.name)
        fd = os.open(output_dir, os.O_RDONLY | getattr(os, 'O_DIRECTORY', 0))
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    return output_dir / filename


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--kernel-release', required=True)
    parser.add_argument('--kernel', required=True, type=Path)
    parser.add_argument('--dtb', required=True, type=Path)
    parser.add_argument('--signing-key', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--rootfs', type=Path, help='Regular-file runtime overlay directory')
    parser.add_argument('--modules', type=Path, help='Complete modules_install kernel-release directory')
    args = parser.parse_args()
    try:
        result = build_bundle(**vars(args))
    except (ValueError, OSError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f'OTA bundle failed: {error}\n')
    print(result)


if __name__ == '__main__':
    main()
