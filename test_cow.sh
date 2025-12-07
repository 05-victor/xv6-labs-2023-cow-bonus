#!/bin/bash
# Auto test COW fork

cd /workspaces/codespaces-blank/xv6-labs-2023-cow-bonus

echo "Building xv6..."
make clean > /dev/null 2>&1
make qemu-nox > test_output.txt 2>&1 &
QEMU_PID=$!

echo "Waiting for xv6 to boot..."
sleep 3

echo "Sending test commands..."
# Try to send commands to QEMU stdin
{
  sleep 1
  echo "echo Simple test"
  sleep 1
  echo "simpletest"
  sleep 2
} | nc -q 1 localhost 1234 2>/dev/null || echo "Could not connect"

sleep 2

echo "Killing QEMU..."
kill $QEMU_PID 2>/dev/null

echo "Test output:"
tail -30 test_output.txt

echo "Looking for errors..."
grep -i "panic\|trap\|fail" test_output.txt || echo "No errors found"
