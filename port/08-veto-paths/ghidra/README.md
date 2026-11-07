# Decompiled evidence

`libgo-upgrade.c` — `upgrade()` from the webui's CGI layer, entry file/objdump
address **0x441ec** (Ghidra labels it 0x541ec; see the caveat in the file). It is
the readable form of the chain this track documented from raw disassembly:

```c
doSystemCmd("decry_firm %s %s", "/var/image", "/tmp/UPGRADE_DECRY_FAIL_FLAG");
if (stat64("/var/image", &st) != -1) {
    fd = open64("/var/image", 0x102, 0777);
    mmap64(0, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd);
    rc = systool_upgradefile_handle(0, map, st.st_size);   /* -> tpi_upload -> check_fw */
    munmap(map, size); close(fd);
    ... websSetStatus/Headers/Done with "%s/corss-domain-res.html?errCode=%d" ...
    if (rc == 0) {
        clean_force_upgrade_info();
        systool_call_writeImage_delay_11ac(fw_type);       /* the ONLY flash trigger */
    }
}
```

That is the whole veto: the flash call is inside `if (rc == 0)`, and `rc` is what
`check_fw()` returns.

`tendaupgrade-main.c` — the vendor flasher's `main` (0x402388), the worker
`FUN_00401fdc` (0x401fdc) and the flash routine `FUN_00401904` (0x401904). It is
what corrected the runbook: `argc` may be 2 or 4, and the worker requires
`fw_type == 2` exactly.

Both were produced with the Ghidra that Craft Anything bundles, because
`craft-anything-cli` itself answers "mips code isn't supported by the built-in
decoder yet" in 0.3.0 and 0.5.0 alike.
