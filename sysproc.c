#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *curproc = myproc();

  if(argint(0, &n) < 0)
    return -1;

  addr = myproc()->sz;

  if(n < 0){
    // 如果是负数，代表缩减堆空间，直接调用原有的growproc释放物理内存
    if(growproc(n) < 0)
      return -1;
  } else{
    // 检查是否超出用户空间最大限制(KERNBASE)
    if(curproc->sz + n >= KERNBASE || curproc->sz + n < curproc->sz)
      return -1;
    // 延迟分配：仅增加虚拟地址空间大小，不实际分配物理页面
    myproc()->sz += n;
  }
  
  return addr;
}

int
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
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_date(void)
{
  struct rtcdate *r;

  // 从用户态获取传递进来的指针
  // argptr 会进行安全检查，防止非法指针访问内核空间
  if(argptr(0, (void*)&r, sizeof(*r)) < 0)
    return -1;

  // cmostime 是 lapic.c 中提供的函数，可以直接读取硬件实时时钟
  cmostime(r);

  return 0;
}

int
sys_alarm(void)
{
  int ticks;
  void (*handler)();

  // 获取用户传入的第一个参数 (ticks) 和第二个参数
  if(argint(0, &ticks) < 0)
    return -1;
  if(argptr(1, (char**)&handler, 1) < 0)
    return -1;
  
  // 保存到当前进程的结构体中
  myproc()->alarmticks = ticks;
  myproc()->alarmhandler = handler;
  myproc()->ticks_count = 0;

  return 0;
}