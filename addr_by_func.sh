#!/bin/bash

nm -a build/qemu-system-x86_64 | grep $1 | sed 's/^\([^ ]*\) .*$/0x\1/'
