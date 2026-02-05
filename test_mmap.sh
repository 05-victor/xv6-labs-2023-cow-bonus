#!/bin/bash
cd /workspaces/xv6-labs-2023-cow-bonus
echo "Starting xv6 and running mmaptest..."
timeout 60 make qemu <<EOF
mmaptest
halt
EOF
