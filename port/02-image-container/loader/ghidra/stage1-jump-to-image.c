/*
 * stage-1 loader: the "Jump to image start" path and the container validator.
 *
 * PROVENANCE
 *   binary  /tmp/mtd1_stage1.bin -- the stage-1 decompressed out of mtd1 at
 *           +0x8d60 (229,584 bytes), sha256
 *           af91f8e14786f1817225db0bd754679380c7e108cf430642348c139d3ac61bb1
 *   engine  Ghidra 12.1.4 (the one Craft Anything installs and Deep mode uses),
 *           analyzeHeadless, -processor MIPS:LE:32:default, image base
 *           0x80000000 (the raw blob was wrapped in a one-segment ELF32 for the
 *           import; craft-anything-cli 0.5.0's own decoder refuses MIPS with
 *           "mips code isn't supported by the built-in decoder yet", so the
 *           CLI opened the target and the analysis ran in its Ghidra engine).
 *   dump    DumpC.java (Craft Anything headless post-script) over all
 *           functions; the two below are verbatim, comments and all.
 *
 * WHY THESE TWO
 *   They are the whole boot contract as the hardware actually executes it:
 *   FUN_8001081c reads a 16-byte cvimg header from SPI flash, byte-swaps the
 *   three big-endian words, copies len-2 bytes to start_addr|0x20000000 and
 *   calls start_addr. FUN_80010be4 is the only thing that can reject a
 *   container before that: the `cs6c`/`cr6c` signature compare and the
 *   additive BE-u16 zero-sum over len bytes from +0x10.
 *
 * WHAT THEY SETTLE (see LOADER.md §"start_addr is not constrained")
 *   * start_addr (+0x04) is used verbatim: the jump is `(*(code **)(hdr+4))()`
 *     and the copy destination is that same value OR 0x20000000. There is no
 *     comparison against it anywhere in the dump -- not against 0x81000000,
 *     not against the vendor's own 0x80d00000 (neither constant appears in the
 *     image at all), and no upper bound tied to RAM size.
 *   * the payload length is `len - 2`, i.e. the header's len counts the
 *     cvimg zero-sum trailer, which is why the rootfs starts at 16+len.
 *   * the checksum loop returns the number of 256-byte blocks it walked
 *     (0 for len==0), which is the value the rootfs-signature branch tests.
 */
/* ================== FUN_8001081c @8001081c ================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_8001081c(int param_1,int param_2)

{
  int iVar1;
  uint uVar2;
  uint uVar3;
  code *pcVar4;
  
  iVar1 = FUN_80009eb8(param_2,param_1,0x10);
  if (iVar1 == 0) {
    FUN_8000f8e4("sflashread fail,addr=%x,size=%d\n",param_1,0x10);
  }
  else {
    uVar2 = *(uint *)(param_2 + 4);
    uVar3 = uVar2 >> 0x18 | uVar2 << 0x18 | (uVar2 & 0xff00) << 8 | (uVar2 & 0xff0000) >> 8;
    *(uint *)(param_2 + 4) = uVar3;
    uVar2 = *(uint *)(param_2 + 8);
    *(uint *)(param_2 + 8) =
         uVar2 >> 0x18 | uVar2 << 0x18 | (uVar2 & 0xff00) << 8 | (uVar2 & 0xff0000) >> 8;
    uVar2 = *(uint *)(param_2 + 0xc);
    uVar2 = uVar2 >> 0x18 | uVar2 << 0x18 | (uVar2 & 0xff00) << 8 | (uVar2 & 0xff0000) >> 8;
    *(uint *)(param_2 + 0xc) = uVar2;
    FUN_80009eb8(uVar3 | 0x20000000,param_1 + 0x50000010,uVar2 - 2);
    _DAT_b8003000 = 0;
    FUN_8000f8e4("Jump to image start:0x%x...\n",*(undefined4 *)(param_2 + 4));
    pcVar4 = *(code **)(param_2 + 4);
    _DAT_b8003000 = 0;
    _DAT_b8003004 = 0;
    _DAT_b8003114 = 0;
    _DAT_b8000010 = _DAT_b8000010 & 0xfffff7ff;
    _DAT_bbdc0300 = 0xffffffff;
    _DAT_bbdc0304 = 0xffffffff;
    setCopReg(0,Status,(Status | 1) ^ 1);
    FUN_8000dce0();
    (*pcVar4)();
  }
  return;
}




/* ================== FUN_80010be4 @80010be4 ================== */

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4 FUN_80010be4(int param_1,int param_2)

{
  int iVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  ushort *puVar5;
  short sVar6;
  undefined4 uVar7;
  undefined4 local_130;
  undefined4 local_12c;
  ushort local_128 [128];
  
  local_130 = 0;
  local_12c = 0;
  if (_DAT_80044d50 == 1) {
LAB_80010e50:
    uVar7 = 0;
  }
  else {
    iVar1 = FUN_80009eb8(param_2,param_1,0x10);
    if (iVar1 == 0) {
      FUN_8000f8e4("flashread fail,addr:%x,size:%d\n",param_1,0x10);
      uVar7 = 0;
    }
    else {
      uVar3 = *(uint *)(param_2 + 4);
      *(uint *)(param_2 + 4) =
           uVar3 >> 0x18 | uVar3 << 0x18 | (uVar3 & 0xff00) << 8 | (uVar3 & 0xff0000) >> 8;
      uVar3 = *(uint *)(param_2 + 8);
      *(uint *)(param_2 + 8) =
           uVar3 >> 0x18 | uVar3 << 0x18 | (uVar3 & 0xff00) << 8 | (uVar3 & 0xff0000) >> 8;
      uVar3 = *(uint *)(param_2 + 0xc);
      *(uint *)(param_2 + 0xc) =
           uVar3 >> 0x18 | uVar3 << 0x18 | (uVar3 & 0xff00) << 8 | (uVar3 & 0xff0000) >> 8;
      FUN_8000ec28(&local_130,&DAT_800311a8,4);
      FUN_8000ec28(&local_12c,&DAT_800311a0,4);
      iVar1 = FUN_8000ec54(param_2,&local_130,4);
      uVar7 = 1;
      if (iVar1 != 0) {
        iVar1 = FUN_8000ec54(param_2,&local_12c,4);
        uVar7 = 2;
        if (iVar1 != 0) {
          FUN_8000f8e4("No sys signature at %X!\n",param_1 + 0x50000000);
          return 0;
        }
      }
      iVar1 = 0;
      if (*(int *)(param_2 + 0xc) != 0) {
        sVar6 = 0;
        do {
          _DAT_80044d54 = _DAT_80044d54 + 1;
          if (0x20000 < _DAT_80044d54) {
            _DAT_80044d54 = 0;
            iVar2 = FUN_800109b8(0);
            if (iVar2 == 1) goto LAB_80010e50;
          }
          uVar3 = *(int *)(param_2 + 0xc) - iVar1;
          if (0x100 < uVar3) {
            uVar3 = 0x100;
          }
          FUN_80009eb8(local_128,param_1 + 0x10 + iVar1,uVar3);
          if (uVar3 != 0) {
            uVar4 = 0;
            do {
              puVar5 = (ushort *)((int)local_128 + uVar4);
              sVar6 = sVar6 + (*puVar5 >> 8 | *puVar5 << 8);
              uVar4 = uVar4 + 2;
            } while (uVar4 < uVar3);
          }
          iVar1 = iVar1 + uVar3;
        } while (*(int *)(param_2 + 0xc) != iVar1);
        if (sVar6 != 0) {
          FUN_8000f8e4("check failed, ret:%d, sum:%x\n",uVar7,sVar6);
          uVar7 = 0;
        }
      }
    }
  }
  return uVar7;
}



