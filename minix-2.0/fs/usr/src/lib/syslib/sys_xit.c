#include "syslib.h"

PUBLIC int sys_xit(parent, proc)
int parent;			/* parent of exiting process */
int proc;			/* which process has exited */
{
/* A process has exited.  Tell the kernel. */

  message m;

  m.m1_i1 = parent;
  m.m1_i2 = proc;
  return(_taskcall(SYSTASK, SYS_XIT, &m));
}
