# Installing an image on the RX2L Pro

This is the procedure that actually writes flash, and the reason it is not the
webui page. Read §1 before anything else; it is the part that decides which file
you may use at all.

Status: written ahead of the first green build. The artifact names below are the
ones `target/linux/rtl819x/image/rtl8197f.mk` produces; until CI is green there
is nothing to install.

---

## 1. The webui is not an option, and that is a proven property

`decry_firm` records its own verdict in `/tmp/UPGRADE_DECRY_FAIL_FLAG`, and
`check_fw()` tests that file **before it looks at a byte of the image**
(`libcommonprod.so@0x2a518`: `access(path, F_OK)` → `bnez` → `rc = -1`, print
`-----image decrypted failed---`). Our image is plaintext, so `decry_firm` fails,
so the flag exists, so the gate refuses and the browser gets `errCode=1`.
Nothing is written. All four vendor entry points (webui, netctrl, ucloud,
TR-069) pass that same flag path to the same gate.

The full chain with addresses: [`../port/08-veto-paths/REPORT.md`](../port/08-veto-paths/REPORT.md).

What the webui *would* have done is one shell command, and that command takes no
gate from us.

## 2. What `tendaupgrade` is, from its own disassembly

`text 0x402388` (`main`, static uClibc binary, no symbols — addresses are file
addresses, base `0x400000`):

* `argc` must be **4** (`0x40243c`: `addiu $v0,$zero,4; bne`). With `argc != 4`
  the path silently defaults to `/var/image` and the reboot flag is left
  uninitialised — do not rely on that; pass all four arguments.
* `argv[1]` → `atoi` → fw type, range-checked `0..6` in `main` (`0x40255c`
  `slti 7`) and then again inside the worker, which rejects the rest with
  `fw type is not support.` Empirically the value that drives a single-image
  `KernelFS` write is **2** (probe result recorded in `FACTS.md`).
* `argv[2]` → `atoi` → must be `0` or `1` (`0x4024d8` compares against 1,
  `0x402510` returns -1 otherwise). Non-zero means **reboot after writing**.
* `argv[3]` → the image path, copied with a `0x40`-byte limit (`0x402464`
  `addiu $a1,$zero,0x40`), format `%s`.
* The worker (`0x401fdc`) resolves the target partition **by name** out of
  `/proc/mtd` (`KernelFS`, with `KernelFS1`/`KernelFS2` for the dual-image
  check), then: unlock → erase → write → env → reboot. Its log lines are
  `flash_write_mtd start .`, `write mtd dev is fail.`, `get mtd name failed.`
* **There is no magic, checksum, size or signature check anywhere in the
  flasher.** It writes the bytes you give it. That is not a convenience — it
  means the only verification an image gets is the one in this repo's CI
  (`tools/check-cr6c.py` over every produced container, which fails the build
  rather than shipping a bad one). Never flash a file that did not come out of
  a green run.

## 2b. The webui route (what the vendor UI can actually do for us)

The webui is reachable after all, through the one hole in the veto: `decry_firm`
creates the failure flag with `creat()` while `check_fw` tests it with
`access()`, and a **dangling symlink** makes them disagree — `access()` reports
ENOENT, `creat()` fails, no flag is written, and `check_fw` proceeds to the
field checks our container already satisfies. Measured under qemu on the stock
binary: normal path -> flag created -> refuse; symlink path -> no flag -> proceed
(`notes/webui-upgrade-contract.md`, "The veto is bypassable").

Needs a root shell for one command, and re-planting after each reboot because
`/tmp` is tmpfs:

```
ln -sf /nonexistent-dir/x /tmp/UPGRADE_DECRY_FAIL_FLAG
```

Then upload `*-tenda-plaintext.bin` in the webui (Local Upgrade). Expect the
page to report success, one `KernelFS` write, and a reboot. If the upload is
rejected with `errCode=1`, the symlink was not in place when `check_fw` ran.

### What the flasher does with the 64-byte header

Both routes hand it the **plaintext** artifact (64-byte Tenda header + `cr6c`
container), and it does not write those 64 bytes to flash: the write path seeks
past them first (`tendaupgrade@0x4018c4: addiu $a1,$zero,0x40` immediately before
the seek/write helper at `0x4027e4`). The partition therefore ends up starting
with `cr6c` at offset 0, which is what the boot loader reads and what the stock
`mtd3` dump looks like.

Consequence for the size budget: the file handed to the flasher is 0x40 bytes
larger than what lands in `KernelFS`, and `check_fw`'s `check_mtd_size` sees the
file. Our images sit far below the 15.5 MiB partition, so this only matters at
the very top of the budget.

## 3. Pre-flight, once, before any flash

Keep the device's own flash content off-device first. That is the only thing
that makes a bad write recoverable at leisure rather than by TFTP at 3 a.m.

```
cat /proc/mtd                 # record it: names, sizes, offsets
for i in 0 1 2 3 4 5 6 7; do
  dd if=/dev/mtd$i of=/tmp/mtd$i.bin
done
```
Pull those files off the device and keep `mtd1` (Bootloader) especially — it is
the loader you would need if a flash goes wrong. Never write to `mtd1`.

Also record `cat /proc/cmdline` and the first `dmesg` lines of a stock boot.

## 4. Flash

Artifacts from a green CI run (the `rx2lpro-images` artifact):

| file | what it is |
|---|---|
| `*-factory-cr6c.bin` | the bare `cr6c` container — **this is the one `tendaupgrade` wants** |
| `*-tenda-plaintext.bin` | same bytes behind the 64-byte Tenda header — for tools that expect a `decry_firm`-shaped file, not for flash |
| `*-initramfs-kernel.bin` | loader/RAM boot, writes no flash |

```
sha256sum image.bin            # must match MANIFEST-built.txt from the run
tendaupgrade 2 1 /tmp/image.bin
```

Expected output, in order: `flash_write_mtd start .` → the mtd name it resolved
→ `env set image_boot 1 unlock` → reboot. If you see
`Failed to process image upgrading` or `fw type is not support.`, nothing was
written.

Serial console is `115200` on `ttyS0` (measured from this board's cmdline).

## 5. What you end up with

* One partition written: `KernelFS` (`/dev/mtd3`, flash offset `0x40000`).
* `CFG`/`CFM` are **not** touched — that is where this unit's MAC and RF
  calibration live (see `FACTS.md` §5–§6). Erasing them would be worse than
  useless.
* Fresh squashfs means no overlay, so first boot runs every
  `/etc/uci-defaults/*` and lands on stock defaults. That *is* the "erase
  userdata, start clean" behaviour the vendor UI implies; no extra erase step
  exists or is needed.

## 6. If it does not come back

1. Power-cycle once and watch the console: a kernel panic and a dead loader look
   completely different, and only one of them needs recovery.
2. Loader `AUTOBURN` over TFTP with `*-factory-cr6c.bin`, or the loader command
   monitor (`XMOD` into RAM, then `J <addr>`) with `*-initramfs-kernel.bin`.
3. The disaster web path is `192.168.1.1` with `serverip=192.168.1.99`
   (`FACTS.md` §7).
4. Last resort: write the saved `mtd` dumps back.
