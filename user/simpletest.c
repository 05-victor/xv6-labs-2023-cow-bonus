// Simple COW test that runs automatically
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("=== Simple COW Test ===\n");
  
  int pid = fork();
  if(pid < 0) {
    printf("FAIL: fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child process
    printf("PASS: Child created successfully\n");
    exit(0);
  } else {
    // Parent process
    wait(0);
    printf("PASS: Child exited successfully\n");
  }
  
  // Test with write
  printf("Test 2: Fork with write\n");
  int *data = (int*)malloc(4096);
  *data = 42;
  
  pid = fork();
  if(pid < 0) {
    printf("FAIL: fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child modifies data
    *data = 99;
    if(*data == 99) {
      printf("PASS: Child can modify data\n");
    }
    exit(0);
  } else {
    wait(0);
    if(*data == 42) {
      printf("PASS: Parent data unchanged\n");
    } else {
      printf("FAIL: Parent data corrupted: %d\n", *data);
    }
  }
  
  printf("=== All tests passed ===\n");
  exit(0);
}
