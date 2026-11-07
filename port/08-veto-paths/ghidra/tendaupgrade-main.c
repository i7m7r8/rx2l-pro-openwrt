// Decompiled with Ghidra 12.1.4 (the engine bundled by Craft Anything), driven
// headlessly because craft-anything-cli's own decompiler has no MIPS support:
//   analyzeHeadless ... -import <file> -postScript DecompAt.java <addrs> <out>
//
// ADDRESS SPACE CAVEAT: Ghidra rebases shared objects by +0x10000 here. Its
// "upgrade @ 0x541ec" is file/objdump address 0x441ec (readelf: logSetPath at
// 0x343d0, logOpen at 0x341c4, supper at 0x345a0 -- all 0x10000 lower). Entries
// below are labelled with Ghidra's address; subtract 0x10000 to match readelf,
// objdump and every other note in this repo. Non-PIE executables (tendaupgrade)
// are not shifted.
//
// ---- FUN_00402388 @ 0x00402388 ----

undefined4 FUN_00402388(int param_1,int param_2)

{
  undefined4 uVar1;
  int iVar2;
  int iVar3;
  undefined4 local_50;
  undefined4 local_48;
  undefined4 local_44;
  undefined2 local_40;
  undefined1 local_3e;
  
  local_50 = 1;
  FUN_00405a20(&local_48,0,0x40);
  if ((param_1 == 4) || (param_1 == 2)) {
    iVar2 = FUN_004073b0(*(undefined4 *)(param_2 + 4));
    if (param_1 == 4) {
      FUN_00403210(&local_48,0x40,PTR_LAB_0042975c + 0x1a04,*(undefined4 *)(param_2 + 0xc));
      iVar3 = FUN_004073b0(*(undefined4 *)(param_2 + 8));
      if ((iVar3 != 0) && (iVar3 = FUN_004073b0(*(undefined4 *)(param_2 + 8)), iVar3 != 1)) {
        return 0xffffffff;
      }
      local_50 = FUN_004073b0(*(undefined4 *)(param_2 + 8));
    }
    else {
      local_48 = *(undefined4 *)(PTR_LAB_0042975c + 0x1a08);
      local_44 = *(undefined4 *)(PTR_LAB_0042975c + 0x1a0c);
      local_40 = *(undefined2 *)(PTR_LAB_0042975c + 0x1a10);
      local_3e = PTR_LAB_0042975c[0x1a12];
    }
    if ((iVar2 < 0) || (6 < iVar2)) {
      FUN_004031a0(PTR_LAB_0042975c + 0x1a14,PTR_LAB_0042975c + 7000,0x32d,
                   *(undefined4 *)(param_2 + 4));
    }
    else {
      FUN_00402274(iVar2,local_50,&local_48);
    }
    uVar1 = 0;
  }
  else {
    uVar1 = 0xffffffff;
  }
  return uVar1;
}



// ---- FUN_00401fdc @ 0x00401fdc ----

undefined4 FUN_00401fdc(undefined4 param_1,int param_2)

{
  int iVar1;
  undefined1 auStack_10c [260];
  
  FUN_00405a20(auStack_10c,0,0x100);
  if (param_2 == 2) {
    iVar1 = FUN_00400a68(auStack_10c,0x100);
    if (iVar1 < 0) {
      FUN_004031a0(PTR_LAB_0042975c + 0x18e0,PTR_LAB_0042975c + 0x1b18,0x2a9);
    }
    else {
      FUN_004031a0(PTR_LAB_0042975c + 0x1900,PTR_LAB_0042975c + 0x1b18,0x2ad);
      iVar1 = FUN_00401904(param_1,auStack_10c);
      if (-1 < iVar1) {
        FUN_00402b30(param_1);
        return 1;
      }
      FUN_004031a0(PTR_LAB_0042975c + 0x1924,PTR_LAB_0042975c + 0x1b18,0x2b1);
    }
  }
  else {
    FUN_004031a0(PTR_LAB_0042975c + 0x1948,PTR_LAB_0042975c + 0x1b18,0x2b7);
  }
  FUN_00402b30(param_1);
  return 0xffffffff;
}



// ---- FUN_004027e4 @ 0x004027e4 ----

undefined4 FUN_004027e4(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  undefined4 uVar1;
  undefined4 uVar2;
  int iVar3;
  undefined4 in_HW_ULR;
  
  iVar3 = getHWRegister(in_HW_ULR);
  if (*(int *)(iVar3 + -0x7460) == 0) {
    uVar1 = FUN_004027b0();
    return uVar1;
  }
  uVar1 = FUN_00407d80();
  uVar2 = FUN_004027b0(param_1,param_2,param_3);
  FUN_00407e2c(uVar1);
  return uVar2;
}




// ---- FUN_004027b0 @ 0x004027b0 ----

undefined4 FUN_004027b0(void)

{
  undefined4 uVar1;
  int iVar2;
  int in_a3;
  undefined4 in_HW_ULR;
  
  uVar1 = 0xfb3;
  syscall(0);
  if (in_a3 != 0) {
    iVar2 = getHWRegister(in_HW_ULR);
    *(undefined4 *)(PTR_004297fc + iVar2) = 0xfb3;
    uVar1 = 0xffffffff;
  }
  return uVar1;
}



// ---- FUN_00401904 @ 0x00401904 ----

undefined4 FUN_00401904(undefined4 param_1,undefined4 param_2)

{
  int iVar1;
  int iVar2;
  int iVar3;
  int iStack_100d8;
  int iStack_100d4;
  undefined1 auStack_100c8 [52];
  undefined4 uStack_10094;
  undefined1 auStack_10030 [65536];
  undefined4 local_30;
  undefined4 local_2c;
  undefined4 local_28;
  undefined4 local_24;
  undefined4 local_20;
  undefined4 local_1c;
  undefined4 local_18;
  undefined4 local_14;
  undefined4 local_10;
  undefined4 local_c;
  
  iStack_100d8 = 0;
  FUN_00405a20(auStack_100c8,0,0x98);
  iStack_100d4 = -1;
  FUN_00405a20(auStack_10030,0,0x10000);
  local_30 = 0;
  local_2c = 0;
  local_28 = 0;
  local_24 = 0;
  local_20 = 0;
  local_1c = 0;
  local_18 = 0;
  local_14 = 0;
  local_10 = 0;
  local_c = 0;
  iVar1 = FUN_00402ac0(param_1,auStack_100c8);
  if (iVar1 < 0) {
    FUN_004031a0(PTR_LAB_0042975c + 0x17e8,PTR_LAB_0042975c + 0x1b00,0x21b,param_1);
  }
  else {
    iVar1 = FUN_004028d8(param_1,2);
    if (iVar1 < 0) {
      FUN_004031a0(PTR_LAB_0042975c + 0x1804,PTR_LAB_0042975c + 0x1b00,0x222,param_1);
    }
    else {
      iVar2 = FUN_004003e0(param_2);
      if (iVar2 < 0) {
        FUN_004031a0(PTR_LAB_0042975c + 0x181c,PTR_LAB_0042975c + 0x1b00,0x22a,param_2);
      }
      else {
        iStack_100d4 = FUN_00400f88(param_2,2);
        if (iStack_100d4 < 0) {
          FUN_004031a0(PTR_LAB_0042975c + 0x16fc,PTR_LAB_0042975c + 0x1b00,0x232,param_2);
        }
        else {
          iVar2 = FUN_00401258(param_2,uStack_10094);
          if (iVar2 < 1) {
            FUN_004031a0(PTR_LAB_0042975c + 0x183c,PTR_LAB_0042975c + 0x1b00,0x239,param_2);
          }
          else {
            FUN_004017cc(iVar1,param_2);
            iVar2 = FUN_00402690(iStack_100d4,0x40204d01,&local_28);
            if (iVar2 != 0) {
              FUN_004031a0(PTR_LAB_0042975c + 0x1720,PTR_LAB_0042975c + 0x1b00,0x242);
            }
            local_2c = local_1c;
            iVar2 = FUN_00402690(iStack_100d4,0x80084d06,&local_30);
            if (iVar2 != 0) {
              FUN_004031a0(PTR_LAB_0042975c + 0x185c,PTR_LAB_0042975c + 0x1b00,0x249);
            }
            while (iVar2 = FUN_004029f4(iVar1,auStack_10030,0x10000), 0 < iVar2) {
              iVar3 = 0x10000;
              if (0xffff < iVar2) {
                iVar3 = iVar2;
              }
              iVar2 = FUN_00402ba4(iStack_100d4,auStack_10030,iVar3);
              if (iVar2 != iVar3) {
                FUN_004031a0(PTR_LAB_0042975c + 0x187c,PTR_LAB_0042975c + 0x1b00,0x25e);
              }
              iStack_100d8 = iStack_100d8 + iVar3;
              FUN_004031a0(PTR_LAB_0042975c + 0x1894,iStack_100d8);
            }
            iVar2 = FUN_00402690(iStack_100d4,0x80084d05,&local_30);
            if (iVar2 != 0) {
              FUN_004031a0(PTR_LAB_0042975c + 0x18a0,PTR_LAB_0042975c + 0x1b00,0x271);
            }
            FUN_00402634(iVar1);
            FUN_00402634(iStack_100d4);
            iVar2 = FUN_00400664(param_2);
            if (-1 < iVar2) {
              return 1;
            }
            FUN_004031a0(PTR_LAB_0042975c + 0x18c0,PTR_LAB_0042975c + 0x1b00,0x27c,param_2);
          }
        }
      }
      if (0 < iVar1) {
        FUN_00402634(iVar1);
      }
      if (0 < iStack_100d4) {
        FUN_00402634(iStack_100d4);
      }
    }
  }
  return 0xffffffff;
}



// ---- FUN_00400a68 @ 0x00400a68 ----

undefined4 FUN_00400a68(int param_1,uint param_2)

{
  bool bVar1;
  int iVar2;
  undefined4 uVar3;
  int iVar4;
  undefined1 auStack_450 [64];
  int local_410;
  undefined1 auStack_40c [256];
  undefined1 auStack_30c [256];
  undefined1 auStack_20c [256];
  undefined4 local_10c;
  undefined4 local_108;
  undefined4 local_104;
  undefined4 local_100;
  undefined4 local_fc;
  undefined4 local_f8;
  undefined4 local_f4;
  
  FUN_00405a20(auStack_450,0,0x40);
  local_410 = -1;
  FUN_00405a20(auStack_40c,0,0x100);
  FUN_00405a20(auStack_30c,0,0x100);
  FUN_00405a20(auStack_20c,0,0x100);
  bVar1 = false;
  FUN_00405a20(&local_10c,0,0x100);
  if ((param_1 == 0) || (param_2 < 9)) {
    FUN_004031a0(PTR_LAB_0042975c + 0x1628,PTR_LAB_0042975c + 0x1aac,0xd6);
    return 0xffffffff;
  }
  iVar2 = FUN_004008e8();
  if (iVar2 == 0) {
    FUN_00405f30(param_1,PTR_LAB_0042975c + 0x16d8,param_2);
  }
  else {
    iVar2 = FUN_00403190(PTR_LAB_0042975c + 0x1564,PTR_LAB_0042975c + 0x1560);
    if (iVar2 == 0) {
      FUN_004031a0(PTR_LAB_0042975c + 0x15f0,PTR_LAB_0042975c + 0x1aac,0xdf);
      return 0xffffffff;
    }
    FUN_00402a80(PTR_LAB_0042975c + 0x164c,auStack_450,0x3f);
    FUN_00404b90(auStack_450,PTR_LAB_0042975c + 0x1658,&local_410);
    FUN_00403210(auStack_40c,0x100,PTR_LAB_0042975c + 0x1664,local_410 + -1);
    do {
      iVar4 = FUN_00404d70(auStack_30c,0x100,iVar2);
      if (iVar4 == 0) goto LAB_00400d88;
      uVar3 = FUN_00405de0(auStack_40c);
      iVar4 = FUN_00405e80(auStack_40c,auStack_30c,uVar3);
    } while (iVar4 != 0);
    FUN_00404b90(auStack_30c,PTR_LAB_0042975c + 0x1588,auStack_20c);
    bVar1 = true;
LAB_00400d88:
    if (!bVar1) {
      FUN_004031a0(PTR_LAB_0042975c + 0x16a4,PTR_LAB_0042975c + 0x1aac,0x10f);
      FUN_00402ec0(iVar2);
      return 0xffffffff;
    }
    iVar4 = FUN_00405db0(auStack_20c,PTR_LAB_0042975c + 0x1610);
    if (iVar4 == 0) {
      local_10c = *(undefined4 *)(PTR_LAB_0042975c + 0x166c);
      local_108 = *(undefined4 *)(PTR_LAB_0042975c + 0x1670);
      local_104 = *(undefined4 *)(PTR_LAB_0042975c + 0x1674);
      local_100 = *(undefined4 *)(PTR_LAB_0042975c + 0x1678);
      local_fc = *(undefined4 *)(PTR_LAB_0042975c + 0x167c);
      local_f8 = *(undefined4 *)(PTR_LAB_0042975c + 0x1680);
      local_f4 = *(undefined4 *)(PTR_LAB_0042975c + 0x1684);
      FUN_00405f30(param_1,PTR_LAB_0042975c + 0x161c,param_2);
    }
    else {
      local_10c = *(undefined4 *)(PTR_LAB_0042975c + 0x1688);
      local_108 = *(undefined4 *)(PTR_LAB_0042975c + 0x168c);
      local_104 = *(undefined4 *)(PTR_LAB_0042975c + 0x1690);
      local_100 = *(undefined4 *)(PTR_LAB_0042975c + 0x1694);
      local_fc = *(undefined4 *)(PTR_LAB_0042975c + 0x1698);
      local_f8 = *(undefined4 *)(PTR_LAB_0042975c + 0x169c);
      local_f4 = *(undefined4 *)(PTR_LAB_0042975c + 0x16a0);
      FUN_00405f30(param_1,PTR_LAB_0042975c + 0x1610,param_2);
    }
    FUN_00408a70(&local_10c);
    FUN_00402ec0(iVar2);
  }
  return 0;
}



// ---- FUN_00402b30 @ 0x00402b30 ----

undefined4 FUN_00402b30(void)

{
  undefined4 uVar1;
  int iVar2;
  int in_a3;
  undefined4 in_HW_ULR;
  
  uVar1 = 0xfaa;
  syscall(0);
  if (in_a3 != 0) {
    iVar2 = getHWRegister(in_HW_ULR);
    *(undefined4 *)(PTR_004297fc + iVar2) = 0xfaa;
    uVar1 = 0xffffffff;
  }
  return uVar1;
}



