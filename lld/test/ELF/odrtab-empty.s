# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t
# RUN: ld.lld %t -o %t2
# RUN: llvm-readobj -sections %t2 | FileCheck %s

# CHECK-NOT: .odrtab
.section .odrtab,"",@llvm_odrtab
