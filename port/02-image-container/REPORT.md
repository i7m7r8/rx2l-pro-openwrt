
## Reaudit: the load address, the stub, and the overlap guard

The recipe's `CVIMG_LOADER_START_ADDR` is `0x82000000` (it was `0x81000000` until
the initramfs image outgrew a 16 MiB window) while the hardware-verified
reference board uses `0x80A00000`. That is not a discrepancy — the header field
must equal the **stub's** link address, and each board links its own stub:

```
LZMA_TEXT_START=$(CVIMG_LOADER_START_ADDR)  LOADADDR=0x80000000
```

so the container's `start_addr` *is* the address the stub is built for, and the
stub decompresses the kernel to `0x80000000`. (The `| 0x20000000` in FACTS §3 is
the uncached alias used for the copy; the jump target is the cached address.)

The real risk in that arrangement is overlap: the loader executes at
`loader_start` while the kernel decompresses upward from `0x80000000`. That is
guarded, and by derivation rather than by a constant:

```
scripts/rx2lpro-image-gates.py kernel --loader-start … --loadaddr … --reserve …
    budget = loader_start - loadaddr          # 0x1000000 = 16 MiB
    limit  = budget - reserve
```

and `Build/rx2lpro-kernel-budget` invokes it (line 160), failing the build with
"decompressed kernel overruns the loader" if the uncompressed kernel+DTB exceeds
the limit.

`RX2LPRO_KERNEL_BUDGET := 0x1000000` is therefore a **redundant copy of the same
number** — it is declared in `DEVICE_VARS` and set in the profile, but nothing
reads it. Harmless, and left in place as a deliberate restatement next to the two
addresses it depends on; if either address moves, that line and the gate both
need the new value.
