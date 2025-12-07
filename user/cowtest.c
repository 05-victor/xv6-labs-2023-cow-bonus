// COW fork test program
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096

// Test 1: Simple fork without modification
void
test_simple_fork(void)
{
  printf("Test 1: Simple fork (no write)\n");
  
  char *buf = (char*)malloc(PGSIZE);
  for(int i = 0; i < PGSIZE; i++)
    buf[i] = 'A';
  
  int pid = fork();
  
  if(pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child - just read
    printf("  Child: buf[0] = %c (read only)\n", buf[0]);
    free(buf);
    exit(0);
  } else {
    // Parent
    wait(0);
    printf("  Parent: buf[0] = %c (after child exit)\n", buf[0]);
    free(buf);
  }
  
  printf("  Test 1: PASSED\n\n");
}

// Test 2: Fork with child modification
void
test_child_write(void)
{
  printf("Test 2: Fork with child write\n");
  
  char *buf = (char*)malloc(PGSIZE);
  for(int i = 0; i < PGSIZE; i++)
    buf[i] = 'A';
  
  int pid = fork();
  
  if(pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child - modify the buffer (triggers COW)
    printf("  Child: Writing 'B' to buffer\n");
    for(int i = 0; i < PGSIZE; i++)
      buf[i] = 'B';
    printf("  Child: buf[0] = %c (after write)\n", buf[0]);
    free(buf);
    exit(0);
  } else {
    // Parent
    wait(0);
    printf("  Parent: buf[0] = %c (should still be 'A')\n", buf[0]);
    if(buf[0] == 'A') {
      printf("  Test 2: PASSED - Parent data unchanged!\n\n");
    } else {
      printf("  Test 2: FAILED - Parent data was modified!\n\n");
    }
    free(buf);
  }
}

// Test 3: Fork with parent modification
void
test_parent_write(void)
{
  printf("Test 3: Fork with parent write\n");
  
  char *buf = (char*)malloc(PGSIZE);
  for(int i = 0; i < PGSIZE; i++)
    buf[i] = 'A';
  
  int pid = fork();
  
  if(pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child - sleep to let parent write first
    sleep(10);
    printf("  Child: buf[0] = %c (should still be 'A')\n", buf[0]);
    if(buf[0] == 'A') {
      printf("  Test 3: PASSED - Child data unchanged!\n");
    } else {
      printf("  Test 3: FAILED - Child data was modified!\n");
    }
    free(buf);
    exit(0);
  } else {
    // Parent - modify the buffer (triggers COW)
    printf("  Parent: Writing 'C' to buffer\n");
    for(int i = 0; i < PGSIZE; i++)
      buf[i] = 'C';
    printf("  Parent: buf[0] = %c (after write)\n", buf[0]);
    wait(0);
    free(buf);
  }
  
  printf("\n");
}

// Test 4: Multiple forks
void
test_multiple_forks(void)
{
  printf("Test 4: Multiple forks\n");
  
  char *buf = (char*)malloc(PGSIZE);
  for(int i = 0; i < PGSIZE; i++)
    buf[i] = 'A';
  
  int pid1 = fork();
  if(pid1 == 0) {
    // First child
    buf[0] = '1';
    printf("  Child 1: Modified to '%c'\n", buf[0]);
    free(buf);
    exit(0);
  }
  
  int pid2 = fork();
  if(pid2 == 0) {
    // Second child
    buf[0] = '2';
    printf("  Child 2: Modified to '%c'\n", buf[0]);
    free(buf);
    exit(0);
  }
  
  wait(0);
  wait(0);
  
  printf("  Parent: buf[0] = %c (should still be 'A')\n", buf[0]);
  if(buf[0] == 'A') {
    printf("  Test 4: PASSED\n\n");
  } else {
    printf("  Test 4: FAILED\n\n");
  }
  free(buf);
}

// Test 5: Large allocation test
void
test_large_allocation(void)
{
  printf("Test 5: Large allocation COW test\n");
  
  // Allocate 10 pages
  int pages = 10;
  char *bufs[10];
  
  for(int i = 0; i < pages; i++) {
    bufs[i] = (char*)malloc(PGSIZE);
    if(bufs[i] == 0) {
      printf("  malloc failed\n");
      return;
    }
    for(int j = 0; j < PGSIZE; j++)
      bufs[i][j] = 'X';
  }
  
  printf("  Allocated %d pages\n", pages);
  
  int pid = fork();
  if(pid == 0) {
    // Child - write to every other page
    for(int i = 0; i < pages; i += 2) {
      bufs[i][0] = 'Y';
    }
    printf("  Child: Modified %d pages\n", pages/2);
    for(int i = 0; i < pages; i++)
      free(bufs[i]);
    exit(0);
  } else {
    wait(0);
    // Check parent's data
    int unchanged = 0;
    for(int i = 0; i < pages; i++) {
      if(bufs[i][0] == 'X')
        unchanged++;
    }
    printf("  Parent: %d/%d pages unchanged\n", unchanged, pages);
    if(unchanged == pages) {
      printf("  Test 5: PASSED\n\n");
    } else {
      printf("  Test 5: FAILED\n\n");
    }
    for(int i = 0; i < pages; i++)
      free(bufs[i]);
  }
}

int
main(int argc, char *argv[])
{
  printf("\n=== COW Fork Test Suite ===\n\n");
  
  test_simple_fork();
  test_child_write();
  test_parent_write();
  test_multiple_forks();
  test_large_allocation();
  
  printf("=== All tests completed ===\n");
  
  exit(0);
}
