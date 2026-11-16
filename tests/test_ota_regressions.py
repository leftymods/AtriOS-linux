"""Offline tests of the real OTA installer: signed artifacts, rollback and HTTPS cache.

Every mutable runtime path is redirected into tmp_path. No mount, reboot,
physical device, root-owned target or network service is used.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import sys
import subprocess
import struct
import tarfile
from types import SimpleNamespace

import pytest

RUNTIME = Path(__file__).resolve().parents[1] / "board/yandex/station2/rootfs_overlay/usr/lib/atrios"


def image_payload(kind):
    if kind == "kernel":
        data = bytearray(64)
        data[:2] = b"MZ"
        struct.pack_into("<QQ", data, 8, 0x80000, len(data))
        data[56:60] = b"ARM\x64"
        return bytes(data)
    # Minimal FDT v17: reserve terminator, empty root node, END_NODE, END.
    return (struct.pack(">10I", 0xd00dfeed, 72, 56, 72, 40, 17, 16, 0, 0, 16)
            + bytes(16) + struct.pack(">4I", 1, 0, 2, 9))


def tar_bytes(files):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, value in files.items():
            member = tarfile.TarInfo(name)
            if isinstance(value, tuple):
                member.type, member.linkname = value
                archive.addfile(member)
            else:
                member.size = len(value)
                member.mode = 0o644
                archive.addfile(member, io.BytesIO(value))
    return output.getvalue()


@pytest.fixture(scope="module")
def signing_keys(tmp_path_factory):
    if not shutil.which("openssl"):
        pytest.skip("OpenSSL required for actual RSA signature verification")
    directory = tmp_path_factory.mktemp("ota-rsa")
    private = directory / "private.pem"
    public = directory / "public.pem"
    subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(private)], check=True, capture_output=True)
    subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public)], check=True, capture_output=True)
    return private, public


@pytest.fixture
def ota(tmp_path, monkeypatch, signing_keys):
    monkeypatch.syspath_prepend(str(RUNTIME))
    for helper in ("atrios_https", "atrios_status"):
        monkeypatch.delitem(sys.modules, helper, raising=False)
    namespace = runpy.run_path(str(RUNTIME / "ota"), run_name="ota_under_test")
    # runpy's returned mapping is not guaranteed to be the function globals.
    globals_ = namespace["main"].__globals__
    root = tmp_path / "device"
    root.mkdir()
    paths = {
        "ROOT_DIR": root, "BOOT_DIR": root / "boot",
        "CONFIG_FILE": root / "etc/atrios/updates.json",
        "STATE_FILE": root / "var/lib/atrios/ota_status.json",
        "JOURNAL": root / "var/lib/atrios/ota-transaction.json",
        "BACKUP_DIR": root / "var/lib/atrios/ota-rollback",
        "CACHE_DIR": root / "var/cache/atrios/updates",
        "VERSION_FILE": root / "etc/atrios-version",
        "LOCK_FILE": root / "run/atrios/ota.lock",
    }
    for name, value in paths.items():
        monkeypatch.setitem(globals_, name, value)
    paths["BOOT_DIR"].mkdir()
    paths["VERSION_FILE"].parent.mkdir(parents=True, exist_ok=True)
    paths["VERSION_FILE"].write_text("1.0\n")
    paths["CONFIG_FILE"].parent.mkdir(parents=True, exist_ok=True)
    paths["CONFIG_FILE"].write_text(json.dumps({"repository": "leftymods/AtriOS-linux", "public_key": str(signing_keys[1])}))
    monkeypatch.setitem(globals_, "device_id", lambda: "station-max")
    monkeypatch.setitem(globals_, "set_error", lambda *args, **kwargs: None)
    mounts = []
    monkeypatch.setitem(globals_, "ensure_boot_mounted", lambda: mounts.append(True))
    def forbidden_network(*args, **kwargs):
        raise AssertionError("Unexpected network access")
    for name in ("open_https", "request_json", "request_bytes"):
        monkeypatch.setitem(globals_, name, forbidden_network)
    globals_["_stop_requested"].clear()
    return SimpleNamespace(g=globals_, root=root, tmp=tmp_path, mounts=mounts, paths=paths, private=signing_keys[0], public=signing_keys[1])


@pytest.fixture
def bundle(ota):
    counter = 0
    def make(files=None, version="2.0", mutate_manifest=None, tamper=None, kind="rootfs", payload_override=None):
        nonlocal counter
        counter += 1
        payload = tar_bytes(files or {"etc/sample": b"new-value"}) if kind == "rootfs" else image_payload(kind)
        if payload_override is not None:
            payload = payload_override
        name = "rootfs.tar" if kind == "rootfs" else kind + ".bin"
        manifest = {"schema": 1, "device_id": "station-max", "version": version,
                    "files": {kind: {"path": name, "sha256": hashlib.sha256(payload).hexdigest()}}}
        if kind == "kernel":
            manifest["kernel_release"] = os.uname().release
        if mutate_manifest:
            mutate_manifest(manifest)
        encoded = json.dumps(manifest, sort_keys=True).encode()
        manifest_path = ota.tmp / ("manifest-" + str(counter))
        manifest_path.write_bytes(encoded)
        signature = subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(ota.private), str(manifest_path)], check=True, capture_output=True).stdout
        contents = {"manifest.json": encoded, "manifest.sig": signature, name: payload}
        if tamper:
            tamper(contents)
        result = ota.tmp / ("bundle-" + str(counter) + ".tar")
        result.write_bytes(tar_bytes(contents))
        return result
    return make


def test_real_rsa_signed_bundle_verification(ota, bundle):
    archive = bundle()
    work = ota.tmp / "verified"
    work.mkdir()
    ota.g["extract_safe"](archive, work)
    manifest, payloads = ota.g["verify_bundle"](work, ota.g["load_config"]())
    assert manifest["version"] == "2.0"
    assert payloads["rootfs"].is_file()


@pytest.mark.parametrize("failure", ["manifest", "payload", "wrong_board", "missing_hash"])
def test_signed_bundle_rejection_before_any_target_mutation(ota, bundle, failure):
    destination = ota.root / "etc/sample"
    destination.write_bytes(b"old")
    mutate = None
    tamper = None
    if failure == "manifest":
        tamper = lambda c: c.__setitem__("manifest.json", c["manifest.json"].replace(b"2.0", b"9.0"))
    elif failure == "payload":
        tamper = lambda c: c.__setitem__("rootfs.tar", c["rootfs.tar"] + b"tampered")
    elif failure == "wrong_board":
        mutate = lambda m: m.__setitem__("device_id", "other-board")
    else:
        mutate = lambda m: m["files"]["rootfs"].pop("sha256")
    archive = bundle(mutate_manifest=mutate, tamper=tamper)
    with pytest.raises(ValueError):
        ota.g["install_bundle_tar"](archive, "test")
    assert destination.read_bytes() == b"old"
    assert ota.paths["VERSION_FILE"].read_text() == "1.0\n"
    assert not ota.paths["JOURNAL"].exists()


@pytest.mark.parametrize("entry", ["../outside", "/absolute", "boot/Image", "proc/value", "var/run/value"])
def test_forbidden_rootfs_destinations_fail_preflight(ota, bundle, entry):
    archive = bundle({"etc/first": b"must-not-install", entry: b"bad"})
    with pytest.raises(ValueError):
        ota.g["install_bundle_tar"](archive, "test")
    assert not (ota.root / "etc/first").exists()
    assert not ota.paths["JOURNAL"].exists()


@pytest.mark.parametrize("member_type", [tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE])
def test_archive_links_and_special_files_rejected(ota, member_type):
    archive = ota.tmp / "unsafe.tar"
    archive.write_bytes(tar_bytes({"entry": (member_type, "../../outside")}))
    destination = ota.tmp / "extract"
    destination.mkdir()
    with pytest.raises(ValueError, match="unsupported"):
        ota.g["extract_safe"](archive, destination)


def test_pax_metadata_bomb_rejected_before_extraction(ota):
    archive = ota.tmp / "pax.tar"
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tf:
        member = tarfile.TarInfo("pax")
        member.type = tarfile.XHDTYPE
        member.size = 65537
        tf.addfile(member, io.BytesIO(b"x" * member.size))
    destination = ota.tmp / "extract"
    destination.mkdir()
    with pytest.raises(ValueError, match="metadata"):
        ota.g["BoundedTarInfo"].frombuf(archive.read_bytes()[:512], "utf-8", "surrogateescape")
    # Python tarfile wraps frombuf ValueError as ReadError on opening.
    with pytest.raises((ValueError, tarfile.ReadError)):
        ota.g["extract_safe"](archive, destination)
    assert not list(destination.iterdir())


def test_expanded_stream_limit(ota, monkeypatch):
    archive = ota.tmp / "large.tar"
    archive.write_bytes(tar_bytes({"large": b"x" * 32768}))
    destination = ota.tmp / "extract"
    destination.mkdir()
    monkeypatch.setitem(ota.g, "MAX_EXPANDED", 16384)
    with pytest.raises(ValueError, match="limit"):
        ota.g["extract_safe"](archive, destination)


@pytest.mark.parametrize("where", ["boot", "outside"])
def test_resolved_parent_symlink_cannot_bypass_destination_policy(ota, bundle, where):
    target = ota.paths["BOOT_DIR"] if where == "boot" else ota.tmp / "outside-root"
    target.mkdir(exist_ok=True)
    (ota.root / "alias").symlink_to(target, target_is_directory=True)
    with pytest.raises(ValueError):
        ota.g["install_bundle_tar"](bundle({"alias/Image": b"bad"}), "test")
    assert not (target / "Image").exists()
    assert not ota.paths["JOURNAL"].exists()


def test_preflight_checks_all_destinations_before_first_write(ota, bundle):
    existing_directory = ota.root / "etc/z-directory"
    existing_directory.mkdir()
    archive = bundle({"etc/a-valid": b"new", "etc/z-directory": b"not-a-directory"})
    with pytest.raises(ValueError, match="non-regular"):
        ota.g["install_bundle_tar"](archive, "test")
    assert not (ota.root / "etc/a-valid").exists()
    assert not ota.paths["JOURNAL"].exists()


def test_atomic_install_and_manual_rollback_restore_old_and_remove_new(ota, bundle):
    target = ota.root / "etc/sample"
    target.write_bytes(b"old-value")
    ota.g["install_bundle_tar"](bundle({"etc/sample": b"new-value", "etc/new-file": b"created"}), "test")
    state = json.loads(ota.paths["JOURNAL"].read_text())
    assert state["status"] == "installed"
    assert target.read_bytes() == b"new-value"
    assert ota.paths["VERSION_FILE"].read_text() == "2.0\n"
    assert Path(state["backup_dir"]).is_dir()
    ota.g["rollback_transaction"]()
    assert target.read_bytes() == b"old-value"
    assert not (ota.root / "etc/new-file").exists()
    assert ota.paths["VERSION_FILE"].read_text() == "1.0\n"
    assert json.loads(ota.paths["JOURNAL"].read_text())["status"] == "rolled_back"


def test_atomic_copy_does_not_expose_partial_file_on_copy_failure(ota, monkeypatch):
    source = ota.tmp / "source"
    target = ota.root / "etc/sample"
    source.write_bytes(b"complete-new")
    target.write_bytes(b"complete-old")
    def broken_copy(src, dst):
        Path(dst).write_bytes(b"partial")
        raise OSError("injected copy failure")
    monkeypatch.setattr(ota.g["shutil"], "copy2", broken_copy)
    with pytest.raises(OSError, match="injected"):
        ota.g["atomic_copy"](source, target)
    assert target.read_bytes() == b"complete-old"
    assert not list(target.parent.glob(".ota-*"))


def test_mid_install_failure_rolls_back_all_preceding_files(ota, bundle, monkeypatch):
    target = ota.root / "etc/sample"
    target.write_bytes(b"old")
    original = ota.g["atomic_copy"]
    failed = False
    def injected(src, dst):
        nonlocal failed
        if Path(dst) == ota.root / "etc/z-fail" and not failed:
            failed = True
            raise OSError("injected install failure")
        return original(src, dst)
    monkeypatch.setitem(ota.g, "atomic_copy", injected)
    with pytest.raises(OSError, match="injected"):
        ota.g["install_bundle_tar"](bundle({"etc/sample": b"new", "etc/z-fail": b"new"}), "test")
    assert target.read_bytes() == b"old"
    assert not (ota.root / "etc/z-fail").exists()
    assert ota.paths["VERSION_FILE"].read_text() == "1.0\n"
    assert json.loads(ota.paths["JOURNAL"].read_text())["status"] == "rolled_back"


@pytest.mark.parametrize("phase", ["installing", "rolling_back"])
def test_recover_resumes_interrupted_transaction(ota, bundle, monkeypatch, phase):
    target = ota.root / "etc/sample"
    target.write_bytes(b"old")
    ota.g["install_bundle_tar"](bundle(), "test")
    state = json.loads(ota.paths["JOURNAL"].read_text())
    state["status"] = phase
    ota.paths["JOURNAL"].write_text(json.dumps(state))
    monkeypatch.setattr(ota.g["sys"], "argv", ["ota", "recover"])
    assert ota.g["main"]() == 0
    assert target.read_bytes() == b"old"
    assert json.loads(ota.paths["JOURNAL"].read_text())["status"] == "rolled_back"


def test_old_backup_survives_a_failed_subsequent_transaction(ota, bundle, monkeypatch):
    target = ota.root / "etc/sample"
    target.write_bytes(b"original")
    ota.g["install_bundle_tar"](bundle(version="2.0"), "first")
    first = json.loads(ota.paths["JOURNAL"].read_text())
    backup = Path(first["backup_dir"])
    old_contents = {p.name: p.read_bytes() for p in backup.iterdir()}
    original = ota.g["atomic_copy"]
    def injected(src, dst):
        if Path(dst) == ota.root / "etc/z-fail":
            raise OSError("second transaction failed")
        return original(src, dst)
    monkeypatch.setitem(ota.g, "atomic_copy", injected)
    with pytest.raises(OSError):
        ota.g["install_bundle_tar"](bundle({"etc/sample": b"third", "etc/z-fail": b"bad"}, version="3.0"), "second")
    assert backup.is_dir()
    assert {p.name: p.read_bytes() for p in backup.iterdir()} == old_contents
    assert target.read_bytes() == b"new-value"
    assert ota.paths["VERSION_FILE"].read_text() == "2.0\n"
    # A failed v3 must preserve the still-applicable v2 -> v1 rollback,
    # not merely leave an unreachable directory of v1 backup files.
    assert json.loads(ota.paths["JOURNAL"].read_text())["backup_dir"] == first["backup_dir"]
    ota.g["rollback_transaction"]()
    assert target.read_bytes() == b"original"
    assert ota.paths["VERSION_FILE"].read_text() == "1.0\n"


def test_kernel_install_requires_boot_mount_but_does_not_touch_real_boot(ota, bundle):
    (ota.paths["BOOT_DIR"] / "Image").write_bytes(b"old-kernel")
    ota.g["install_bundle_tar"](bundle(kind="kernel"), "test")
    assert ota.mounts
    assert (ota.paths["BOOT_DIR"] / "Image").read_bytes() == image_payload("kernel")
    ota.g["rollback_transaction"]()
    assert (ota.paths["BOOT_DIR"] / "Image").read_bytes() == b"old-kernel"


def release_response(body=b"download"):
    name = "AtriOS-station-max-ota.tar.gz"
    url = "https://github.com/leftymods/AtriOS-linux/releases/download/v2.0/" + name
    asset = {"name": name, "browser_download_url": url, "size": len(body), "digest": "sha256:" + hashlib.sha256(body).hexdigest()}
    return {"tag_name": "v2.0", "draft": False, "prerelease": False, "assets": [asset]}


def test_latest_release_selects_exact_board_asset(ota, monkeypatch):
    response = release_response()
    response["assets"].insert(0, {"name": "AtriOS-other-ota.tar.gz"})
    monkeypatch.setitem(ota.g, "request_json", lambda *a, **kw: response)
    result = ota.g["latest_release"]()
    assert result["asset"] == "AtriOS-station-max-ota.tar.gz"
    assert result["sha256"] == hashlib.sha256(b"download").hexdigest()


@pytest.mark.parametrize("failure", ["draft", "prerelease", "missing_asset", "missing_digest", "wrong_url", "http", "too_large"])
def test_latest_release_rejects_bad_metadata(ota, monkeypatch, failure):
    response = release_response()
    if failure in ("draft", "prerelease"):
        response[failure] = True
    elif failure == "missing_asset":
        response["assets"][0]["name"] = "other-board.tar.gz"
    elif failure == "missing_digest":
        response["assets"][0].pop("digest")
    elif failure == "wrong_url":
        response["assets"][0]["browser_download_url"] = "https://example.invalid/payload"
    elif failure == "http":
        response["assets"][0]["browser_download_url"] = "http://github.com/leftymods/AtriOS-linux/releases/download/v2/payload"
    else:
        response["assets"][0]["size"] = ota.g["MAX_DOWNLOAD"] + 1
    monkeypatch.setitem(ota.g, "request_json", lambda *a, **kw: response)
    with pytest.raises(ValueError):
        ota.g["latest_release"]()


def test_latest_release_checksum_sidecar_is_bound_to_asset(ota, monkeypatch):
    response = release_response()
    asset = response["assets"][0]
    asset.pop("digest")
    response["assets"].append({"name": asset["name"] + ".sha256", "browser_download_url": asset["browser_download_url"] + ".sha256"})
    monkeypatch.setitem(ota.g, "request_json", lambda *a, **kw: response)
    monkeypatch.setitem(ota.g, "request_bytes", lambda *a, **kw: (hashlib.sha256(b"download").hexdigest() + "  " + asset["name"] + "\n").encode())
    assert ota.g["latest_release"]()["sha256"] == hashlib.sha256(b"download").hexdigest()
    response["assets"][1]["browser_download_url"] = "https://example.invalid/hash"
    with pytest.raises(ValueError, match="digest"):
        ota.g["latest_release"]()


def release_for(ota, body):
    response = release_response(body)
    asset = response["assets"][0]
    return {"tag": "v2.0", "version": "2.0", "asset": asset["name"], "url": asset["browser_download_url"], "size": len(body), "sha256": hashlib.sha256(body).hexdigest()}


def test_verified_download_cache_and_corrupt_cache_replacement(ota, monkeypatch):
    body = b"verified-artifact" * 5000
    release = release_for(ota, body)
    requests = []
    def https(url, **kwargs):
        assert url.startswith("https://github.com/")
        assert kwargs["total_timeout"] == 1800
        assert callable(kwargs["cancel"])
        requests.append(url)
        return io.BytesIO(body)
    monkeypatch.setitem(ota.g, "open_https", https)
    target = ota.g["download_release"](release)
    assert target.read_bytes() == body
    assert ota.g["download_release"](release) == target
    assert len(requests) == 1
    target.write_bytes(b"corrupt-cache")
    assert ota.g["download_release"](release).read_bytes() == body
    assert len(requests) == 2
    assert not list(ota.paths["CACHE_DIR"].glob(".download-*"))


@pytest.mark.parametrize("failure", ["hash", "truncated", "oversized", "cancel"])
def test_invalid_download_never_publishes_partial_cache(ota, monkeypatch, failure):
    body = b"correct-artifact"
    release = release_for(ota, body)
    actual = {"hash": b"x" * len(body), "truncated": body[:-1], "oversized": body + b"x", "cancel": body}[failure]
    monkeypatch.setitem(ota.g, "open_https", lambda *a, **kw: io.BytesIO(actual))
    if failure == "cancel":
        ota.g["_stop_requested"].set()
    with pytest.raises((ValueError, InterruptedError)):
        ota.g["download_release"](release)
    assert not list(ota.paths["CACHE_DIR"].iterdir())
    assert not ota.paths["STATE_FILE"].exists()


@pytest.mark.parametrize("where", ["backup", "target"])
def test_insufficient_space_is_detected_before_mutation(ota, bundle, monkeypatch, where):
    target = ota.root / "etc/sample"
    target.write_bytes(b"old")
    disk_usage = ota.g["shutil"].disk_usage
    def space(path):
        actual = disk_usage(path)
        reject = Path(path) == (ota.paths["BACKUP_DIR"].parent if where == "backup" else target.parent)
        return type(actual)(actual.total, actual.used, 0 if reject else actual.free)
    monkeypatch.setattr(ota.g["shutil"], "disk_usage", space)
    with pytest.raises(OSError, match="Insufficient"):
        ota.g["install_bundle_tar"](bundle(), "test")
    assert target.read_bytes() == b"old"
    assert not ota.paths["JOURNAL"].exists()


def test_new_kernel_without_matching_modules_fails_preflight(ota, bundle):
    old = ota.paths["BOOT_DIR"] / "Image"
    old.write_bytes(b"old")
    archive = bundle(kind="kernel", mutate_manifest=lambda m: m.__setitem__("kernel_release", "new-release-without-modules"))
    with pytest.raises(ValueError, match="matching modules"):
        ota.g["install_bundle_tar"](archive, "test")
    assert old.read_bytes() == b"old"
    assert not ota.paths["JOURNAL"].exists()


def test_oversized_member_count_is_bounded(ota, monkeypatch):
    archive = ota.tmp / "many.tar"
    archive.write_bytes(tar_bytes({"one": b"1", "two": b"2"}))
    destination = ota.tmp / "extract"
    destination.mkdir()
    monkeypatch.setitem(ota.g, "MAX_MEMBERS", 1)
    with pytest.raises(ValueError, match="Too many"):
        ota.g["extract_safe"](archive, destination)
    assert not (destination / "two").exists()


def test_download_deadline_cleanup(ota, monkeypatch):
    body = b"artifact"
    release = release_for(ota, body)
    monkeypatch.setitem(ota.g, "open_https", lambda *a, **kw: io.BytesIO(body))
    ticks = iter((0, 1801))
    monkeypatch.setattr(ota.g["time"], "monotonic", lambda: next(ticks))
    with pytest.raises(TimeoutError, match="deadline"):
        ota.g["download_release"](release)
    assert not list(ota.paths["CACHE_DIR"].iterdir())


@pytest.mark.parametrize("tag", ["v1.9", "v2.0", "v2.0.0"])
def test_automatic_update_never_downloads_equal_or_older_release(ota, monkeypatch, tag):
    ota.paths["VERSION_FILE"].write_text("2.0\n")
    release = release_for(ota, b"artifact")
    release.update(tag=tag, version=tag.removeprefix("v"))
    monkeypatch.setitem(ota.g, "latest_release", lambda: release)
    def forbidden(*args, **kwargs):
        raise AssertionError("Older/equal release must not be downloaded")
    monkeypatch.setitem(ota.g, "download_release", forbidden)
    assert ota.g["check_and_download"](install=True) is None
    assert not ota.paths["JOURNAL"].exists()


def test_version_comparison_is_numeric_and_normalizes_missing_components(ota):
    version = ota.g["version_key"]
    assert version("2.9") < version("2.10")
    assert version("v2.0") == version("2.0.0.0")
    assert version("2.0.0.1") > version("2.0.0")


@pytest.mark.parametrize("version", ["", "2.0-rc1", "2.0.0.0.1", "../../1", "NaN", None])
def test_invalid_stable_versions_fail_closed(ota, version):
    with pytest.raises(ValueError):
        ota.g["version_key"](version)


def test_signed_local_downgrade_rejected_before_target_mutation(ota, bundle):
    ota.paths["VERSION_FILE"].write_text("3.0\n")
    target = ota.root / "etc/sample"
    target.write_bytes(b"current")
    with pytest.raises(ValueError, match="downgrade"):
        ota.g["install_bundle_tar"](bundle(version="2.0"), "local")
    assert target.read_bytes() == b"current"
    assert not ota.paths["JOURNAL"].exists()


def test_signed_manifest_must_match_expected_downloaded_release(ota, bundle):
    target = ota.root / "etc/sample"
    target.write_bytes(b"current")
    with pytest.raises(ValueError, match="does not match"):
        ota.g["install_bundle_tar"](bundle(version="3.0"), "github-release", expected_version="2.0")
    assert target.read_bytes() == b"current"
    assert not ota.paths["JOURNAL"].exists()


def test_newer_release_install_passes_expected_version_binding(ota, monkeypatch):
    ota.paths["VERSION_FILE"].write_text("1.9\n")
    release = release_for(ota, b"artifact")
    downloaded = ota.tmp / "downloaded.tar"
    monkeypatch.setitem(ota.g, "latest_release", lambda: release)
    monkeypatch.setitem(ota.g, "download_release", lambda value: downloaded)
    installs = []
    monkeypatch.setitem(ota.g, "install_bundle_tar", lambda *args, **kwargs: installs.append((args, kwargs)))
    assert ota.g["check_and_download"](install=True) == downloaded
    assert installs == [((downloaded, release["url"], False), {"expected_version": "2.0"})]


@pytest.mark.parametrize("kind", ["kernel", "dtb"])
def test_internal_version_file_cannot_replace_verified_payload(ota, bundle, kind):
    target = ota.paths["BOOT_DIR"] / ("Image" if kind == "kernel" else "dtb.img")
    target.write_bytes(b"old")
    def rename_payload(contents):
        contents["new-version"] = contents.pop(kind + ".bin")
    archive = bundle(kind=kind,
                     mutate_manifest=lambda m: m["files"][kind].__setitem__("path", "new-version"),
                     tamper=rename_payload)
    try:
        ota.g["install_bundle_tar"](archive, "test")
    except ValueError:
        # Reserving internal staging names is also a valid fail-closed policy.
        assert target.read_bytes() == b"old"
        assert not ota.paths["JOURNAL"].exists()
    else:
        assert target.read_bytes() == image_payload(kind), "Installer replaced verified payload with internal version text"


@pytest.mark.parametrize("kind, damage", [("kernel", "magic"), ("kernel", "size"), ("dtb", "magic"), ("dtb", "size")])
def test_signed_but_invalid_boot_payload_rejected_before_mutation(ota, bundle, kind, damage):
    payload = bytearray(image_payload(kind))
    if damage == "magic":
        at = 56 if kind == "kernel" else 0
        payload[at:at + 4] = b"BAD!"
    elif kind == "kernel":
        struct.pack_into("<Q", payload, 16, 128 * 1024 * 1024 + 1)
    else:
        struct.pack_into(">I", payload, 4, len(payload) + 4096)
    target = ota.paths["BOOT_DIR"] / ("Image" if kind == "kernel" else "dtb.img")
    target.write_bytes(b"old")
    archive = bundle(kind=kind, payload_override=bytes(payload))
    with pytest.raises(ValueError):
        ota.g["install_bundle_tar"](archive, "test")
    assert target.read_bytes() == b"old"
    assert not ota.paths["JOURNAL"].exists()


def test_arm64_image_declared_bss_larger_than_file_is_valid(ota, bundle):
    payload = bytearray(image_payload("kernel"))
    struct.pack_into("<Q", payload, 16, len(payload) + 4096)
    ota.g["install_bundle_tar"](bundle(kind="kernel", payload_override=bytes(payload)), "test")
    assert (ota.paths["BOOT_DIR"] / "Image").read_bytes() == bytes(payload)
    assert ota.paths["VERSION_FILE"].read_text() == "2.0\n"


def test_boot_mount_requires_boot_partition_on_same_emmc(ota):
    assert ota.g["inspect_boot_mount"]("/dev/mmcblk1p2", "/dev/mmcblk1p1", "vfat", "MMC", "BOOT") is None


@pytest.mark.parametrize("root_source, boot_source, fstype, device_type, label", [
    ("/dev/mmcblk1p2", "/dev/mmcblk0p1", "vfat", "MMC", "BOOT"),
    ("/dev/mmcblk1p2", "/dev/mmcblk1p1", "vfat", "SD", "BOOT"),
    ("/dev/mmcblk1p2", "/dev/mmcblk1p1", "vfat", "MMC", "OTHER"),
    ("/dev/mmcblk1p2", "/dev/mmcblk1p1", "ext4", "MMC", "BOOT"),
    ("/dev/mmcblk1p3", "/dev/mmcblk1p1", "vfat", "MMC", "BOOT"),
    ("/dev/sda2", "/dev/sda1", "vfat", "MMC", "BOOT"),
    ("/dev/mmcblk1p2", "/dev/mmcblk1p2", "vfat", "MMC", "BOOT"),
])
def test_boot_mount_rejects_wrong_disk_media_label_and_root_shape(ota, root_source, boot_source, fstype, device_type, label):
    with pytest.raises(RuntimeError, match="same eMMC"):
        ota.g["inspect_boot_mount"](root_source, boot_source, fstype, device_type, label)


def test_firmware_discovery_coexists_with_latest_apk(ota, monkeypatch):
    import copy
    app = {"tag_name": "app-v1.4", "draft": False, "assets": [{"name": "AtriOS-Control-v1.4.apk"}]}
    firmware = release_response()
    old = copy.deepcopy(firmware)
    old["tag_name"] = "v1.5"
    future = copy.deepcopy(firmware)
    future["tag_name"] = "v9.0"
    future["prerelease"] = True
    monkeypatch.setitem(ota.g, "request_json", lambda *args, **kwargs: app)
    monkeypatch.setitem(ota.g, "request_bytes", lambda *args, **kwargs: json.dumps([app, old, future, firmware]).encode())
    assert ota.g["latest_release"]()["tag"] == "v2.0"


def test_only_apk_releases_are_not_firmware(ota, monkeypatch):
    app = {"tag_name": "app-v1.4", "assets": []}
    monkeypatch.setitem(ota.g, "request_json", lambda *args, **kwargs: app)
    monkeypatch.setitem(ota.g, "request_bytes", lambda *args, **kwargs: json.dumps([app]).encode())
    with pytest.raises(ota.g["NoFirmwareRelease"]):
        ota.g["latest_release"]()


def kernel_library_layout(ota, merged):
    (ota.root / 'usr/lib').mkdir(parents=True)
    if merged:
        (ota.root / 'lib').symlink_to('usr/lib', target_is_directory=True)
    else:
        (ota.root / 'lib').mkdir()


@pytest.mark.parametrize('merged', [False, True])
@pytest.mark.parametrize('payload_prefix', ['lib', 'usr/lib'])
@pytest.mark.parametrize('suffix', ['modules/7.3.1-AtriOS-aml/kernel/drivers/atrios/quasar_led_ring.ko',
                                  'firmware/regulatory.db-upstream',
                                  'firmware/regulatory.db.p7s-upstream'])
def test_usr_kernel_payload_updates_canonical_lib_with_journalled_rollback(ota, bundle, merged, payload_prefix, suffix):
    kernel_library_layout(ota, merged)
    canonical = (ota.root / 'lib' / suffix).resolve()
    canonical.parent.mkdir(parents=True, exist_ok=True)
    canonical.write_bytes(b'old-active-file')
    shadow = ota.root / 'usr/lib' / suffix
    if not merged:
        shadow.parent.mkdir(parents=True, exist_ok=True)
        shadow.write_bytes(b'unrelated-shadow-file')
    flat_recovery = ota.root / 'lib/modules/7.3.1-AtriOS-aml/quasar_led_ring.ko'
    flat_recovery.parent.mkdir(parents=True, exist_ok=True)
    flat_recovery.write_bytes(b'flat-recovery-copy')
    ota.g['install_bundle_tar'](bundle({payload_prefix + '/' + suffix: b'new-active-file'}), 'test')
    assert canonical.read_bytes() == b'new-active-file'
    if not merged:
        assert shadow.read_bytes() == b'unrelated-shadow-file'
    state = json.loads(ota.paths['JOURNAL'].read_text())
    entry = next(item for item in state['entries'] if item['destination'] == str(canonical))
    assert entry['existed'] is True
    assert (Path(state['backup_dir']) / entry['backup']).read_bytes() == b'old-active-file'
    assert flat_recovery.read_bytes() == b'flat-recovery-copy'
    ota.g['rollback_transaction']()
    assert canonical.read_bytes() == b'old-active-file'
    assert flat_recovery.read_bytes() == b'flat-recovery-copy'
    if not merged:
        assert shadow.read_bytes() == b'unrelated-shadow-file'
    assert ota.paths['VERSION_FILE'].read_text() == '1.0\n'


@pytest.mark.parametrize('merged', [False, True])
@pytest.mark.parametrize('suffix', ['modules/7.3.1-AtriOS-aml/example.ko', 'firmware/regulatory.db-upstream'])
def test_usr_and_lib_alias_collision_fails_before_first_write(ota, bundle, merged, suffix):
    kernel_library_layout(ota, merged)
    canonical = (ota.root / 'lib' / suffix).resolve()
    canonical.parent.mkdir(parents=True, exist_ok=True)
    canonical.write_bytes(b'keep-active-file')
    archive = bundle({'etc/a-before': b'must-not-install',
                      'lib/' + suffix: b'first', 'usr/lib/' + suffix: b'second'})
    with pytest.raises(ValueError, match='Duplicate OTA destinations'):
        ota.g['install_bundle_tar'](archive, 'test')
    assert canonical.read_bytes() == b'keep-active-file'
    assert not (ota.root / 'etc/a-before').exists()
    assert not ota.paths['JOURNAL'].exists()
    assert ota.paths['VERSION_FILE'].read_text() == '1.0\n'


@pytest.mark.parametrize('subtree', ['modules', 'firmware'])
@pytest.mark.parametrize('escape', ['outside', 'boot'])
def test_canonical_kernel_mapping_still_applies_resolved_destination_policy(ota, bundle, subtree, escape):
    kernel_library_layout(ota, False)
    target = ota.tmp / 'outside-root' if escape == 'outside' else ota.paths['BOOT_DIR']
    target.mkdir(exist_ok=True)
    (ota.root / 'lib' / subtree).symlink_to(target, target_is_directory=True)
    archive = bundle({'etc/a-before': b'never-installed', 'usr/lib/' + subtree + '/payload': b'bad'})
    with pytest.raises(ValueError):
        ota.g['install_bundle_tar'](archive, 'test')
    assert not (target / 'payload').exists()
    assert not (ota.root / 'etc/a-before').exists()
    assert not ota.paths['JOURNAL'].exists()


def test_module_firmware_mapping_does_not_remap_other_usr_subtrees(ota, bundle):
    kernel_library_layout(ota, False)
    files = {'usr/bin/example': b'command', 'usr/lib/atrios/helper': b'helper',
             'usr/lib/modules-extra/example': b'not-a-module-tree'}
    ota.g['install_bundle_tar'](bundle(files), 'test')
    for name, value in files.items():
        assert (ota.root / name).read_bytes() == value
    assert not (ota.root / 'bin/example').exists()
    assert not (ota.root / 'lib/atrios/helper').exists()
    assert not (ota.root / 'lib/modules-extra/example').exists()
