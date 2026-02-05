#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "fcntl.h"
#include "file.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
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
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
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
sys_mmap(void)
{
  uint64 addr;
  uint64 length;
  int prot;
  int flags;
  int fd;
  int offset;

  // Get arguments from user space
  argaddr(0, &addr);
  argaddr(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argint(5, &offset);

  struct proc *p = myproc();
  struct file *f;

  // Validate file descriptor
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;

  // Check permissions for MAP_SHARED
  if(flags == MAP_SHARED) {
    if((prot & PROT_READ) && !f->readable)
      return -1;
    if((prot & PROT_WRITE) && !f->writable)
      return -1;
  }

  // Find an unused VMA slot
  int i;
  for(i = 0; i < VMASIZE; i++) {
    if(p->vmas[i].valid == -1)
      break;
  }
  if(i == VMASIZE)
    return -1;  // No free VMA slots

  // Allocate virtual address space at the end of process memory
  uint64 map_addr = p->sz;
  p->sz += length;

  // Increment file reference count
  f = filedup(f);

  // Initialize VMA (lazy allocation - no physical pages yet)
  p->vmas[i].addr = map_addr;
  p->vmas[i].length = length;
  p->vmas[i].prot = prot;
  p->vmas[i].flags = flags;
  p->vmas[i].fd = fd;
  p->vmas[i].offset = offset;
  p->vmas[i].valid = 0;
  p->vmas[i].file = f;

  return map_addr;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  uint64 length;
  
  argaddr(0, &addr);
  argaddr(1, &length);

  struct proc *p = myproc();
  
  // Find the VMA containing this address
  int i;
  for(i = 0; i < VMASIZE; i++) {
    if(p->vmas[i].valid == 0) {
      uint64 start = p->vmas[i].addr;
      uint64 end = start + p->vmas[i].length;
      if(addr >= start && addr < end)
        break;
    }
  }
  
  if(i == VMASIZE)
    return -1;  // VMA not found

  // Write back dirty pages if MAP_SHARED and writable
  uint64 sz;
  for(sz = 0; sz < length; sz += PGSIZE) {
    if(walkaddr(p->pagetable, addr + sz)) {
      // Page is mapped
      if(p->vmas[i].flags == MAP_SHARED) {
        // Set file offset before writing
        uint64 file_offset = p->vmas[i].offset + (addr + sz - p->vmas[i].addr);
        p->vmas[i].file->off = file_offset;
        if(filewrite(p->vmas[i].file, addr + sz, PGSIZE) <= 0)
          return -1;
      }
      uvmunmap(p->pagetable, PGROUNDDOWN(addr + sz), 1, 1);
    }
  }

  // Update VMA
  p->vmas[i].addr = addr + length;
  p->vmas[i].length -= length;
  p->sz -= length;

  // If the entire VMA is unmapped, free it
  if(p->vmas[i].length == 0) {
    p->vmas[i].valid = -1;
    fileclose(p->vmas[i].file);
  }

  return 0;
}
