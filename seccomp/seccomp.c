#include <unistd.h>
#include <stdlib.h>
#include <seccomp.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <elf.h>

#include "policy.h"
#include "../isolate.h"
#include <stdio.h>

void setup_seccomp(uint32_t arch_token)
{
  scmp_filter_ctx ctx;
  ctx = seccomp_init(seccomp_default_action);
  seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execve), 0);
  seccomp_arch_add(ctx, arch_token);
  for (int* syscallptr = seccomp_allow; *syscallptr != -1; syscallptr++)
      seccomp_rule_add(ctx, SCMP_ACT_ALLOW, *syscallptr, 0);
  seccomp_load(ctx);
}

void elf_get_ident(char *path, unsigned char *buffer)
{
  int fd;
  if ((fd = open(path, O_RDONLY)) < 0) goto out;
  if (read(fd, buffer, 16) != 16) goto out;
  close(fd);
  return;
  out: die("failed to get e_ident of %s. %m", path);
}

uint32_t get_arch(char *path)
{
  unsigned char ident[16];
  elf_get_ident(path, ident);
  if (ident[EI_CLASS] == ELFCLASS64) return SCMP_ARCH_X86_64;
  else if (ident[EI_CLASS] == ELFCLASS32) return SCMP_ARCH_X86;
  else die("Unknown architecture");
}
