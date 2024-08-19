#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);

  backtrace();

  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
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

uint64
sys_sigalarm(void){
  int alarmticks;
  uint64 handler;
  if(argint(0, &alarmticks) < 0 || argaddr(1, &handler) < 0)
    return -1;

  struct proc *p = myproc();
  p->alarmticks = alarmticks;
  p->handler = (void (*)())handler;
  p->passedticks = 0;

  return 0;
}

uint64
sys_sigreturn(){
  struct proc *p = myproc();
  // 恢复寄存器内容
  // 这里不能直接让p->trapframe = p->trapframecopy，会造成原p->trapframe的内存无法释放
  if(p->trapframecopy != (struct trapframe *)((char *)p->trapframe + 512)) {
    return -1;
  }
  memmove(p->trapframe, p->trapframecopy, sizeof(struct trapframe));

  // 返回前再重新开始计时，这样就不会冲突
  p->passedticks = 0;

  // 返回值会存储在a0中，如果返回其它值，会覆盖原有的a0，所以只能返回p->trapframe->a0
  return p->trapframe->a0;
}
