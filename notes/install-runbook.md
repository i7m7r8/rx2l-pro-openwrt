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

* `argc` may be **4** or **2**. Ghidra's decompilation of `main` shows
  `if ((argc == 4) || (argc == 2))`: with 4 it takes the path from `argv[3]` and
  the reboot flag from `argv[2]` (which must parse as 0 or 1); with 2 it uses
  `/var/image` and a reboot flag of **1**. Any other count returns -1. An earlier
  version of this file said "must be 4" and "the reboot flag is uninitialised" --
  both wrong; the flag defaults to 1.
* `argv[1]` → `atoi` → fw type. `main` range-checks `0..6` (`0x40255c`,
  `slti 7`), but the decompiled worker (`FUN_00401fdc`) then requires
  **`param_2 == 2`** exactly and logs `fw type is not support.` otherwise. So
  **2 is the only value that flashes**, and the 0..6 window is only a coarse
  filter -- which is what the earlier probe had observed empirically.
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
container), and the flash itself proves the header does not land in the
partition: this unit's `mtd3` starts with `cr6c` at offset 0, while `/var/image`
(what `decry_firm` writes) starts with the uImage magic `27 05 19 56`. So the
flasher skips the 64 bytes somewhere.

**How** it skips them is not established. An earlier version of this file named
`tendaupgrade@0x4018c4 (addiu $a1,$zero,0x40)` as a seek; Ghidra's decompilation
of the write path (`FUN_00401904`) instead shows a plain
`while (read(image_fd, buf, 0x10000) > 0) write(mtd_fd, buf, n)` loop, i.e. from
offset 0, and the helper that the `0x40` is passed to decompiles to ABI/thunk
noise. Neither reading can be trusted without the device, and the two disagree.

What is safe to act on: hand the flasher the **plaintext** artifact (it is what
the vendor's own webui feeds it, and what `check_fw` inspects), and verify
`/proc/mtd` plus the first four bytes of `mtd3` afterwards. If the partition
starts with `27 05 19 56` instead of `cr6c`, the header was *not* stripped and the
image must be re-flashed from the bare container -- do not reboot in that state.

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

The bootloader in mtd1 is a Realtek stage-1 with a command monitor. Its strings
were read out of this unit's own dump (the stage is gzip-compressed at mtd1
`+0x8d60`; decompressed, 229,584 bytes), so the syntax below is the loader's own,
not a guess:

```
<RealTek>                      prompt
LOADADDR: <Load Address>       set the load address
J: Jump to <TargetAddress>     jump
XMOD <addr>  [jump]            XMODEM receive into RAM, optionally jump
AUTOBURN: 0/1                  arm/disarm the burn of what was downloaded
FL: 0/1                        flash unlock/lock
T : T <len>                    timer
COUNTER: Dump Asic Counter
```

and the loader's own message strings include
`**TFTP GET File %s,Size %X Byte`, `*TFTP Client Download Success! File Size = %X Bytes`,
`Find burn`, `Find kernel-fs`, `Find root-fs`, `r6cr`, plus the `cr6c`/`cs6c`
signatures. In other words it parses the container itself and looks for the
kernel-fs and root-fs inside it — which is why `*-factory-cr6c.bin` is the right
artifact for the burn path.

Procedure:

1. Power-cycle once and watch the console. A kernel panic and a dead loader look
   completely different, and only one needs recovery.
2. **RAM boot** (writes no flash): `XMOD <addr>` to receive
   `*-initramfs-kernel.bin` into RAM, then `J <addr>`.
3. **Burn path**: `AUTOBURN 1` then the TFTP download of `*-factory-cr6c.bin`,
   then flash it from the loader.
4. **Disaster web**: the environment defaults in mtd1 (`0x2f004`) are
   `disaster_web=1`, `ipaddr=192.168.1.1`, `netmask=255.255.255.0`,
   `serverip=192.168.1.99` — so the recovery page is at `192.168.1.1` with the
   server at `.99`.
5. Last resort: write the saved `mtd` dumps back from a root shell.
