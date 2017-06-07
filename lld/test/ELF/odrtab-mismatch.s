# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t
# RUN: ld.lld %t -o %t2 2>&1 | FileCheck %s

# CHECK: warning: ODR violation detected: foo
# CHECK-NEXT: >>> defined at loc1.c
# CHECK-NEXT: odrtab-mismatch.s.tmp
# CHECK-NEXT: >>> defined at loc2.c
# CHECK-NEXT: odrtab-mismatch.s.tmp

.section .odrtab,"",@llvm_odrtab
start:

.4byte 0
.4byte end-start
.4byte producer-strtab
.4byte producer_end-producer
.4byte symtab-start
.4byte (end-symtab)/20

strtab:

producer:
.ascii "clang"
producer_end:

name:
.ascii "foo"
name_end:

location1:
.ascii "loc1.c"
location1_end:

location2:
.ascii "loc2.c"
location2_end:

symtab:
.4byte name-strtab
.4byte name_end-name
.4byte location1-strtab
.4byte location1_end-location1
.4byte 1

.4byte name-strtab
.4byte name_end-name
.4byte location2-strtab
.4byte location2_end-location2
.4byte 2

end:
