#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

extern int disk_policy;
extern int raid_policy;
extern int disk_fail;

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// return hello message
uint64
sys_hello(void)
{
  printf("Hello from the kernel!\n");
  return 0;
}

uint64
sys_getpid2(void)
{
  return myproc()->pid;
}

uint64
sys_getppid(void)
{
  return kgetppid();
}

uint64
sys_getnumchild(void)
{
  return kgetnumchild();
}

uint64
sys_getsyscount(void)
{
  return kgetsyscount();
}

uint64
sys_getchildsyscount(void)
{
  int pid;
  argint(0, &pid);
  
  return kgetchildsyscount(pid);
}

uint64
sys_getlevel(void)
{
  return kgetlevel();
}

uint64
sys_getmlfqinfo(void)
{
  int pid;
  uint64 info_addr;
  struct mlfqinfo kinfo;
  struct proc *p = myproc();

  argint(0, &pid);
  argaddr(1, &info_addr);

  if (kgetmlfqinfo(pid, &kinfo) < 0){
    return -1;
  }

  if (copyout(p -> pagetable, info_addr, (char *) &kinfo, sizeof(kinfo)) < 0){
    return -1;
  }

  return 0;
}

uint64
sys_getvmstats(void)
{
  int pid;
  uint64 info_addr;
  struct vmstats kinfo;
  struct proc *p = myproc();

  argint(0, &pid);
  argaddr(1, &info_addr);

  if (kgetvmstats(pid, &kinfo) < 0){
    return -1;
  }

  if (copyout(p -> pagetable, info_addr, (char *) &kinfo, sizeof(kinfo)) < 0){
    return -1;
  }

  return 0;
}

uint64
sys_setdisksched(void)
{
  int policy;
  argint(0, &policy);
  
  if (policy != 0 && policy != 1){
    return -1;
  }
  
  disk_policy = policy;

  return 0;
}

uint64
sys_setraidpolicy(void)
{
  int policy;
  argint(0, &policy);
  
  if (policy != 0 && policy != 1 && policy != 5){
    return -1;
  }
  
  raid_policy = policy;

  return 0;
}

uint64
sys_setdiskfail(void)
{
  int disk;
  argint(0, &disk);
  
  if (disk < -1 || disk > 3){
    return -1;
  }
  
  disk_fail = disk;

  return 0;
}