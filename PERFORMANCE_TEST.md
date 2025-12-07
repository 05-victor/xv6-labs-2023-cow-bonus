# Hướng dẫn Test Performance: Fork Gốc vs COW Fork

## 📋 Tổng quan

Document này hướng dẫn cách test và so sánh performance giữa Fork gốc (eager copy) và COW Fork (lazy copy).

## 🌳 Cấu trúc Git Branches

```
xv6-labs-2023-cow-bonus/
├── util (gốc)
│   └── fork-original ← Fork GỐC (eager copy) + perftest
│       
└── cow-fork ← Fork COW (lazy copy) + perftest + cowtest
```

## 📁 Files Quan trọng

### Documentation Files:
- `FORK_ORIGINAL_EXPLAINED.md` - Giải thích chi tiết fork gốc hoạt động thế nào
- `COW_FORK_COMPARISON.md` - So sánh chi tiết từng thay đổi
- `README_COW.md` - Tài liệu COW fork (English)
- `HUONG_DAN.md` - Hướng dẫn ngắn gọn (Vietnamese)
- `PERFORMANCE_TEST.md` - File này

### Test Programs:
- `user/perftest.c` - Performance test (có ở CẢ 2 nhánh)
- `user/cowtest.c` - COW specific test (chỉ có ở cow-fork)

## 🧪 Cách Test

### Bước 1: Test Fork Gốc (Original)

```bash
# Checkout nhánh fork-original
cd /workspaces/codespaces-blank/xv6-labs-2023-cow-bonus
git checkout fork-original

# Build
make clean
make

# Run xv6
make qemu
```

Trong xv6:
```bash
$ perftest
```

**Lưu kết quả**: Copy output hoặc chụp ảnh màn hình

### Bước 2: Test COW Fork

```bash
# Thoát qemu (Ctrl-A X)

# Checkout nhánh cow-fork
git checkout cow-fork

# Build
make clean
make

# Run xv6
make qemu
```

Trong xv6:
```bash
$ perftest
```

**Lưu kết quả**: Copy output hoặc chụp ảnh màn hình

### Bước 3: So sánh kết quả

## 📊 Kết quả Mong đợi

### Test 1: Fork Time

**16 KB (4 pages):**
- Fork Original: ~2-5 ticks
- COW Fork: ~1-2 ticks
- **Improvement: 2-3x faster**

**256 KB (64 pages):**
- Fork Original: ~20-50 ticks
- COW Fork: ~1-3 ticks
- **Improvement: 10-20x faster**

### Test 2: Fork + Exec

**256 KB:**
- Fork Original: ~25-60 ticks (copy 256KB rồi discard!)
- COW Fork: ~2-5 ticks (không copy gì cả)
- **Improvement: 10-30x faster**

### Test 3: Fork + Read Only

**128 KB:**
- Fork Original: ~15-40 ticks (copy nhưng không dùng)
- COW Fork: ~2-4 ticks (share pages)
- **Improvement: 5-15x faster**

### Test 4: Fork + Write 10%

**128 KB, write 10%:**
- Fork Original: ~15-40 ticks (copy tất cả)
- COW Fork: ~3-8 ticks (copy 10% khi fault)
- **Improvement: 3-8x faster**

### Test 5: Fork + Write 50%

**128 KB, write 50%:**
- Fork Original: ~15-40 ticks
- COW Fork: ~8-20 ticks
- **Improvement: 2-3x faster**

### Test 6: Fork + Both Write (worst case)

**64 KB, both write:**
- Fork Original: ~10-30 ticks
- COW Fork: ~12-35 ticks (page faults overhead)
- **Difference: Similar or slightly slower**

### Test 7: Multiple Forks

**5 forks, 64 KB each:**
- Fork Original: ~50-150 ticks (copy 320KB total)
- COW Fork: ~5-15 ticks (share all!)
- **Improvement: 10-30x faster**

## 📈 Biểu đồ Performance

```
Fork Time vs Process Size
         
Ticks    Fork Original
  |      .-----------'
 100     /
  |     /
  50   /              COW Fork
  |   /               .------'
  25  /              /
  |  /              /
  10 +--------------
  |  |   |   |   |
  0  16  64  128 256 (KB)

→ Fork Original: Linear với size (O(N))
→ COW Fork: Constant (O(1))
```

## 🎯 Test Scenarios và Analysis

### Scenario 1: Web Server

```c
// Typical web server pattern
while(1) {
    accept_connection();
    if(fork() == 0) {
        handle_request();  // May exec() CGI script
        exit(0);
    }
}
```

**Analysis:**
- Fork Original: Copy toàn bộ server memory (có thể 10-100MB)
- COW Fork: Share memory, no copy
- **Winner: COW Fork** (50-100x faster)

### Scenario 2: Shell Command

```c
// Shell pattern
if(fork() == 0) {
    exec("/bin/ls", ...);  // Immediately discard memory
}
```

**Analysis:**
- Fork Original: Copy rồi discard → Lãng phí
- COW Fork: Share → No waste
- **Winner: COW Fork** (10-50x faster)

### Scenario 3: Parallel Processing

```c
// Fork để xử lý data song song
for(int i = 0; i < 10; i++) {
    if(fork() == 0) {
        process_chunk(data, i);  // Read mostly, write results
        exit(0);
    }
}
```

**Analysis:**
- Fork Original: 10 copies of data
- COW Fork: Share data, copy khi write results
- **Winner: COW Fork** (5-20x faster, 90% less memory)

### Scenario 4: Data Processing (Heavy Write)

```c
// Child modifies all data
if(fork() == 0) {
    for(int i = 0; i < size; i++)
        data[i] = transform(data[i]);
}
```

**Analysis:**
- Fork Original: Copy once
- COW Fork: Copy incrementally via page faults
- **Winner: Similar** (COW có thể chậm hơn 10-20% do page fault overhead)

## 🔧 Advanced Testing

### Custom Test

Tạo test riêng trong xv6:

```c
// test_custom.c
#include "kernel/types.h"
#include "user/user.h"

int main() {
    char *buf = malloc(1024 * 1024);  // 1MB
    
    // Fill buffer
    for(int i = 0; i < 1024 * 1024; i++)
        buf[i] = i;
    
    uint64 start = uptime();
    int pid = fork();
    uint64 end = uptime();
    
    if(pid == 0) {
        printf("Child: fork took %d ticks\n", end - start);
        exit(0);
    } else {
        wait(0);
        printf("Parent: fork took %d ticks\n", end - start);
    }
    
    free(buf);
    exit(0);
}
```

### Memory Tracking

Để track memory usage chi tiết hơn, thêm debug vào kalloc.c:

```c
// In kalloc.c
void
print_mem_stats(void)
{
    int free_pages = 0;
    struct run *r;
    
    acquire(&kmem.lock);
    for(r = kmem.freelist; r; r = r->next)
        free_pages++;
    release(&kmem.lock);
    
    printf("Free pages: %d (%d KB)\n", free_pages, free_pages * 4);
}
```

## 📝 Checklist Test

- [ ] Test Fork Original (fork-original branch)
  - [ ] perftest chạy thành công
  - [ ] Lưu kết quả
  
- [ ] Test COW Fork (cow-fork branch)
  - [ ] perftest chạy thành công
  - [ ] cowtest chạy thành công
  - [ ] Lưu kết quả
  
- [ ] So sánh kết quả
  - [ ] Fork time: COW nhanh hơn?
  - [ ] Fork + exec: COW nhanh hơn nhiều?
  - [ ] Multiple forks: COW nhanh hơn rất nhiều?
  - [ ] Both write: Performance tương đương?

## 🐛 Troubleshooting

### Lỗi: perftest không chạy

```bash
# Check xem perftest có trong Makefile không
grep perftest Makefile

# Build lại
make clean && make
```

### Lỗi: cowtest panic

```bash
# Chỉ chạy được ở cow-fork branch
git checkout cow-fork
make clean && make
```

### Lỗi: Kernel panic khi test

```bash
# COW implementation có bug, check:
# 1. Reference counting đúng chưa?
# 2. Page fault handler đúng chưa?
# 3. PTE flags đúng chưa?
```

## 📊 Sample Results Template

```
=== FORK ORIGINAL BRANCH ===

Test: Fork Time 16 KB
Result: 3 ticks

Test: Fork Time 256 KB  
Result: 42 ticks

Test: Fork + Exec 256 KB
Result: 45 ticks

Test: Multiple Forks (5x 64KB)
Result: 98 ticks

---

=== COW FORK BRANCH ===

Test: Fork Time 16 KB
Result: 1 tick
Improvement: 3x faster ✓

Test: Fork Time 256 KB
Result: 2 ticks
Improvement: 21x faster ✓✓✓

Test: Fork + Exec 256 KB
Result: 3 ticks
Improvement: 15x faster ✓✓✓

Test: Multiple Forks (5x 64KB)
Result: 8 ticks
Improvement: 12x faster ✓✓✓

CONCLUSION: COW Fork significantly faster!
```

## 🎓 Learning Points

### Quan sát chính:

1. **Fork time**: COW fork O(1), Original fork O(N)
2. **Memory savings**: COW fork 50-90% less memory
3. **Page faults**: COW có overhead nhưng acceptable
4. **Best case**: Fork + exec → COW wins huge (100x)
5. **Worst case**: Both write all → Similar performance

### Trade-offs:

| Aspect | Original | COW |
|--------|----------|-----|
| Simplicity | ✓✓✓ Simple | ✓ Complex |
| Fork speed | ✗ Slow | ✓✓✓ Fast |
| Memory | ✗ 2x | ✓✓ 1x+ |
| Page faults | ✓ None | ✗ On write |
| Best for | Small processes | Large processes |

## 🚀 Kết luận

**COW Fork tốt hơn trong hầu hết cases thực tế:**
- Web servers
- Shell commands  
- Parallel processing
- Large processes

**Original Fork chỉ tốt hơn khi:**
- Process rất nhỏ (< 10KB)
- Child write tất cả pages
- Simplicity > performance

**Recommendation**: Dùng COW fork cho production systems!

---

**Happy Testing!** 🎉
