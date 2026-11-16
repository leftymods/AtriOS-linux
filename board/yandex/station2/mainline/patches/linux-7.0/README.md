# Linux 7.3.1 wireless source provenance

Apply the numbered patches in lexical order to the pristine Linux 7.3.1 tree.
The baseline archive SHA-256 is
`fa595aad93734f829c963c2a90ef7c3673224fd7fdae7b49406e397cde2de231`.
Per-file baseline and working SHA-256 values are recorded in `provenance.json`.

The patches capture the working source used following station kernel build #73
(`7.3.1-AtriOS-aml`) before the deployment rebuild on 2026-11-16. They retain all
existing RTL8822CS/RTL8822E Wi-Fi and Bluetooth changes, including the SDIO/TX
changes that were missing from the earlier patch series. No live kernel source
was reset or rewritten during this capture.

Validation used real GNU patch application to an isolated extraction of the
pristine archive, followed by byte-for-byte comparison of all 100 inspected
wireless/Bluetooth source files against the working source. Both numbered
patches also pass reverse dry-run against that working source. Changed-target
hashes are available in the JSON provenance file.

`mac80211.c` in the working source uses CRLF. The patch deliberately retains
those bytes, so its diff is larger than a normalized source diff. The scoped
Git attributes disable text conversion and source-style whitespace checks for
patch data only; normal source files retain their usual checks. Do not run a
line-ending converter or whitespace fixer over these patch files.
