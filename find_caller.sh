#!/bin/bash

# Example:
# Have:
# static ssize_t
# e1000_receive_iov(NetClientState *nc, const struct iovec *iov, int iovcnt)
# {
# Insert this to the start:
# #if !defined(RETURN_ADDR_OFFSET_PRINT)
#        #define RETURN_ADDR_OFFSET_PRINT(func) \
#         { \
#             int64_t value = (int64_t) (uint64_t) __builtin_return_address(0) - (int64_t) (uint64_t) func; \
#             if (value < 0) { \
#                 printf("QEMU mod: %s, return_address - func_addr = -0x%llx.\n", __func__, (unsigned long long) (-value)); \
#             } else { \
#                 printf("QEMU mod: %s, return_address - func_addr = 0x%llx.\n", __func__, (unsigned long long) value); \
#             } \
#         }
#     #endif
#     RETURN_ADDR_OFFSET_PRINT(e1000_receive_iov); // Func name here.
# It yields return address offset 0x137c5c in logs.
# Call this script with params e1000_receive_iov 0x137c5c

./func_by_addr.sh $(./addr_by_offset_from_other.sh $(./addr_by_func.sh $1) $2)
