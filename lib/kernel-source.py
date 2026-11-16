#!/usr/bin/env python3
"""Fetch a pinned HTTPS kernel archive and publish a fully validated source tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import ssl
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request

MAX_ARCHIVE = 512 * 1024 * 1024
MAX_EXPANDED = 4 * 1024 * 1024 * 1024
MAX_MEMBERS = 250000


def validate_pin(url, sha256):
    parsed = urllib.parse.urlsplit(url)
    filename = PurePosixPath(parsed.path).name
    if (parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password
            or parsed.fragment or not re.fullmatch(r'linux-[A-Za-z0-9.+_-]+\.tar\.xz', filename)):
        raise ValueError('KERNEL_ARCHIVE_URL must name an HTTPS Linux .tar.xz archive')
    if not re.fullmatch(r'[0-9a-fA-F]{64}', sha256):
        raise ValueError('KERNEL_ARCHIVE_SHA256 must be an explicit SHA-256 digest')
    return filename[:-len('.tar.xz')]


class HTTPSRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        if urllib.parse.urlsplit(new_url).scheme != 'https':
            raise ValueError('Refusing a kernel archive redirect outside HTTPS')
        return super().redirect_request(request, fp, code, message, headers, new_url)


class BoundedTarInfo(tarfile.TarInfo):
    @classmethod
    def frombuf(cls, buf, encoding, errors):
        info = super().frombuf(buf, encoding, errors)
        if info.type in (tarfile.GNUTYPE_LONGNAME, tarfile.GNUTYPE_LONGLINK, tarfile.XHDTYPE, tarfile.XGLTYPE) and info.size > 65536:
            raise ValueError('Oversized kernel archive metadata')
        return info


def verify_digest(archive, expected):
    if archive.stat().st_size > MAX_ARCHIVE:
        raise ValueError('Kernel archive exceeds download size limit')
    digest = hashlib.sha256()
    with archive.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != expected.lower():
        raise ValueError('Kernel archive SHA-256 mismatch; source tree was not created')


def within(path, root):
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Kernel archive path or link escapes its source root')


def stage_archive(archive, sha256, destination, expected_root, source_url=None):
    verify_digest(archive, sha256)
    if destination.exists() or destination.is_symlink():
        raise ValueError('Refusing to replace an existing kernel cache')
    if not hasattr(tarfile, 'data_filter'):
        raise ValueError('Kernel extraction requires an updated Python with tarfile.data_filter')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kernel-stage-', dir=destination.parent) as temporary:
        work = Path(temporary)
        root = work / expected_root
        seen, links, expanded = set(), [], 0

        def safe_member(info, target_dir):
            name = info.name.rstrip('/') if info.isdir() else info.name
            parts = name.split('/')
            if (not name or any(part in ('', '.', '..') for part in parts)
                    or parts[0] != expected_root or '\\' in name):
                raise ValueError('Unsafe kernel archive path: ' + name)
            if name == expected_root and not info.isdir():
                raise ValueError('Kernel archive root is not a directory')
            if name in seen:
                raise ValueError('Duplicate kernel archive path: ' + name)
            seen.add(name)
            if not (info.isfile() or info.isdir() or info.issym() or info.islnk()) or getattr(info, 'sparse', None):
                raise ValueError('Special kernel archive member is forbidden: ' + name)
            if not info.isfile() and info.size:
                raise ValueError('Non-file kernel archive member has payload')
            target = work / name
            within(target, root)
            if info.issym() or info.islnk():
                if PurePosixPath(info.linkname).is_absolute() or '\\' in info.linkname:
                    raise ValueError('Unsafe kernel archive link')
                link_target = (target.parent if info.issym() else work) / info.linkname
                within(link_target, root)
                links.append(target)
            return tarfile.data_filter(info, target_dir)

        with tarfile.open(archive, mode='r|xz', tarinfo=BoundedTarInfo) as tf:
            for count, info in enumerate(tf, 1):
                if count > MAX_MEMBERS or info.size < 0:
                    raise ValueError('Kernel archive member limit exceeded')
                expanded += info.size
                if expanded > MAX_EXPANDED:
                    raise ValueError('Expanded kernel archive size limit exceeded')
                tf.extract(info, work, filter=safe_member)
        for link in links:
            within(link, root)
        if not (root / 'Makefile').is_file() or (root / 'Makefile').is_symlink():
            raise ValueError('Kernel archive has no regular top-level Makefile')
        (root / '.atrios-kernel-source.json').write_text(json.dumps(
            {'url': source_url, 'sha256': sha256.lower()}, sort_keys=True) + '\n')
        if destination.exists() or destination.is_symlink():
            raise ValueError('Kernel cache appeared during extraction; refusing replacement')
        os.rename(root, destination)
    return destination


def fetch_kernel(url, sha256, destination):
    expected_root = validate_pin(url, sha256)
    if destination.exists() or destination.is_symlink():
        raise ValueError('Refusing to replace an existing kernel cache')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kernel-fetch-', dir=destination.parent) as temporary:
        archive = Path(temporary) / 'kernel.tar.xz'
        opener = urllib.request.build_opener(HTTPSRedirects(),
            urllib.request.HTTPSHandler(context=ssl.create_default_context()))
        deadline, size = time.monotonic() + 600, 0
        with opener.open(url, timeout=30) as response, archive.open('xb') as out:
            if urllib.parse.urlsplit(response.geturl()).scheme != 'https':
                raise ValueError('Kernel archive response is not HTTPS')
            while True:
                if time.monotonic() > deadline:
                    raise TimeoutError('Kernel archive download deadline exceeded')
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_ARCHIVE:
                    raise ValueError('Kernel archive download size limit exceeded')
                out.write(chunk)
        return stage_archive(archive, sha256, destination, expected_root, source_url=url)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(fetch_kernel(args.url, args.sha256, args.destination))
    except (OSError, ValueError, tarfile.TarError) as error:
        parser.exit(1, f'Kernel source preparation failed: {error}\n')


if __name__ == '__main__':
    main()
