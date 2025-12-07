# So sánh Fork Gốc vs COW Fork - Chi tiết Từng Bước

## 📋 Tổng quan

Document này giải thích **CHI TIẾT** những thay đổi từ Fork gốc sang COW Fork.

## 🔄 So sánh Side-by-Side

### 1. FORK GỐC vs COW FORK

| Aspect | Fork Gốc | COW Fork |
|--------|----------|----------|
| **Chiến lược** | Eager Copy | Lazy Copy |
| **Khi fork** | Copy tất cả pages | Chỉ copy page tables |
| **Memory copy** | Ngay lập tức | Khi cần (on-demand) |
| **Time complexity** | O(N × 4KB) | O(1) |
| **Memory usage** | 2× ngay lập tức | ~1× ban đầu |
| **Page permissions** | Giữ nguyên | Clear write, set COW |

## 🔧 Thay đổi Chi tiết Từng File

### FILE 1: kernel/kalloc.c - Reference Counting

#### ❌ TRƯỚC (Fork Gốc):

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

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
    memset((char*)r, 5, PGSIZE);
  return (void*)r;
}
```

**Vấn đề:**
- Không track bao nhiêu processes đang dùng page
- kfree() free ngay lập tức → Lỗi nếu 2 processes share page

#### ✅ SAU (COW Fork):

```c
// THÊM: Reference count array
struct {
  struct spinlock lock;
  int count[(PHYSTOP - KERNBASE) / PGSIZE];  // ⭐ MỚI
} pageref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pageref.lock, "pageref");  // ⭐ MỚI
  freerange(end, (void*)PHYSTOP);
}

// ⭐ MỚI: Helper để convert địa chỉ thành index
int
pa2idx(void *pa)
{
  return ((uint64)pa - KERNBASE) / PGSIZE;
}

// ⭐ MỚI: Tăng reference count
void
krefpage(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return;
  
  acquire(&pageref.lock);
  pageref.count[pa2idx(pa)]++;
  release(&pageref.lock);
}

// ⭐ MỚI: Giảm reference count
int
kderefpage(void *pa)
{
  int count;
  
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kderefpage");
  
  acquire(&pageref.lock);
  count = --pageref.count[pa2idx(pa)];
  release(&pageref.lock);
  
  return count;
}

// ⭐ SỬA: kfree chỉ free khi ref count = 0
void
kfree(void *pa)
{
  struct run *r;
  
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // ⭐ MỚI: Chỉ free nếu không còn ai dùng
  if(kderefpage(pa) > 0)
    return;
  
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// ⭐ SỬA: kalloc set ref count = 1
void *
kalloc(void)
{
  struct run *r;
  
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  
  if(r) {
    memset((char*)r, 5, PGSIZE);
    krefpage((void*)r);  // ⭐ MỚI: Set reference count = 1
  }
  return (void*)r;
}
```

**Giải thích:**

1. **Reference count array**: Track bao nhiêu processes share mỗi page
2. **krefpage()**: Tăng count khi share page (trong fork)
3. **kderefpage()**: Giảm count khi process không dùng nữa
4. **kfree()**: Chỉ free page khi count = 0 (không ai dùng)

**Ví dụ:**
```
Parent forks → Page X ref count = 2 (parent + child)
Child exits  → kfree() → ref count = 1 → KHÔNG free
Parent exits → kfree() → ref count = 0 → FREE
```

### FILE 2: kernel/riscv.h - COW Flag

#### ❌ TRƯỚC:

```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1) // readable
#define PTE_W (1L << 2) // writable
#define PTE_X (1L << 3) // executable
#define PTE_U (1L << 4) // user can access
```

#### ✅ SAU:

```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1) // readable
#define PTE_W (1L << 2) // writable
#define PTE_X (1L << 3) // executable
#define PTE_U (1L << 4) // user can access

// ⭐ MỚI: COW flag sử dụng RSW (Reserved for Software) bit
#define PTE_COW (1L << 8)  // Copy-On-Write page
```

**Giải thích:**

RISC-V PTE format:
```
Bits:  63...54  53...10   9...8    7...0
       Reserved   PPN     RSW     Flags
                           ↑
                    Dùng bit này cho COW
```

- Bit 8 là Reserved for Software → OS tự do dùng
- Set PTE_COW = 1 để đánh dấu "page này là COW"

### FILE 3: kernel/vm.c - Core COW Logic

#### ❌ TRƯỚC: uvmcopy() - COPY TẤT CẢ

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
    
    // ❌ ALLOCATE PAGE MỚI
    if((mem = kalloc()) == 0)
      goto err;
    
    // ❌ COPY DATA (4KB mỗi lần)
    memmove(mem, (char*)pa, PGSIZE);
    
    // ❌ MAP PAGE MỚI vào child
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

**Flow Fork Gốc:**
```
Parent page A (physical addr 0x1000)
    ↓ fork()
    ├─ kalloc() → new page B (0x2000)
    ├─ memmove(B, A, 4KB) → COPY DATA
    └─ Child page B (0x2000)

Result: 2 pages, 2 copies data
```

#### ✅ SAU: uvmcopy() - SHARE PAGES

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    // ⭐ THAY ĐỔI: Nếu page writable, clear write và set COW
    if(flags & PTE_W) {
      flags &= ~PTE_W;     // Clear write permission
      flags |= PTE_COW;    // Mark as COW
      *pte = PA2PTE(pa) | flags;  // Update parent PTE too!
    }
    
    // ⭐ KHÔNG KALLOC, KHÔNG COPY!
    // Chỉ map CÙNG physical page vào child
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      goto err;
    }
    
    // ⭐ MỚI: Tăng reference count
    krefpage((void*)pa);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

**Flow COW Fork:**
```
Parent page A (physical addr 0x1000, R/W)
    ↓ fork()
    ├─ Clear PTE_W, Set PTE_COW
    ├─ Parent PTE: addr=0x1000, flags=R/COW
    ├─ Child PTE:  addr=0x1000, flags=R/COW (SAME ADDR!)
    └─ Ref count[0x1000] = 2

Result: 1 page, shared by 2 processes
```

**So sánh:**

| Operation | Fork Gốc | COW Fork |
|-----------|----------|----------|
| kalloc() | ✅ Có (N lần) | ❌ Không |
| memmove() | ✅ Có (N × 4KB) | ❌ Không |
| mappages() | ✅ Map page mới | ✅ Map page cũ |
| krefpage() | ❌ Không | ✅ Có |
| Clear PTE_W | ❌ Không | ✅ Có |

#### ✅ MỚI: cowcopy() - Handle Page Fault

```c
// ⭐ FUNCTION HOÀN TOÀN MỚI
int
cowcopy(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;
  uint flags;
  char *mem;

  // Check address valid
  if(va >= MAXVA)
    return -1;

  // Get PTE
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return -1;
  if((*pte & PTE_V) == 0)
    return -1;
  if((*pte & PTE_U) == 0)
    return -1;

  // ⭐ Check nếu đây là COW page
  if(!(*pte & PTE_COW))
    return -1;

  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);

  // ⭐ Allocate page MỚI (BÂY GIỜ MỚI ALLOCATE!)
  mem = kalloc();
  if(mem == 0)
    return -1;

  // ⭐ Copy data (BÂY GIỜ MỚI COPY!)
  memmove(mem, (char*)pa, PGSIZE);

  // ⭐ Update PTE: restore write, clear COW
  flags |= PTE_W;
  flags &= ~PTE_COW;
  *pte = PA2PTE((uint64)mem) | flags;

  // ⭐ Giảm ref count của page cũ
  kfree((void*)pa);

  return 0;
}
```

**Khi nào cowcopy() được gọi?**

```
Process write vào COW page
    ↓
Page fault (permission denied - no PTE_W)
    ↓
usertrap() detect scause == 15 (store page fault)
    ↓
cowcopy() được gọi
    ↓
    ├─ Allocate page mới
    ├─ Copy data từ shared page
    ├─ Update PTE → write permission
    └─ Decrease ref count
    ↓
Return to user → retry write → SUCCESS
```

#### ✅ SỬA: copyout() - Handle COW

```c
// ❌ TRƯỚC:
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    pte = walk(pagetable, va0, 0);
    
    // ❌ Chỉ check PTE_W
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1;
      
    pa0 = PTE2PA(*pte);
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);
    ...
  }
  return 0;
}
```

```c
// ✅ SAU:
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    pte = walk(pagetable, va0, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
      return -1;
    
    // ⭐ MỚI: Nếu là COW page, handle trước
    if((*pte & PTE_COW) && !(*pte & PTE_W)) {
      if(cowcopy(pagetable, va0) != 0)
        return -1;
      pte = walk(pagetable, va0, 0);  // Re-walk sau khi cowcopy
    }
    
    // Bây giờ mới check write permission
    if((*pte & PTE_W) == 0)
      return -1;
      
    pa0 = PTE2PA(*pte);
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);
    ...
  }
  return 0;
}
```

**Tại sao cần sửa copyout()?**

Khi kernel copy data vào user space (ví dụ: read() system call):
```c
// User program:
char buf[100];
read(fd, buf, 100);  // Kernel writes to user buffer

// Nếu buf là COW page:
// → copyout() phải handle COW trước khi write
```

### FILE 4: kernel/trap.c - Page Fault Handler

#### ❌ TRƯỚC:

```c
void
usertrap(void)
{
  ...
  if(r_scause() == 8){
    // system call
    syscall();
  } else if((which_dev = devintr()) != 0){
    // device interrupt
  } else {
    // ❌ Tất cả exceptions khác → kill process
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }
  ...
}
```

#### ✅ SAU:

```c
void
usertrap(void)
{
  ...
  if(r_scause() == 8){
    // system call
    syscall();
  } else if((which_dev = devintr()) != 0){
    // device interrupt
  } else {
    // ⭐ MỚI: Check for store page fault (scause == 15)
    if(r_scause() == 15) {
      uint64 va = r_stval();  // Virtual address gây fault
      
      // Try to handle COW
      if(cowcopy(p->pagetable, PGROUNDDOWN(va)) != 0) {
        // COW handler failed → kill
        printf("usertrap(): COW page fault failed va=%p pid=%d\n", va, p->pid);
        setkilled(p);
      }
      // Nếu thành công, return và retry instruction
    } else {
      // Other exceptions → kill
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      setkilled(p);
    }
  }
  ...
}
```

**RISC-V Exception Causes:**

```
scause = 8:  System call (ecall)
scause = 13: Load page fault (read from invalid page)
scause = 15: Store page fault (write to read-only page) ⭐ COW
scause = 12: Instruction page fault
```

**Flow khi write COW page:**

```
User: *ptr = 42;  // ptr points to COW page
    ↓
Hardware: Check PTE → No PTE_W → FAULT!
    ↓
Hardware: scause = 15, stval = ptr address
    ↓
usertrap():
    ├─ r_scause() == 15 ? YES
    ├─ va = r_stval() = ptr
    ├─ cowcopy(pagetable, va)
    │   ├─ Check PTE_COW? YES
    │   ├─ kalloc() new page
    │   ├─ memmove() copy data
    │   ├─ Update PTE: +PTE_W, -PTE_COW
    │   └─ kfree() old page (decrease ref)
    └─ Return to user
    ↓
Hardware: Retry *ptr = 42
    ↓
Hardware: Check PTE → PTE_W OK → SUCCESS!
```

## 📊 Performance Comparison

### Scenario 1: Fork then exec()

```c
if(fork() == 0) {
    exec("/bin/ls", ...);
}
```

| Metric | Fork Gốc | COW Fork | Improvement |
|--------|----------|----------|-------------|
| Pages copied | N (all) | 0 | ∞ |
| Time | O(N × 4KB) | O(1) | ~100x faster |
| Memory used | 2N pages | N pages | 50% less |

**Ví dụ cụ thể:**
- Process 100MB = 25,600 pages
- Fork gốc: Copy 25,600 pages = 50ms
- COW fork: Share pages = 0.5ms
- **Improvement: 100x faster!**

### Scenario 2: Fork then child modifies 10%

```c
pid = fork();
if(pid == 0) {
    // Child modifies 10% of data
    for(i = 0; i < N/10; i++)
        buf[i*10] = i;
}
```

| Metric | Fork Gốc | COW Fork | Improvement |
|--------|----------|----------|-------------|
| At fork | Copy 100% | Share 100% | 100x faster |
| During run | 0 | Copy 10% | 10% overhead |
| Total pages | 200% | 110% | 45% less memory |

### Scenario 3: Fork then both modify heavily

```c
pid = fork();
// Both parent and child write to 90% of pages
```

| Metric | Fork Gốc | COW Fork | Difference |
|--------|----------|----------|------------|
| At fork | Copy 100% | Share 100% | Faster |
| During run | 0 | Copy 90% | Slower |
| Total | 100% copied | 90% copied | Similar |

**Kết luận**: COW vẫn tốt hơn hoặc tương đương!

## 🎯 Tổng kết Thay đổi

| Component | Fork Gốc | COW Fork | Benefit |
|-----------|----------|----------|---------|
| **kalloc.c** | Simple free list | + Reference counting | Share pages safely |
| **riscv.h** | 5 PTE flags | + PTE_COW flag | Mark COW pages |
| **vm.c/uvmcopy** | Copy all pages | Share + mark COW | 100x faster fork |
| **vm.c/cowcopy** | N/A | Handle page fault | Lazy copy |
| **vm.c/copyout** | Direct write | Check COW first | Kernel writes work |
| **trap.c** | Kill on fault | Handle COW fault | Allow write to COW |

## 🔍 Key Insights

### 1. Lazy is Better
```
Fork gốc: Copy everything NOW (eager)
COW fork: Copy when NEEDED (lazy)
→ Most pages never modified → Huge savings!
```

### 2. Trade-offs
```
Fork gốc:
  + Simple
  - Slow, wasteful

COW fork:
  + Fast, efficient
  - Complex (ref counting, page faults)
  - Small overhead on write
```

### 3. When COW Wins
```
✅ exec() after fork (no memory used!)
✅ Read-heavy workloads
✅ Large processes
✅ Many forks (web servers)
```

### 4. When Fork Gốc OK
```
✅ Tiny processes (< 1MB)
✅ Child modifies everything
✅ Simplicity matters more than speed
```

---

**NEXT**: Xem `PERFORMANCE_TEST.md` để test thực tế!
