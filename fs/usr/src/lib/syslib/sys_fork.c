#include "syslib.h"

PUBLIC int sys_fork(parent, child, pid)
int parent;			/* process doing the fork */
int child;			/* which proc has been created by the fork */
int pid;			/* process id assigned by MM */
{
/* A process has forked.  Tell the kernel. */

  message m;

  m.m1_i1 = parent;
  m.m1_i2 = child;
  m.m1_i3 = pid;
  return(_taskcall(SYSTASK, SYS_FORK, &m));
}
