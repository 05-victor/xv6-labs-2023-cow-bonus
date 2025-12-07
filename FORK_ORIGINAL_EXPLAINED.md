# Fork Gốc (Original Fork) - Giải thích Chi tiết

## 📖 Tổng quan

Fork gốc trong xv6 sử dụng phương pháp **Eager Copy** - copy toàn bộ memory của process cha sang process con ngay lập tức khi fork được gọi.

## 🔍 Cách hoạt động chi tiết

### 1. Hàm fork() - kernel/proc.c (dòng 280-323)

```c
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}
```

#### Các bước thực hiện:

**Bước 1: Allocate process mới (allocproc)**
- Tìm slot trống trong process table
- Allocate kernel stack cho process con
- Tạo page table mới
- Allocate trapframe để lưu registers

**Bước 2: Copy memory (uvmcopy) ⭐ QUAN TRỌNG**
```c
if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
```
- Đây là bước TỐN KÉM nhất
- Copy TOÀN BỘ user memory từ cha sang con
- Nếu process cha có 100MB, phải copy cả 100MB

**Bước 3: Copy trapframe**
```c
*(np->trapframe) = *(p->trapframe);
```
- Copy tất cả registers (PC, SP, general purpose registers)

**Bước 4: Set return value**
```c
np->trapframe->a0 = 0;
```
- Child process sẽ return 0 từ fork
- Parent return PID của child

**Bước 5: Copy file descriptors**
```c
for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
```
- Share file descriptors giữa parent và child
- Tăng reference count

**Bước 6: Setup process relationship**
- Set parent pointer
- Mark child as RUNNABLE
- Return PID to parent

### 2. Hàm uvmcopy() - kernel/vm.c (dòng 313-339)

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

#### Chi tiết từng bước trong vòng lặp:

**Iteration qua từng page (4KB):**

```
Process size = 100KB
=> 100KB / 4KB = 25 pages
=> Loop chạy 25 lần
```

**Mỗi iteration:**

1. **Walk page table** (dòng 321):
   ```c
   if((pte = walk(old, i, 0)) == 0)
   ```
   - Tìm PTE (Page Table Entry) trong page table của parent
   - PTE chứa physical address và permissions

2. **Check valid** (dòng 323):
   ```c
   if((*pte & PTE_V) == 0)
   ```
   - Verify page tồn tại và valid

3. **Get physical address** (dòng 325):
   ```c
   pa = PTE2PA(*pte);
   ```
   - Extract physical address từ PTE
   - PTE format: `[Physical Address | Flags]`

4. **Get permissions** (dòng 326):
   ```c
   flags = PTE_FLAGS(*pte);
   ```
   - Lấy permissions: Read, Write, Execute, User

5. **Allocate new page** (dòng 327-328):
   ```c
   if((mem = kalloc()) == 0)
       goto err;
   ```
   - **QUAN TRỌNG**: Allocate 1 page mới (4KB) cho child
   - Gọi kalloc() để lấy free page từ free list

6. **Copy data** (dòng 329):
   ```c
   memmove(mem, (char*)pa, PGSIZE);
   ```
   - **QUAN TRỌNG**: Copy TOÀN BỘ 4KB từ page cũ sang page mới
   - Đây là operation TỐN THỜI GIAN

7. **Map page vào child page table** (dòng 330-333):
   ```c
   if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
       kfree(mem);
       goto err;
   }
   ```
   - Tạo mapping trong page table của child
   - Virtual address `i` -> Physical address `mem`

### 3. Memory Allocator - kernel/kalloc.c

```c
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

**Cách hoạt động:**
- Free pages được quản lý trong linked list `freelist`
- `kalloc()` lấy page đầu tiên từ free list
- Phải lock để tránh race condition
- Fill với junk data để catch bugs

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

**kfree():**
- Add page vào đầu free list
- Fill với junk để detect use-after-free

## 📊 Phân tích Performance

### Chi phí Fork Gốc

Giả sử process có **N pages** (mỗi page 4KB):

| Operation | Cost | Times Called | Total Cost |
|-----------|------|--------------|------------|
| kalloc() | O(1) | N | O(N) |
| memmove() | O(4KB) | N | O(N × 4KB) |
| mappages() | O(log N) | N | O(N log N) |
| walk() | O(log N) | N | O(N log N) |

**Tổng complexity: O(N × 4KB)** - Linear với process size

### Ví dụ cụ thể:

**Process 100MB:**
- 100MB / 4KB = 25,600 pages
- 25,600 × kalloc() = 25,600 allocations
- 25,600 × 4KB copy = **100MB data copied**
- Time: ~10-50ms (tùy CPU)

**Process 1GB:**
- 1GB / 4KB = 262,144 pages
- 262,144 × 4KB copy = **1GB data copied**
- Time: ~100-500ms

## ⚠️ Vấn đề của Fork Gốc

### 1. Lãng phí thời gian
```
Fork: Copy 100MB
Child: exec() ngay lập tức
=> 100MB copied nhưng KHÔNG BAO GIỜ dùng!
```

Ví dụ:
```c
if(fork() == 0) {
    exec("/bin/ls", ...);  // Discard all copied memory!
}
```

### 2. Lãng phí memory

```
Parent: 100MB
Child:  100MB (copy)
Total:  200MB

But child chỉ modify 1-2 pages
=> 99.98MB copied không cần thiết!
```

### 3. Slow fork

```c
// Large process
char buf[100 * 1024 * 1024];  // 100MB

// Fork takes 50ms to copy all 100MB
int pid = fork();  // SLOW!
```

### 4. Memory pressure

```
System: 1GB RAM
Parent: 500MB
Fork: Need 500MB more for child
=> Out of memory! (even though child might not use it)
```

## 🔄 Flow Diagram

```
fork() called
    ↓
allocproc() - Allocate child process structure
    ↓
uvmcopy() - START EXPENSIVE PART
    ↓
    Loop each page:
        ├─ kalloc() - Allocate new page
        ├─ memmove() - Copy 4KB data ⏱️ SLOW
        └─ mappages() - Map in child page table
    ↓
Copy trapframe (registers)
    ↓
Setup file descriptors
    ↓
Set child RUNNABLE
    ↓
Return PID to parent, 0 to child
```

## 📈 Memory Timeline

```
Time:   T0              T1 (fork)           T2
        
Parent: [100MB]    →    [100MB]        →   [100MB]
                        
Child:   None      →    [100MB copy]   →   [100MB copy]
                         ⬆️
                    COPY HAPPENS HERE
                    (expensive!)
```

## 🎯 Kết luận

**Fork gốc:**
- ✅ Đơn giản, dễ hiểu
- ✅ Isolated memory (parent & child hoàn toàn độc lập)
- ❌ **Chậm**: Phải copy toàn bộ memory
- ❌ **Lãng phí**: Copy cả pages không dùng
- ❌ **Memory intensive**: Nhân đôi memory usage ngay lập tức

**Phù hợp khi:**
- Process nhỏ (< 1MB)
- Child thực sự cần modify nhiều data
- Memory không phải vấn đề

**Không phù hợp khi:**
- Process lớn (> 10MB)
- Child exec() ngay sau fork
- Child chỉ read hoặc modify ít data
- System có nhiều forks (web server, shell)

---

**NEXT**: Xem `COW_FORK_COMPARISON.md` để hiểu COW fork cải thiện như thế nào!
