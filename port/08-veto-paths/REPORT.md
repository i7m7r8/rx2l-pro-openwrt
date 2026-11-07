# 08 — Veto / rollback paths on the stock RX2L Pro upgrade flow

Question answered: *after `check_fw()` says yes, does anything else in the stock
system veto, roll back, or re-verify the upgrade — before or after reboot?*

Status: IN PROGRESS (Q1 answered, Q2/Q3/Q4 pending — see bottom).

---

## VERDICT (provisional, driven by Q1)

**NO — the vendor "Local Upgrade" page cannot flash a hand-built OpenWrt image.**

`upgrade()` (`libgo.so@0x441ec`) runs `/bin/decry_firm /var/image
/tmp/UPGRADE_DECRY_FAIL_FLAG` **unconditionally, before** `check_fw()`, and
`decry_firm` creates that flag file **by itself** whenever its
decrypt/verify step returns non-zero (PROVEN, see 1.3). A plaintext (ours, or
an already-decrypted vendor) image always returns 255 → flag is created →
`check_fw()`'s very first test `access(flag,F_OK)` returns -1 → `tpi_upload()`
runs `rm /var/image` and the browser is redirected with `errCode=1`.
Reversing this requires producing a container that `decry_firm` accepts, i.e.
the RSA-4096/SHA-512-signed TDE01 wrapper (`notes/tde01-verdict.md`:
unbreakable, public key embedded in `decry_firm .rodata@0x41cd00`, e=65537).

The veto is NOT "the caller checks rc and writes the flag" — the callers ignore
rc; the flag is written by `decry_firm` itself, so no caller-side patch helps.

---

## Q1. Who creates `/tmp/UPGRADE_DECRY_FAIL_FLAG` — PROVEN

### 1.1 The string appears in exactly 5 objects, and in no script
```
cd /tmp/rx_dump/rootfs && grep -rl "UPGRADE_DECRY_FAIL_FLAG" .
  ./rx2lpro_rootfs.tar
  ./x/bin/business_proc   ./x/bin/cwmpd   ./x/bin/netctrl
  ./x/lib/libcommonprod.so ./x/lib/libgo.so
```
`grep -I` (text-only) over the whole tree, including `/etc`, `/lib` rc scripts
and any `.sh`, returns **zero hits** → the flag is a pure userland-binary
mechanism, nothing in shell recreates or removes it. (PROVEN)

### 1.2 Function-level reference map (`so_xref.py`), each = one writer site
| file:address | function | how the string is used |
|---|---|---|
| `lib/libgo.so@0x441ec` | `upgrade` | arg 2 of `"decry_firm %s %s"` |
| `bin/netctrl@0x4247c0` | `begin_upgrade` | arg 2 of `"decry_firm %s %s"` |
| `bin/business_proc@0x444c40` | `biz_ucloud_start_upgrade` | arg 2 of `"decry_firm %s %s"` |
| `bin/cwmpd@0x413d00` | `cwmp_check_upgrade_image` | arg 2 of `"decry_firm %s %s"` |
| `lib/libcommonprod.so@0x2a518` | `check_fw` | arg of `access(path,F_OK)` (reader) |

Call-site evidence (PIC-folded args, `mipsfn.py`):
```
libgo.so upgrade():            000443d8 addiu $a2,$v0,-0x3334 ['/tmp/UPGRADE_DECRY_FAIL_FLAG']
                               000443e4 addiu $a1,$v0,-0x3650 ['/var/image']
                               000443ec addiu $a0,$v0,-0x3314 ['decry_firm %s %s']
                               000443f8 jalr $t9              <doSystemCmd>   ; rc ignored
netctrl begin_upgrade():       00424844 $a2 ['/tmp/UPGRADE_DECRY_FAIL_FLAG']
                               0042484c $a1 ['/var/image'] 00424854 $a0 ['decry_firm %s %s']
                               00424860 jalr <doSystemCmd>
                               (preceded at 0042482c by set_agent_sched_reboot_status("upgrading"))
business_proc ...:             00444cd0 $a2 [flag] 00444cd8 $a1 ['/var/image'] 00444ce0 $a0 ['decry_firm %s %s']
                               00444cec jalr <doSystemCmd>
cwmpd cwmp_check_upgrade_image:00413d40 $a2 [flag] 00413d48 $a1 ['/var/image'] 00413d50 $a0 ['decry_firm %s %s']
                               00413d5c jalr <doSystemCmd>
```
So **all four vendor upgrade entry points (webui, netctrl/mesh, ucloud, TR-069)
hand the flag path to `decry_firm` as argv[2]** and none of them touches the
file itself. There is no entry point that calls `decry_firm` with a single
argument, so there is no upgrade path that can skip flag creation. (PROVEN for
these 4 binaries; `tendaupgrade` and `httpd` contain no `decry_firm` string at
all → the flasher itself never decrypts.)

### 1.3 `decry_firm` main() — the writer, and it is conditional on decrypt failure
`bin/decry_firm` `main@0x406728` (pseudo-code from the disassembly):
```
00406744 argv[2] != NULL:
0040676c   jal sub_0041f690 = access(argv[2], F_OK /*a1=0*/)
00406774   if (access == 0) jal sub_0041f660 = unlink(argv[2])   ; clear stale flag
004067a4 v0 = sub_00406290(argv[1])                              ; decrypt in place
004067b4 if (v0 == 0) return 0                                   ; success, no flag
004067c0 puts('openssl decode failed')
004067d4 if (argv[2] != NULL) {
004067f8     fd = sub_0041f5f0(argv[2], 0x1ff /*0777*/)          ; == creat()
00406814     sub_0041f6b0(fd)                                    ; == close()
           }
0040681c return -1
```
Import table confirms exactly these three helpers are present: `access`,
`unlink`, `creat`, `close`. **`decry_firm` is the writer.**

### 1.4 Empirical qemu proof (`qemu/decry-flag-test.sh`)
```
$ qemu-mipsel-static -L /tmp/rx_dump/rootfs/x /tmp/rx_dump/rootfs/x/bin/decry_firm \
      /tmp/veto/a/image.bin /tmp/veto/a/UPGRADE_DECRY_FAIL_FLAG
  (a/image.bin = genuine US_RX2LProV1.0re_V16.03.51.17_multi_TDE01.bin)
rc=0
flag exists? NO
sha256 = d1ee92c0339dfaa451aaf4ae0f32638e1a097a50fb11f31ce32d5b3ae409bc70  == dec/RX2LPro_51.17_decrypted.bin

$ qemu-mipsel-static -L ... /tmp/veto/b/image.bin /tmp/veto/b/UPGRADE_DECRY_FAIL_FLAG
  (b/image.bin = already-decrypted PLAINTEXT image, i.e. our header+cr6c layout)
[decrypt_signed_digest][36] get signed_digest failed!
[decrypt_firmware][118] get signed2 failed!
openssl decode failed
rc=255
flag exists? YES   -rwx------. 1 root root 0 ... UPGRADE_DECRY_FAIL_FLAG
sha256 unchanged (d1ee92c0…)
```
Positive control (genuine signed+encrypted upload) → rc 0 and **no flag**.
Plaintext (our image) → rc 255 and **flag created, mode 0700 after umask,
0 bytes**. Both PROVEN by execution, not inference.

### 1.5 Downstream: the veto really is the one that bites
`libgo.so@0x43cb0 systool_upgradefile_handle(type,buf,len)`:
```
00043ce0 beqz $v0 → tpi_upload(buf,len)      ; upgrade() calls it with a0 = 0
00043ce8 beq  $v0,1 → tpi_sys_cfg_upload     ; config restore path
00043d44 else printf('invalid type') and return 1002 (0x3ea)
```
`libgo.so@0x42e38 tpi_upload(buf,len)` = the whole pre-flash gate:
```
00042e78 lw $a1,-0x73dc($gp) {fw_type}
00042e74 addiu $a0,... ['/var/image']
00042e7c jalr <check_fw>                      ; check_fw("/var/image", fw_type, buf, len)
00042e94 bnez $v0 → 00042ec4 doSystemCmd('rm /var/image'); return err
00042ea0 puts('check type ok.begain to upgrade....'); return 0
```
`check_fw()`'s flag test is its first action (established by the caller's
decompile), so with the flag present `check_fw` returns -1 and
`tpi_upload` deletes the staged image. Back in `upgrade()`:
`0x44654 bgez` → errCode=abs(-1)=1 → 302 redirect
`%s/corss-domain-res.html?errCode=1`, and because rc!=0 it **skips**
`clean_force_upgrade_info()` + `systool_call_writeImage_delay_11ac()` (the only
flash trigger, at `0x4473c`/`0x4475c`) and falls through to
`rm -rf /var/image` and `doSystemCmd("sleep 5 && reboot &")` at `0x4481c`.
Nothing is ever written to flash. (PROVEN by disassembly; the on-device
log/syslib side is UNTESTED.)
