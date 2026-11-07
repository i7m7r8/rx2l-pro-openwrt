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
// ---- upgrade @ 0x000541ec ----

void upgrade(int param_1)

{
  int iVar1;
  int iVar2;
  int local_384;
  int local_380;
  undefined1 auStack_370 [56];
  undefined4 local_338;
  undefined1 auStack_2cc [64];
  undefined1 auStack_28c [128];
  undefined1 auStack_20c [256];
  undefined1 auStack_10c [256];
  int local_c;
  
  local_c = *(int *)PTR___stack_chk_guard_000dbd54;
  local_384 = -1;
  (*(code *)PTR_memset_000db590)(auStack_20c,0,0x100);
  (*(code *)PTR_memset_000db590)(auStack_10c,0,0x100);
  local_380 = 0;
  (*(code *)PTR_memset_000db590)(auStack_28c,0,0x80);
  (*(code *)PTR_memset_000db590)(auStack_2cc,0,0x40);
  (*(code *)PTR_memset_000db590)(auStack_370,0,0xa0);
  iVar1 = (*(code *)PTR_hashFirst_000db0f4)(*(undefined4 *)(param_1 + 0x20c));
  if (iVar1 == 0) {
    (*(code *)PTR_printf_000db1d8)
              (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x4048,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x2c98,0x615);
  }
  else {
    iVar1 = *(int *)(iVar1 + 0x18);
    (*(code *)PTR_snprintf_000dba24)
              (auStack_10c,0x100,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3360,
               *(undefined4 *)(iVar1 + 4));
    (*(code *)PTR_doSystemCmd_000db518)(auStack_10c);
    (*(code *)PTR_snprintf_000dba24)
              (auStack_20c,0x100,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x334c,
               *(undefined4 *)(iVar1 + 4));
    (*(code *)PTR_doSystemCmd_000db518)(auStack_20c);
    (*(code *)PTR_doSystemCmd_000db518)
              (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3314,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3650,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3334);
    iVar1 = (*(code *)PTR_stat64_000db94c)
                      (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3650,
                       auStack_370);
    if (iVar1 == -1) {
      (*(code *)PTR_puts_000db188)
                (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3300);
    }
    else {
      local_384 = (*(code *)PTR_open64_000db310)
                            (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3650
                             ,0x102,0x1ff);
      if (local_384 < 0) {
        (*(code *)PTR_error_000db654)
                  (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x32e8);
      }
      else {
        (*(code *)PTR_ftruncate64_000db154)(local_384);
        local_380 = (*(code *)PTR_mmap64_000db6b0)(0,local_338,3,1,local_384);
        if (local_380 == 0) {
          (*(code *)PTR_error_000db654)
                    (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3fb0);
        }
        else {
          iVar1 = (*(code *)PTR_systool_upgradefile_handle_000db2d4)(0,local_380,local_338);
          iVar2 = (*(code *)PTR_munmap_000db534)(local_380,local_338);
          if (iVar2 == -1) {
            (*(code *)PTR_perror_000db554)
                      (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3fa8);
          }
          (*(code *)PTR_close_000db768)(local_384);
          FUN_00053f6c(*(undefined4 *)(param_1 + 0x180),auStack_2cc);
          iVar2 = iVar1;
          if (iVar1 < 0) {
            iVar2 = -iVar1;
          }
          (*(code *)PTR_snprintf_000dba24)
                    (auStack_28c,0x80,
                     PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x32d0,
                     auStack_2cc,iVar2);
          (*(code *)PTR_websSetStatus_000dbba4)(param_1,0x12e);
          (*(code *)PTR_websWriteHeaders_000dbc08)(param_1,0,auStack_28c);
          (*(code *)PTR_websWriteEndHeaders_000db274)(param_1);
          (*(code *)PTR_websDone_000db254)(param_1);
          if (iVar1 == 0) {
            (*(code *)PTR_printf_000db1d8)
                      (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x32ac,
                       PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x2c90,0x651
                      );
            (*(code *)PTR_clean_force_upgrade_info_000db2f4)();
            (*(code *)PTR_systool_call_writeImage_delay_11ac_000db0b4)
                      (*(undefined4 *)PTR_fw_type_000dbc24);
            goto LAB_00054838;
          }
        }
      }
    }
  }
  if (local_380 != 0) {
    iVar1 = (*(code *)PTR_munmap_000db534)(local_380,local_338);
    if (iVar1 == -1) {
      (*(code *)PTR_perror_000db554)
                (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3fa8);
    }
  }
  if (0 < local_384) {
    (*(code *)PTR_close_000db768)(local_384);
    (*(code *)PTR_doSystemCmd_000db518)
              (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x327c);
  }
  (*(code *)PTR_doSystemCmd_000db518)
            (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3268);
LAB_00054838:
  if (local_c != *(int *)PTR___stack_chk_guard_000dbd54) {
    (*(code *)PTR___stack_chk_fail_000db878)();
  }
  return;
}



// ---- upgrade @ 0x000541ec ----

void upgrade(int param_1)

{
  int iVar1;
  int iVar2;
  int local_384;
  int local_380;
  undefined1 auStack_370 [56];
  undefined4 local_338;
  undefined1 auStack_2cc [64];
  undefined1 auStack_28c [128];
  undefined1 auStack_20c [256];
  undefined1 auStack_10c [256];
  int local_c;
  
  local_c = *(int *)PTR___stack_chk_guard_000dbd54;
  local_384 = -1;
  (*(code *)PTR_memset_000db590)(auStack_20c,0,0x100);
  (*(code *)PTR_memset_000db590)(auStack_10c,0,0x100);
  local_380 = 0;
  (*(code *)PTR_memset_000db590)(auStack_28c,0,0x80);
  (*(code *)PTR_memset_000db590)(auStack_2cc,0,0x40);
  (*(code *)PTR_memset_000db590)(auStack_370,0,0xa0);
  iVar1 = (*(code *)PTR_hashFirst_000db0f4)(*(undefined4 *)(param_1 + 0x20c));
  if (iVar1 == 0) {
    (*(code *)PTR_printf_000db1d8)
              (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x4048,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x2c98,0x615);
  }
  else {
    iVar1 = *(int *)(iVar1 + 0x18);
    (*(code *)PTR_snprintf_000dba24)
              (auStack_10c,0x100,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3360,
               *(undefined4 *)(iVar1 + 4));
    (*(code *)PTR_doSystemCmd_000db518)(auStack_10c);
    (*(code *)PTR_snprintf_000dba24)
              (auStack_20c,0x100,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x334c,
               *(undefined4 *)(iVar1 + 4));
    (*(code *)PTR_doSystemCmd_000db518)(auStack_20c);
    (*(code *)PTR_doSystemCmd_000db518)
              (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3314,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3650,
               PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3334);
    iVar1 = (*(code *)PTR_stat64_000db94c)
                      (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3650,
                       auStack_370);
    if (iVar1 == -1) {
      (*(code *)PTR_puts_000db188)
                (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3300);
    }
    else {
      local_384 = (*(code *)PTR_open64_000db310)
                            (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3650
                             ,0x102,0x1ff);
      if (local_384 < 0) {
        (*(code *)PTR_error_000db654)
                  (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x32e8);
      }
      else {
        (*(code *)PTR_ftruncate64_000db154)(local_384);
        local_380 = (*(code *)PTR_mmap64_000db6b0)(0,local_338,3,1,local_384);
        if (local_380 == 0) {
          (*(code *)PTR_error_000db654)
                    (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3fb0);
        }
        else {
          iVar1 = (*(code *)PTR_systool_upgradefile_handle_000db2d4)(0,local_380,local_338);
          iVar2 = (*(code *)PTR_munmap_000db534)(local_380,local_338);
          if (iVar2 == -1) {
            (*(code *)PTR_perror_000db554)
                      (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3fa8);
          }
          (*(code *)PTR_close_000db768)(local_384);
          FUN_00053f6c(*(undefined4 *)(param_1 + 0x180),auStack_2cc);
          iVar2 = iVar1;
          if (iVar1 < 0) {
            iVar2 = -iVar1;
          }
          (*(code *)PTR_snprintf_000dba24)
                    (auStack_28c,0x80,
                     PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x32d0,
                     auStack_2cc,iVar2);
          (*(code *)PTR_websSetStatus_000dbba4)(param_1,0x12e);
          (*(code *)PTR_websWriteHeaders_000dbc08)(param_1,0,auStack_28c);
          (*(code *)PTR_websWriteEndHeaders_000db274)(param_1);
          (*(code *)PTR_websDone_000db254)(param_1);
          if (iVar1 == 0) {
            (*(code *)PTR_printf_000db1d8)
                      (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x32ac,
                       PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x2c90,0x651
                      );
            (*(code *)PTR_clean_force_upgrade_info_000db2f4)();
            (*(code *)PTR_systool_call_writeImage_delay_11ac_000db0b4)
                      (*(undefined4 *)PTR_fw_type_000dbc24);
            goto LAB_00054838;
          }
        }
      }
    }
  }
  if (local_380 != 0) {
    iVar1 = (*(code *)PTR_munmap_000db534)(local_380,local_338);
    if (iVar1 == -1) {
      (*(code *)PTR_perror_000db554)
                (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3fa8);
    }
  }
  if (0 < local_384) {
    (*(code *)PTR_close_000db768)(local_384);
    (*(code *)PTR_doSystemCmd_000db518)
              (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x327c);
  }
  (*(code *)PTR_doSystemCmd_000db518)
            (PTR_s_k_type__s__old__s__area__s__old__000bffe4_0x1c_000db038 + -0x3268);
LAB_00054838:
  if (local_c != *(int *)PTR___stack_chk_guard_000dbd54) {
    (*(code *)PTR___stack_chk_fail_000db878)();
  }
  return;
}



