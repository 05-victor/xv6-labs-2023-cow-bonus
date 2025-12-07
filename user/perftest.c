// Performance test: Fork Original vs COW Fork
// This program measures fork time and memory usage
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define PGSIZE 4096
#define KB 1024
#define MB (1024 * 1024)

// Timer helpers
uint64 get_time() {
    // Note: xv6 không có high-resolution timer
    // Dùng uptime (ticks) để estimate
    return uptime();
}

// Test 1: Fork time with different memory sizes
void
test_fork_time(int size_kb)
{
    char *buf;
    int pid;
    uint64 start, end;
    
    printf("\n=== Test Fork Time: %d KB ===\n", size_kb);
    
    // Allocate memory
    int pages = (size_kb * KB) / PGSIZE;
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    // Fill memory to ensure pages are allocated
    printf("Filling %d pages...\n", pages);
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = (char)i;
    }
    
    // Measure fork time
    printf("Forking...\n");
    start = get_time();
    pid = fork();
    end = get_time();
    
    if(pid < 0) {
        printf("fork failed\n");
        free(buf);
        return;
    }
    
    if(pid == 0) {
        // Child: just exit
        free(buf);
        exit(0);
    } else {
        // Parent: print results
        wait(0);
        printf("Fork time: %d ticks\n", (int)(end - start));
        printf("Average per page: %d ticks\n", pages > 0 ? (int)((end - start) * 100 / pages) : 0);
        free(buf);
    }
}

// Test 2: Fork + exec (no memory actually needed)
void
test_fork_exec(int size_kb)
{
    char *buf;
    int pid;
    uint64 start, end;
    
    printf("\n=== Test Fork + Exec: %d KB ===\n", size_kb);
    
    // Allocate and fill
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = (char)i;
    }
    
    printf("Fork + exec...\n");
    start = get_time();
    pid = fork();
    
    if(pid < 0) {
        printf("fork failed\n");
        free(buf);
        return;
    }
    
    if(pid == 0) {
        // Child: exec immediately (memory copied but not used!)
        char *argv[] = { "echo", "child", 0 };
        exec("/bin/echo", argv);
        printf("exec failed\n");
        exit(1);
    } else {
        // Parent
        wait(0);
        end = get_time();
        printf("Fork + exec time: %d ticks\n", (int)(end - start));
        printf("(COW fork should be much faster here!)\n");
        free(buf);
    }
}

// Test 3: Fork + child read only
void
test_fork_readonly(int size_kb)
{
    char *buf;
    int pid;
    uint64 start, end;
    
    printf("\n=== Test Fork + Read Only: %d KB ===\n", size_kb);
    
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = (char)i;
    }
    
    start = get_time();
    pid = fork();
    
    if(pid < 0) {
        printf("fork failed\n");
        free(buf);
        return;
    }
    
    if(pid == 0) {
        // Child: only READ data
        int sum = 0;
        for(int i = 0; i < size_kb * KB; i += PGSIZE) {
            sum += buf[i];  // Read only, no write
        }
        printf("Child read sum: %d\n", sum);
        free(buf);
        exit(0);
    } else {
        wait(0);
        end = get_time();
        printf("Fork + read time: %d ticks\n", (int)(end - start));
        printf("(COW: no page faults, Original: copy wasted)\n");
        free(buf);
    }
}

// Test 4: Fork + child write 10%
void
test_fork_write_partial(int size_kb, int write_percent)
{
    char *buf;
    int pid;
    uint64 start, end, fault_time;
    
    printf("\n=== Test Fork + Write %d%%: %d KB ===\n", write_percent, size_kb);
    
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = 'A';
    }
    
    start = get_time();
    pid = fork();
    
    if(pid < 0) {
        printf("fork failed\n");
        free(buf);
        return;
    }
    
    if(pid == 0) {
        // Child: write to specified percentage
        fault_time = get_time();
        int bytes_to_write = (size_kb * KB * write_percent) / 100;
        for(int i = 0; i < bytes_to_write; i += PGSIZE) {
            buf[i] = 'B';  // Trigger COW on this page
        }
        uint64 write_done = get_time();
        
        printf("Child wrote %d bytes in %d ticks\n", bytes_to_write, 
               (int)(write_done - fault_time));
        printf("(COW: page faults on write, Original: no faults)\n");
        free(buf);
        exit(0);
    } else {
        wait(0);
        end = get_time();
        printf("Total time: %d ticks\n", (int)(end - start));
        free(buf);
    }
}

// Test 5: Fork + both write (worst case for COW)
void
test_fork_both_write(int size_kb)
{
    char *buf;
    int pid;
    
    printf("\n=== Test Fork + Both Write: %d KB ===\n", size_kb);
    
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = 'A';
    }
    
    uint64 start = get_time();
    pid = fork();
    
    if(pid < 0) {
        printf("fork failed\n");
        free(buf);
        return;
    }
    
    if(pid == 0) {
        // Child: write all pages
        for(int i = 0; i < size_kb * KB; i += PGSIZE) {
            buf[i] = 'C';
        }
        printf("Child wrote all pages\n");
        free(buf);
        exit(0);
    } else {
        // Parent: also write
        for(int i = 0; i < size_kb * KB; i += PGSIZE) {
            buf[i] = 'P';
        }
        wait(0);
        uint64 end = get_time();
        printf("Both wrote, total time: %d ticks\n", (int)(end - start));
        printf("(Worst case for COW: all pages copied anyway)\n");
        free(buf);
    }
}

// Test 6: Multiple forks
void
test_multiple_forks(int num_forks, int size_kb)
{
    char *buf;
    
    printf("\n=== Test %d Forks: %d KB each ===\n", num_forks, size_kb);
    
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = (char)i;
    }
    
    uint64 start = get_time();
    
    for(int i = 0; i < num_forks; i++) {
        int pid = fork();
        if(pid < 0) {
            printf("fork %d failed\n", i);
            break;
        }
        if(pid == 0) {
            // Child: exit immediately
            free(buf);
            exit(0);
        }
    }
    
    // Parent: wait for all children
    for(int i = 0; i < num_forks; i++) {
        wait(0);
    }
    
    uint64 end = get_time();
    printf("%d forks completed in %d ticks\n", num_forks, (int)(end - start));
    printf("Average per fork: %d ticks\n", num_forks > 0 ? (int)((end - start) * 100 / num_forks) : 0);
    printf("(COW: huge savings with multiple forks!)\n");
    free(buf);
}

// Test 7: Memory usage estimation
void
test_memory_usage(int size_kb)
{
    char *buf;
    int pid;
    
    printf("\n=== Test Memory Usage: %d KB ===\n", size_kb);
    
    buf = (char*)malloc(size_kb * KB);
    if(buf == 0) {
        printf("malloc failed\n");
        return;
    }
    
    for(int i = 0; i < size_kb * KB; i++) {
        buf[i] = 'A';
    }
    
    printf("Parent allocated %d KB\n", size_kb);
    
    pid = fork();
    if(pid < 0) {
        printf("fork failed\n");
        free(buf);
        return;
    }
    
    if(pid == 0) {
        // Child: sleep to keep process alive
        printf("Child: same %d KB (Original: copied, COW: shared)\n", size_kb);
        sleep(50);
        
        // Now write to trigger COW
        buf[0] = 'B';
        printf("Child wrote 1 byte (COW: +1 page = 4KB, Original: 0)\n");
        
        sleep(50);
        free(buf);
        exit(0);
    } else {
        sleep(30);
        printf("Parent + Child memory:\n");
        printf("  Original fork: %d KB (all copied)\n", size_kb * 2);
        printf("  COW fork: %d KB (shared)\n", size_kb);
        
        sleep(70);  // Wait for child to write
        printf("After child write:\n");
        printf("  Original fork: still %d KB\n", size_kb * 2);
        printf("  COW fork: %d KB (1 page copied)\n", size_kb + 4);
        
        wait(0);
        free(buf);
    }
}

void
print_header(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║     Fork Performance Test: Original vs COW Fork        ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("This test compares:\n");
    printf("  - Fork ORIGINAL: Copy all pages immediately\n");
    printf("  - Fork COW: Share pages, copy on write\n");
    printf("\n");
    printf("NOTE: Results depend on which kernel you're running!\n");
    printf("  - On 'fork-original' branch: all copy\n");
    printf("  - On 'cow-fork' branch: copy-on-write\n");
    printf("\n");
}

int
main(int argc, char *argv[])
{
    print_header();
    
    // Run tests with increasing sizes
    printf("Running tests...\n");
    
    // Small size tests
    test_fork_time(16);      // 16 KB = 4 pages
    test_fork_time(64);      // 64 KB = 16 pages
    test_fork_time(256);     // 256 KB = 64 pages
    
    // Fork + exec test (COW wins big here!)
    test_fork_exec(256);
    
    // Read-only test (COW wins)
    test_fork_readonly(128);
    
    // Partial write tests (COW wins)
    test_fork_write_partial(128, 10);   // Write 10%
    test_fork_write_partial(128, 50);   // Write 50%
    
    // Both write test (similar performance)
    test_fork_both_write(64);
    
    // Multiple forks (COW wins huge!)
    test_multiple_forks(5, 64);
    
    // Memory usage
    test_memory_usage(128);
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║                   Tests Complete!                      ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Summary:\n");
    printf("  COW fork should be FASTER for:\n");
    printf("    ✓ Fork + exec (no memory used)\n");
    printf("    ✓ Fork + read only\n");
    printf("    ✓ Fork + partial writes\n");
    printf("    ✓ Multiple forks\n");
    printf("\n");
    printf("  Performance similar when:\n");
    printf("    • Both parent and child write all pages\n");
    printf("\n");
    printf("To compare: Run this on both branches!\n");
    printf("  1. git checkout fork-original && make qemu\n");
    printf("  2. $ perftest > results-original.txt\n");
    printf("  3. (exit qemu)\n");
    printf("  4. git checkout cow-fork && make qemu\n");
    printf("  5. $ perftest > results-cow.txt\n");
    printf("  6. Compare results!\n");
    printf("\n");
    
    exit(0);
}
