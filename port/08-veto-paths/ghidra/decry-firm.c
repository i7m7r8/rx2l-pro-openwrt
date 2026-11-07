// Decompiled with Ghidra 12.1.4 (the engine Craft Anything bundles) via
// analyzeHeadless + DecompAt.java. decry_firm is a non-PIE executable, so these
// addresses are the file's own (no rebase).
//
// Read together with ../DECRY-FIRM-FORMAT.md: this is the readable form of the
// two-signature scheme that document describes from disassembly.
//
// ---- main @ 0x00406728 ----

undefined4 main(undefined4 param_1,int param_2)

{
  int iVar1;
  undefined4 uVar2;
  
  if ((*(int *)(param_2 + 8) != 0) && (iVar1 = access(*(char **)(param_2 + 8),0), iVar1 == 0)) {
    unlink(*(char **)(param_2 + 8));
  }
  iVar1 = FUN_00406290(*(undefined4 *)(param_2 + 4));
  if (iVar1 == 0) {
    uVar2 = 0;
  }
  else {
    puts("openssl decode failed");
    if (*(int *)(param_2 + 8) != 0) {
      iVar1 = creat(*(char **)(param_2 + 8),0x1ff);
      if (-1 < iVar1) {
        close(iVar1);
      }
    }
    uVar2 = 0xffffffff;
  }
  return uVar2;
}



// ---- FUN_00406290 @ 0x00406290 ----

/* WARNING: Type propagation algorithm not settling */

undefined4 FUN_00406290(char *param_1)

{
  int __fd;
  int iVar1;
  void *__dest;
  void *__ptr;
  undefined4 local_d0;
  void *local_cc;
  void *local_c8;
  stat sStack_b0;
  size_t local_18 [4];
  
  local_d0 = 0xffffffff;
  local_18[0] = 0;
  local_18[1] = 0;
  local_cc = (void *)0x0;
  local_18[2] = 0;
  local_c8 = (void *)0x0;
  local_18[3] = 0;
  if (param_1 == (char *)0x0) {
    fprintf(stderr,"[%s][%d] path is NULL!\n","decrypt_firmware",0x58);
    local_d0 = 0xffffffff;
  }
  else {
    __fd = open(param_1,0x102);
    if (__fd < 0) {
      fprintf(stderr,"[%s][%d] open ori failed!\n","decrypt_firmware",0x5f);
      local_d0 = 0xffffffff;
    }
    else {
      iVar1 = fstat(__fd,&sStack_b0);
      if ((iVar1 == 0) && ((sStack_b0.st_nlink & 0xf000) == 0x8000)) {
        __dest = mmap((void *)0x0,sStack_b0.st_blocks,3,1,__fd,0);
        __ptr = (void *)FUN_00405b8c(local_18 + 1);
        if (__ptr == (void *)0x0) {
          fprintf(stderr,"[%s][%d] get aes_pass failed!\n","decrypt_firmware",0x6f);
        }
        else {
          local_c8 = (void *)FUN_00406080((int)__dest + 4,local_18 + 3);
          if (local_c8 == (void *)0x0) {
            fprintf(stderr,"[%s][%d] get signed2 failed!\n","decrypt_firmware",0x76);
          }
          else {
            iVar1 = FUN_004061fc((int)__dest + 0x204,sStack_b0.st_blocks - 0x204,local_c8,
                                 local_18[3]);
            if (iVar1 == 0) {
              FUN_00405d08((int)__dest + 0x204,sStack_b0.st_blocks - 0x204,__ptr,0x40,__dest,
                           local_18);
              local_cc = (void *)FUN_00406080(__dest,local_18 + 2);
              if (local_cc == (void *)0x0) {
                fprintf(stderr,"[%s][%d] get signed1 failed!\n","decrypt_firmware",0x86);
              }
              else {
                iVar1 = FUN_004061fc((int)__dest + 0x200,
                                     (local_18[0] - 0x200) -
                                     (uint)*(byte *)((int)__dest + (local_18[0] - 1)),local_cc,
                                     local_18[2]);
                if (iVar1 == 0) {
                  local_18[0] = (local_18[0] - 0x200) -
                                (uint)*(byte *)((int)__dest + (local_18[0] - 1));
                  memcpy(__dest,(void *)((int)__dest + 0x200),local_18[0]);
                  ftruncate(__fd,local_18[0]);
                  local_d0 = 0;
                }
                else {
                  fprintf(stderr,"[%s][%d] signed2 HASH failed!\n","decrypt_firmware",0x8c);
                }
              }
            }
            else {
              fprintf(stderr,"[%s][%d] signed2 HASH failed!\n","decrypt_firmware",0x7c);
            }
          }
        }
        if (__ptr != (void *)0x0) {
          free(__ptr);
        }
        if (local_cc != (void *)0x0) {
          free(local_cc);
        }
        if (local_c8 != (void *)0x0) {
          free(local_c8);
        }
        munmap(__dest,sStack_b0.st_blocks);
        close(__fd);
      }
      else {
        fprintf(stderr,"[%s][%d] get ori stat failed!\n","decrypt_firmware",100);
        close(__fd);
        local_d0 = 0xffffffff;
      }
    }
  }
  return local_d0;
}



// ---- FUN_00406080 @ 0x00406080 ----

void * FUN_00406080(undefined4 param_1,size_t *param_2)

{
  int iVar1;
  void *__dest;
  size_t __n;
  undefined1 auStack_608 [1536];
  
  memset(auStack_608,0,0x600);
  iVar1 = FUN_00405490(param_1,0x200,auStack_608,param_2);
  if (iVar1 == 0) {
    __n = *param_2;
    if (__n == 0) {
      fprintf(stderr,"[%s][%d] signed_digest len failed!\n","decrypt_signed_digest",0x2b);
      __dest = (void *)0x0;
    }
    else {
      __dest = malloc(__n + 1);
      if (__dest == (void *)0x0) {
        fprintf(stderr,"[%s][%d] signed_digest malloc failed!\n","decrypt_signed_digest",0x31);
        __dest = (void *)0x0;
      }
      else {
        memcpy(__dest,auStack_608,__n);
        *(undefined1 *)((int)__dest + __n) = 0;
      }
    }
  }
  else {
    fprintf(stderr,"[%s][%d] get signed_digest failed!\n","decrypt_signed_digest",0x24);
    __dest = (void *)0x0;
  }
  return __dest;
}



