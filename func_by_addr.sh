#!/bin/bash

addr2line -f -e build/qemu-system-x86_64 $1

#objdump -x --start-address=$1 -M intel build/qemu-system-x86_64 | tail -n5
