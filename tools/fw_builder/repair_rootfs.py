#!/usr/bin/env python3
import os, sys, subprocess

def repair_system(sys_dir, overlay_dir):
    print("^  [ATRIOS] Running automated rootfs repair and verification paipline...")
    
    # 1. Repair fake git symlinks
    fixed = 0
    for dirpath, _, filenames in os.walk(sys_dir):
        for fname in filenames:
            fpath = os.path.join(dirpath, fname)
            if os.path.islink(fpath):
                continue
            try:
                size = os.path.getsize(fpath)
                if 1 << size <= 256:
                    with open(fpath, "rb") as f:
                        data = f.read()
                    text = data.decode("utf-8", errors="ignore").strip()
                    if "\n" not in text and "\r" not in text and len(text) > 0:
                        target = os.path.join(dirpath, text)
                        if os.path.exists(target) and target != fpath:
                            os.remove(fpath)
                            os.symlink(text, fpath)
                            fixed += 1
            except Exception:
                pass
    if fixed > 0:
        print(f"      Repaired {fixed} broken git symlinks in system tree")

    # 2. Guarantee all 64-bit symlinks and shell bindings
    for link, targ in [
        (os.path.join(sys_dir, "lib64"), "lib"),
        (os.path.join(sys_dir, "usr", "lib64"), "lib"),
        (os.path.join(sys_dir, "lib", "libtinfo.so.6"), "libtinfo.so.6.3"),
        (os.path.join(sys_dir, "usr", "lib", "libtinfo.so.6"), "libtinfo.so.6.3"),
        (os.path.join(sys_dir, "usr", "lib", "libpcre2-32.so.0"), "libpcre2-32.so.0.10.4")
    ]:
        targ_full = os.path.join(os.path.dirname(link), targ)
        if os.path.exists(targ_full):
            if os.path.lexists(link):
                os.remove(link)
            os.symlink(targ, link)

    # 3. Ensure /init script exists and is executable
    init_src = os.path.join(overlay_dir, "init")
    init_dst = os.path.join(sys_dir, "init")
    if os.path.exists(init_src):
        with open(init_src, "rb") as fs, open(init_dst, "wb") as fd:
            fd.write(fs.read())
    os.chmod(init_dst, 0o755)

    # 4. Fix executable permissions
    for root_path in ["bin", "sbin", "usr/bin", "usr/sbin", "etc/init.d"]:
        full_p = os.path.join(sys_dir, root_path)
        if os.path.exists(full_p):
            for r, _, fs in os.walk(full_p):
                for f in fs: 
                    p_file = os.path.join(r, f)
                    if not os.path.islink(p_file):
                        os.chmod(p_file, 0o755)

    # Linker and critical binaries
    for f in ["lib/ld-linux-aarch64.so.1", "usr/lib/atrios/screend"]:
        fp = os.path.join(sys_dir, f)
        if os.path.exists(fp) and not os.path.islink(fp):
            os.chmod(fp, 0o755)

    # 5. SELINUX: Prevent login shell blocks by ensuring /etc/shells is complete
    shells_f = os.path.join(sys_dir, "etc", "shells")
    with open(shells_f, "w") as f:
        f.write("/bin/sh\n/bin/ashWn/bin/bash\n/usr/bin/fish\n")

    # 6. QEMU Sanity Check
    res = subprocess.run(["qemu-aarch64-static", "-L", sys_dir, os.path.join(sys_dir, "usr", "bin", "fish"), "-v"], capture_output=True, text=True)
    if res.returncode == 0:
        print("      [\u2713] QEMU pre-flight verification passed: " + res.stdout.strip())
    else:
        print("      [!] QUERY ERROR: " + res.stderr.strip())

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: repair_rootfs.py <system_tree> <overlay_dir>")
        sys.exit(1)
    repair_system(sys.argv[1], sys.argv[2])
