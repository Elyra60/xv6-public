#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }

    // 只有在当前有进程运行，且来自用户空间((tf->cs & 3) == 3)时，才计算tick
    if(myproc() != 0 && (tf->cs & 3) == 3){
      if(myproc()->alarmticks > 0){
        myproc()->ticks_count++;
        // 达到预设的 ticks 数
        if(myproc()->ticks_count == myproc()->alarmticks){
          // 重置计数器
          myproc()->ticks_count = 0;

          // 把当前被打断的指令地址压入用户栈
          myproc()->tf->esp -= 4;
          *((uint*)(myproc()->tf->esp)) = myproc()->tf->eip;

          // 将 eip 修改为 handler 的地址
          myproc()->tf->eip = (uint)myproc()->alarmhandler;
        }
      }
    }

    lapiceoi();
    break;

  // 延迟分配
  case T_PGFLT: {
    
    uint va;
    uint a;
    char *mem;
    struct proc *curproc;

    // 获取引发页错误的虚拟地址
    va = rcr2();
    curproc = myproc();

    // 边界检查
    // va必须小于当前进程的虚拟空间大小sz
    // va不能越界到用户栈下方过深的非法保护页
    if(va >= curproc->sz || va >= KERNBASE || va < curproc->tf->esp) {
      if(curproc->pid == 1){  // init进程不能被杀
        panic("trap: page fault in init");
      }
      cprintf("pid %d %s: trap %d err %d on cpu %d eip 0x%x addr 0x%x--kill proc\n",
              curproc->pid, curproc->name, tf->trapno, tf->err, cpuid(), tf->eip, va);
      curproc->killed = 1;
      break;
    }

    // 计算对齐后的虚拟页边界
    a = PGROUNDDOWN(va);

    // 分配物理内存页
    mem = kalloc();
    if (mem == 0) {
      cprintf("Lazy alloc: out of physical memory\n");
      curproc->killed = 1;
      break;
    }

    // 清零新分配的页面
    memset(mem, 0, PGSIZE);

    // 建立映射关系
    if(mappages(curproc->pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("Lazy alloc: mappages failed\n");
      kfree(mem);
      curproc->killed = 1;
      break;
    }
    break;
  }
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
